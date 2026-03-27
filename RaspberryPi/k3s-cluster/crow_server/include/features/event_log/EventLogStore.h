#pragma once

#include <optional>
#include <string>
#include <vector>

// Input payload for inserting a new event log record.
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

// Stored event log record returned to API callers.
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

// Input payload for updating the operator-action columns of an existing event log.
struct EventLogActionUpdateParams {
    bool action_requested = true;
    std::optional<std::string> action_type;
    std::optional<std::string> action_result;
    std::optional<std::string> action_message;
};

// Inserts a new event row and optionally returns the generated identifier.
bool insertEventLog(const EventLogInsertParams& params, unsigned long long* inserted_id = nullptr);
// Loads recent event rows up to the caller-provided limit.
bool listEventLogs(int limit, std::vector<EventLogRecord>* out_records);
// Updates the operator action columns for one event row.
bool updateEventLogAction(unsigned long long id,
                          const EventLogActionUpdateParams& params,
                          bool* updated = nullptr);
// Deletes a single event row by identifier.
bool deleteEventLogById(unsigned long long id, bool* deleted = nullptr);
// Deletes all stored event rows.
bool deleteAllEventLogs();
