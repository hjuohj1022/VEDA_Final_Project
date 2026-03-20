#pragma once

#include <string>

// Apps Script 웹앱으로 인증 메일 전송 요청 함수 선언
bool sendAppScriptMail(const std::string& to_email,
                       const std::string& code,
                       const std::string& purpose,
                       std::string* error_message);
