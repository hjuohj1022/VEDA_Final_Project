#ifndef EVENT_LOG_STORE_H
#define EVENT_LOG_STORE_H

#include <optional>
#include <string>
#include <vector>

struct EventLogInsertParams {
    std::string source;
    std::string event_type;
    std::string severity;
    std::string title;
    std::string message;
    std::optional<int> frame_id;
    std::optional<int> signal_value;
    std::optional<int> threshold_value;
    std::optional<int> hot_area_pixels;
    std::optional<int> candidate_area;
    std::optional<int> center_x;
    std::optional<int> center_y;
    bool action_requested = false;
    std::optional<std::string> action_type;
    std::optional<std::string> action_result;
    std::optional<std::string> action_message;
    std::optional<std::string> payload_json;
};

struct EventLogRecord {
    unsigned long long id = 0;
    std::string source;
    std::string event_type;
    std::string severity;
    std::string title;
    std::string message;
    std::string occurred_at;
    std::optional<int> frame_id;
    std::optional<int> signal_value;
    std::optional<int> threshold_value;
    std::optional<int> hot_area_pixels;
    std::optional<int> candidate_area;
    std::optional<int> center_x;
    std::optional<int> center_y;
    bool action_requested = false;
    std::optional<std::string> action_type;
    std::optional<std::string> action_result;
    std::optional<std::string> action_message;
    std::optional<std::string> payload_json;
};

struct EventLogActionUpdateParams {
    bool action_requested = true;
    std::optional<std::string> action_type;
    std::optional<std::string> action_result;
    std::optional<std::string> action_message;
};

bool insertEventLog(const EventLogInsertParams& params, unsigned long long* inserted_id = nullptr);
bool listEventLogs(int limit, std::vector<EventLogRecord>* out_records);
// 저장된 이벤트 row의 비상 동작 상태를 최신 결과로 갱신한다.
bool updateEventLogAction(unsigned long long id,
                          const EventLogActionUpdateParams& params,
                          bool* updated = nullptr);
bool deleteEventLogById(unsigned long long id, bool* deleted = nullptr);
bool deleteAllEventLogs();

#endif // EVENT_LOG_STORE_H
