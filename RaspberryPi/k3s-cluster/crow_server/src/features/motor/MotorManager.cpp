#include "features/motor/MotorManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

// 모터 제어 MQTT 요청-응답 래퍼 구현 파일이다.
// REST 라우트는 입력 검증만 담당하고,
// 실제 명령 발행, 응답 대기, 최근 상태 기록은 이 파일의 로직이 처리한다.
namespace {
constexpr int kMinMotorIndex = 1;
constexpr int kMaxMotorIndex = 3;
constexpr int kMinMotorSpeed = 1;
constexpr int kMaxMotorSpeed = 10;
constexpr char kDefaultEmergencySequenceName[] = "emergency_evacuation";
constexpr char kDefaultEmergencyScript[] =
    "motor2 set 30; motor2 set 47; motor3 set 104; motor2 set 55; "
    "motor3 set 77; motor2 set 65; motor3 set 90; motor2 set 70;";

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string trimWhitespace(std::string value) {
    const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };

    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(),
                             [&](unsigned char ch) { return !is_space(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](unsigned char ch) { return !is_space(ch); })
                    .base(),
                value.end());
    return value;
}

std::string trimLineEndings(std::string value) {
    while (!value.empty() && ((value.back() == '\n') || (value.back() == '\r'))) {
        value.pop_back();
    }
    return value;
}

std::string normalizeWhitespaceLower(std::string value) {
    value = trimLineEndings(std::move(value));

    std::string normalized;
    normalized.reserve(value.size());

    bool last_was_space = true;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            if (!last_was_space) {
                normalized.push_back(' ');
                last_was_space = true;
            }
            continue;
        }

        normalized.push_back(static_cast<char>(std::tolower(ch)));
        last_was_space = false;
    }

    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    return normalized;
}

bool isMotorIndexValid(int motor) {
    return (motor >= kMinMotorIndex) && (motor <= kMaxMotorIndex);
}

bool isMotorSpeedValid(int speed) {
    return (speed >= kMinMotorSpeed) && (speed <= kMaxMotorSpeed);
}

bool isDirectionValid(const std::string& direction) {
    return (direction == "left") || (direction == "right");
}

bool isMotorErrorResponse(const std::string& response) {
    return response.rfind("ERR", 0) == 0;
}

bool tryNormalizeLaserCommand(const std::string& raw_command, std::string& normalized_command) {
    const std::string normalized = normalizeWhitespaceLower(raw_command);

    if ((normalized == "on") || (normalized == "laser on")) {
        normalized_command = "laser on";
        return true;
    }

    if ((normalized == "off") || (normalized == "laser off")) {
        normalized_command = "laser off";
        return true;
    }

    return false;
}

int clampServoAngle(int angle) {
    return std::max(0, std::min(180, angle));
}

void writeMotorCommandFields(crow::json::wvalue& body, const MotorCommandResult& result) {
    body["ok"] = result.ok;
    body["command"] = result.command;
    body["published"] = result.published;
    body["timed_out"] = result.timed_out;
    body["broker_connected"] = result.broker_connected;
    body["response"] = result.response;
}

crow::response makeMotorCommandResponse(const MotorCommandResult& result) {
    crow::json::wvalue body;
    writeMotorCommandFields(body, result);

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

crow::json::wvalue makeStatusBody(const MotorStatusSnapshot& status) {
    crow::json::wvalue body;
    body["broker_connected"] = status.broker_connected;
    body["awaiting_response"] = status.awaiting_response;
    body["control_topic"] = status.control_topic;
    body["response_topic"] = status.response_topic;
    body["timeout_ms"] = status.timeout_ms;
    body["last_command"] = status.last_command;
    body["last_command_topic"] = status.last_command_topic;
    body["last_response"] = status.last_response;
    body["last_response_is_error"] = status.last_response_is_error;
    body["response_sequence"] = static_cast<unsigned long long>(status.response_sequence);
    body["last_response_age_ms"] = status.last_response_age_ms;
    return body;
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
std::string getEmergencyScript() {
    const char* configured = std::getenv("MOTOR_EMERGENCY_SCRIPT");
    if ((configured == nullptr) || (*configured == '\0')) {
        return kDefaultEmergencyScript;
    }

    const std::string trimmed = trimWhitespace(configured);
    return trimmed.empty() ? std::string(kDefaultEmergencyScript) : trimmed;
}

std::vector<std::string> splitEmergencyCommands(const std::string& raw_script) {
    std::vector<std::string> commands;
    size_t start = 0;

    while (start <= raw_script.size()) {
        const size_t delimiter_pos = raw_script.find(';', start);
        const size_t count = (delimiter_pos == std::string::npos) ? std::string::npos
                                                                  : (delimiter_pos - start);
        std::string command = trimWhitespace(raw_script.substr(start, count));
        if (!command.empty()) {
            commands.push_back(std::move(command));
        }

        if (delimiter_pos == std::string::npos) {
            break;
        }
        start = delimiter_pos + 1;
    }

    return commands;
}

crow::response makeEmergencySequenceResponse(MotorManager& motor_mgr) {
    const MotorStatusSnapshot status = motor_mgr.getStatus();
    const std::string raw_script = getEmergencyScript();
    const std::vector<std::string> commands = splitEmergencyCommands(raw_script);

    crow::json::wvalue body;
    body["status"] = "OK";
    body["sequence_name"] = kDefaultEmergencySequenceName;
    body["delivery_mode"] = "sequential_mqtt_commands";
    body["control_topic"] = status.control_topic;
    body["response_topic"] = status.response_topic;
    body["raw_script"] = raw_script;
    body["note"] =
        "Current STM32 firmware accepts one motor command per line, so the server "
        "splits the emergency script on ';' and publishes each step in order.";
    body["total_steps"] = static_cast<int>(commands.size());
    body["attempted_steps"] = 0;
    body["successful_steps"] = 0;
    body["published_steps"] = 0;
    body["stopped_early"] = false;

    if (commands.empty()) {
        body["status"] = "EMERGENCY_ROUTE_MISCONFIGURED";
        body["message"] = "MOTOR_EMERGENCY_SCRIPT is empty or contains no valid commands";
        return crow::response(500, body);
    }

    int attempted_steps = 0;
    int successful_steps = 0;
    int published_steps = 0;
    int http_status = 200;

    for (size_t index = 0; index < commands.size(); ++index) {
        const auto result = motor_mgr.sendCommand(commands[index]);
        auto& step = body["steps"][static_cast<unsigned>(index)];

        step["index"] = static_cast<int>(index + 1);
        writeMotorCommandFields(step, result);

        ++attempted_steps;
        if (result.published) {
            ++published_steps;
        }
        if (result.ok) {
            ++successful_steps;
            continue;
        }

        body["failed_step"] = static_cast<int>(index + 1);
        body["stopped_early"] = ((index + 1U) < commands.size());

        if (result.timed_out) {
            body["status"] = "TIMEOUT";
            http_status = 504;
        } else if (!result.published || !result.broker_connected) {
            body["status"] = "MQTT_UNAVAILABLE";
            http_status = 503;
        } else {
            body["status"] = "STEP_ERROR";
            http_status = 400;
        }
        break;
    }

    body["attempted_steps"] = attempted_steps;
    body["successful_steps"] = successful_steps;
    body["published_steps"] = published_steps;
    return crow::response(http_status, body);
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
    // 응답 토픽을 먼저 구독하고, 도착한 응답은 handleMessage()로 한곳에서 모아 처리한다.
    mqtt_->set_message_callback([this](const std::string& topic, const std::string& payload) {
        handleMessage(topic, payload);
    });
    mqtt_->subscribeTopic(response_topic_);
}

MotorCommandResult MotorManager::sendCommand(const std::string& command) {
    return sendCommandToTopic(control_topic_, command);
}

MotorCommandResult MotorManager::sendCommandToTopic(const std::string& control_topic, const std::string& command) {
    MotorCommandResult result;
    result.command = trimLineEndings(command);
    result.broker_connected = mqtt_->isConnected();

    if (result.command.empty()) {
        result.response = "Command is empty";
        return result;
    }

    const std::string publish_topic = trimLineEndings(control_topic);
    if (publish_topic.empty()) {
        result.response = "Control topic is empty";
        return result;
    }

    std::unique_lock<std::mutex> request_lock(request_mutex_);

    std::uint64_t start_sequence = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        awaiting_response_ = true;
        last_command_ = result.command;
        last_command_topic_ = publish_topic;
        start_sequence = response_sequence_;
    }

    result.published = mqtt_->publishMessage(publish_topic, result.command);
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
        result.response = "Timed out waiting for device response";
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

MotorCommandResult MotorManager::setSpeed(int motor, int speed) {
    return sendCommand("motor" + std::to_string(motor) + " speed " + std::to_string(speed));
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
    snapshot.last_command_topic = last_command_topic_;
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
    // 현재는 응답 토픽 하나만 사용하므로, 그 토픽에서 온 메시지만 최신 응답으로 기록한다.
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
    // 아래 라우트들은 모두 JSON 본문을 검증한 뒤 MotorManager의 공통 sendCommand 경로로 들어간다.
    CROW_ROUTE(app, "/motor/emergency").methods(crow::HTTPMethod::POST)
    ([&motor_mgr]() {
        return makeEmergencySequenceResponse(motor_mgr);
    });

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

    CROW_ROUTE(app, "/motor/control/speed").methods(crow::HTTPMethod::POST)
    ([&motor_mgr](const crow::request& req) {
        const auto body = crow::json::load(req.body);
        if (!body) {
            return makeInvalidBodyResponse("Invalid JSON");
        }

        int motor = 0;
        int speed = 0;
        if (!tryReadJsonInt(body, "motor", motor) || !tryReadJsonInt(body, "speed", speed)) {
            return makeInvalidBodyResponse("Fields 'motor' and 'speed' are required");
        }
        if (!isMotorIndexValid(motor)) {
            return makeInvalidBodyResponse("Motor index must be between 1 and 3");
        }
        if (!isMotorSpeedValid(speed)) {
            return makeInvalidBodyResponse("Speed must be between 1 and 10");
        }

        return makeMotorCommandResponse(motor_mgr.setSpeed(motor, speed));
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
        return crow::response(makeStatusBody(status));
    });
}

void registerLaserRoutes(crow::SimpleApp& app, MotorManager& motor_mgr, const std::string& laser_control_topic) {
    CROW_ROUTE(app, "/laser/control/command").methods(crow::HTTPMethod::POST)
    ([&motor_mgr, laser_control_topic](const crow::request& req) {
        const auto body = crow::json::load(req.body);
        if (!body) {
            return makeInvalidBodyResponse("Invalid JSON");
        }

        std::string command;
        if (!tryReadJsonString(body, "command", command)) {
            return makeInvalidBodyResponse("Field 'command' must be a string");
        }

        std::string normalized_command;
        if (!tryNormalizeLaserCommand(command, normalized_command)) {
            return makeInvalidBodyResponse("Command must be 'on', 'off', 'laser on', or 'laser off'");
        }

        return makeMotorCommandResponse(motor_mgr.sendCommandToTopic(laser_control_topic, normalized_command));
    });

    CROW_ROUTE(app, "/laser/control/on").methods(crow::HTTPMethod::POST)
    ([&motor_mgr, laser_control_topic]() {
        return makeMotorCommandResponse(motor_mgr.sendCommandToTopic(laser_control_topic, "laser on"));
    });

    CROW_ROUTE(app, "/laser/control/off").methods(crow::HTTPMethod::POST)
    ([&motor_mgr, laser_control_topic]() {
        return makeMotorCommandResponse(motor_mgr.sendCommandToTopic(laser_control_topic, "laser off"));
    });

    CROW_ROUTE(app, "/laser/status")
    ([&motor_mgr, laser_control_topic]() {
        const auto status = motor_mgr.getStatus();
        auto body = makeStatusBody(status);
        body["control_topic"] = laser_control_topic;
        body["bridge_default_control_topic"] = status.control_topic;
        return crow::response(body);
    });
}
