#pragma once

#include "crow.h"

#include "features/common/RequestAuthorizer.h"

// 인증된 사용자가 이벤트 로그를 조회, 수정, 삭제할 수 있도록
// event-log 관련 CRUD 라우트를 Crow 애플리케이션에 등록한다.
void registerEventLogRoutes(crow::SimpleApp& app, const RequestAuthorizer& is_authorized);
