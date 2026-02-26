#pragma once

#include <string>
#include <vector>

struct Request {
    int channel = -1;
    bool headless = false;
    bool headlessSet = false;
    bool gui = false;
    bool stop = false;
};

std::vector<std::string> SplitTokens(const std::string& line);
bool ParseInt(const std::string& s, int& out);
Request ParseRequest(const std::string& line);
