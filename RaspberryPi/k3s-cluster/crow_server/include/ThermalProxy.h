#pragma once

#include "crow.h"

// Registers REST/WebSocket routes for thermal UDP passthrough streaming.
void registerThermalProxyRoutes(crow::SimpleApp& app);

// Stops the background UDP receiver thread during server shutdown.
void shutdownThermalProxy();
