#include "infra/mqtt/MqttManager.h"

#include <algorithm>
#include <cstring>
#include <iostream>

// mosquitto C 라이브러리의 프로세스 전역 초기화/정리 요구 사항 반영.
// main.cpp의 MqttLibraryGuard 수명을 앱 전체 수명과 일치.
MqttLibraryGuard::MqttLibraryGuard() {
    mosqpp::lib_init();
}

MqttLibraryGuard::~MqttLibraryGuard() {
    mosqpp::lib_cleanup();
}

MqttManager::MqttManager(const char* id, const char* host, int port)
    : mosqpp::mosquittopp(id) {
    // 기본 사용 토픽의 사전 기억.
    // 실제 subscribe 시점은 connect 완료 후 on_connect 경로.
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
    // 재연결 시 이전 구독 유지를 위한 subscriptions_ 전체 재등록.
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
    // mosquittopp 콜백 시그니처의 프로젝트 공통 콜백 형태 변환.
    if (!message_cb_) {
        return;
    }

    std::string topic(message->topic);
    std::string payload(static_cast<const char*>(message->payload), message->payloadlen);
    message_cb_(topic, payload);
}
