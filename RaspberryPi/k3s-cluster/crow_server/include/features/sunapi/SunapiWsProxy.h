#pragma once

#include "crow.h"

// Registers WebSocket routes that tunnel the SUNAPI streaming server.
void registerSunapiWsProxyRoutes(crow::SimpleApp& app);
