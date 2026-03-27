#include "integrations/telegram/TelegramAlertSender.h"

#include "crow.h"

#include <curl/curl.h>

#include <cstdlib>
#include <string>

namespace {

// curl 응답 본문을 문자열 버퍼에 이어 붙인다.
size_t writeResponseCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    if (!ptr || !userdata) {
        return 0;
    }

    std::string* response_body = static_cast<std::string*>(userdata);
    response_body->append(ptr, size * nmemb);
    return size * nmemb;
}

// 환경 변수 문자열을 읽고 값이 없으면 빈 문자열을 돌려준다.
std::string readEnvString(const char* key)
{
    const char* value = std::getenv(key);
    return value ? std::string(value) : std::string{};
}

}  // namespace

// Telegram Bot API로 단일 텍스트 알림을 전송한다.
bool sendTelegramAlert(const std::string& text, std::string* error_message)
{
    const std::string bot_token = readEnvString("TELEGRAM_BOT_TOKEN");
    const std::string chat_id = readEnvString("TELEGRAM_CHAT_ID");
    const std::string timeout_ms_text = readEnvString("TELEGRAM_ALERT_TIMEOUT_MS");

    if (bot_token.empty()) {
        if (error_message) {
            *error_message = "TELEGRAM_BOT_TOKEN 환경 변수가 설정되지 않았습니다.";
        }
        return false;
    }
    if (chat_id.empty()) {
        if (error_message) {
            *error_message = "TELEGRAM_CHAT_ID 환경 변수가 설정되지 않았습니다.";
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

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error_message) {
            *error_message = "Telegram HTTP 클라이언트 초기화에 실패했습니다.";
        }
        return false;
    }

    const std::string endpoint = "https://api.telegram.org/bot" + bot_token + "/sendMessage";
    char* encoded_chat_id = curl_easy_escape(curl, chat_id.c_str(), static_cast<int>(chat_id.size()));
    char* encoded_text = curl_easy_escape(curl, text.c_str(), static_cast<int>(text.size()));

    if (!encoded_chat_id || !encoded_text) {
        if (error_message) {
            *error_message = "Telegram 요청 본문 인코딩에 실패했습니다.";
        }
        if (encoded_chat_id) {
            curl_free(encoded_chat_id);
        }
        if (encoded_text) {
            curl_free(encoded_text);
        }
        curl_easy_cleanup(curl);
        return false;
    }

    const std::string payload =
        std::string("chat_id=") + encoded_chat_id
        + "&text=" + encoded_text
        + "&disable_web_page_preview=true";

    curl_free(encoded_chat_id);
    curl_free(encoded_text);

    std::string response_body;
    long response_code = 0;

    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeResponseCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    const CURLcode curl_result = curl_easy_perform(curl);
    if (curl_result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }

    curl_easy_cleanup(curl);

    if (curl_result != CURLE_OK) {
        if (error_message) {
            *error_message = std::string("Telegram API 호출 실패: ") + curl_easy_strerror(curl_result);
        }
        return false;
    }

    if (response_code != 200) {
        if (error_message) {
            *error_message = "Telegram API가 정상 응답을 반환하지 않았습니다.";
        }
        return false;
    }

    const auto json = crow::json::load(response_body);
    if (!json || !json.has("ok") || !json["ok"].b()) {
        if (error_message) {
            *error_message = "Telegram API 응답 확인에 실패했습니다.";
        }
        return false;
    }

    return true;
}
