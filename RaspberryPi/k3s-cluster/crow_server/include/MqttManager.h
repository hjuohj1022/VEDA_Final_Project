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

// 프로세스 전체에서 한 번만 필요한 mosquitto 전역 초기화/정리 담당.
class MqttLibraryGuard {
public:
    MqttLibraryGuard();
    ~MqttLibraryGuard();

    MqttLibraryGuard(const MqttLibraryGuard&) = delete;
    MqttLibraryGuard& operator=(const MqttLibraryGuard&) = delete;
};

// 역할:
// - mosquittopp 클라이언트의 Crow 서버 친화적 래핑
// - 재연결 시 이전 구독 목록의 자동 재등록
// - 수신 메시지의 std::function 콜백 전달
class MqttManager : public mosqpp::mosquittopp {
public:
    using MessageCallback = std::function<void(const std::string&, const std::string&)>;

    MqttManager(const char* id, const char* host, int port);
    ~MqttManager();

    // 현재 브로커 연결 상태 기준 즉시 publish 시도.
    bool publishMessage(const std::string& topic, const std::string& payload);
    // 재연결 후 자동 복구용 topic/qos 내부 목록 저장.
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
