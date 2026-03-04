#include <iostream>

#include <winsock2.h>

#include "server_runtime.h"

bool InitServerSocket(int port, int backlog, ServerSocketContext& ctx) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }
    ctx.wsaStarted = true;

    ctx.server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx.server == INVALID_SOCKET) {
        std::cerr << "socket failed" << std::endl;
        WSACleanup();
        ctx.wsaStarted = false;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(ctx.server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed (port=" << port << ", wsa=" << WSAGetLastError() << ")" << std::endl;
        closesocket(ctx.server);
        ctx.server = INVALID_SOCKET;
        WSACleanup();
        ctx.wsaStarted = false;
        return false;
    }

    if (listen(ctx.server, backlog) == SOCKET_ERROR) {
        std::cerr << "listen failed" << std::endl;
        closesocket(ctx.server);
        ctx.server = INVALID_SOCKET;
        WSACleanup();
        ctx.wsaStarted = false;
        return false;
    }

    return true;
}

void ShutdownServerSocket(ServerSocketContext& ctx) {
    if (ctx.server != INVALID_SOCKET) {
        closesocket(ctx.server);
        ctx.server = INVALID_SOCKET;
    }
    if (ctx.wsaStarted) {
        WSACleanup();
        ctx.wsaStarted = false;
    }
}
