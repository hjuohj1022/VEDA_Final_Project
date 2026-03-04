#pragma once

#include <winsock2.h>

struct ServerSocketContext {
    SOCKET server = INVALID_SOCKET;
    bool wsaStarted = false;
};

bool InitServerSocket(int port, int backlog, ServerSocketContext& ctx);
void ShutdownServerSocket(ServerSocketContext& ctx);
