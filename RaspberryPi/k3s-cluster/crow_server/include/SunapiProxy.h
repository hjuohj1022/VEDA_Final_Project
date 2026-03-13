#pragma once

#include "crow.h"

// 역할:
// - Crow 서버 유입 인증 요청의 SUNAPI HTTP API 재전달
// - 재생, 내보내기, PTZ, 화면 설정 등 카메라 관련 엔드포인트 집약
void registerSunapiProxyRoutes(crow::SimpleApp& app);
