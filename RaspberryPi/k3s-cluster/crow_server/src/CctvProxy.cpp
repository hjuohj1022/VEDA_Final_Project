#include "../include/CctvProxy.h"
#include <set>
#include <mutex>
#include <iostream>

namespace {
    std::set<crow::websocket::connection*> cctv_clients;
    std::mutex clients_mutex;
}

void registerCctvProxyRoutes(crow::SimpleApp& app, CctvManager& cctv_mgr) {
    
    // 스트림 콜백 설정: 백엔드에서 온 데이터(헤더 포함)를 그대로 모든 WS 클라이언트에게 전송
    cctv_mgr.setStreamCallback([](const std::vector<uint8_t>& full_frame) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (cctv_clients.empty()) return;

        std::string binary_data(full_frame.begin(), full_frame.end());
        for (auto client : cctv_clients) {
            client->send_binary(binary_data);
        }
    });

    // 1.1 CCTV 시작
    CROW_ROUTE(app, "/cctv/control/start").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("channel") || !x.has("mode")) return crow::response(400, "Invalid JSON");

        std::string mode = x["mode"].s();
        std::string cmd = "channel=" + std::to_string(x["channel"].i()) + " " + mode;
        std::string res = cctv_mgr.sendCommand(cmd);
        
        crow::json::wvalue result;
        result["status"] = "OK";
        result["message"] = res;
        return crow::response(result);
    });

    // 1.2 CCTV 중지
    CROW_ROUTE(app, "/cctv/control/stop").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        return crow::response(200, cctv_mgr.sendCommand("stop"));
    });

    // 1.5 뷰 설정 변경
    CROW_ROUTE(app, "/cctv/control/view").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid JSON");

        std::string cmd = "pc_view";
        if (x.has("rx")) cmd += " rx=" + std::to_string(x["rx"].d());
        if (x.has("ry")) cmd += " ry=" + std::to_string(x["ry"].d());
        if (x.has("flipx")) cmd += " flipx=" + std::to_string(x["flipx"].b() ? 1 : 0);
        if (x.has("flipy")) cmd += " flipy=" + std::to_string(x["flipy"].b() ? 1 : 0);
        if (x.has("flipz")) cmd += " flipz=" + std::to_string(x["flipz"].b() ? 1 : 0);
        if (x.has("wire")) cmd += " wire=" + std::to_string(x["wire"].b() ? 1 : 0);
        if (x.has("mesh")) cmd += " mesh=" + std::to_string(x["mesh"].b() ? 1 : 0);

        return crow::response(200, cctv_mgr.sendCommand(cmd));
    });

    // 2.1 포인트클라우드 스트림 (WebSocket)
    // 경로에 파라미터를 넣으면 onopen 시그니처와 충돌하므로 고정 경로 사용
    CROW_WEBSOCKET_ROUTE(app, "/cctv/stream")
        .onopen([&](crow::websocket::connection& conn) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            cctv_clients.insert(&conn);
            
            // 기본값으로 pc_stream 시작 (필요 시 클라이언트에서 제어 명령 전송)
            cctv_mgr.sendCommand("pc_stream");
            std::cout << "[CCTV_WS] Image stream started (default)." << std::endl;
        })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            cctv_clients.erase(&conn);
        });

    // 3.1 CCTV 상태 조회
    CROW_ROUTE(app, "/cctv/status")
    ([&cctv_mgr]() {
        crow::json::wvalue result;
        result["backend_connected"] = cctv_mgr.isConnected();
        result["stream_mode"] = static_cast<int>(cctv_mgr.getStreamMode());
        return crow::response(result);
    });
}
