#pragma once

#ifndef CROW_USE_BOOST
#define CROW_USE_BOOST
#endif
#include "crow.h"

// SUNAPI StreamingServer WebSocket 프록시 라우트 등록
void registerSunapiWsProxyRoutes(crow::SimpleApp& app);

