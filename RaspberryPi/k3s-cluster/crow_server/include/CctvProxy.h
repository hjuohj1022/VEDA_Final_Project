#pragma once

#include "crow.h"
#include "CctvManager.h"

// CCTV 관련 REST 및 WebSocket 라우트 등록
void registerCctvProxyRoutes(crow::SimpleApp& app, CctvManager& cctv_mgr);
