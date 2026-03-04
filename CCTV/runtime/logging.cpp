#include <iostream>
#include <string>

#include "logging.h"

void LogInfo(const std::string& msg) {
    std::cout << "[INFO] " << msg << std::endl;
}

void LogWarn(const std::string& msg) {
    std::cout << "[WARN] " << msg << std::endl;
}

void LogError(const std::string& msg) {
    std::cerr << "[ERR] " << msg << std::endl;
}
