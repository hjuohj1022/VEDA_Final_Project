#pragma once

#include <string>

#include <winsock2.h>

#include "request.h"
#include "runtime_config.h"
#include "server_runtime.h"

struct ParsedControlRequest {
    ServerClient client{};
    sockaddr_in clientAddr{};
    std::string line;
    Request request;
};

bool InitControlServerContext(const RuntimeConfig& cfg,
                              int port,
                              const std::string& bindAddress,
                              const TlsServerConfig& tlsCfg,
                              const std::string& listenSuffix,
                              ServerSocketContext& serverCtx);

bool AcceptParsedControlRequest(ServerSocketContext& serverCtx,
                                const RuntimeConfig& cfg,
                                ParsedControlRequest& outRequest,
                                const std::string& recvFailurePrefix = "");
