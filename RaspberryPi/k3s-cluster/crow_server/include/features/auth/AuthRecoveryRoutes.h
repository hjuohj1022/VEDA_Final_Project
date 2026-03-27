#pragma once

#include "crow.h"

// 회원가입 이메일 인증, 비밀번호 재설정 요청, 재설정 완료 처리를 담당하는
// 인증 보조 라우트를 Crow 애플리케이션에 등록한다.
void registerAuthRecoveryRoutes(crow::SimpleApp& app);

// 회원가입 직전에 해당 사용자 ID와 이메일 조합에 대한 인증이 이미 완료되었는지 확인한다.
// 실패 원인은 error_message에 채워 상위 회원가입 라우트가 그대로 응답 메시지로 사용할 수 있다.
bool checkSignupEmailVerified(const std::string& user_id,
                              const std::string& email,
                              std::string* error_message);

// 회원가입이 성공한 뒤, 방금 사용한 이메일 인증 완료 기록을 "소모됨" 상태로 처리한다.
// 같은 인증 토큰이 재사용되지 않도록 보안 후처리를 담당한다.
bool consumeSignupEmailVerification(const std::string& user_id,
                                    const std::string& email,
                                    std::string* error_message);
