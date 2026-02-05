#include "../include/MqttManager.h"
#include <cstring> // for strlen

MqttManager::MqttManager(const char* id, const char* host, int port) 
    : mosquittopp::mosquittopp(id) {

    mosquittopp::lib_init(); // 라이브러리 초기화

    // 비동기 연결 시작
    connect_async(host, port, 60); 
    
    // 백그라운드 스레드에서 MQTT 루프 실행 (중요: Crow 서버 멈춤 방지)
    loop_start(); 
    std::cout << "[MQTT] Connecting to " << host << ":" << port << "..." << std::endl;
}

MqttManager::~MqttManager() {
    loop_stop();           // 루프 중지
    mosquittopp::lib_cleanup(); // 라이브러리 정리
}

bool MqttManager::publishMessage(const std::string& topic, const std::string& payload) {
    // publish(메시지ID포인터, 토픽, 길이, 데이터, QoS, Retain)
    int ret = publish(NULL, topic.c_str(), payload.length(), payload.c_str(), 1, false);
    
    if (ret == MOSQ_ERR_SUCCESS) {
        std::cout << "[MQTT] Sent: " << payload << " -> " << topic << std::endl;
        return true;
    } else {
        std::cerr << "[MQTT] Failed to send message. Error code: " << ret << std::endl;
        return false;
    }
}

void MqttManager::on_connect(int rc) {
    if (rc == 0) {
        std::cout << "[MQTT] Connected successfully!" << std::endl;
    } else {
        std::cerr << "[MQTT] Connection failed. Error: " << rc << std::endl;
    }
}
