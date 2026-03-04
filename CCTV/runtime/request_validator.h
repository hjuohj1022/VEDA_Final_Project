#pragma once

#include <string>

#include "request.h"

struct RequestValidationResult {
    bool ok = true;
    std::string error;
};

RequestValidationResult ValidateRequest(const Request& req);
