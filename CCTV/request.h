#pragma once

#include <string>
#include <vector>

struct Request {
    int channel = -1;
    bool headless = false;
    bool headlessSet = false;
    bool gui = false;
    bool stop = false;
    bool depthStream = false;
    bool pcStream = false;
    bool pcView = false;
    bool rxSet = false;
    bool rySet = false;
    bool flipXSet = false;
    bool flipYSet = false;
    bool flipZSet = false;
    float rx = -20.0f;
    float ry = 35.0f;
    bool flipX = false;
    bool flipY = false;
    bool flipZ = false;
};

std::vector<std::string> SplitTokens(const std::string& line);
bool ParseInt(const std::string& s, int& out);
Request ParseRequest(const std::string& line);
