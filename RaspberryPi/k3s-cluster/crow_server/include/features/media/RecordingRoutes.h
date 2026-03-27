#pragma once

#include "crow.h"

#include "features/common/RequestAuthorizer.h"

// 인증된 사용자가 녹화 파일 목록을 조회하고, 파일을 삭제하고,
// 바이트 범위 지정 방식으로 스트리밍 다운로드할 수 있는 라우트를 등록한다.
// 파일명 검증과 범위 헤더 처리 규칙도 이 모듈 안에서 함께 담당한다.
void registerRecordingRoutes(crow::SimpleApp& app, const RequestAuthorizer& is_authorized);
