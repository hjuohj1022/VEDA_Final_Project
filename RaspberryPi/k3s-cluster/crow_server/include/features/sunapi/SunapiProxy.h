#pragma once

#include "crow.h"

// Registers REST routes that proxy authenticated SUNAPI camera requests.
void registerSunapiProxyRoutes(crow::SimpleApp& app);
