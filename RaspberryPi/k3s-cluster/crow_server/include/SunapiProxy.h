#pragma once

#include "crow.h"

// SUNAPI 제어 요청을 카메라로 프록시하는 라우트를 등록
void registerSunapiProxyRoutes(crow::SimpleApp& app);

