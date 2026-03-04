#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <mosquittopp.h>
#include <string>
#include <iostream>
#include <functional>

class MqttManager : public mosqpp::mosquittopp {
public:
    using MessageCallback = std::function<void(const std::string&, const std::string&)>;

    MqttManager(const char* id, const char* host, int port);
    ~MqttManager();

    // 메시지 발행 함수
    bool publishMessage(const std::string& topic, const std::string& payload);

    // 콜백 등록
    void set_message_callback(MessageCallback cb) { message_cb_ = cb; }

    // (선택) 연결되었을 때 자동 호출되는 콜백
    void on_connect(int rc) override;
    void on_message(const struct mosquitto_message* message) override;

private:
    MessageCallback message_cb_;
};

#endif
