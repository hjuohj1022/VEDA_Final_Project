#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace thermal_dtls_gateway {

long long currentTimeMs();
std::string trimCopy(const std::string& value);
std::string envOrDefault(const char* name, const std::string& fallback);
std::string requireEnv(const char* name);
int envIntOrDefault(const char* name, int fallback);
std::uint16_t readBe16(const unsigned char* p);
bool envBoolOrDefault(const char* name, bool fallback);
std::vector<unsigned char> parseHex(const std::string& hex);
void printOpenSslErrors(const std::string& prefix);

} // namespace thermal_dtls_gateway
