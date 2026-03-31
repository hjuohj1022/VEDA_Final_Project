#pragma once

#include <optional>
#include <string>
#include <vector>

// 새 이벤트 로그를 저장할 때 호출부가 넘겨주는 입력 파라미터 묶음이다.
// 열화상, 모터, 시스템 이벤트 등 서로 다른 소스가 공통 테이블 형식으로 저장될 수 있게 설계되어 있다.
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

// DB에서 읽어온 이벤트 로그 한 건을 API 계층에 넘길 때 사용하는 구조체다.
// 테이블 컬럼을 그대로 담되, 값이 없을 수 있는 항목은 std::optional로 표현한다.
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

// 기존 이벤트 로그의 "운영자 조치 결과" 컬럼만 갱신할 때 사용하는 입력 구조체다.
struct EventLogActionUpdateParams {
    bool action_requested = true;
    std::optional<std::string> action_type;
    std::optional<std::string> action_result;
    std::optional<std::string> action_message;
};

// 이벤트 로그를 새로 저장하고, 필요하면 방금 생성된 행 식별자까지 돌려준다.
bool insertEventLog(const EventLogInsertParams& params, unsigned long long* inserted_id = nullptr);
// 최근 이벤트 로그를 limit 개수만큼 불러와 out_records에 채운다.
bool listEventLogs(int limit, std::vector<EventLogRecord>* out_records);
// 특정 이벤트의 운영자 조치 결과 컬럼만 부분 갱신한다.
bool updateEventLogAction(unsigned long long id,
                          const EventLogActionUpdateParams& params,
                          bool* updated = nullptr);
// id 기준으로 이벤트 로그 한 건을 삭제한다.
bool deleteEventLogById(unsigned long long id, bool* deleted = nullptr);
// 저장된 이벤트 로그를 모두 삭제한다.
bool deleteAllEventLogs();
