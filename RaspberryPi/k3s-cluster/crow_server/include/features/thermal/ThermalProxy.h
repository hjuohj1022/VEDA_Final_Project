#pragma once

#include "crow.h"

// 열화상 UDP 수신기를 제어하는 REST API와,
// 정규화된 열화상 프레임을 실시간으로 전달하는 웹소켓 라우트를 등록한다.
// 내부적으로는 백그라운드 수신 스레드, 이벤트 감지, 상태 조회 기능까지 함께 묶여 있다.
void registerThermalProxyRoutes(crow::SimpleApp& app);

// 서버 종료 시 열화상 수신 스레드와 MQTT 리소스를 순서대로 정리한다.
// 장시간 실행되던 백그라운드 작업이 남아 프로세스 종료를 막지 않도록 마지막 정리 지점을 제공한다.
void shutdownThermalProxy();
