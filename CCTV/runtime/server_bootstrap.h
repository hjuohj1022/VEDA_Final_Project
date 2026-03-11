#pragma once

#include <string>

#include "runtime_config.h"
#include "server_runtime.h"

std::string ResolveConfigPath(const std::string& configuredPath);
TlsServerConfig BuildTlsServerConfig(const RuntimeConfig& cfg, bool disableControlTls);
bool ValidateServerStartupConfig(const RuntimeConfig& cfg, int port, const TlsServerConfig& tlsCfg, std::string& outErr);
