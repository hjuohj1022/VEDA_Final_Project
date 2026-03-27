#pragma once

#include <mosquittopp.h>

#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// mosquitto C 라이브러리는 프로세스 단위 초기화/정리 규칙을 요구하므로,
// 애플리케이션 시작과 종료 시점을 명확히 맞추기 위한 수명 관리 래퍼다.
class MqttLibraryGuard {
public:
    // 프로세스에서 mosquitto 라이브러리를 사용할 준비를 한 번만 수행한다.
    MqttLibraryGuard();
    // 프로세스 종료 시점에 전역 mosquitto 리소스를 정리한다.
    ~MqttLibraryGuard();

    MqttLibraryGuard(const MqttLibraryGuard&) = delete;
    MqttLibraryGuard& operator=(const MqttLibraryGuard&) = delete;
};

// mosquittopp 클라이언트를 프로젝트 공통 방식으로 감싼 얇은 어댑터다.
// 연결 상태 추적, 재연결 후 재구독, 메시지 콜백 전달을 한 곳에서 담당한다.
class MqttManager : public mosqpp::mosquittopp {
public:
    using MessageCallback = std::function<void(const std::string&, const std::string&)>;

    // 지정한 브로커에 연결을 시도하고 백그라운드 네트워크 루프를 시작한다.
    MqttManager(const char* id, const char* host, int port);
    // 네트워크 루프를 중단하고 브로커 연결을 안전하게 종료한다.
    ~MqttManager();

    // 브로커와 연결된 상태일 때만 즉시 publish를 시도한다.
    // 호출 성공 여부는 "전송 시도 가능 여부"를 의미하며, 브로커 수신 보장과는 다르다.
    bool publishMessage(const std::string& topic, const std::string& payload);
    // 구독 토픽을 현재 브로커에 등록하고, 재연결 시 자동 복구할 수 있게 내부 목록에도 기억한다.
    bool subscribeTopic(const std::string& topic, int qos = 0);

    // 수신 메시지를 상위 서비스 계층으로 전달할 콜백을 등록한다.
    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }
    // 현재 브로커 연결이 살아 있는지 빠르게 확인한다.
    bool isConnected() const { return connected_.load(); }

    // 브로커 연결 완료 시, 이전에 기억해 둔 토픽들을 다시 구독한다.
    void on_connect(int rc) override;
    // 브로커 연결이 끊겼음을 내부 상태에 반영한다.
    void on_disconnect(int rc) override;
    // mosquitto 콜백 시그니처를 프로젝트 공통 문자열 콜백 형태로 변환해 전달한다.
    void on_message(const struct mosquitto_message* message) override;

private:
    MessageCallback message_cb_;
    std::vector<std::pair<std::string, int>> subscriptions_;
    mutable std::mutex subscriptions_mutex_;
    std::atomic<bool> connected_{false};
};
