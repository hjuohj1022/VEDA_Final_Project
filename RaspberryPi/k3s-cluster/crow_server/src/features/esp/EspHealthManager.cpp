#include "features/esp/EspHealthManager.h"

#include <atomic>

// ESP32 watchdog 관련 REST API의 MQTT publish/subscribe 래핑부.
namespace {
// request_id 충돌 방지용 프로세스 전역 증가값.
std::atomic<unsigned long long> g_request_counter{0};

crow::response makeCommandResponse(const EspHealthCommandResult& result) {
    crow::json::wvalue body;
    body["status"] = result.published ? "ACCEPTED" : "MQTT_UNAVAILABLE";
    body["published"] = result.published;
    body["broker_connected"] = result.broker_connected;
    body["control_topic"] = result.control_topic;
    body["request_id"] = result.request_id;
    body["payload"] = result.payload;
    body["message"] = result.message;

    return crow::response(result.published ? 202 : 503, body);
}
}  // 익명 네임스페이스

EspHealthManager::EspHealthManager(const std::string& broker_host,
                                   int broker_port,
                                   const std::string& client_id,
                                   const std::string& control_topic,
                                   const std::string& status_topic)
    : mqtt_(std::make_unique<MqttManager>(client_id.c_str(), broker_host.c_str(), broker_port)),
      control_topic_(control_topic),
      status_topic_(status_topic) {
    // 최신 watchdog 상태 추적용 status topic 구독.
    mqtt_->set_message_callback([this](const std::string& topic, const std::string& payload) {
        handleMessage(topic, payload);
    });
    mqtt_->subscribeTopic(status_topic_);
}

EspHealthCommandResult EspHealthManager::requestPublishNow() {
    // ESP32 즉시 상태 재발행 요청용 제어 메시지 생성.
    EspHealthCommandResult result;
    result.control_topic = control_topic_;
    result.broker_connected = mqtt_->isConnected();

    crow::json::wvalue payload;
    payload["cmd"] = "publish_status_now";

    result.request_id = generateRequestId();
    payload["request_id"] = result.request_id;

    result.payload = payload.dump();
    result.published = mqtt_->publishMessage(control_topic_, result.payload);
    result.broker_connected = mqtt_->isConnected();
    result.message = result.published ? "ESP32 watchdog control request published"
                                      : "Failed to publish ESP32 watchdog control request";

    if (result.published) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_request_id_ = result.request_id;
        last_request_payload_ = result.payload;
    }

    return result;
}

EspHealthStatusSnapshot EspHealthManager::getStatusSnapshot() const {
    EspHealthStatusSnapshot snapshot;
    snapshot.broker_connected = mqtt_->isConnected();
    snapshot.control_topic = control_topic_;
    snapshot.status_topic = status_topic_;

    std::lock_guard<std::mutex> lock(state_mutex_);
    snapshot.last_request_id = last_request_id_;
    snapshot.last_request_payload = last_request_payload_;
    snapshot.latest_status_json = latest_status_json_;
    snapshot.latest_status_valid_json = latest_status_valid_json_;
    snapshot.has_status = !latest_status_json_.empty();
    snapshot.status_sequence = status_sequence_;

    if (snapshot.has_status) {
        snapshot.last_status_age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - last_status_time_)
                                          .count();
    }

    return snapshot;
}

void EspHealthManager::handleMessage(const std::string& topic, const std::string& payload) {
    // 수신 payload 원문 저장 및 JSON 파싱 가능 여부 병행 기록.
    if (topic != status_topic_) {
        return;
    }

    const auto parsed = crow::json::load(payload);

    std::lock_guard<std::mutex> lock(state_mutex_);
    latest_status_json_ = payload;
    latest_status_valid_json_ = static_cast<bool>(parsed);
    last_status_time_ = std::chrono::steady_clock::now();
    ++status_sequence_;
}

std::string EspHealthManager::generateRequestId() const {
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const auto counter = ++g_request_counter;
    return "esp-watchdog-" + std::to_string(now_ms) + "-" + std::to_string(counter);
}

void registerEspHealthRoutes(crow::SimpleApp& app, EspHealthManager& esp_health_mgr) {
    // request 라우트: 제어 메시지 발행.
    // latest/status 라우트: 최근 수신 상태 스냅샷의 서로 다른 형태 노출.
    CROW_ROUTE(app, "/esp32/watchdog/request").methods(crow::HTTPMethod::POST)
    ([&esp_health_mgr]() {
        return makeCommandResponse(esp_health_mgr.requestPublishNow());
    });

    CROW_ROUTE(app, "/esp32/watchdog/latest")
    ([&esp_health_mgr]() {
        const auto snapshot = esp_health_mgr.getStatusSnapshot();
        crow::json::wvalue body;
        body["broker_connected"] = snapshot.broker_connected;
        body["has_status"] = snapshot.has_status;
        body["status_topic"] = snapshot.status_topic;
        body["control_topic"] = snapshot.control_topic;
        body["last_request_id"] = snapshot.last_request_id;
        body["last_status_age_ms"] = snapshot.last_status_age_ms;
        body["status_sequence"] = static_cast<unsigned long long>(snapshot.status_sequence);

        if (snapshot.has_status) {
            if (snapshot.latest_status_valid_json) {
                const auto parsed = crow::json::load(snapshot.latest_status_json);
                body["status_payload"] = crow::json::wvalue(parsed);
            } else {
                body["status_payload_raw"] = snapshot.latest_status_json;
            }
        }

        return crow::response(body);
    });

    CROW_ROUTE(app, "/esp32/watchdog/status")
    ([&esp_health_mgr]() {
        const auto snapshot = esp_health_mgr.getStatusSnapshot();
        crow::json::wvalue body;
        body["broker_connected"] = snapshot.broker_connected;
        body["has_status"] = snapshot.has_status;
        body["latest_status_valid_json"] = snapshot.latest_status_valid_json;
        body["control_topic"] = snapshot.control_topic;
        body["status_topic"] = snapshot.status_topic;
        body["last_request_id"] = snapshot.last_request_id;
        body["last_request_payload"] = snapshot.last_request_payload;
        body["last_status_age_ms"] = snapshot.last_status_age_ms;
        body["status_sequence"] = static_cast<unsigned long long>(snapshot.status_sequence);
        return crow::response(body);
    });
}
