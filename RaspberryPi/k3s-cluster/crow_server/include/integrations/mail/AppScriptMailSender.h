#pragma once

#include <string>

// Sends an email through the configured Apps Script webhook.
bool sendAppScriptMail(const std::string& to_email,
                       const std::string& code,
                       const std::string& purpose,
                       std::string* error_message);
