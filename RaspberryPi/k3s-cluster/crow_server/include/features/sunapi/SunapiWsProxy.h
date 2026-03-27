#pragma once

#include "crow.h"

// Crow WebSocket 클라이언트와 SUNAPI StreamingServer 사이를 중계하는 라우트를 등록한다.
// 클라이언트별 업스트림 연결 생성, 프레임 전달, 종료 시 정리까지 이 모듈이 맡는다.
void registerSunapiWsProxyRoutes(crow::SimpleApp& app);
