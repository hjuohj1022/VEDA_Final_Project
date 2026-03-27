#include "infra/mqtt/MqttManager.h"

#include <algorithm>
#include <cstring>
#include <iostream>

// 프로젝트 공통 MQTT 래퍼 구현 파일이다.
// mosquitto 전역 초기화 규칙과 재연결 후 재구독 동작을 표준화해
// 상위 기능 모듈이 MQTT 라이브러리 세부 구현에 덜 의존하도록 만든다.
MqttLibraryGuard::MqttLibraryGuard() {
    mosqpp::lib_init();
}

MqttLibraryGuard::~MqttLibraryGuard() {
    mosqpp::lib_cleanup();
}

MqttManager::MqttManager(const char* id, const char* host, int port)
    : mosqpp::mosquittopp(id) {
    // 재연결 시 자동 복구할 기본 구독 목록을 먼저 기록해 둔다.
    // 실제 브로커 구독은 연결이 완료된 뒤 on_connect()에서 수행한다.
    subscriptions_.push_back({"lepton/frame/#", 0});

    constexpr int keepalive = 60;
    connect(host, port, keepalive);
    loop_start();

    std::cout << "[MQTT] Connecting to " << host << ":" << port << "..." << std::endl;
}

MqttManager::~MqttManager() {
    disconnect();
    loop_stop();
}

bool MqttManager::publishMessage(const std::string& topic, const std::string& payload) {
    const int ret = publish(nullptr, topic.c_str(), static_cast<int>(payload.length()),
                            payload.c_str(), 1, false);

    if (ret == MOSQ_ERR_SUCCESS) {
        std::cout << "[MQTT] Sent: " << payload << " -> " << topic << std::endl;
        return true;
    }

    std::cerr << "[MQTT] Failed to send message. Error code: " << ret << std::endl;
    return false;
}

bool MqttManager::subscribeTopic(const std::string& topic, int qos) {
    if (topic.empty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        const auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                                     [&topic](const auto& entry) { return entry.first == topic; });
        if (it == subscriptions_.end()) {
            subscriptions_.push_back({topic, qos});
        }
    }

    if (!connected_) {
        return true;
    }

    const int ret = subscribe(nullptr, topic.c_str(), qos);
    if (ret == MOSQ_ERR_SUCCESS) {
        std::cout << "[MQTT] Subscribed to " << topic << std::endl;
        return true;
    }

    std::cerr << "[MQTT] Failed to subscribe to " << topic << ". Error code: " << ret << std::endl;
    return false;
}

void MqttManager::on_connect(int rc) {
    // 연결이 다시 살아나면 이전에 기억해 둔 구독 목록을 모두 재등록해
    // 상위 서비스가 연결 끊김을 별도로 복구하지 않아도 되게 한다.
    if (rc != 0) {
        connected_ = false;
        std::cerr << "[MQTT] Connection failed. Error: " << rc << std::endl;
        return;
    }

    connected_ = true;
    std::cout << "[MQTT] Connected successfully!" << std::endl;

    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    for (const auto& [topic, qos] : subscriptions_) {
        const int ret = subscribe(nullptr, topic.c_str(), qos);
        if (ret == MOSQ_ERR_SUCCESS) {
            std::cout << "[MQTT] Subscribed to " << topic << std::endl;
        } else {
            std::cerr << "[MQTT] Failed to subscribe to " << topic
                      << ". Error code: " << ret << std::endl;
        }
    }
}

void MqttManager::on_disconnect(int rc) {
    connected_ = false;
    std::cout << "[MQTT] Disconnected. Error: " << rc << std::endl;
}

void MqttManager::on_message(const struct mosquitto_message* message) {
    // mosquittopp 고유 콜백 시그니처를 프로젝트 공통 문자열 콜백으로 바꿔
    // 상위 계층이 토픽 이름과 메시지 본문만 보고 처리할 수 있게 만든다.
    if (!message_cb_) {
        return;
    }

    std::string topic(message->topic);
    std::string payload(static_cast<const char*>(message->payload), message->payloadlen);
    message_cb_(topic, payload);
}
