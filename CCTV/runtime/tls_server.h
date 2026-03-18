#pragma once

#include <string>

#include "server_runtime.h"

bool TlsServerInit(const TlsServerConfig& cfg, void** outTlsCtx, std::string& outErr);
bool TlsServerAccept(void* tlsCtx, int socketFd, void** outSsl, std::string& outErr);
int TlsServerRecv(void* ssl, char* buf, int len);
int TlsServerSend(void* ssl, const char* data, int len);
std::string TlsServerGetLastIoError();
void TlsServerCloseClient(void* ssl);
void TlsServerShutdown(void* tlsCtx);
