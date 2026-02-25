#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

struct Request {
    int channel = -1;
    bool headless = false;
    bool headlessSet = false;
    bool gui = false;
    bool stop = false;
};

static std::vector<std::string> SplitTokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

static bool ParseInt(const std::string& s, int& out) {
    char* end = nullptr;
    long val = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = static_cast<int>(val);
    return true;
}

static Request ParseRequest(const std::string& line) {
    Request req;
    auto tokens = SplitTokens(line);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (t == "stop") {
            req.stop = true;
            continue;
        }
        if (t == "headless" || t == "headless=1" || t == "headless=true") {
            req.headless = true;
            req.headlessSet = true;
            continue;
        }
        if (t == "headless=0" || t == "headless=false") {
            req.headless = false;
            req.headlessSet = true;
            continue;
        }
        if (t == "gui" || t == "gui=1" || t == "gui=true") {
            req.gui = true;
            req.headlessSet = true;
            req.headless = false;
            continue;
        }
        if (t.rfind("channel=", 0) == 0) {
            int ch = -1;
            if (ParseInt(t.substr(8), ch)) req.channel = ch;
            continue;
        }
        if (t == "channel" && i + 1 < tokens.size()) {
            int ch = -1;
            if (ParseInt(tokens[i + 1], ch)) req.channel = ch;
            ++i;
            continue;
        }
        int ch = -1;
        if (ParseInt(t, ch)) {
            req.channel = ch;
            continue;
        }
    }
    return req;
}

static bool KillProcessByName(const std::string& exeName) {
    bool killedAny = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, exeName.c_str()) == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) {
                    TerminateProcess(h, 1);
                    CloseHandle(h);
                    killedAny = true;
                }
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return killedAny;
}

static std::string GetLastErrorString() {
    DWORD err = GetLastError();
    if (err == 0) return "0";
    char* msg = nullptr;
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&msg),
        0,
        nullptr
    );
    std::string out = len ? msg : "Unknown error";
    if (msg) LocalFree(msg);
    return out;
}

static bool FileExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool StartDepthProcess(const std::string& exePath, const Request& req) {
    if (!FileExists(exePath)) {
        std::cout << "[ENTRANCE] depth_trt.exe not found at: " << exePath << std::endl;
        return false;
    }

    std::string cmd = "\"" + exePath + "\"";
    if (req.headlessSet && req.headless) cmd += " --headless";
    if (req.headlessSet && !req.headless && req.gui) cmd += " --gui";
    if (req.channel >= 0) cmd += " --channel " + std::to_string(req.channel);

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    char cmdline[1024];
    std::snprintf(cmdline, sizeof(cmdline), "%s", cmd.c_str());

    std::string workdir = exePath;
    size_t pos = workdir.find_last_of("\\/");
    if (pos != std::string::npos) workdir = workdir.substr(0, pos);
    else workdir = ".";

    BOOL ok = CreateProcessA(
        nullptr,
        cmdline,
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_CONSOLE,
        nullptr,
        workdir.c_str(),
        &si,
        &pi
    );
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    std::cout << "[ENTRANCE] CreateProcess failed: " << GetLastErrorString() << std::endl;
    return false;
}

static void SendResponse(SOCKET client, const std::string& msg) {
    send(client, msg.c_str(), static_cast<int>(msg.size()), 0);
}

int main(int argc, char** argv) {
    int port = 9090;
    std::string exePath = "depth_trt.exe";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--port=", 0) == 0) {
            port = std::stoi(arg.substr(7));
        } else if (arg.rfind("--exe=", 0) == 0) {
            exePath = arg.substr(6);
        }
    }

    std::cout << "[ENTRANCE] depth_trt path: " << exePath << std::endl;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "socket failed" << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    if (listen(server, 5) == SOCKET_ERROR) {
        std::cerr << "listen failed" << std::endl;
        closesocket(server);
        WSACleanup();
        return 1;
    }

    std::cout << "[ENTRANCE] Listening on port " << port << std::endl;

    while (true) {
        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET client = accept(server, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (client == INVALID_SOCKET) continue;

        char buf[1024];
        int len = recv(client, buf, sizeof(buf) - 1, 0);
        if (len <= 0) {
            std::cout << "[ENTRANCE] Client connected but sent no data" << std::endl;
            closesocket(client);
            continue;
        }
        buf[len] = '\0';

        std::string line(buf);
        {
            char ip[64] = {0};
            inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
            std::cout << "[ENTRANCE] Request from " << ip << ":" << ntohs(clientAddr.sin_port)
                      << " -> " << line << std::endl;
        }
        Request req = ParseRequest(line);

        if (req.channel < -1 || req.channel > 3) {
            std::cout << "[ENTRANCE] Invalid channel request" << std::endl;
            SendResponse(client, "ERR invalid channel\n");
            closesocket(client);
            continue;
        }

        std::cout << "[ENTRANCE] Killing existing depth_trt.exe..." << std::endl;
        KillProcessByName("depth_trt.exe");

        if (req.stop) {
            std::cout << "[ENTRANCE] Stop request processed" << std::endl;
            SendResponse(client, "OK stopped\n");
            closesocket(client);
            continue;
        }

        bool started = StartDepthProcess(exePath, req);
        if (started) {
            std::cout << "[ENTRANCE] depth_trt.exe started" << std::endl;
            SendResponse(client, "OK started\n");
        } else {
            std::cout << "[ENTRANCE] depth_trt.exe failed to start" << std::endl;
            SendResponse(client, "ERR start failed\n");
        }

        closesocket(client);
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
