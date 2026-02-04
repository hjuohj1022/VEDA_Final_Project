#include "../include/crow_all.h"
#include "../include/MqttManager.h"

int main()
{
    crow::SimpleApp app;

    // 1. MQTT 매니저 생성
    // 중요: K3s 내부에서는 서비스 이름("mosquitto-service")을 호스트로 사용
    // 로컬 테스트라면 "127.0.0.1" 사용
    MqttManager mqtt("crow-server-client", "mosquitto-service", 1883);

    // 2. 기본 경로
    CROW_ROUTE(app, "/")([](){
        return "Hello, This is Crow + MQTT Server!";
    });
    
    
    // 3. 제어 API ( 추후변경 예정) 
    CROW_ROUTE(app, "/control/<string>")([&mqtt](std::string cmd){
        crow::json::wvalue x;
        std::string topic = "home/livingroom/led"; // Qt Client가 구독할 토픽
        
        if (cmd == "on") {
            x["status"] = "LED ON";
            // Qt Client에게 "ON" 메시지 전송
            mqtt.publishMessage(topic, "ON");
        } else {
            x["status"] = "LED OFF";
            // Qt Client에게 "OFF" 메시지 전송
            mqtt.publishMessage(topic, "OFF");
        }
        return x;
    });

    // 4. 서버 실행
    app.port(8080).multithreaded().run();
}
