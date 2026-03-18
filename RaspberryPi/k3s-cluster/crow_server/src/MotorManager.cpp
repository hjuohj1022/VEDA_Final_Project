#include "../include/MotorManager.h"

#include <algorithm>
#include <cctype>

// 모터 제어용 REST API의 MQTT request-response 래핑부.
// 라우트는 JSON 검증 담당, MotorManager는 publish/응답대기/상태보관 담당.
namespace {
constexpr int kMinMotorIndex = 1;
constexpr int kMaxMotorIndex = 3;

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string trimLineEndings(std::string value) {
    while (!value.empty() && ((value.back() == '\n') || (value.back() == '\r'))) {
        value.pop_back();
    }
    return value;
}

bool isMotorIndexValid(int motor) {
    return (motor >= kMinMotorIndex) && (motor <= kMaxMotorIndex);
}

bool isDirectionValid(const std::string& direction) {
    return (direction == "left") || (direction == "right");
}

bool isMotorErrorResponse(const std::string& response) {
    return response.rfind("ERR", 0) == 0;
}

int clampServoAngle(int angle) {
    return std::max(0, std::min(180, angle));
}

crow::response makeMotorCommandResponse(const MotorCommandResult& result) {
    crow::json::wvalue body;
    body["ok"] = result.ok;
    body["command"] = result.command;
    body["published"] = result.published;
    body["timed_out"] = result.timed_out;
    body["broker_connected"] = result.broker_connected;
    body["response"] = result.response;

    if (result.command.empty()) {
        body["status"] = "INVALID_REQUEST";
        return crow::response(400, body);
    }

    if (result.timed_out) {
        body["status"] = "TIMEOUT";
        return crow::response(504, body);
    }

    if (!result.published || !result.broker_connected) {
        body["status"] = "MQTT_UNAVAILABLE";
        return crow::response(503, body);
    }

    if (isMotorErrorResponse(result.response)) {
        body["status"] = "ERR";
        return crow::response(400, body);
    }

    body["status"] = "OK";
    return crow::response(200, body);
}

bool tryReadJsonInt(const crow::json::rvalue& value, const char* key, int& out) {
    if (!value.has(key)) {
        return false;
    }

    const auto& field = value[key];
    if (field.t() != crow::json::type::Number) {
        return false;
    }

    out = field.i();
    return true;
}

bool tryReadJsonString(const crow::json::rvalue& value, const char* key, std::string& out) {
    if (!value.has(key)) {
        return false;
    }

    const auto& field = value[key];
    if (field.t() != crow::json::type::String) {
        return false;
    }

    out = field.s();
    return true;
}

crow::response makeInvalidBodyResponse(const std::string& message) {
    crow::json::wvalue body;
    body["status"] = "INVALID_REQUEST";
    body["message"] = message;
    return crow::response(400, body);
}
}  // 익명 네임스페이스

MotorManager::MotorManager(const std::string& broker_host,
                           int broker_port,
                           const std::string& client_id,
                           const std::string& control_topic,
                           const std::string& response_topic,
                           int timeout_ms)
    : mqtt_(std::make_unique<MqttManager>(client_id.c_str(), broker_host.c_str(), broker_port)),
      control_topic_(control_topic),
      response_topic_(response_topic),
      timeout_ms_(timeout_ms) {
    // response topic 선구독 및 도착 응답의 handleMessage() 집계.
    mqtt_->set_message_callback([this](const std::string& topic, const std::string& payload) {
        handleMessage(topic, payload);
    });
    mqtt_->subscribeTopic(response_topic_);
}

MotorCommandResult MotorManager::sendCommand(const std::string& command) {
    // 외부 노출 형태는 동기식 함수.
    // 내부 처리 순서: publish -> response topic 수신 대기 -> 최신 응답 반환.
    MotorCommandResult result;
    result.command = trimLineEndings(command);
    result.broker_connected = mqtt_->isConnected();

    if (result.command.empty()) {
        result.response = "Command is empty";
        return result;
    }

    std::unique_lock<std::mutex> request_lock(request_mutex_);

    std::uint64_t start_sequence = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        awaiting_response_ = true;
        last_command_ = result.command;
        start_sequence = response_sequence_;
    }

    result.published = mqtt_->publishMessage(control_topic_, result.command);
    result.broker_connected = mqtt_->isConnected();

    if (!result.published) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        awaiting_response_ = false;
        result.response = "Failed to publish MQTT command";
        return result;
    }

    const auto timeout = std::chrono::milliseconds(std::max(timeout_ms_, 1));
    std::unique_lock<std::mutex> state_lock(state_mutex_);
    const bool got_response = response_cv_.wait_for(state_lock, timeout, [&]() {
        return response_sequence_ > start_sequence;
    });

    result.broker_connected = mqtt_->isConnected();
    if (!got_response) {
        awaiting_response_ = false;
        result.timed_out = true;
        result.response = "Timed out waiting for motor response";
        return result;
    }

    awaiting_response_ = false;
    result.response = last_response_;
    result.ok = !last_response_is_error_;
    return result;
}

MotorCommandResult MotorManager::press(int motor, const std::string& direction) {
    return sendCommand("motor" + std::to_string(motor) + " " + direction + " press");
}

MotorCommandResult MotorManager::release(int motor) {
    return sendCommand("motor" + std::to_string(motor) + " release");
}

MotorCommandResult MotorManager::stop(int motor) {
    return sendCommand("motor" + std::to_string(motor) + " stop");
}

MotorCommandResult MotorManager::setAngle(int motor, int angle) {
    return sendCommand("motor" + std::to_string(motor) + " set " + std::to_string(clampServoAngle(angle)));
}

MotorCommandResult MotorManager::moveRelative(int motor, const std::string& direction, int degrees) {
    return sendCommand("motor" + std::to_string(motor) + " " + direction + " " + std::to_string(std::max(0, degrees)));
}

MotorCommandResult MotorManager::readAngles() {
    return sendCommand("read");
}

MotorCommandResult MotorManager::ping() {
    return sendCommand("ping");
}

MotorCommandResult MotorManager::stopAll() {
    return sendCommand("stopall");
}

MotorStatusSnapshot MotorManager::getStatus() const {
    MotorStatusSnapshot snapshot;
    snapshot.broker_connected = mqtt_->isConnected();
    snapshot.control_topic = control_topic_;
    snapshot.response_topic = response_topic_;
    snapshot.timeout_ms = timeout_ms_;

    std::lock_guard<std::mutex> lock(state_mutex_);
    snapshot.awaiting_response = awaiting_response_;
    snapshot.last_command = last_command_;
    snapshot.last_response = last_response_;
    snapshot.last_response_is_error = last_response_is_error_;
    snapshot.response_sequence = response_sequence_;

    if (response_sequence_ > 0) {
        snapshot.last_response_age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - last_response_time_)
                                            .count();
    }

    return snapshot;
}

void MotorManager::handleMessage(const std::string& topic, const std::string& payload) {
    // 현재 사용 대상은 response topic 하나, 해당 topic 메시지만 최신 응답으로 기록.
    if (topic != response_topic_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        last_response_ = trimLineEndings(payload);
        last_response_is_error_ = isMotorErrorResponse(last_response_);
        last_response_time_ = std::chrono::steady_clock::now();
        ++response_sequence_;
        awaiting_response_ = false;
    }

    response_cv_.notify_all();
}

void registerMotorRoutes(crow::SimpleApp& app, MotorManager& motor_mgr) {
    // 아래 라우트들의 공통 구조: JSON 본문 검증 후 MotorManager 공통 sendCommand 경로 진입.
    CROW_ROUTE(app, "/motor/control/command").methods(crow::HTTPMethod::POST)
    ([&motor_mgr](const crow::request& req) {
        const auto body = crow::json::load(req.body);
        if (!body) {
            return makeInvalidBodyResponse("Invalid JSON");
        }

        std::string command;
        if (!tryReadJsonString(body, "command", command)) {
            return makeInvalidBodyResponse("Field 'command' must be a string");
        }

        return makeMotorCommandResponse(motor_mgr.sendCommand(command));
    });

    CROW_ROUTE(app, "/motor/control/press").methods(crow::HTTPMethod::POST)
    ([&motor_mgr](const crow::request& req) {
        const auto body = crow::json::load(req.body);
        if (!body) {
            return makeInvalidBodyResponse("Invalid JSON");
        }

        int motor = 0;
        std::string direction;
        if (!tryReadJsonInt(body, "motor", motor) || !tryReadJsonString(body, "direction", direction)) {
            return makeInvalidBodyResponse("Fields 'motor' and 'direction' are required");
        }

        direction = toLowerCopy(direction);
        if (!isMotorIndexValid(motor)) {
            return makeInvalidBodyResponse("Motor index must be between 1 and 3");
        }
        if (!isDirectionValid(direction)) {
            return makeInvalidBodyResponse("Direction must be 'left' or 'right'");
        }

        return makeMotorCommandResponse(motor_mgr.press(motor, direction));
    });

    CROW_ROUTE(app, "/motor/control/release").methods(crow::HTTPMethod::POST)
    ([&motor_mgr](const crow::request& req) {
        const auto body = crow::json::load(req.body);
        if (!body) {
            return makeInvalidBodyResponse("Invalid JSON");
        }

        int motor = 0;
        if (!tryReadJsonInt(body, "motor", motor)) {
            return makeInvalidBodyResponse("Field 'motor' is required");
        }
        if (!isMotorIndexValid(motor)) {
            return makeInvalidBodyResponse("Motor index must be between 1 and 3");
        }

        return makeMotorCommandResponse(motor_mgr.release(motor));
    });

    CROW_ROUTE(app, "/motor/control/stop").methods(crow::HTTPMethod::POST)
    ([&motor_mgr](const crow::request& req) {
        const auto body = crow::json::load(req.body);
        if (!body) {
            return makeInvalidBodyResponse("Invalid JSON");
        }

        int motor = 0;
        if (!tryReadJsonInt(body, "motor", motor)) {
            return makeInvalidBodyResponse("Field 'motor' is required");
        }
        if (!isMotorIndexValid(motor)) {
            return makeInvalidBodyResponse("Motor index must be between 1 and 3");
        }

        return makeMotorCommandResponse(motor_mgr.stop(motor));
    });

    CROW_ROUTE(app, "/motor/control/set").methods(crow::HTTPMethod::POST)
    ([&motor_mgr](const crow::request& req) {
        const auto body = crow::json::load(req.body);
        if (!body) {
            return makeInvalidBodyResponse("Invalid JSON");
        }

        int motor = 0;
        int angle = 0;
        if (!tryReadJsonInt(body, "motor", motor) || !tryReadJsonInt(body, "angle", angle)) {
            return makeInvalidBodyResponse("Fields 'motor' and 'angle' are required");
        }
        if (!isMotorIndexValid(motor)) {
            return makeInvalidBodyResponse("Motor index must be between 1 and 3");
        }

        return makeMotorCommandResponse(motor_mgr.setAngle(motor, angle));
    });

    CROW_ROUTE(app, "/motor/control/move").methods(crow::HTTPMethod::POST)
    ([&motor_mgr](const crow::request& req) {
        const auto body = crow::json::load(req.body);
        if (!body) {
            return makeInvalidBodyResponse("Invalid JSON");
        }

        int motor = 0;
        int degrees = 0;
        std::string direction;
        if (!tryReadJsonInt(body, "motor", motor) ||
            !tryReadJsonInt(body, "degrees", degrees) ||
            !tryReadJsonString(body, "direction", direction)) {
            return makeInvalidBodyResponse("Fields 'motor', 'direction', and 'degrees' are required");
        }

        direction = toLowerCopy(direction);
        if (!isMotorIndexValid(motor)) {
            return makeInvalidBodyResponse("Motor index must be between 1 and 3");
        }
        if (!isDirectionValid(direction)) {
            return makeInvalidBodyResponse("Direction must be 'left' or 'right'");
        }
        if (degrees < 0) {
            return makeInvalidBodyResponse("Degrees must be zero or greater");
        }

        return makeMotorCommandResponse(motor_mgr.moveRelative(motor, direction, degrees));
    });

    CROW_ROUTE(app, "/motor/control/stopall").methods(crow::HTTPMethod::POST)
    ([&motor_mgr]() {
        return makeMotorCommandResponse(motor_mgr.stopAll());
    });

    CROW_ROUTE(app, "/motor/control/center").methods(crow::HTTPMethod::POST)
    ([&motor_mgr](const crow::request& req) {
        int angle = 90;
        if (!req.body.empty()) {
            const auto body = crow::json::load(req.body);
            if (!body) {
                return makeInvalidBodyResponse("Invalid JSON");
            }
            if (body.has("angle")) {
                if (!tryReadJsonInt(body, "angle", angle)) {
                    return makeInvalidBodyResponse("Field 'angle' must be a number");
                }
            }
        }

        crow::json::wvalue result;
        result["status"] = "OK";
        result["target_angle"] = clampServoAngle(angle);

        for (int motor = kMinMotorIndex; motor <= kMaxMotorIndex; ++motor) {
            const auto command_result = motor_mgr.setAngle(motor, angle);
            const std::string key = "motor" + std::to_string(motor);

            result[key]["ok"] = command_result.ok;
            result[key]["command"] = command_result.command;
            result[key]["response"] = command_result.response;
            result[key]["timed_out"] = command_result.timed_out;

            if (!command_result.ok) {
                result["status"] = command_result.timed_out ? "TIMEOUT" : "PARTIAL_ERROR";
            }
        }

        return crow::response(200, result);
    });

    CROW_ROUTE(app, "/motor/angles")
    ([&motor_mgr]() {
        return makeMotorCommandResponse(motor_mgr.readAngles());
    });

    CROW_ROUTE(app, "/motor/ping")
    ([&motor_mgr]() {
        return makeMotorCommandResponse(motor_mgr.ping());
    });

    CROW_ROUTE(app, "/motor/status")
    ([&motor_mgr]() {
        const auto status = motor_mgr.getStatus();
        crow::json::wvalue body;
        body["broker_connected"] = status.broker_connected;
        body["awaiting_response"] = status.awaiting_response;
        body["control_topic"] = status.control_topic;
        body["response_topic"] = status.response_topic;
        body["timeout_ms"] = status.timeout_ms;
        body["last_command"] = status.last_command;
        body["last_response"] = status.last_response;
        body["last_response_is_error"] = status.last_response_is_error;
        body["response_sequence"] = static_cast<unsigned long long>(status.response_sequence);
        body["last_response_age_ms"] = status.last_response_age_ms;
        return crow::response(body);
    });
}
