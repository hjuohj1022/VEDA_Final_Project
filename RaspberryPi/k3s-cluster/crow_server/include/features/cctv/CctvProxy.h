#pragma once

#include "crow.h"

#include "features/cctv/CctvManager.h"

// Registers HTTP and WebSocket routes that bridge the CCTV backend to Crow clients.
void registerCctvProxyRoutes(crow::SimpleApp& app, CctvManager& cctv_mgr);

// Stops the background CCTV command worker during server shutdown.
void shutdownCctvProxyWorker();
