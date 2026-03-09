#include <iostream>
#include <string>

#include "logging.h"

namespace {
void LogTo(std::ostream& stream, const char* level, const std::string& msg) {
    stream << level << msg << '\n';
}
}  // namespace

void LogInfo(const std::string& msg) {
    LogTo(std::cout, "[INFO] ", msg);
}

void LogWarn(const std::string& msg) {
    LogTo(std::cout, "[WARN] ", msg);
}

void LogError(const std::string& msg) {
    LogTo(std::cerr, "[ERR] ", msg);
}
