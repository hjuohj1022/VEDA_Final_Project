#pragma once

#include "crow.h"

// 이메일 인증/비밀번호 재설정 라우트 등록 함수 선언
void registerAuthRecoveryRoutes(crow::SimpleApp& app);

// 회원가입 전 이메일 인증 완료 여부 조회 함수
bool checkSignupEmailVerified(const std::string& user_id,
                              const std::string& email,
                              std::string* error_message);

// 회원가입 완료 시 이메일 인증 토큰 사용 처리 함수
bool consumeSignupEmailVerification(const std::string& user_id,
                                    const std::string& email,
                                    std::string* error_message);
