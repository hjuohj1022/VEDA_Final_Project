#pragma once

#include "crow.h"

#include "features/common/RequestAuthorizer.h"

// 시스템 상태, 저장소 용량, Swagger 문서 라우트를 등록한다.
void registerSystemRoutes(crow::SimpleApp& app, const RequestAuthorizer& is_authorized);
