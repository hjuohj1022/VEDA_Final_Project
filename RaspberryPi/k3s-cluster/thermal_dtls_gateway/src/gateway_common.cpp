#include "thermal_dtls_gateway/gateway_common.h"

#include <openssl/err.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace thermal_dtls_gateway {

long long currentTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string trimCopy(const std::string& value)
{
    size_t begin = 0;
    size_t end = value.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string envOrDefault(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return fallback;
    }

    const std::string trimmed = trimCopy(value);
    return trimmed.empty() ? fallback : trimmed;
}

std::string requireEnv(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr) {
        throw std::runtime_error(std::string("Missing required environment variable: ") + name);
    }

    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        throw std::runtime_error(std::string("Environment variable is empty: ") + name);
    }
    return trimmed;
}

int envIntOrDefault(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return fallback;
    }

    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return fallback;
    }

    try {
        return std::stoi(trimmed);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("Invalid integer environment variable: ") + name);
    }
}

std::uint16_t readBe16(const unsigned char* p)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8)
                                      | static_cast<std::uint16_t>(p[1]));
}

bool envBoolOrDefault(const char* name, bool fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return fallback;
    }

    std::string trimmed = trimCopy(value);
    for (char& ch : trimmed) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    if (trimmed.empty()) {
        return fallback;
    }
    if (trimmed == "1" || trimmed == "true" || trimmed == "yes" || trimmed == "on") {
        return true;
    }
    if (trimmed == "0" || trimmed == "false" || trimmed == "no" || trimmed == "off") {
        return false;
    }

    throw std::runtime_error(std::string("Invalid boolean environment variable: ") + name);
}

std::vector<unsigned char> parseHex(const std::string& hex)
{
    std::string filtered;
    filtered.reserve(hex.size());
    for (char ch : hex) {
        if (std::isxdigit(static_cast<unsigned char>(ch))) {
            filtered.push_back(ch);
        }
    }

    if (filtered.size() % 2 != 0) {
        throw std::runtime_error("Hex input must contain an even number of digits");
    }

    std::vector<unsigned char> bytes;
    bytes.reserve(filtered.size() / 2);
    for (size_t i = 0; i < filtered.size(); i += 2) {
        const std::string pair = filtered.substr(i, 2);
        bytes.push_back(static_cast<unsigned char>(std::stoul(pair, nullptr, 16)));
    }
    return bytes;
}

void printOpenSslErrors(const std::string& prefix)
{
    std::cerr << prefix << '\n';
    ERR_print_errors_fp(stderr);
}

} // namespace thermal_dtls_gateway
