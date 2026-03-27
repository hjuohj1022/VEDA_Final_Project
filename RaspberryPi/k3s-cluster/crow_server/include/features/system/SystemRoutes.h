#pragma once

#include "crow.h"

#include "features/common/RequestAuthorizer.h"

// 서버 헬스체크, 녹화 저장소 용량 조회, 스웨거 정적 문서 서빙 라우트를 등록한다.
// 보호가 필요한 엔드포인트는 is_authorized 콜백을 통해 공통 인증 로직을 재사용한다.
void registerSystemRoutes(crow::SimpleApp& app, const RequestAuthorizer& is_authorized);
