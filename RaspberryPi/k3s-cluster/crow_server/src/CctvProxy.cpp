#include "../include/CctvProxy.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <thread>
#include <mutex>
#include <set>

// CCTV 제어용 REST API와 스트림 중계용 WebSocket API 등록부.
// 핵심 관심사: HTTP 요청의 명령 문자열 변환, 단일 백그라운드 worker 유지.
namespace {
// 현재 CCTV 바이너리 스트림을 구독 중인 Crow WebSocket 클라이언트 목록.
std::set<crow::websocket::connection*> cctv_clients;
std::mutex clients_mutex;

// 한 번에 하나만 실행되는 백그라운드 CCTV 제어 명령의 상태.
struct AsyncCommandStatus {
    bool running = false;
    bool last_ok = true;
    std::string active_command;
    std::string last_command;
    std::string last_result = "No async command has run yet";
};

AsyncCommandStatus async_command_status;
std::mutex async_command_mutex;
std::mutex view_rotation_throttle_mutex;
std::chrono::steady_clock::time_point last_view_rotation_command_at;
bool has_last_view_rotation_command = false;

// 비동기 CCTV 제어 명령의 단일 실행 worker.
// detach() 미사용, join 가능한 단일 thread 유지 기반 종료 시점 정리 단순화.
class AsyncCommandWorker {
public:
    // 첫 비동기 요청 진입 시 worker thread 시작.
    void start(CctvManager& cctv_mgr) {
        std::lock_guard<std::mutex> lock(mutex_);
        cctv_mgr_ = &cctv_mgr;
        if (worker_thread_.joinable()) {
            return;
        }

        stopping_ = false;
        worker_thread_ = std::thread(&AsyncCommandWorker::run, this);
    }

    // 실행 중 작업 부재 시에만 새 명령 큐 적재.
    bool schedule(const std::string& command) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || !worker_thread_.joinable() || running_ || has_pending_command_ || !cctv_mgr_) {
                return false;
            }

            pending_command_ = command;
            has_pending_command_ = true;
        }

        {
            std::lock_guard<std::mutex> lock(async_command_mutex);
            async_command_status.running = true;
            async_command_status.active_command = command;
            async_command_status.last_command = command;
            async_command_status.last_result = "Command is running";
        }

        cv_.notify_one();
        return true;
    }

    // 서버 종료 시 대기 중 명령 정리 및 worker thread join.
    void shutdown() {
        std::string cancelled_command;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            if (has_pending_command_) {
                cancelled_command = pending_command_;
                pending_command_.clear();
                has_pending_command_ = false;
            }
        }

        if (!cancelled_command.empty()) {
            std::lock_guard<std::mutex> lock(async_command_mutex);
            async_command_status.running = false;
            async_command_status.last_ok = false;
            async_command_status.active_command.clear();
            async_command_status.last_command = cancelled_command;
            async_command_status.last_result = "Command cancelled during shutdown";
        }

        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

        std::lock_guard<std::mutex> lock(mutex_);
        cctv_mgr_ = nullptr;
        running_ = false;
        stopping_ = false;
    }

private:
    // condition_variable 기반 새 명령 대기 후 sendCommand() 호출.
    void run() {
        while (true) {
            std::string command;
            CctvManager* cctv_mgr = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return stopping_ || has_pending_command_; });
                if (stopping_ && !has_pending_command_) {
                    break;
                }

                command = std::move(pending_command_);
                pending_command_.clear();
                has_pending_command_ = false;
                running_ = true;
                cctv_mgr = cctv_mgr_;
            }

            const std::string result = cctv_mgr ? cctv_mgr->sendCommand(command)
                                                : "Error: CCTV async worker is unavailable";
            const bool ok = result.rfind("Error:", 0) != 0;

            {
                std::lock_guard<std::mutex> lock(async_command_mutex);
                async_command_status.running = false;
                async_command_status.last_ok = ok;
                async_command_status.active_command.clear();
                async_command_status.last_command = command;
                async_command_status.last_result = result;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_thread_;
    CctvManager* cctv_mgr_ = nullptr;
    bool stopping_ = false;
    bool running_ = false;
    bool has_pending_command_ = false;
    std::string pending_command_;
};

AsyncCommandWorker async_command_worker;

constexpr auto kViewRotationThrottleWindow = std::chrono::milliseconds(500);

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

crow::response makeThrottledViewResponse(const std::string& command, int retry_after_ms) {
    crow::json::wvalue body;
    body["status"] = "THROTTLED";
    body["command"] = command;
    body["message"] = "View rotation command ignored due to 500ms cooldown";
    body["retry_after_ms"] = retry_after_ms;
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
    async_command_worker.start(cctv_mgr);
    return async_command_worker.schedule(command);
}

// 명령 특성별 즉시 실행/worker 위임 결정.
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

// view 회전 명령의 과도한 연속 입력 방지용 별도 쿨다운.
crow::response dispatchViewCommand(CctvManager& cctv_mgr, const std::string& command, bool has_rotation_update) {
    if (!has_rotation_update) {
        return dispatchCommand(cctv_mgr, command, true);
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> throttle_lock(view_rotation_throttle_mutex);

    if (has_last_view_rotation_command) {
        const auto elapsed = now - last_view_rotation_command_at;
        if (elapsed < kViewRotationThrottleWindow) {
            const auto retry_after = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         kViewRotationThrottleWindow - elapsed)
                                         .count();
            return makeThrottledViewResponse(command, static_cast<int>(retry_after));
        }
    }

    if (!tryScheduleAsyncCommand(cctv_mgr, command)) {
        std::lock_guard<std::mutex> async_lock(async_command_mutex);
        return makeBusyResponse(async_command_status.active_command);
    }

    has_last_view_rotation_command = true;
    last_view_rotation_command_at = now;
    return makeAcceptedResponse(command);
}
}  // 익명 네임스페이스

void registerCctvProxyRoutes(crow::SimpleApp& app, CctvManager& cctv_mgr) {
    // CCTV 매니저 수신 바이너리 프레임의 현재 접속 중 모든 WebSocket 클라이언트 전달.
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
        // Stop should preempt stream/control flow immediately.
        // Avoid async BUSY gating so the command always reaches CCTV relay.
        return makeCommandResponse("stop", cctv_mgr.sendCommand("stop"));
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

        const bool has_rotation_update = x.has("rx") || x.has("ry");
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

        return dispatchViewCommand(cctv_mgr, cmd, has_rotation_update);
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

        std::cout << "[CCTV_API] /cctv/control/stream request stream=" << stream << std::endl;
        std::string result;
        bool ok = false;
        try {
            result = cctv_mgr.sendCommand(stream);
            ok = (result.rfind("Error:", 0) != 0);
        } catch (const std::exception& e) {
            result = std::string("Error: Exception in async command thread: ") + e.what();
            ok = false;
            std::cerr << "[CCTV_API][ASYNC] exception command=" << stream
                      << " what=" << e.what() << std::endl;
        } catch (...) {
            result = "Error: Unknown exception in async command thread";
            ok = false;
            std::cerr << "[CCTV_API][ASYNC] unknown exception command=" << stream << std::endl;
        }
        std::cout << "[CCTV_API] /cctv/control/stream response stream=" << stream
                  << " ok=" << (ok ? "true" : "false")
                  << " result=" << result.substr(0, 180) << std::endl;
        return makeCommandResponse(stream, result);
    });

    CROW_WEBSOCKET_ROUTE(app, "/cctv/stream")
        .onopen([&](crow::websocket::connection& conn) {
            bool should_start_default_stream = false;
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                cctv_clients.insert(&conn);
                should_start_default_stream = (cctv_mgr.getStreamMode() == CctvStreamMode::NONE);
            }

            // 첫 클라이언트 접속 + 스트림 모드 부재 시 기본 pc_stream 기동.
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

void shutdownCctvProxyWorker() {
    async_command_worker.shutdown();
}
