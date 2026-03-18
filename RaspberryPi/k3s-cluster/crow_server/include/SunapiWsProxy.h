#pragma once

#include "crow.h"

// 역할:
// - Crow WebSocket 클라이언트와 SUNAPI StreamingServer 사이의 중계
// - 업스트림 연결 생성, 양방향 메시지 전달, 종료 시 정리 담당
void registerSunapiWsProxyRoutes(crow::SimpleApp& app);
