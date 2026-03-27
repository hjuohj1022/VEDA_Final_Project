#pragma once

#include "crow.h"

// 열화상 UDP 수신 결과를 REST/WebSocket으로 노출하는 라우트를 등록한다.
void registerThermalProxyRoutes(crow::SimpleApp& app);

// 서버 종료 시 백그라운드 열화상 수신 스레드를 안전하게 정리한다.
void shutdownThermalProxy();
