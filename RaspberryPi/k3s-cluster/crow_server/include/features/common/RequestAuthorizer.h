#pragma once

#include "crow.h"

#include <functional>

// 보호된 라우트에서 공통으로 사용하는 요청 인증 콜백 형식이다.
// 각 기능 모듈은 이 콜백으로 "이 요청을 처리해도 되는가"만 판단하고,
// 실제 JWT 검증, 사용자 존재 확인, 권한 판정 같은 세부 로직은 상위 계층에 맡긴다.
using RequestAuthorizer = std::function<bool(const crow::request&)>;
