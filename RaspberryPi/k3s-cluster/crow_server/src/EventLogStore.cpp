#include "../include/EventLogStore.h"

#include <mysql/mysql.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
constexpr char kDatabaseName[] = "veda_db";

using MysqlBindFlag = std::remove_pointer_t<decltype(std::declval<MYSQL_BIND>().is_null)>;

struct MysqlConnectionCloser {
    void operator()(MYSQL* connection) const
    {
        if (connection) {
            mysql_close(connection);
        }
    }
};

struct MysqlStatementCloser {
    void operator()(MYSQL_STMT* statement) const
    {
        if (statement) {
            mysql_stmt_close(statement);
        }
    }
};

struct MysqlResultCloser {
    void operator()(MYSQL_RES* result) const
    {
        if (result) {
            mysql_free_result(result);
        }
    }
};

using MysqlConnectionPtr = std::unique_ptr<MYSQL, MysqlConnectionCloser>;
using MysqlStatementPtr = std::unique_ptr<MYSQL_STMT, MysqlStatementCloser>;
using MysqlResultPtr = std::unique_ptr<MYSQL_RES, MysqlResultCloser>;

MysqlConnectionPtr openDatabaseConnection()
{
    const char* db_host = std::getenv("DB_HOST");
    const char* db_user = std::getenv("DB_USER");
    const char* db_pass = std::getenv("DB_PASSWORD");

    if (!db_host || !db_user || !db_pass) {
        std::cerr << "[EVENT_LOG][DB] Missing DB_HOST, DB_USER, or DB_PASSWORD" << std::endl;
        return {};
    }

    MysqlConnectionPtr connection(mysql_init(nullptr));
    if (!connection) {
        std::cerr << "[EVENT_LOG][DB] mysql_init failed" << std::endl;
        return {};
    }

    if (!mysql_real_connect(connection.get(), db_host, db_user, db_pass, kDatabaseName, 3306, nullptr, 0)) {
        std::cerr << "[EVENT_LOG][DB] " << mysql_error(connection.get()) << std::endl;
        return {};
    }

    if (mysql_set_character_set(connection.get(), "utf8mb4") != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to set utf8mb4 charset: "
                  << mysql_error(connection.get()) << std::endl;
        return {};
    }

    return connection;
}

MysqlStatementPtr prepareStatement(MYSQL* connection, const char* sql)
{
    MysqlStatementPtr statement(mysql_stmt_init(connection));
    if (!statement) {
        std::cerr << "[EVENT_LOG][DB] mysql_stmt_init failed" << std::endl;
        return {};
    }

    if (mysql_stmt_prepare(statement.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to prepare statement: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return {};
    }

    return statement;
}

void bindRequiredString(MYSQL_BIND& bind, const std::string& value, unsigned long& length)
{
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = const_cast<char*>(value.c_str());
    length = static_cast<unsigned long>(value.size());
    bind.length = &length;
}

void bindOptionalString(MYSQL_BIND& bind,
                        const std::optional<std::string>& value,
                        unsigned long& length,
                        MysqlBindFlag& is_null)
{
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_STRING;
    is_null = value.has_value() ? 0 : 1;
    bind.is_null = &is_null;
    if (!value) {
        return;
    }

    bind.buffer = const_cast<char*>(value->c_str());
    length = static_cast<unsigned long>(value->size());
    bind.length = &length;
}

void bindOptionalInt(MYSQL_BIND& bind, int& value, const std::optional<int>& source, MysqlBindFlag& is_null)
{
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    value = source.value_or(0);
    bind.buffer = &value;
    is_null = source.has_value() ? 0 : 1;
    bind.is_null = &is_null;
}

std::optional<int> parseOptionalInt(const char* text)
{
    if (!text) {
        return std::nullopt;
    }

    try {
        return std::stoi(text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::string> parseOptionalString(const char* text, unsigned long length)
{
    if (!text) {
        return std::nullopt;
    }
    return std::string(text, length);
}
} // namespace

bool insertEventLog(const EventLogInsertParams& params, unsigned long long* inserted_id)
{
    static constexpr char kSql[] =
        "INSERT INTO event_logs ("
        "source, event_type, severity, title, message, "
        "frame_id, signal_value, threshold_value, hot_area_pixels, candidate_area, center_x, center_y, "
        "action_requested, action_type, action_result, action_message, payload_json"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    auto connection = openDatabaseConnection();
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(connection.get(), kSql);
    if (!statement) {
        return false;
    }

    MYSQL_BIND bind[17];
    std::memset(bind, 0, sizeof(bind));

    unsigned long source_length = 0;
    unsigned long event_type_length = 0;
    unsigned long severity_length = 0;
    unsigned long title_length = 0;
    unsigned long message_length = 0;
    unsigned long action_type_length = 0;
    unsigned long action_result_length = 0;
    unsigned long action_message_length = 0;
    unsigned long payload_json_length = 0;

    int frame_id_value = 0;
    int signal_value_value = 0;
    int threshold_value_value = 0;
    int hot_area_pixels_value = 0;
    int candidate_area_value = 0;
    int center_x_value = 0;
    int center_y_value = 0;
    signed char action_requested_value = params.action_requested ? 1 : 0;

    MysqlBindFlag frame_id_null = 0;
    MysqlBindFlag signal_value_null = 0;
    MysqlBindFlag threshold_value_null = 0;
    MysqlBindFlag hot_area_pixels_null = 0;
    MysqlBindFlag candidate_area_null = 0;
    MysqlBindFlag center_x_null = 0;
    MysqlBindFlag center_y_null = 0;
    MysqlBindFlag action_type_null = 0;
    MysqlBindFlag action_result_null = 0;
    MysqlBindFlag action_message_null = 0;
    MysqlBindFlag payload_json_null = 0;

    bindRequiredString(bind[0], params.source, source_length);
    bindRequiredString(bind[1], params.event_type, event_type_length);
    bindRequiredString(bind[2], params.severity, severity_length);
    bindRequiredString(bind[3], params.title, title_length);
    bindRequiredString(bind[4], params.message, message_length);
    bindOptionalInt(bind[5], frame_id_value, params.frame_id, frame_id_null);
    bindOptionalInt(bind[6], signal_value_value, params.signal_value, signal_value_null);
    bindOptionalInt(bind[7], threshold_value_value, params.threshold_value, threshold_value_null);
    bindOptionalInt(bind[8], hot_area_pixels_value, params.hot_area_pixels, hot_area_pixels_null);
    bindOptionalInt(bind[9], candidate_area_value, params.candidate_area, candidate_area_null);
    bindOptionalInt(bind[10], center_x_value, params.center_x, center_x_null);
    bindOptionalInt(bind[11], center_y_value, params.center_y, center_y_null);

    std::memset(&bind[12], 0, sizeof(bind[12]));
    bind[12].buffer_type = MYSQL_TYPE_TINY;
    bind[12].buffer = &action_requested_value;

    bindOptionalString(bind[13], params.action_type, action_type_length, action_type_null);
    bindOptionalString(bind[14], params.action_result, action_result_length, action_result_null);
    bindOptionalString(bind[15], params.action_message, action_message_length, action_message_null);
    bindOptionalString(bind[16], params.payload_json, payload_json_length, payload_json_null);

    if (mysql_stmt_bind_param(statement.get(), bind) != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to bind params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to insert event log: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (inserted_id) {
        *inserted_id = static_cast<unsigned long long>(mysql_insert_id(connection.get()));
    }
    return true;
}

bool listEventLogs(int limit, std::vector<EventLogRecord>* out_records)
{
    if (!out_records) {
        return false;
    }

    auto connection = openDatabaseConnection();
    if (!connection) {
        return false;
    }

    const int safe_limit = std::clamp(limit, 1, 200);
    std::ostringstream query;
    query << "SELECT "
          << "id, source, event_type, severity, title, message, "
          << "DATE_FORMAT(occurred_at, '%Y-%m-%d %H:%i:%s') AS occurred_at, "
          << "frame_id, signal_value, threshold_value, hot_area_pixels, candidate_area, center_x, center_y, "
          << "action_requested, action_type, action_result, action_message, payload_json "
          << "FROM event_logs "
          << "ORDER BY occurred_at DESC, id DESC "
          << "LIMIT " << safe_limit;

    if (mysql_query(connection.get(), query.str().c_str()) != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to query event logs: "
                  << mysql_error(connection.get()) << std::endl;
        return false;
    }

    MysqlResultPtr result(mysql_store_result(connection.get()));
    if (!result) {
        std::cerr << "[EVENT_LOG][DB] Failed to store query result: "
                  << mysql_error(connection.get()) << std::endl;
        return false;
    }

    std::vector<EventLogRecord> records;
    while (MYSQL_ROW row = mysql_fetch_row(result.get())) {
        const unsigned long* lengths = mysql_fetch_lengths(result.get());
        if (!lengths) {
            continue;
        }

        EventLogRecord record;
        record.id = row[0] ? std::strtoull(row[0], nullptr, 10) : 0ULL;
        record.source = row[1] ? std::string(row[1], lengths[1]) : std::string{};
        record.event_type = row[2] ? std::string(row[2], lengths[2]) : std::string{};
        record.severity = row[3] ? std::string(row[3], lengths[3]) : std::string{};
        record.title = row[4] ? std::string(row[4], lengths[4]) : std::string{};
        record.message = row[5] ? std::string(row[5], lengths[5]) : std::string{};
        record.occurred_at = row[6] ? std::string(row[6], lengths[6]) : std::string{};
        record.frame_id = parseOptionalInt(row[7]);
        record.signal_value = parseOptionalInt(row[8]);
        record.threshold_value = parseOptionalInt(row[9]);
        record.hot_area_pixels = parseOptionalInt(row[10]);
        record.candidate_area = parseOptionalInt(row[11]);
        record.center_x = parseOptionalInt(row[12]);
        record.center_y = parseOptionalInt(row[13]);
        record.action_requested = row[14] && std::atoi(row[14]) != 0;
        record.action_type = parseOptionalString(row[15], lengths[15]);
        record.action_result = parseOptionalString(row[16], lengths[16]);
        record.action_message = parseOptionalString(row[17], lengths[17]);
        record.payload_json = parseOptionalString(row[18], lengths[18]);
        records.push_back(std::move(record));
    }

    *out_records = std::move(records);
    return true;
}

bool updateEventLogAction(unsigned long long id,
                          const EventLogActionUpdateParams& params,
                          bool* updated)
{
    static constexpr char kSql[] =
        "UPDATE event_logs "
        "SET action_requested = ?, action_type = ?, action_result = ?, action_message = ? "
        "WHERE id = ?";

    auto connection = openDatabaseConnection();
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(connection.get(), kSql);
    if (!statement) {
        return false;
    }

    MYSQL_BIND bind[5];
    std::memset(bind, 0, sizeof(bind));

    signed char action_requested_value = params.action_requested ? 1 : 0;
    unsigned long action_type_length = 0;
    unsigned long action_result_length = 0;
    unsigned long action_message_length = 0;
    MysqlBindFlag action_type_null = 0;
    MysqlBindFlag action_result_null = 0;
    MysqlBindFlag action_message_null = 0;

    std::memset(&bind[0], 0, sizeof(bind[0]));
    bind[0].buffer_type = MYSQL_TYPE_TINY;
    bind[0].buffer = &action_requested_value;

    bindOptionalString(bind[1], params.action_type, action_type_length, action_type_null);
    bindOptionalString(bind[2], params.action_result, action_result_length, action_result_null);
    bindOptionalString(bind[3], params.action_message, action_message_length, action_message_null);

    std::memset(&bind[4], 0, sizeof(bind[4]));
    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = &id;
    bind[4].is_unsigned = true;

    if (mysql_stmt_bind_param(statement.get(), bind) != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to bind action update params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to update event action: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (updated) {
        *updated = mysql_stmt_affected_rows(statement.get()) > 0;
    }
    return true;
}

bool deleteEventLogById(unsigned long long id, bool* deleted)
{
    static constexpr char kSql[] = "DELETE FROM event_logs WHERE id = ?";

    auto connection = openDatabaseConnection();
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(connection.get(), kSql);
    if (!statement) {
        return false;
    }

    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &id;
    bind[0].is_unsigned = true;

    if (mysql_stmt_bind_param(statement.get(), bind) != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to bind delete param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to delete event log: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (deleted) {
        *deleted = mysql_stmt_affected_rows(statement.get()) > 0;
    }
    return true;
}

bool deleteAllEventLogs()
{
    auto connection = openDatabaseConnection();
    if (!connection) {
        return false;
    }

    static constexpr char kSql[] = "DELETE FROM event_logs";
    if (mysql_query(connection.get(), kSql) != 0) {
        std::cerr << "[EVENT_LOG][DB] Failed to delete all event logs: "
                  << mysql_error(connection.get()) << std::endl;
        return false;
    }
    return true;
}
