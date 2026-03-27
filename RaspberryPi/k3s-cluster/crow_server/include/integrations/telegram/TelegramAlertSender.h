#pragma once

#include <string>

// Telegram Bot API로 모바일 알림 메시지를 전송한다.
// 실패 사유가 있으면 error_message에 사람이 읽을 수 있는 설명을 채운다.
bool sendTelegramAlert(const std::string& text, std::string* error_message);
