#pragma once

#include "crow.h"

// 이메일 인증/비밀번호 재설정 라우트 등록 함수 선언
void registerAuthRecoveryRoutes(crow::SimpleApp& app);
