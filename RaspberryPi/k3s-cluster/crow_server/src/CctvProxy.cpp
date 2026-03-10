#include "../include/CctvProxy.h"

#include <iostream>
#include <mutex>
#include <set>

namespace {
std::set<crow::websocket::connection*> cctv_clients;
std::mutex clients_mutex;

crow::response makeCommandResponse(const std::string& command, const std::string& result) {
    crow::json::wvalue body;
    body["status"] = "OK";
    body["command"] = command;
    body["message"] = result;
    return crow::response(200, body);
}
}  // namespace

void registerCctvProxyRoutes(crow::SimpleApp& app, CctvManager& cctv_mgr) {
    cctv_mgr.setStreamCallback([](const std::vector<uint8_t>& full_frame) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (cctv_clients.empty()) {
            return;
        }

        std::string binary_data(full_frame.begin(), full_frame.end());
        for (auto* client : cctv_clients) {
            client->send_binary(binary_data);
        }
    });

    CROW_ROUTE(app, "/cctv/control/start").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("channel") || !x.has("mode")) {
            return crow::response(400, "Invalid JSON");
        }

        const std::string mode = x["mode"].s();
        const std::string cmd = "channel=" + std::to_string(x["channel"].i()) + " " + mode;
        return makeCommandResponse(cmd, cctv_mgr.sendCommand(cmd));
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

        return makeCommandResponse(cmd, cctv_mgr.sendCommand(cmd));
    });

    CROW_ROUTE(app, "/cctv/control/stop").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        return makeCommandResponse("stop", cctv_mgr.sendCommand("stop"));
    });

    CROW_ROUTE(app, "/cctv/control/pause").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        return makeCommandResponse("pause", cctv_mgr.sendCommand("pause"));
    });

    CROW_ROUTE(app, "/cctv/control/resume").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        return makeCommandResponse("resume", cctv_mgr.sendCommand("resume"));
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

        return makeCommandResponse(cmd, cctv_mgr.sendCommand(cmd));
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
            std::lock_guard<std::mutex> lock(clients_mutex);
            cctv_clients.insert(&conn);
            if (cctv_mgr.getStreamMode() == CctvStreamMode::NONE) {
                cctv_mgr.sendCommand("pc_stream");
                std::cout << "[CCTV_WS] Image stream started (default pc_stream)." << std::endl;
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
        return crow::response(result);
    });
}
