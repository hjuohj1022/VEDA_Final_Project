#include "../include/CctvProxy.h"
#include <set>
#include <mutex>
#include <iostream>

namespace {
    std::set<crow::websocket::connection*> cctv_clients;
    std::mutex clients_mutex;
}

void registerCctvProxyRoutes(crow::SimpleApp& app, CctvManager& cctv_mgr) {
    
    // 스트림 콜백 설정: 백엔드에서 데이터가 오면 모든 WS 클라이언트에게 전송
    cctv_mgr.setStreamCallback([](const FrameHeader& header, const std::vector<uint8_t>& payload) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (cctv_clients.empty()) return;

        // 헤더 + 페이로드 합치기
        std::vector<uint8_t> full_frame;
        full_frame.reserve(sizeof(header) + payload.size());
        uint8_t* h_ptr = (uint8_t*)&header;
        full_frame.insert(full_frame.end(), h_ptr, h_ptr + sizeof(header));
        full_frame.insert(full_frame.end(), payload.begin(), payload.end());

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

        std::string cmd = "channel=" + std::to_string(x["channel"].i()) + " " + x["mode"].s();
        std::string res = cctv_mgr.sendCommand(cmd);
        
        crow::json::wvalue result;
        result["status"] = "OK";
        result["message"] = res;
        return crow::response(result);
    });

    // 1.2 CCTV 중지
    CROW_ROUTE(app, "/cctv/control/stop").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        std::string res = cctv_mgr.sendCommand("stop");
        return crow::response(200, res);
    });

    // 1.3 CCTV 일시중지
    CROW_ROUTE(app, "/cctv/control/pause").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        std::string res = cctv_mgr.sendCommand("pause");
        return crow::response(200, res);
    });

    // 1.4 CCTV 재개
    CROW_ROUTE(app, "/cctv/control/resume").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr]() {
        std::string res = cctv_mgr.sendCommand("resume");
        return crow::response(200, res);
    });

    // 1.5 뷰 설정 변경
    CROW_ROUTE(app, "/cctv/control/view").methods(crow::HTTPMethod::POST)
    ([&cctv_mgr](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid JSON");

        // 명세서 형식: pc_view rx=-20.0 ry=35.0 flipx=0 flipy=0 flipz=0 wire=0 mesh=1
        std::string cmd = "pc_view";
        if (x.has("rx")) cmd += " rx=" + std::to_string(x["rx"].d());
        if (x.has("ry")) cmd += " ry=" + std::to_string(x["ry"].d());
        if (x.has("flipx")) cmd += " flipx=" + std::to_string(x["flipx"].b() ? 1 : 0);
        if (x.has("flipy")) cmd += " flipy=" + std::to_string(x["flipy"].b() ? 1 : 0);
        if (x.has("flipz")) cmd += " flipz=" + std::to_string(x["flipz"].b() ? 1 : 0);
        if (x.has("wire")) cmd += " wire=" + std::to_string(x["wire"].b() ? 1 : 0);
        if (x.has("mesh")) cmd += " mesh=" + std::to_string(x["mesh"].b() ? 1 : 0);

        std::string res = cctv_mgr.sendCommand(cmd);
        return crow::response(200, res);
    });

    // 2.1 포인트클라우드 스트림 (WebSocket)
    CROW_WEBSOCKET_ROUTE(app, "/cctv/stream")
        .onopen([&](crow::websocket::connection& conn) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            cctv_clients.insert(&conn);
            std::cout << "[CCTV_WS] Client connected. Total: " << cctv_clients.size() << std::endl;
            
            // 스트림이 아직 시작되지 않았다면 백엔드에 요청
            if (!cctv_mgr.isStreaming()) {
                cctv_mgr.sendCommand("pc_stream");
            }
        })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            cctv_clients.erase(&conn);
            std::cout << "[CCTV_WS] Client disconnected. Reason: " << reason << std::endl;
        });

    // 3.1 CCTV 상태 조회
    CROW_ROUTE(app, "/cctv/status")
    ([&cctv_mgr]() {
        crow::json::wvalue result;
        result["backend_connected"] = cctv_mgr.isConnected();
        result["streaming"] = cctv_mgr.isStreaming();
        return crow::response(result);
    });
}
