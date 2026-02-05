#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <mosquittopp.h>
#include <string>
#include <iostream>

class MqttManager : public mosquittopp::mosquittopp {
public:
    MqttManager(const char* id, const char* host, int port);
    ~MqttManager();

    // 메시지 발행 함수
    bool publishMessage(const std::string& topic, const std::string& payload);

    // (선택) 연결되었을 때 자동 호출되는 콜백
    void on_connect(int rc) override;
};

#endif
