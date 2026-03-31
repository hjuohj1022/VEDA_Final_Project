#include "integrations/mail/AppScriptMailSender.h"

#include "crow.h"

#include <curl/curl.h>

#include <cstdlib>
#include <string>

// 구글 앱스 스크립트 기반 메일 전송 구현 파일이다.
// 인증 코드나 복구 코드를 메일 본문에 담아 전송할 때 필요한
// JSON 직렬화, HTTP POST 요청, 응답 수집 과정을 한 곳에서 담당한다.
namespace {

// JSON 문자열 안에 그대로 넣기 어려운 문자를 안전한 이스케이프 형태로 바꾼다.
std::string escapeJsonString(const std::string& text)
{
    std::string escaped;
    escaped.reserve(text.size() + 16);

    for (const unsigned char ch : text) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(static_cast<char>(ch));
            break;
        }
    }

    return escaped;
}

// curl이 내려주는 응답 본문 조각을 하나의 문자열 버퍼로 이어 붙이는 콜백이다.
size_t writeResponseCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    if (!ptr || !userdata) {
        return 0;
    }

    std::string* response_body = static_cast<std::string*>(userdata);
    response_body->append(ptr, size * nmemb);
    return size * nmemb;
}

// 주어진 환경 변수 값을 문자열로 읽고, 없으면 빈 문자열을 돌려준다.
std::string readEnvString(const char* key)
{
    const char* value = std::getenv(key);
    return value ? std::string(value) : std::string{};
}

}  // 익명 네임스페이스

// 앱스 스크립트 웹앱으로 실제 메일 발송 요청을 보내는 공개 진입점이다.
// 외부 호출부는 이 함수를 통해 메일 목적과 코드를 넘기고,
// 실패 시 상세 사유를 error_message로 받아 사용자 안내나 로그에 활용한다.
bool sendAppScriptMail(const std::string& to_email,
                       const std::string& code,
                       const std::string& purpose,
                       std::string* error_message)
{
    const std::string webapp_url = readEnvString("MAIL_APP_SCRIPT_URL");
    const std::string shared_secret = readEnvString("MAIL_SHARED_SECRET");
    const std::string timeout_ms_text = readEnvString("MAIL_APP_SCRIPT_TIMEOUT_MS");

    if (webapp_url.empty()) {
        if (error_message) {
            *error_message = "MAIL_APP_SCRIPT_URL 환경 변수가 설정되지 않았습니다.";
        }
        return false;
    }
    if (shared_secret.empty()) {
        if (error_message) {
            *error_message = "MAIL_SHARED_SECRET 환경 변수가 설정되지 않았습니다.";
        }
        return false;
    }

    long timeout_ms = 10000L;
    if (!timeout_ms_text.empty()) {
        try {
            timeout_ms = std::stol(timeout_ms_text);
        } catch (...) {
            timeout_ms = 10000L;
        }
    }

    const std::string payload =
        std::string("{")
        + "\"secret\":\"" + escapeJsonString(shared_secret) + "\","
        + "\"to\":\"" + escapeJsonString(to_email) + "\","
        + "\"code\":\"" + escapeJsonString(code) + "\","
        + "\"purpose\":\"" + escapeJsonString(purpose) + "\""
        + "}";

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error_message) {
            *error_message = "메일 발송 HTTP 클라이언트 초기화에 실패했습니다.";
        }
        return false;
    }

    std::string response_body;
    long response_code = 0;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, webapp_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeResponseCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    const CURLcode curl_result = curl_easy_perform(curl);
    if (curl_result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (curl_result != CURLE_OK) {
        if (error_message) {
            *error_message = "메일 발송 웹앱에 연결하지 못했습니다.";
        }
        return false;
    }

    if (response_code != 200) {
        if (error_message) {
            *error_message = "메일 발송 웹앱이 정상 응답을 반환하지 않았습니다.";
        }
        return false;
    }

    const auto json = crow::json::load(response_body);
    if (!json || json["status"].s() != "success") {
        if (error_message) {
            *error_message = "메일 발송 웹앱 처리에 실패했습니다.";
        }
        return false;
    }

    return true;
}
