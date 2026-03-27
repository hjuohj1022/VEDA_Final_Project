#pragma once

#include <string>

// 구글 앱스 스크립트 웹훅을 호출해 메일을 전송한다.
// 주로 회원가입 인증, 비밀번호 재설정 같은 보안성 메일을 보내는 데 사용하며,
// 실패 시 상세 원인을 error_message에 채워 호출부가 사용자 응답이나 운영 로그에 활용할 수 있게 한다.
bool sendAppScriptMail(const std::string& to_email,
                       const std::string& code,
                       const std::string& purpose,
                       std::string* error_message);
