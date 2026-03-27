#pragma once

#include "crow.h"

// Registers signup-email verification and password-recovery routes.
void registerAuthRecoveryRoutes(crow::SimpleApp& app);

// Checks whether the pending signup email verification has been completed.
bool checkSignupEmailVerified(const std::string& user_id,
                              const std::string& email,
                              std::string* error_message);

// Marks a completed signup verification token as consumed after registration succeeds.
bool consumeSignupEmailVerification(const std::string& user_id,
                                    const std::string& email,
                                    std::string* error_message);
