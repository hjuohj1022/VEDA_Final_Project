#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <mosquittopp.h>

#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class MqttManager : public mosqpp::mosquittopp {
public:
    using MessageCallback = std::function<void(const std::string&, const std::string&)>;

    MqttManager(const char* id, const char* host, int port);
    ~MqttManager();

    bool publishMessage(const std::string& topic, const std::string& payload);
    bool subscribeTopic(const std::string& topic, int qos = 0);

    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }
    bool isConnected() const { return connected_.load(); }

    void on_connect(int rc) override;
    void on_disconnect(int rc) override;
    void on_message(const struct mosquitto_message* message) override;

private:
    MessageCallback message_cb_;
    std::vector<std::pair<std::string, int>> subscriptions_;
    mutable std::mutex subscriptions_mutex_;
    std::atomic<bool> connected_{false};
};

#endif
