#pragma once

#include "crow.h"

#include "features/common/RequestAuthorizer.h"

// 인증된 사용자를 위한 녹화 목록, 삭제, 바이트 범위 스트리밍 라우트를 등록한다.
void registerRecordingRoutes(crow::SimpleApp& app, const RequestAuthorizer& is_authorized);
