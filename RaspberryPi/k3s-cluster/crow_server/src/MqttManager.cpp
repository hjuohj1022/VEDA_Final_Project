#include "../include/MqttManager.h"

#include <algorithm>
#include <cstring>
#include <iostream>

MqttManager::MqttManager(const char* id, const char* host, int port)
    : mosqpp::mosquittopp(id) {
    subscriptions_.push_back({"lepton/frame/#", 0});

    mosqpp::lib_init();

    constexpr int keepalive = 60;
    connect(host, port, keepalive);
    loop_start();

    std::cout << "[MQTT] Connecting to " << host << ":" << port << "..." << std::endl;
}

MqttManager::~MqttManager() {
    loop_stop();
    mosqpp::lib_cleanup();
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
    if (!message_cb_) {
        return;
    }

    std::string topic(message->topic);
    std::string payload(static_cast<const char*>(message->payload), message->payloadlen);
    message_cb_(topic, payload);
}
