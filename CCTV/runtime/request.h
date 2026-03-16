#pragma once

#include <string>
#include <vector>

struct Request {
    int channel = -1;
    bool headless = false;
    bool headlessSet = false;
    bool gui = false;
    bool statusQuery = false;
    bool stop = false;
    bool pauseSet = false;
    bool pause = false;
    bool depthStream = false;
    bool rgbdStream = false;
    bool pcStream = false;
    bool pcView = false;
    bool rxSet = false;
    bool rySet = false;
    bool flipXSet = false;
    bool flipYSet = false;
    bool flipZSet = false;
    bool wireSet = false;
    bool meshSet = false;
    float rx = -20.0f;
    float ry = 35.0f;
    bool flipX = false;
    bool flipY = false;
    bool flipZ = false;
    bool wire = false;
    bool mesh = false;
};

std::vector<std::string> SplitTokens(const std::string& line);
bool ParseInt(const std::string& s, int& out);
Request ParseRequest(const std::string& line);
