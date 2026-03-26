#include "../include/EventLogRoutes.h"

#include "../include/EventLogStore.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {
int parseLimit(const char* raw_limit)
{
    if (!raw_limit) {
        return 50;
    }

    char* end = nullptr;
    const long parsed = std::strtol(raw_limit, &end, 10);
    if (!end || *end != '\0') {
        return 50;
    }
    if (parsed < 1) {
        return 1;
    }
    if (parsed > 200) {
        return 200;
    }
    return static_cast<int>(parsed);
}

bool parseEventLogId(const std::string& raw_id, unsigned long long* out_id)
{
    if (!out_id || raw_id.empty()) {
        return false;
    }

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(raw_id.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return false;
    }

    *out_id = parsed;
    return true;
}
} // namespace

void registerEventLogRoutes(crow::SimpleApp& app, const EventLogRequestAuthorizer& is_authorized)
{
    CROW_ROUTE(app, "/events").methods(crow::HTTPMethod::GET)
    ([is_authorized](const crow::request& req) {
        if (!is_authorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        std::vector<EventLogRecord> records;
        if (!listEventLogs(parseLimit(req.url_params.get("limit")), &records)) {
            return crow::response(500, "Failed to load event logs");
        }

        crow::json::wvalue response;
        response["status"] = "success";
        response["count"] = static_cast<uint64_t>(records.size());

        crow::json::wvalue events(crow::json::type::List);
        for (size_t index = 0; index < records.size(); ++index) {
            const auto& record = records[index];
            events[index]["id"] = static_cast<uint64_t>(record.id);
            events[index]["source"] = record.source;
            events[index]["event_type"] = record.event_type;
            events[index]["severity"] = record.severity;
            events[index]["title"] = record.title;
            events[index]["message"] = record.message;
            events[index]["occurred_at"] = record.occurred_at;
            events[index]["action_requested"] = record.action_requested;

            if (record.frame_id) {
                events[index]["frame_id"] = *record.frame_id;
            }
            if (record.signal_value) {
                events[index]["signal_value"] = *record.signal_value;
            }
            if (record.threshold_value) {
                events[index]["threshold_value"] = *record.threshold_value;
            }
            if (record.hot_area_pixels) {
                events[index]["hot_area_pixels"] = *record.hot_area_pixels;
            }
            if (record.candidate_area) {
                events[index]["candidate_area"] = *record.candidate_area;
            }
            if (record.center_x) {
                events[index]["center_x"] = *record.center_x;
            }
            if (record.center_y) {
                events[index]["center_y"] = *record.center_y;
            }
            if (record.action_type) {
                events[index]["action_type"] = *record.action_type;
            }
            if (record.action_result) {
                events[index]["action_result"] = *record.action_result;
            }
            if (record.action_message) {
                events[index]["action_message"] = *record.action_message;
            }
            if (record.payload_json) {
                events[index]["payload_json"] = *record.payload_json;
            }
        }

        response["events"] = std::move(events);
        return crow::response(response);
    });

    CROW_ROUTE(app, "/events").methods(crow::HTTPMethod::DELETE)
    ([is_authorized](const crow::request& req) {
        if (!is_authorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        const char* raw_id = req.url_params.get("id");
        crow::json::wvalue response;
        response["status"] = "success";

        if (!raw_id) {
            if (!deleteAllEventLogs()) {
                return crow::response(500, "Failed to delete event logs");
            }
            response["deleted_all"] = true;
            return crow::response(response);
        }

        char* end = nullptr;
        const unsigned long long id = std::strtoull(raw_id, &end, 10);
        if (!end || *end != '\0') {
            return crow::response(400, "Invalid event log id");
        }

        bool deleted = false;
        if (!deleteEventLogById(id, &deleted)) {
            return crow::response(500, "Failed to delete event log");
        }
        if (!deleted) {
            return crow::response(404, "Event log not found");
        }

        response["deleted_id"] = static_cast<uint64_t>(id);
        return crow::response(response);
    });

    CROW_ROUTE(app, "/events/<string>/action").methods(crow::HTTPMethod::PATCH)
    ([is_authorized](const crow::request& req, const std::string& raw_id) {
        if (!is_authorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        unsigned long long id = 0;
        if (!parseEventLogId(raw_id, &id)) {
            return crow::response(400, "Invalid event log id");
        }

        const auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON body");
        }
        if (!body.has("action_type") || body["action_type"].t() != crow::json::type::String) {
            return crow::response(400, "action_type is required");
        }
        if (!body.has("action_result") || body["action_result"].t() != crow::json::type::String) {
            return crow::response(400, "action_result is required");
        }

        // Qt에서 보낸 비상 동작 결과를 기존 이벤트 row에 반영한다.
        EventLogActionUpdateParams params;
        params.action_requested = body.has("action_requested") ? body["action_requested"].b() : true;
        params.action_type = std::string(body["action_type"].s());
        params.action_result = std::string(body["action_result"].s());
        if (body.has("action_message") && body["action_message"].t() == crow::json::type::String) {
            params.action_message = std::string(body["action_message"].s());
        }

        bool updated = false;
        if (!updateEventLogAction(id, params, &updated)) {
            return crow::response(500, "Failed to update event action");
        }
        if (!updated) {
            return crow::response(404, "Event log not found");
        }

        crow::json::wvalue response;
        response["status"] = "success";
        response["updated_id"] = static_cast<uint64_t>(id);
        response["action_requested"] = params.action_requested;
        response["action_type"] = params.action_type.value_or("");
        response["action_result"] = params.action_result.value_or("");
        response["action_message"] = params.action_message.value_or("");
        return crow::response(response);
    });
}
