#pragma once

#include "crow.h"

#include <functional>

// Shared authorization callback used by protected Crow routes.
using RequestAuthorizer = std::function<bool(const crow::request&)>;
