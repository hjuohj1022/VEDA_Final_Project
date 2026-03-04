#pragma once

#include <string>
#include <winsock2.h>

void SendResponse(SOCKET client, const std::string& msg);
