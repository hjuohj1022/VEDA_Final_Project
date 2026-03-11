#include "../include/CctvProxy.h"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <thread>
#include <mutex>
#include <set>

namespace {
std::set<crow::websocket::connection*> cctv_clients;
std::mutex clients_mutex;

struct AsyncCommandStatus {
    bool running = false;
    bool last_ok = true;
    std::string active_command;
    std::string last_command;
    std::string last_result = "No async command has run yet";
};

AsyncCommandStatus async_command_status;
std::mutex async_command_mutex;

crow::response makeCommandResponse(const std::string& command, const std::string& result) {
    crow::json::wvalue body;
    body["status"] = "OK";
    body["command"] = command;
    body["message"] = result;
    return crow::response(200, body);
}

crow::response makeAcceptedResponse(const std::string& command) {
    crow::json::wvalue body;
    body["status"] = "ACCEPTED";
    body["command"] = command;
    body["message"] = "Command queued for background execution";
    return crow::response(202, body);
}

crow::response makeBusyResponse(const std::string& active_command) {
    crow::json::wvalue body;
    body["status"] = "BUSY";
    body["message"] = "Another async CCTV command is already running";
    body["active_command"] = active_command;
    return crow::response(409, body);
}

bool isAsyncPreferredCommand(const std::string& command) {
    return (command == "pause") ||
           (command == "resume") ||
           (command == "stop") ||
           (command.rfind("channel=", 0) == 0) ||
           (command.rfind("pc_view", 0) == 0);
}

bool tryScheduleAsyncCommand(CctvManager& cctv_mgr, const std::string& command) {
    {
        std::lock_guard<std::mutex> lock(async_command_mutex);
        if (async_command_status.running) {
            return false;
        }
        async_command_status.running = true;
        async_command_status.active_command = command;
        async_command_status.last_command = command;
        async_command_status.last_result = "Command is running";
    }

    std::thread([&cctv_mgr, command]() {
        const std::string result = cctv_mgr.sendCommand(command);
        const bool ok = result.rfind("Error:", 0) != 0;

        std::lock_guard<std::mutex> lock(async_command_mutex);
        async_command_status.running = false;
        async_command_status.last_ok = ok;
        async_command_status.active_command.clear();
        async_command_status.last_command = command;
        async_command_status.last_result = result;
    }).detach();

    return true;
}

crow::response dispatchCommand(CctvManager& cctv_mgr, const std::string& command, bool force_async = false) {
    if (force_async || isAsyncPreferredCommand(command)) {
        if (!tryScheduleAsyncCommand(cctv_mgr, command)) {
            std::lock_guard<std::mutex> lock(async_command_mutex);
            return makeBusyResponse(async_command_status.active_command);
        }
        return makeAcceptedResponse(command);
    }

    return makeCommandResponse(command, cctv_mgr.sendCommand(command));
}
}  // namespace

void registerCctvProxyRoutes(crow::SimpleApp& app, CctvManager& cctv_mgr) {
    cctv_mgr.setStreamCallback([](const std::vector<uint8_t>& full_frame) {
        std::vector<crow::websocket::connection*> clients;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            if (cctv_clients.empty()) {
                return;
            }
            clients.assign(cctv_clients.begin(), cctv_clients.end());
        }

        if (clients.empty()) {
            return;
        }

        std::string binary_data(full_frame.begin(), full_frame.end());
        for (auto* client : clients) {
            client->send_binary(binary_data);
        }
    });

    CROW_ROUTE(app, "/cctv/control/start").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("channel") || !x.has("mode")) {
            return crow::response(400, "Invalid JSON");
        }

        std::string mode = x["mode"].s();
        std::transform(mode.begin(), mode.end(), mode.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if ((mode != "headless") && (mode != "gui")) {
            return crow::response(400, "Mode must be 'headless' or 'gui'");
        }
        const std::string cmd = "channel=" + std::to_string(x["channel"].i()) + " " + mode;
        return dispatchCommand(cctv_mgr, cmd, true);
    });

    CROW_ROUTE(app, "/cctv/control/command").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("command") || x["command"].t() != crow::json::type::String) {
            return crow::response(400, "Invalid JSON");
        }

        const std::string cmd = x["command"].s();
        if (cmd.empty()) {
            return crow::response(400, "Command is empty");
        }

        return dispatchCommand(cctv_mgr, cmd);
    });

    CROW_ROUTE(app, "/cctv/control/stop").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        return dispatchCommand(cctv_mgr, "stop", true);
    });

    CROW_ROUTE(app, "/cctv/control/pause").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        return dispatchCommand(cctv_mgr, "pause", true);
    });

    CROW_ROUTE(app, "/cctv/control/resume").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        return dispatchCommand(cctv_mgr, "resume", true);
    });

    CROW_ROUTE(app, "/cctv/control/view").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) {
            return crow::response(400, "Invalid JSON");
        }

        std::string cmd = "pc_view";
        if (x.has("rx")) {
            cmd += " rx=" + std::to_string(x["rx"].d());
        }
        if (x.has("ry")) {
            cmd += " ry=" + std::to_string(x["ry"].d());
        }
        if (x.has("flipx")) {
            cmd += " flipx=" + std::to_string(x["flipx"].b() ? 1 : 0);
        }
        if (x.has("flipy")) {
            cmd += " flipy=" + std::to_string(x["flipy"].b() ? 1 : 0);
        }
        if (x.has("flipz")) {
            cmd += " flipz=" + std::to_string(x["flipz"].b() ? 1 : 0);
        }
        if (x.has("wire")) {
            cmd += " wire=" + std::to_string(x["wire"].b() ? 1 : 0);
        }
        if (x.has("mesh")) {
            cmd += " mesh=" + std::to_string(x["mesh"].b() ? 1 : 0);
        }

        return dispatchCommand(cctv_mgr, cmd, true);
    });

    CROW_ROUTE(app, "/cctv/control/stream").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("stream") || x["stream"].t() != crow::json::type::String) {
            return crow::response(400, "Invalid JSON");
        }

        const std::string stream = x["stream"].s();
        if ((stream != "pc_stream") && (stream != "rgbd_stream") && (stream != "depth_stream")) {
            return crow::response(400, "Unsupported stream");
        }

        return makeCommandResponse(stream, cctv_mgr.sendCommand(stream));
    });

    CROW_WEBSOCKET_ROUTE(app, "/cctv/stream")
        .onopen([&](crow::websocket::connection& conn) {
            bool should_start_default_stream = false;
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                cctv_clients.insert(&conn);
                should_start_default_stream = (cctv_mgr.getStreamMode() == CctvStreamMode::NONE);
            }

            if (should_start_default_stream) {
                const std::string result = cctv_mgr.sendCommand("pc_stream");
                if (result.rfind("Error:", 0) == 0) {
                    std::cerr << "[CCTV_WS] Failed to start default pc_stream: " << result << std::endl;
                } else {
                    std::cout << "[CCTV_WS] Image stream started (default pc_stream)." << std::endl;
                }
            }
        })
        .onclose([&](crow::websocket::connection& conn, const std::string&) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            cctv_clients.erase(&conn);
        });

    CROW_ROUTE(app, "/cctv/status")
    ([&cctv_mgr]() {
        crow::json::wvalue result;
        result["backend_connected"] = cctv_mgr.isConnected();
        result["stream_mode"] = static_cast<int>(cctv_mgr.getStreamMode());
        {
            std::lock_guard<std::mutex> lock(async_command_mutex);
            result["async_running"] = async_command_status.running;
            result["async_active_command"] = async_command_status.active_command;
            result["async_last_command"] = async_command_status.last_command;
            result["async_last_result"] = async_command_status.last_result;
            result["async_last_ok"] = async_command_status.last_ok;
        }
        return crow::response(result);
    });
}
