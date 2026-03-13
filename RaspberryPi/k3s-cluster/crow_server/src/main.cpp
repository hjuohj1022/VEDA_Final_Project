#include "crow.h"
#include "../include/SunapiProxy.h"
#include "../include/SunapiWsProxy.h"
#include "../include/CctvManager.h"
#include "../include/CctvProxy.h"
#include "../include/EspHealthManager.h"
#include "../include/MqttManager.h"
#include "../include/MotorManager.h"
#include <jwt-cpp/jwt.h> 
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <cstdlib> 
#include <fstream> 
#include <iostream>
#include <memory>
#include <mysql/mysql.h>
#include <optional>
#include <sstream> 
#include <sys/statvfs.h> 
#include <algorithm>
#include <cctype>
#include <chrono>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

// crow_server의 진입점.
// 인증, 파일 스트리밍, SUNAPI 프록시, CCTV, MQTT 기반 장치 제어 라우트의 단일 프로세스 집약.
namespace fs = std::filesystem;

namespace {
// 인증/DB/파일 스트리밍 헬퍼의 라우트 바깥 배치.
// main()의 서비스 등록 흐름 집중 목적 구조.
constexpr char kDatabaseName[] = "veda_db";
constexpr size_t kMaxUserIdLength = 64;
constexpr size_t kMaxPasswordLength = 128;
constexpr size_t kStoredPasswordBufferBytes = 256;
constexpr int kPasswordHashIterations = 120000;
constexpr size_t kPasswordSaltBytes = 16;
constexpr size_t kPasswordHashBytes = 32;
constexpr char kPasswordHashPrefix[] = "pbkdf2_sha256";
constexpr long long kMaxStreamResponseBytes = 8LL * 1024LL * 1024LL;

using MysqlBindFlag = std::remove_pointer_t<decltype(std::declval<MYSQL_BIND>().is_null)>;

struct MysqlConnectionCloser {
    void operator()(MYSQL* connection) const {
        if (connection) {
            mysql_close(connection);
        }
    }
};

struct MysqlStatementCloser {
    void operator()(MYSQL_STMT* statement) const {
        if (statement) {
            mysql_stmt_close(statement);
        }
    }
};

using MysqlConnectionPtr = std::unique_ptr<MYSQL, MysqlConnectionCloser>;
using MysqlStatementPtr = std::unique_ptr<MYSQL_STMT, MysqlStatementCloser>;

std::optional<std::string> validateCredentials(const std::string& user_id,
                                               const std::string& password) {
    if (user_id.empty() || password.empty()) {
        return "ID and password are required";
    }
    if (user_id.size() > kMaxUserIdLength) {
        return "ID is too long";
    }
    if (password.size() > kMaxPasswordLength) {
        return "Password is too long";
    }
    return std::nullopt;
}

MysqlConnectionPtr openDatabaseConnection() {
    const char* db_host = std::getenv("DB_HOST");
    const char* db_user = std::getenv("DB_USER");
    const char* db_pass = std::getenv("DB_PASSWORD");

    if (!db_host || !db_user || !db_pass) {
        std::cerr << "[DB Error] Missing DB_HOST, DB_USER, or DB_PASSWORD" << std::endl;
        return {};
    }

    MysqlConnectionPtr connection(mysql_init(nullptr));
    if (!connection) {
        std::cerr << "[DB Error] mysql_init failed" << std::endl;
        return {};
    }

    if (!mysql_real_connect(connection.get(), db_host, db_user, db_pass, kDatabaseName, 3306, nullptr, 0)) {
        std::cerr << "[DB Error] " << mysql_error(connection.get()) << std::endl;
        return {};
    }

    return connection;
}

MysqlStatementPtr prepareStatement(MYSQL* connection, const char* sql) {
    if (!connection) {
        return {};
    }

    MysqlStatementPtr statement(mysql_stmt_init(connection));
    if (!statement) {
        std::cerr << "[DB Error] mysql_stmt_init failed" << std::endl;
        return {};
    }

    if (mysql_stmt_prepare(statement.get(), sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
        std::cerr << "[DB Error] Failed to prepare statement: " << mysql_stmt_error(statement.get()) << std::endl;
        return {};
    }

    return statement;
}

std::string bytesToHex(const unsigned char* data, size_t size) {
    static constexpr char kHexDigits[] = "0123456789abcdef";

    std::string hex;
    hex.resize(size * 2);
    for (size_t index = 0; index < size; ++index) {
        hex[index * 2] = kHexDigits[(data[index] >> 4) & 0x0F];
        hex[index * 2 + 1] = kHexDigits[data[index] & 0x0F];
    }
    return hex;
}

bool hexToBytes(const std::string& hex, std::vector<unsigned char>* bytes) {
    if (!bytes || (hex.size() % 2) != 0) {
        return false;
    }

    auto hexValue = [](char ch, unsigned char* value) {
        if ((ch >= '0') && (ch <= '9')) {
            *value = static_cast<unsigned char>(ch - '0');
            return true;
        }
        if ((ch >= 'a') && (ch <= 'f')) {
            *value = static_cast<unsigned char>(ch - 'a' + 10);
            return true;
        }
        if ((ch >= 'A') && (ch <= 'F')) {
            *value = static_cast<unsigned char>(ch - 'A' + 10);
            return true;
        }
        return false;
    };

    bytes->clear();
    bytes->reserve(hex.size() / 2);
    for (size_t index = 0; index < hex.size(); index += 2) {
        unsigned char high = 0;
        unsigned char low = 0;
        if (!hexValue(hex[index], &high) || !hexValue(hex[index + 1], &low)) {
            bytes->clear();
            return false;
        }
        bytes->push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return true;
}

std::vector<std::string> splitString(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t delimiter_pos = text.find(delimiter, start);
        if (delimiter_pos == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }

        parts.push_back(text.substr(start, delimiter_pos - start));
        start = delimiter_pos + 1;
    }
    return parts;
}

std::string hashPassword(const std::string& password) {
    std::array<unsigned char, kPasswordSaltBytes> salt{};
    std::array<unsigned char, kPasswordHashBytes> digest{};

    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        std::cerr << "[AUTH] RAND_bytes failed" << std::endl;
        return {};
    }

    if (PKCS5_PBKDF2_HMAC(password.c_str(),
                          static_cast<int>(password.size()),
                          salt.data(),
                          static_cast<int>(salt.size()),
                          kPasswordHashIterations,
                          EVP_sha256(),
                          static_cast<int>(digest.size()),
                          digest.data()) != 1) {
        std::cerr << "[AUTH] PKCS5_PBKDF2_HMAC failed" << std::endl;
        return {};
    }

    return std::string(kPasswordHashPrefix) + "$" +
           std::to_string(kPasswordHashIterations) + "$" +
           bytesToHex(salt.data(), salt.size()) + "$" +
           bytesToHex(digest.data(), digest.size());
}

bool verifyPasswordHash(const std::string& password, const std::string& stored_password) {
    const auto parts = splitString(stored_password, '$');
    if (parts.size() != 4 || parts[0] != kPasswordHashPrefix) {
        return false;
    }

    int iterations = 0;
    try {
        iterations = std::stoi(parts[1]);
    } catch (const std::exception&) {
        return false;
    }

    if (iterations <= 0) {
        return false;
    }

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected_digest;
    if (!hexToBytes(parts[2], &salt) || !hexToBytes(parts[3], &expected_digest) || expected_digest.empty()) {
        return false;
    }

    std::vector<unsigned char> derived_digest(expected_digest.size());
    if (PKCS5_PBKDF2_HMAC(password.c_str(),
                          static_cast<int>(password.size()),
                          salt.data(),
                          static_cast<int>(salt.size()),
                          iterations,
                          EVP_sha256(),
                          static_cast<int>(derived_digest.size()),
                          derived_digest.data()) != 1) {
        return false;
    }

    return CRYPTO_memcmp(derived_digest.data(), expected_digest.data(), expected_digest.size()) == 0;
}

bool loadStoredPassword(MYSQL* connection,
                        const std::string& user_id,
                        std::string* stored_password) {
    if (!stored_password) {
        return false;
    }

    auto statement = prepareStatement(connection, "SELECT password FROM users WHERE id = ? LIMIT 1");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[1] = {};
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(user_id.c_str());
    param_bind[0].buffer_length = user_id_length;
    param_bind[0].length = &user_id_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind user lookup param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to execute user lookup: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    std::array<char, kStoredPasswordBufferBytes> stored_password_buffer{};
    unsigned long stored_password_length = 0;
    MysqlBindFlag is_null = 0;
    MysqlBindFlag bind_error = 0;
    MYSQL_BIND result_bind[1] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = stored_password_buffer.data();
    result_bind[0].buffer_length = static_cast<unsigned long>(stored_password_buffer.size());
    result_bind[0].length = &stored_password_length;
    result_bind[0].is_null = &is_null;
    result_bind[0].error = &bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind user lookup result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to store user lookup result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if ((fetch_result == MYSQL_NO_DATA) || is_null) {
        return false;
    }
    if ((fetch_result == 1) || (fetch_result == MYSQL_DATA_TRUNCATED) || bind_error) {
        std::cerr << "[DB Error] Failed to fetch stored password hash" << std::endl;
        return false;
    }

    stored_password->assign(stored_password_buffer.data(), stored_password_length);
    return true;
}

bool updateStoredPasswordHash(MYSQL* connection,
                              const std::string& user_id,
                              const std::string& password_hash) {
    auto statement = prepareStatement(connection, "UPDATE users SET password = ? WHERE id = ?");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[2] = {};
    unsigned long password_hash_length = static_cast<unsigned long>(password_hash.size());
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(password_hash.c_str());
    param_bind[0].buffer_length = password_hash_length;
    param_bind[0].length = &password_hash_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(user_id.c_str());
    param_bind[1].buffer_length = user_id_length;
    param_bind[1].length = &user_id_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind password upgrade params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to upgrade legacy password hash: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return true;
}

bool parseLongLong(const std::string& text, long long* value) {
    if (!value || text.empty()) {
        return false;
    }

    try {
        size_t parsed_length = 0;
        const long long parsed_value = std::stoll(text, &parsed_length);
        if (parsed_length != text.size()) {
            return false;
        }

        *value = parsed_value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
}  // namespace

// -------------------------------------------------------
// JWT 관련 설정 및 함수
// -------------------------------------------------------
std::string getJwtSecret() {
    const char* secret = std::getenv("JWT_SECRET");
    return secret ? std::string(secret) : "default_veda_secret_key_2024";
}

std::string generateJWT(const std::string& userId) {
    auto token = jwt::create()
        .set_issuer("veda_auth_server")
        .set_type("JWS")
        .set_payload_claim("user_id", jwt::claim(userId))
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours{24})
        .sign(jwt::algorithm::hs256{getJwtSecret()});
    return token;
}

bool verifyJWT(const std::string& token) {
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{getJwtSecret()})
            .with_issuer("veda_auth_server");
        
        verifier.verify(decoded);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[JWT Verify Error] " << e.what() << std::endl;
        return false;
    }
}

// -------------------------------------------------------
// 경로 보안 확인 헬퍼 함수 (추가됨)
// -------------------------------------------------------
std::string resolveSafeRecordingPath(const std::string& filename) {
    const std::string base_path = "/app/recordings";
    
    // 1. 경로 조작 시도 차단 (.. 또는 / 포함 확인)
    if (filename.find("..") != std::string::npos || filename.find("/") != std::string::npos || filename.find("\\") != std::string::npos) {
        return "";
    }

    // 2. 최종 경로 생성
    fs::path p = fs::path(base_path) / filename;
    return p.string();
}

// -------------------------------------------------------
// MariaDB에서 ID/PW 확인
// -------------------------------------------------------
[[maybe_unused]] bool checkUserFromDBLegacy(std::string inputId, std::string inputPw) {
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;
    
    // 환경변수에서 접속 정보 가져오기 (YAML에 설정함)
    const char* db_host = std::getenv("DB_HOST");     // mariadb-service
    const char* db_user = std::getenv("DB_USER");     // veda_user
    const char* db_pass = std::getenv("DB_PASSWORD"); // secret에서 가져옴
    const char* db_name = "veda_db";                  // DB 이름 (YAML에 설정 필요, 없으면 기본값)

    // 환경변수 없으면 실패 처리 (안전장치)
    if(!db_host || !db_user || !db_pass) {
        std::cerr << "[Error] DB Environment variables are missing!" << std::endl;
        return false;
    }

    conn = mysql_init(NULL);

    // DB 연결
    if (!mysql_real_connect(conn, db_host, db_user, db_pass, "veda_db", 3306, NULL, 0)) {
        std::cerr << "[DB Error] " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return false;
    }

    // 쿼리 실행 (보안을 위해선 PreparedStatement를 써야 하지만, 지금은 간단히 구현)
    // 주의: 실제 상용 서비스에선 SQL Injection 방지 처리가 필요함
    std::string query = "SELECT count(*) FROM users WHERE id='" + inputId + "' AND password='" + inputPw + "'";
    
    if (mysql_query(conn, query.c_str())) {
        std::cerr << "[Query Error] " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return false;
    }

    // 결과 확인
    res = mysql_use_result(conn);
    bool loginSuccess = false;
    if ((row = mysql_fetch_row(res)) != NULL) {
        // count(*)가 1이면 로그인 성공
        if (std::stoi(row[0]) == 1) {
            loginSuccess = true;
        }
    }

    mysql_free_result(res);
    mysql_close(conn);
    return loginSuccess;
}

// -------------------------------------------------------
// MariaDB에 새로운 사용자 등록
// -------------------------------------------------------
[[maybe_unused]] bool registerUserToDBLegacy(std::string inputId, std::string inputPw) {
    MYSQL *conn;
    
    const char* db_host = std::getenv("DB_HOST");
    const char* db_user = std::getenv("DB_USER");
    const char* db_pass = std::getenv("DB_PASSWORD");

    if(!db_host || !db_user || !db_pass) {
        std::cerr << "[Error] DB Environment variables are missing!" << std::endl;
        return false;
    }

    conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, db_host, db_user, db_pass, "veda_db", 3306, NULL, 0)) {
        std::cerr << "[DB Error] " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return false;
    }

    // 기존 사용자 확인
    std::string checkQuery = "SELECT count(*) FROM users WHERE id='" + inputId + "'";
    if (mysql_query(conn, checkQuery.c_str())) {
        std::cerr << "[Query Error] " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return false;
    }

    MYSQL_RES *res = mysql_use_result(conn);
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row && std::stoi(row[0]) > 0) {
        mysql_free_result(res);
        mysql_close(conn);
        return false; // 중복 ID
    }
    mysql_free_result(res);

    // 사용자 추가
    std::string insertQuery = "INSERT INTO users (id, password) VALUES ('" + inputId + "', '" + inputPw + "')";
    if (mysql_query(conn, insertQuery.c_str())) {
        std::cerr << "[Insert Error] " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return false;
    }

    mysql_close(conn);
    return true;
}

bool checkUserFromDBSecure(const std::string& inputId, const std::string& inputPw) {
    // 현재 저장값이 해시인 경우 해시 검증 경로.
    // 과거 평문 계정인 경우 로그인 성공 시 해시 자동 승격 경로.
    if (validateCredentials(inputId, inputPw)) {
        return false;
    }

    auto connection = openDatabaseConnection();
    if (!connection) {
        return false;
    }

    std::string stored_password;
    if (!loadStoredPassword(connection.get(), inputId, &stored_password)) {
        return false;
    }

    if (verifyPasswordHash(inputPw, stored_password)) {
        return true;
    }

    if (stored_password != inputPw) {
        return false;
    }

    const std::string upgraded_hash = hashPassword(inputPw);
    if (!upgraded_hash.empty()) {
        updateStoredPasswordHash(connection.get(), inputId, upgraded_hash);
    }
    return true;
}

bool registerUserToDBSecure(const std::string& inputId, const std::string& inputPw) {
    // 신규 계정의 prepared statement + PBKDF2 해시 형태 저장.
    if (validateCredentials(inputId, inputPw)) {
        return false;
    }

    auto connection = openDatabaseConnection();
    if (!connection) {
        return false;
    }

    const std::string password_hash = hashPassword(inputPw);
    if (password_hash.empty()) {
        return false;
    }

    auto statement = prepareStatement(connection.get(), "INSERT INTO users (id, password) VALUES (?, ?)");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[2] = {};
    unsigned long user_id_length = static_cast<unsigned long>(inputId.size());
    unsigned long password_hash_length = static_cast<unsigned long>(password_hash.size());

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(inputId.c_str());
    param_bind[0].buffer_length = user_id_length;
    param_bind[0].length = &user_id_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(password_hash.c_str());
    param_bind[1].buffer_length = password_hash_length;
    param_bind[1].length = &password_hash_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind register params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to register user: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return true;
}

int main()
{
    // mosquitto 전역 초기화의 앱 수명 동안 1회 유지.
    MqttLibraryGuard mqtt_library_guard;
    crow::SimpleApp app;

    // SUNAPI 프록시 라우트 등록 (/sunapi/stw-cgi/*)
    registerSunapiProxyRoutes(app);
    // SUNAPI StreamingServer WebSocket 프록시 등록 (/sunapi/StreamingServer)
    registerSunapiWsProxyRoutes(app);

    // ==========================================
    // CCTV 매니저 초기화 및 라우트 등록
    // ==========================================
    const char* cctv_host = std::getenv("CCTV_BACKEND_HOST") ? std::getenv("CCTV_BACKEND_HOST") : "127.0.0.1";
    int cctv_port = std::getenv("CCTV_BACKEND_PORT") ? std::atoi(std::getenv("CCTV_BACKEND_PORT")) : 9090;
    
    // 인증서 경로 (기본값 설정)
    std::string cert_dir = "/app/certs";
    CctvManager cctv_mgr(
        cctv_host, cctv_port,
        cert_dir + "/rootCA.crt",
        cert_dir + "/cctv.crt", 
        cert_dir + "/cctv.key"
    );
    
    // CCTV 라우트 등록
    registerCctvProxyRoutes(app, cctv_mgr);

    const char* mqtt_host = std::getenv("MQTT_HOST") ? std::getenv("MQTT_HOST") : "mqtt-service";
    int mqtt_port = std::getenv("MQTT_PORT") ? std::atoi(std::getenv("MQTT_PORT")) : 1883;
    const char* motor_client_id = std::getenv("MOTOR_MQTT_CLIENT_ID") ? std::getenv("MOTOR_MQTT_CLIENT_ID") : "crow_motor_api";
    const char* motor_control_topic = std::getenv("MOTOR_CONTROL_TOPIC") ? std::getenv("MOTOR_CONTROL_TOPIC") : "motor/control";
    const char* motor_response_topic = std::getenv("MOTOR_RESPONSE_TOPIC") ? std::getenv("MOTOR_RESPONSE_TOPIC") : "motor/response";
    int motor_timeout_ms = std::getenv("MOTOR_COMMAND_TIMEOUT_MS") ? std::atoi(std::getenv("MOTOR_COMMAND_TIMEOUT_MS")) : 3000;

    MotorManager motor_mgr(
        mqtt_host,
        mqtt_port,
        motor_client_id,
        motor_control_topic,
        motor_response_topic,
        motor_timeout_ms
    );
    registerMotorRoutes(app, motor_mgr);

    const char* esp_watchdog_client_id = std::getenv("ESP32_WATCHDOG_CLIENT_ID") ? std::getenv("ESP32_WATCHDOG_CLIENT_ID") : "crow_esp_watchdog_api";
    const char* esp_watchdog_control_topic = std::getenv("ESP32_SYSTEM_CONTROL_TOPIC") ? std::getenv("ESP32_SYSTEM_CONTROL_TOPIC") : "system/control";
    const char* esp_watchdog_status_topic = std::getenv("ESP32_SYSTEM_STATUS_TOPIC") ? std::getenv("ESP32_SYSTEM_STATUS_TOPIC") : "system/status";

    EspHealthManager esp_health_mgr(
        mqtt_host,
        mqtt_port,
        esp_watchdog_client_id,
        esp_watchdog_control_topic,
        esp_watchdog_status_topic
    );
    registerEspHealthRoutes(app, esp_health_mgr);

    // ==========================================
    // 회원가입 API
    // ==========================================
    CROW_ROUTE(app, "/register").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid JSON");

        if (!x.has("id") || !x.has("password")) {
            return crow::response(400, "Missing id or password");
        }

        std::string id = x["id"].s();
        std::string pw = x["password"].s();

        if (const auto credential_error = validateCredentials(id, pw)) {
            return crow::response(400, *credential_error);
        }

        if (registerUserToDBSecure(id, pw)) {
            crow::json::wvalue res;
            res["status"] = "success";
            res["message"] = "User registered successfully";
            return crow::response(201, res);
        } else {
            return crow::response(409, "Registration Failed: ID already exists or DB error");
        }
    });

    // ==========================================
    // 로그인 API (실제 JWT 발급)
    // ==========================================
    CROW_ROUTE(app, "/login").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid JSON");

        if (!x.has("id") || !x.has("password")) {
            return crow::response(400, "Missing id or password");
        }

        std::string id = x["id"].s();
        std::string pw = x["password"].s();

        if (const auto credential_error = validateCredentials(id, pw)) {
            return crow::response(400, *credential_error);
        }

        // mTLS 정보 확인 (로깅용)
        std::string device_id = req.get_header_value("X-Device-ID");
        if (!device_id.empty()) std::cout << "[mTLS Device] " << device_id << std::endl;

        // DB 확인 후 토큰 생성
        if (checkUserFromDBSecure(id, pw)) {
            std::string token = generateJWT(id);
            
            crow::json::wvalue res;
            res["status"] = "success";
            res["token"] = token;
            return crow::response(res);
        } else {
            return crow::response(401, "Login Failed: Check ID or Password");
        }
    });

    // ==========================================
    // 토큰 검증 헬퍼 (캡처를 위해 람다로 정의)
    // ==========================================
    auto is_authorized = [](const crow::request& req) {
        std::string auth_header = req.get_header_value("Authorization");
        if (auth_header.length() < 7 || auth_header.substr(0, 7) != "Bearer ") {
            return false;
        }
        return verifyJWT(auth_header.substr(7));
    };

        // ==========================================
        // 녹화된 파일 목록 조회 (보호됨)
        // ==========================================
        CROW_ROUTE(app, "/recordings")
        ([&is_authorized](const crow::request& req){
            if (!is_authorized(req)) return crow::response(401, "Unauthorized");
    
            std::string path = "/app/recordings"; 
            crow::json::wvalue result;
            
            // files 배열 초기화 (파일 없으면 빈 배열 반환)
            result["files"] = std::vector<std::string>(); 
    
            if (fs::exists(path) && fs::is_directory(path)) {
                int i = 0;
                for (const auto& entry : fs::directory_iterator(path)) {
                    if (entry.path().extension() == ".mp4") {
                        // 파일명과 크기를 담음
                        result["files"][i]["name"] = entry.path().filename().string();
                        result["files"][i]["size"] = (long)entry.file_size(); 
                        i++;
                    }
                }
            }
            return crow::response(result); 
        });
    

    // ==========================================
    // 파일 삭제 API (보호됨)
    // ==========================================
    CROW_ROUTE(app, "/recordings").methods(crow::HTTPMethod::DELETE)
    ([&is_authorized](const crow::request& req){
        if (!is_authorized(req)) return crow::response(401, "Unauthorized");

        // 쿼리 파라미터(?file=...)에서 파일명 가져오기
        const char* file_param = req.url_params.get("file");
        
        if (!file_param) {
            return crow::response(400, "Missing file parameter");
        }

        auto safePath = resolveSafeRecordingPath(file_param);
        if (safePath.empty()) {
            return crow::response(403, "Invalid filename");
        }

        if (!fs::exists(safePath) || !fs::is_regular_file(safePath)) {
            return crow::response(404, "File not found");
        }

        std::error_code ec;
        fs::remove(safePath, ec);
        if (ec) {
            std::cerr << "[Delete Error] " << ec.message() << std::endl;
            return crow::response(500, "Failed to delete file");
        }

        std::cout << "[Delete] Removed file: " << safePath << std::endl;
        return crow::response(200, "File deleted");
    });

    // ==========================================
    // 파일 다운로드/재생 (보호됨)
    // ==========================================
    CROW_ROUTE(app, "/stream")
    ([&is_authorized](const crow::request& req, crow::response& res){
        if (!is_authorized(req)) {
            res.code = 401;
            res.write("Unauthorized");
            res.end();
            return;
        }

        // 쿼리 파라미터(?file=...)에서 파일명 가져오기
        const char* file_param = req.url_params.get("file");
        
        if (!file_param) {
            res.code = 400;
            res.write("Missing file parameter");
            res.end();
            return;
        }

        auto safePath = resolveSafeRecordingPath(file_param);
        if (safePath.empty()) {
            res.code = 403;
            res.write("Invalid filename");
            res.end();
            return;
        }

        // 파일 크기 확인 (Range Support를 위해 필요)
        std::ifstream ifs(safePath, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) {
            std::cerr << "[Error] Cannot open file: " << safePath << std::endl; // 에러 로그
            res.code = 404;
            res.write("File not found or cannot open");
            res.end();
            return; 
        }
        
        const long long file_size = static_cast<long long>(ifs.tellg());
        ifs.seekg(0, std::ios::beg); // 다시 처음으로 돌림

        // Range 헤더 파싱
        long long start = 0;
        long long end = file_size - 1;
        bool is_range = false;

        const std::string range_header = req.get_header_value("Range");
        if (!range_header.empty()) {
            if (range_header.rfind("bytes=", 0) != 0) {
                res.code = 416;
                res.write("Invalid Range header");
                res.end();
                return;
            }

            is_range = true;
            // 예: "bytes=1024-" 또는 "bytes=1024-2048"
            const std::string range_value = range_header.substr(6);
            const size_t dash_pos = range_value.find('-');
            if (dash_pos == std::string::npos) {
                res.code = 416;
                res.write("Invalid Range header");
                res.end();
                return;
            }

            const std::string start_str = range_value.substr(0, dash_pos);
            const std::string end_str = range_value.substr(dash_pos + 1);
            if ((!start_str.empty() && !parseLongLong(start_str, &start)) ||
                (!end_str.empty() && !parseLongLong(end_str, &end))) {
                res.code = 416;
                res.write("Invalid Range header");
                res.end();
                return;
            }
        }

        // 범위 유효성 검사
        if (start < 0 || start > end || start >= file_size) {
            res.code = 416; // 요청한 범위를 만족할 수 없음
            res.end();
            return;
        }

        if (end >= file_size) {
            end = file_size - 1;
        }

        const long long content_length = end - start + 1;
        if ((content_length <= 0) || (content_length > kMaxStreamResponseBytes)) {
            res.code = 416;
            res.write("Requested range is too large");
            res.end();
            return;
        }

        // 응답 헤더 설정
        res.add_header("Accept-Ranges", "bytes");
        res.add_header("Content-Type", "video/mp4");
        res.add_header("Content-Length", std::to_string(content_length));
        
        if (is_range) {
            res.code = 206; // 부분 응답
            std::string content_range = "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size);
            res.add_header("Content-Range", content_range);
        } else {
            res.code = 200; // 전체 응답 성공
        }
        
        // 요청된 부분만 읽어서 전송
        std::vector<char> buffer(static_cast<size_t>(content_length));
        ifs.seekg(start);
        ifs.read(buffer.data(), static_cast<std::streamsize>(content_length));
        if (ifs.gcount() != static_cast<std::streamsize>(content_length)) {
            res.code = 500;
            res.write("Failed to read requested range");
            res.end();
            return;
        }

        res.body.assign(buffer.begin(), buffer.end());
        res.end();
    });
    
    // ==========================================
    // 시스템 저장소 용량 조회 (보호됨)
    // ==========================================
    CROW_ROUTE(app, "/system/storage")
    ([&is_authorized](const crow::request& req){
        if (!is_authorized(req)) return crow::response(401, "Unauthorized");

        struct statvfs stat;
        std::string path = "/app/recordings"; // 마운트된 경로 확인 필요
        crow::json::wvalue result;
        
        if (statvfs(path.c_str(), &stat) != 0) {
            // 실패 시 (경로가 없거나 권한 문제 등)
            result["error"] = "Failed to get storage info";
            return crow::response(500, result);
        }
        // f_bsize: 블록 크기, f_blocks: 전체 블록 수, f_bavail: 일반 유저가 사용 가능한 블록 수
        unsigned long long total = (unsigned long long)stat.f_blocks * stat.f_frsize;
        unsigned long long available = (unsigned long long)stat.f_bavail * stat.f_frsize;
        unsigned long long used = total - available;
        result["total_bytes"] = total;
        result["used_bytes"] = used;
        result["available_bytes"] = available;
                
        return crow::response(result);
    });
        
    // ==========================================
    // 시스템 헬스 체크 및 연결 정보 조회 (테스트용)
    // ==========================================
    CROW_ROUTE(app, "/health")
    ([](const crow::request& req){
        crow::json::wvalue result;
                
        // 1. Crow Server Status
        result["crow_server"]["status"] = "OK";
        result["crow_server"]["message"] = "Crow server is receiving requests successfully.";
        
        // 2. Nginx mTLS Status
        std::string device_id = req.get_header_value("X-Device-ID");
        std::string device_verify = req.get_header_value("X-Device-Verify");
                
        if (device_verify == "SUCCESS") {
            result["nginx_mtls"]["status"] = "VERIFIED";
            result["nginx_mtls"]["device_id"] = device_id;
        } else {
            result["nginx_mtls"]["status"] = "UNVERIFIED/NOT_PRESENT";
            result["nginx_mtls"]["message"] = "Client certificate was not verified by Nginx.";
        }
        
        // 3. 안내 정보 (클라이언트 테스트용 주소 가이드)
        result["services_info"]["mqtt"] = "Port 8883 (MQTTS) is exposed via Nginx Gateway.";
        result["services_info"]["mediamtx_rtsp"] = "Port 8554 (RTSP) / 8555 (RTSPS) are available.";
        result["services_info"]["mediamtx_hls"] = "/hls/ path is available on port 443.";
        
        return crow::response(result);
    });
        
    // ==========================================
    // Swagger API 문서 서빙
    // ==========================================
    CROW_ROUTE(app, "/docs")
    ([](){
        std::ifstream ifs("/app/swagger/index.html");
        if (!ifs.is_open()) return crow::response(404, "Swagger UI file not found");
        
        std::stringstream buffer;
        buffer << ifs.rdbuf();
        
        crow::response res(buffer.str());
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    CROW_ROUTE(app, "/swagger.yaml")
    ([](){
        std::ifstream ifs("/app/swagger/swagger.yaml");
        if (!ifs.is_open()) return crow::response(404, "Swagger YAML file not found");
        
        std::stringstream buffer;
        buffer << ifs.rdbuf();
        
        crow::response res(buffer.str());
        res.add_header("Content-Type", "text/yaml; charset=utf-8");
        return res;
    });
        
    app.port(8080).multithreaded().run();
    shutdownCctvProxyWorker();
}
