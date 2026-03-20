#include "../include/AuthRecoveryRoutes.h"
#include "../include/AppScriptMailSender.h"
#include <mysql/mysql.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {
constexpr char kDatabaseName[] = "veda_db";
constexpr size_t kMaxUserIdLength = 64;
constexpr size_t kMaxPasswordLength = 128;
constexpr size_t kPasswordPolicyMinLength = 8;
constexpr size_t kPasswordPolicyMaxLength = 16;
constexpr int kPasswordHashIterations = 120000;
constexpr size_t kPasswordSaltBytes = 16;
constexpr size_t kPasswordHashBytes = 32;
constexpr char kPasswordHashPrefix[] = "pbkdf2_sha256";
constexpr int kEmailVerifyCodeTtlSeconds = 300;
constexpr int kEmailVerifyResendCooldownSeconds = 30;
constexpr int kPasswordResetCodeTtlSeconds = 300;
constexpr size_t kEmailVerifyCodeDigits = 6;
constexpr size_t kEmailBufferBytes = 320;
constexpr size_t kUserIdBufferBytes = 128;

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

struct UserEmailInfo {
    bool found = false;
    std::string email;
    bool is_email_verified = false;
};

struct SignupEmailVerificationRecord {
    bool found = false;
    unsigned long long id = 0;
    std::string user_id;
    std::string email;
};

struct SignupEmailVerificationState {
    bool found = false;
    long long created_at = 0;
    long long expires_at = 0;
    bool verified = false;
    bool consumed = false;
};

// DB 연결 생성 함수
MysqlConnectionPtr openDatabaseConnection() {
    const char* db_host = std::getenv("DB_HOST");
    const char* db_user = std::getenv("DB_USER");
    const char* db_pass = std::getenv("DB_PASSWORD");

    if (!db_host || !db_user || !db_pass) {
        std::cerr << "[AUTH][DB] Missing DB_HOST, DB_USER, or DB_PASSWORD" << std::endl;
        return {};
    }

    MysqlConnectionPtr connection(mysql_init(nullptr));
    if (!connection) {
        std::cerr << "[AUTH][DB] mysql_init failed" << std::endl;
        return {};
    }

    if (!mysql_real_connect(connection.get(), db_host, db_user, db_pass, kDatabaseName, 3306, nullptr, 0)) {
        std::cerr << "[AUTH][DB] " << mysql_error(connection.get()) << std::endl;
        return {};
    }

    return connection;
}

// Prepared statement 생성 함수
MysqlStatementPtr prepareStatement(MYSQL* connection, const std::string& sql) {
    if (!connection) {
        return {};
    }

    MysqlStatementPtr statement(mysql_stmt_init(connection));
    if (!statement) {
        std::cerr << "[AUTH][DB] mysql_stmt_init failed" << std::endl;
        return {};
    }

    if (mysql_stmt_prepare(statement.get(), sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
        std::cerr << "[AUTH][DB] Failed to prepare statement: " << mysql_stmt_error(statement.get()) << std::endl;
        return {};
    }

    return statement;
}

// 바이트 배열을 16진수 문자열로 변환하는 함수
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

// SHA-256 해시를 16진수 문자열로 계산하는 함수
std::string sha256Hex(const std::string& text) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return {};
    }

    bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1
              && EVP_DigestUpdate(ctx, text.data(), text.size()) == 1
              && EVP_DigestFinal_ex(ctx, digest, &digest_len) == 1;
    EVP_MD_CTX_free(ctx);

    if (!ok) {
        return {};
    }

    return bytesToHex(digest, digest_len);
}

// 인증 토큰 랜덤 생성 함수
std::string generateRandomTokenHex(size_t bytes) {
    if (bytes == 0) {
        return {};
    }

    std::vector<unsigned char> random_bytes(bytes);
    if (RAND_bytes(random_bytes.data(), static_cast<int>(random_bytes.size())) != 1) {
        return {};
    }

    return bytesToHex(random_bytes.data(), random_bytes.size());
}

// 6자리 인증 코드 생성 함수
std::string generateEmailVerificationCode(size_t digits) {
    if (digits == 0) {
        return {};
    }

    unsigned int modulus = 1;
    for (size_t index = 0; index < digits; ++index) {
        modulus *= 10;
    }

    std::array<unsigned char, sizeof(unsigned int)> random_bytes{};
    if (RAND_bytes(random_bytes.data(), static_cast<int>(random_bytes.size())) != 1) {
        return {};
    }

    unsigned int random_value = 0;
    for (unsigned char byte : random_bytes) {
        random_value = (random_value << 8U) | static_cast<unsigned int>(byte);
    }

    std::ostringstream stream;
    stream << std::setw(static_cast<int>(digits))
           << std::setfill('0')
           << (random_value % modulus);
    return stream.str();
}

// 비밀번호 PBKDF2 해시 생성 함수
std::string hashPassword(const std::string& password) {
    std::array<unsigned char, kPasswordSaltBytes> salt{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        return {};
    }

    std::array<unsigned char, kPasswordHashBytes> digest{};
    if (PKCS5_PBKDF2_HMAC(password.c_str(),
                          static_cast<int>(password.size()),
                          salt.data(),
                          static_cast<int>(salt.size()),
                          kPasswordHashIterations,
                          EVP_sha256(),
                          static_cast<int>(digest.size()),
                          digest.data()) != 1) {
        return {};
    }

    return std::string(kPasswordHashPrefix) + "$" +
           std::to_string(kPasswordHashIterations) + "$" +
           bytesToHex(salt.data(), salt.size()) + "$" +
           bytesToHex(digest.data(), digest.size());
}

// ID 입력값 검증 함수
std::optional<std::string> validateUserId(const std::string& user_id) {
    if (user_id.empty()) {
        return "아이디를 입력해 주세요.";
    }
    if (user_id.size() > kMaxUserIdLength) {
        return "아이디 길이가 너무 깁니다.";
    }
    return std::nullopt;
}

// 이메일 형식 검증 함수
std::optional<std::string> validateEmail(const std::string& email) {
    if (email.empty()) {
        return "이메일을 입력해 주세요.";
    }
    if (std::any_of(email.begin(), email.end(), [](unsigned char ch) { return std::isspace(ch) != 0; })) {
        return "이메일에는 공백을 사용할 수 없습니다.";
    }

    const size_t at_pos = email.find('@');
    if (at_pos == std::string::npos || at_pos == 0 || at_pos + 1 >= email.size()) {
        return "이메일 형식이 올바르지 않습니다.";
    }

    const size_t dot_pos = email.find('.', at_pos + 1);
    if (dot_pos == std::string::npos || dot_pos + 1 >= email.size()) {
        return "이메일 형식이 올바르지 않습니다.";
    }

    return std::nullopt;
}

// 비밀번호 복잡도 검증 함수
std::optional<std::string> validatePasswordComplexity(const std::string& password) {
    if (password.size() < kPasswordPolicyMinLength) {
        return "비밀번호는 8자 이상이어야 합니다.";
    }
    if (password.size() > kPasswordPolicyMaxLength) {
        return "비밀번호는 16자 이하여야 합니다.";
    }
    if (password.size() > kMaxPasswordLength) {
        return "비밀번호 길이가 너무 깁니다.";
    }

    bool has_digit = false;
    bool has_special = false;
    for (unsigned char ch : password) {
        if (std::isspace(ch)) {
            return "비밀번호에는 공백을 사용할 수 없습니다.";
        }
        if (std::isdigit(ch)) {
            has_digit = true;
        }
        if (!std::isalnum(ch)) {
            has_special = true;
        }
    }

    if (!has_digit) {
        return "비밀번호에는 숫자가 1개 이상 포함되어야 합니다.";
    }
    if (!has_special) {
        return "비밀번호에는 특수문자가 1개 이상 포함되어야 합니다.";
    }

    return std::nullopt;
}

// 이메일 인증 코드 형식 검증 함수
std::optional<std::string> validateEmailVerificationCode(const std::string& code) {
    if (code.size() != kEmailVerifyCodeDigits) {
        return "인증 코드는 6자리 숫자여야 합니다.";
    }
    if (!std::all_of(code.begin(), code.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return "인증 코드는 6자리 숫자여야 합니다.";
    }
    return std::nullopt;
}

// Password reset code validation helper
std::optional<std::string> validatePasswordResetCode(const std::string& code) {
    if (code.empty()) {
        return "재설정 코드를 입력해 주세요.";
    }
    if (code.size() != kEmailVerifyCodeDigits) {
        return "재설정 코드는 6자리 숫자여야 합니다.";
    }
    if (!std::all_of(code.begin(), code.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return "재설정 코드는 6자리 숫자여야 합니다.";
    }
    return std::nullopt;
}

// 사용자 이메일 인증 상태 조회 함수
bool loadUserEmailInfo(MYSQL* connection, const std::string& user_id, UserEmailInfo* out) {
    if (!connection || !out) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "SELECT IFNULL(email, ''), is_email_verified FROM users WHERE id = ? LIMIT 1");
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
        std::cerr << "[AUTH][DB] Failed to bind loadUserEmailInfo param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute loadUserEmailInfo: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    std::array<char, kEmailBufferBytes> email_buffer{};
    unsigned long email_length = 0;
    MysqlBindFlag email_is_null = 0;
    MysqlBindFlag email_bind_error = 0;
    signed char verified_value = 0;
    MysqlBindFlag verified_is_null = 0;
    MysqlBindFlag verified_bind_error = 0;

    MYSQL_BIND result_bind[2] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = email_buffer.data();
    result_bind[0].buffer_length = static_cast<unsigned long>(email_buffer.size());
    result_bind[0].length = &email_length;
    result_bind[0].is_null = &email_is_null;
    result_bind[0].error = &email_bind_error;

    result_bind[1].buffer_type = MYSQL_TYPE_TINY;
    result_bind[1].buffer = &verified_value;
    result_bind[1].is_null = &verified_is_null;
    result_bind[1].error = &verified_bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind loadUserEmailInfo result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to store loadUserEmailInfo result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        out->found = false;
        out->email.clear();
        out->is_email_verified = false;
        return true;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED || email_bind_error || verified_bind_error) {
        std::cerr << "[AUTH][DB] Failed to fetch loadUserEmailInfo result" << std::endl;
        return false;
    }

    out->found = true;
    out->email = email_is_null ? std::string{} : std::string(email_buffer.data(), email_length);
    out->is_email_verified = !verified_is_null && verified_value != 0;
    return true;
}

// 회원가입 아이디 중복 여부 조회 함수
bool isUserIdAlreadyRegistered(MYSQL* connection,
                               const std::string& user_id,
                               bool* out_exists) {
    if (!connection || !out_exists) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "SELECT 1 FROM users WHERE id = ? LIMIT 1");
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
        std::cerr << "[AUTH][DB] Failed to bind isUserIdAlreadyRegistered param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute isUserIdAlreadyRegistered: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    signed char exists_value = 0;
    MysqlBindFlag exists_is_null = 0;
    MysqlBindFlag exists_bind_error = 0;
    MYSQL_BIND result_bind[1] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_TINY;
    result_bind[0].buffer = &exists_value;
    result_bind[0].is_null = &exists_is_null;
    result_bind[0].error = &exists_bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind isUserIdAlreadyRegistered result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to store isUserIdAlreadyRegistered result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        *out_exists = false;
        return true;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED || exists_bind_error) {
        std::cerr << "[AUTH][DB] Failed to fetch isUserIdAlreadyRegistered result" << std::endl;
        return false;
    }

    *out_exists = !exists_is_null && exists_value != 0;
    return true;
}

// 회원가입 이메일 중복 여부 조회 함수
bool isEmailAlreadyRegistered(MYSQL* connection,
                              const std::string& email,
                              bool* out_exists) {
    if (!connection || !out_exists) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "SELECT 1 FROM users WHERE email = ? LIMIT 1");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[1] = {};
    unsigned long email_length = static_cast<unsigned long>(email.size());
    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(email.c_str());
    param_bind[0].buffer_length = email_length;
    param_bind[0].length = &email_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind isEmailAlreadyRegistered param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute isEmailAlreadyRegistered: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    signed char exists_value = 0;
    MysqlBindFlag exists_is_null = 0;
    MysqlBindFlag exists_bind_error = 0;
    MYSQL_BIND result_bind[1] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_TINY;
    result_bind[0].buffer = &exists_value;
    result_bind[0].is_null = &exists_is_null;
    result_bind[0].error = &exists_bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind isEmailAlreadyRegistered result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to store isEmailAlreadyRegistered result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        *out_exists = false;
        return true;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED || exists_bind_error) {
        std::cerr << "[AUTH][DB] Failed to fetch isEmailAlreadyRegistered result" << std::endl;
        return false;
    }

    *out_exists = !exists_is_null && exists_value != 0;
    return true;
}

// 회원가입 전 이메일 인증 코드 해시 저장 함수
bool insertSignupEmailVerificationCode(MYSQL* connection,
                                       const std::string& user_id,
                                       const std::string& email,
                                       const std::string& code_hash,
                                       long long expires_at,
                                       const std::string& request_ip,
                                       const std::string& user_agent,
                                       unsigned long long* out_record_id) {
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "INSERT INTO signup_email_verifications "
        "(user_id, email, token_hash, expires_at, request_ip, user_agent) "
        "VALUES (?, ?, ?, FROM_UNIXTIME(?), ?, ?)");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[6] = {};
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    unsigned long email_length = static_cast<unsigned long>(email.size());
    unsigned long code_hash_length = static_cast<unsigned long>(code_hash.size());
    long long expires_at_value = expires_at;

    unsigned long request_ip_length = static_cast<unsigned long>(request_ip.size());
    MysqlBindFlag request_ip_is_null = request_ip.empty() ? 1 : 0;
    unsigned long user_agent_length = static_cast<unsigned long>(user_agent.size());
    MysqlBindFlag user_agent_is_null = user_agent.empty() ? 1 : 0;

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(user_id.c_str());
    param_bind[0].buffer_length = user_id_length;
    param_bind[0].length = &user_id_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(email.c_str());
    param_bind[1].buffer_length = email_length;
    param_bind[1].length = &email_length;

    param_bind[2].buffer_type = MYSQL_TYPE_STRING;
    param_bind[2].buffer = const_cast<char*>(code_hash.c_str());
    param_bind[2].buffer_length = code_hash_length;
    param_bind[2].length = &code_hash_length;

    param_bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[3].buffer = &expires_at_value;

    param_bind[4].buffer_type = MYSQL_TYPE_STRING;
    param_bind[4].buffer = request_ip_is_null ? nullptr : const_cast<char*>(request_ip.c_str());
    param_bind[4].buffer_length = request_ip_length;
    param_bind[4].length = &request_ip_length;
    param_bind[4].is_null = &request_ip_is_null;

    param_bind[5].buffer_type = MYSQL_TYPE_STRING;
    param_bind[5].buffer = user_agent_is_null ? nullptr : const_cast<char*>(user_agent.c_str());
    param_bind[5].buffer_length = user_agent_length;
    param_bind[5].length = &user_agent_length;
    param_bind[5].is_null = &user_agent_is_null;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind insertSignupEmailVerificationCode params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute insertSignupEmailVerificationCode: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (out_record_id) {
        *out_record_id = static_cast<unsigned long long>(mysql_insert_id(connection));
    }
    return true;
}

// 회원가입 이메일 인증 최신 상태 조회 함수
bool loadLatestSignupEmailVerificationState(MYSQL* connection,
                                            const std::string& user_id,
                                            const std::string& email,
                                            SignupEmailVerificationState* out) {
    if (!connection || !out) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "SELECT UNIX_TIMESTAMP(created_at), "
        "UNIX_TIMESTAMP(expires_at), "
        "verified_at IS NOT NULL, "
        "consumed_at IS NOT NULL "
        "FROM signup_email_verifications "
        "WHERE user_id = ? AND email = ? "
        "ORDER BY id DESC LIMIT 1");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[2] = {};
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    unsigned long email_length = static_cast<unsigned long>(email.size());

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(user_id.c_str());
    param_bind[0].buffer_length = user_id_length;
    param_bind[0].length = &user_id_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(email.c_str());
    param_bind[1].buffer_length = email_length;
    param_bind[1].length = &email_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind loadLatestSignupEmailVerificationState params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute loadLatestSignupEmailVerificationState: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    long long created_at = 0;
    MysqlBindFlag created_at_is_null = 0;
    MysqlBindFlag created_at_bind_error = 0;
    long long expires_at = 0;
    MysqlBindFlag expires_at_is_null = 0;
    MysqlBindFlag expires_at_bind_error = 0;
    signed char verified_value = 0;
    MysqlBindFlag verified_is_null = 0;
    MysqlBindFlag verified_bind_error = 0;
    signed char consumed_value = 0;
    MysqlBindFlag consumed_is_null = 0;
    MysqlBindFlag consumed_bind_error = 0;

    MYSQL_BIND result_bind[4] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = &created_at;
    result_bind[0].is_null = &created_at_is_null;
    result_bind[0].error = &created_at_bind_error;

    result_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[1].buffer = &expires_at;
    result_bind[1].is_null = &expires_at_is_null;
    result_bind[1].error = &expires_at_bind_error;

    result_bind[2].buffer_type = MYSQL_TYPE_TINY;
    result_bind[2].buffer = &verified_value;
    result_bind[2].is_null = &verified_is_null;
    result_bind[2].error = &verified_bind_error;

    result_bind[3].buffer_type = MYSQL_TYPE_TINY;
    result_bind[3].buffer = &consumed_value;
    result_bind[3].is_null = &consumed_is_null;
    result_bind[3].error = &consumed_bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind loadLatestSignupEmailVerificationState result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to store loadLatestSignupEmailVerificationState result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        out->found = false;
        return true;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED ||
        created_at_bind_error || expires_at_bind_error ||
        verified_bind_error || consumed_bind_error) {
        std::cerr << "[AUTH][DB] Failed to fetch loadLatestSignupEmailVerificationState result" << std::endl;
        return false;
    }

    out->found = true;
    out->created_at = created_at_is_null ? 0 : created_at;
    out->expires_at = expires_at_is_null ? 0 : expires_at;
    out->verified = !verified_is_null && verified_value != 0;
    out->consumed = !consumed_is_null && consumed_value != 0;
    return true;
}

// 인증 코드와 사용자 식별자로 유효한 회원가입 이메일 인증 레코드 조회 함수
bool loadActiveSignupEmailVerificationByCode(MYSQL* connection,
                                             const std::string& user_id,
                                             const std::string& email,
                                             const std::string& code_hash,
                                             SignupEmailVerificationRecord* out) {
    if (!connection || !out) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "SELECT id, user_id, email "
        "FROM signup_email_verifications "
        "WHERE user_id = ? AND email = ? AND token_hash = ? "
        "AND consumed_at IS NULL AND expires_at > NOW() "
        "ORDER BY id DESC LIMIT 1");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[3] = {};
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    unsigned long email_length = static_cast<unsigned long>(email.size());
    unsigned long code_hash_length = static_cast<unsigned long>(code_hash.size());

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(user_id.c_str());
    param_bind[0].buffer_length = user_id_length;
    param_bind[0].length = &user_id_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(email.c_str());
    param_bind[1].buffer_length = email_length;
    param_bind[1].length = &email_length;

    param_bind[2].buffer_type = MYSQL_TYPE_STRING;
    param_bind[2].buffer = const_cast<char*>(code_hash.c_str());
    param_bind[2].buffer_length = code_hash_length;
    param_bind[2].length = &code_hash_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind loadActiveSignupEmailVerificationByCode param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute loadActiveSignupEmailVerificationByCode: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    long long record_id = 0;
    MysqlBindFlag record_id_is_null = 0;
    MysqlBindFlag record_id_bind_error = 0;
    std::array<char, kUserIdBufferBytes> user_id_buffer{};
    unsigned long user_id_result_length = 0;
    MysqlBindFlag user_id_is_null = 0;
    MysqlBindFlag user_id_bind_error = 0;
    std::array<char, kEmailBufferBytes> email_buffer{};
    unsigned long email_result_length = 0;
    MysqlBindFlag email_is_null = 0;
    MysqlBindFlag email_bind_error = 0;

    MYSQL_BIND result_bind[3] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = &record_id;
    result_bind[0].is_null = &record_id_is_null;
    result_bind[0].error = &record_id_bind_error;

    result_bind[1].buffer_type = MYSQL_TYPE_STRING;
    result_bind[1].buffer = user_id_buffer.data();
    result_bind[1].buffer_length = static_cast<unsigned long>(user_id_buffer.size());
    result_bind[1].length = &user_id_result_length;
    result_bind[1].is_null = &user_id_is_null;
    result_bind[1].error = &user_id_bind_error;

    result_bind[2].buffer_type = MYSQL_TYPE_STRING;
    result_bind[2].buffer = email_buffer.data();
    result_bind[2].buffer_length = static_cast<unsigned long>(email_buffer.size());
    result_bind[2].length = &email_result_length;
    result_bind[2].is_null = &email_is_null;
    result_bind[2].error = &email_bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind loadActiveSignupEmailVerificationByCode result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to store loadActiveSignupEmailVerificationByCode result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        out->found = false;
        return true;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED ||
        record_id_bind_error || user_id_bind_error || email_bind_error) {
        std::cerr << "[AUTH][DB] Failed to fetch loadActiveSignupEmailVerificationByCode result" << std::endl;
        return false;
    }

    out->found = true;
    out->id = record_id_is_null ? 0 : static_cast<unsigned long long>(record_id);
    out->user_id = user_id_is_null ? std::string{} : std::string(user_id_buffer.data(), user_id_result_length);
    out->email = email_is_null ? std::string{} : std::string(email_buffer.data(), email_result_length);
    return true;
}

// 회원가입 이메일 인증 레코드 삭제 함수
bool deleteSignupEmailVerificationRecord(MYSQL* connection, unsigned long long record_id) {
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "DELETE FROM signup_email_verifications WHERE id = ? LIMIT 1");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[1] = {};
    long long record_id_value = static_cast<long long>(record_id);
    param_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[0].buffer = &record_id_value;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind deleteSignupEmailVerificationRecord param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute deleteSignupEmailVerificationRecord: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

// 회원가입 이메일 인증 완료 처리 함수
bool markSignupEmailVerificationConfirmed(MYSQL* connection, unsigned long long record_id) {
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "UPDATE signup_email_verifications "
        "SET verified_at = NOW() "
        "WHERE id = ? AND consumed_at IS NULL");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[1] = {};
    long long record_id_value = static_cast<long long>(record_id);
    param_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[0].buffer = &record_id_value;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind markSignupEmailVerificationConfirmed param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute markSignupEmailVerificationConfirmed: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

// 회원가입 시 사용할 이메일 인증 완료 상태 조회 함수
bool hasVerifiedSignupEmailVerification(MYSQL* connection,
                                        const std::string& user_id,
                                        const std::string& email,
                                        bool* out_verified) {
    if (!connection || !out_verified) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "SELECT 1 FROM signup_email_verifications "
        "WHERE user_id = ? AND email = ? "
        "AND verified_at IS NOT NULL AND consumed_at IS NULL AND expires_at > NOW() "
        "ORDER BY id DESC LIMIT 1");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[2] = {};
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    unsigned long email_length = static_cast<unsigned long>(email.size());

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(user_id.c_str());
    param_bind[0].buffer_length = user_id_length;
    param_bind[0].length = &user_id_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(email.c_str());
    param_bind[1].buffer_length = email_length;
    param_bind[1].length = &email_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind hasVerifiedSignupEmailVerification params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute hasVerifiedSignupEmailVerification: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    signed char verified_value = 0;
    MysqlBindFlag verified_is_null = 0;
    MysqlBindFlag verified_bind_error = 0;
    MYSQL_BIND result_bind[1] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_TINY;
    result_bind[0].buffer = &verified_value;
    result_bind[0].is_null = &verified_is_null;
    result_bind[0].error = &verified_bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind hasVerifiedSignupEmailVerification result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to store hasVerifiedSignupEmailVerification result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        *out_verified = false;
        return true;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED || verified_bind_error) {
        std::cerr << "[AUTH][DB] Failed to fetch hasVerifiedSignupEmailVerification result" << std::endl;
        return false;
    }

    *out_verified = !verified_is_null && verified_value != 0;
    return true;
}

// 회원가입 완료 후 인증 레코드 사용 처리 함수
bool consumeVerifiedSignupEmailVerification(MYSQL* connection,
                                            const std::string& user_id,
                                            const std::string& email) {
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "UPDATE signup_email_verifications "
        "SET consumed_at = NOW() "
        "WHERE user_id = ? AND email = ? "
        "AND verified_at IS NOT NULL AND consumed_at IS NULL");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[2] = {};
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    unsigned long email_length = static_cast<unsigned long>(email.size());

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(user_id.c_str());
    param_bind[0].buffer_length = user_id_length;
    param_bind[0].length = &user_id_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(email.c_str());
    param_bind[1].buffer_length = email_length;
    param_bind[1].length = &email_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind consumeVerifiedSignupEmailVerification params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute consumeVerifiedSignupEmailVerification: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

// 동일 이메일의 타 사용자 존재 여부 조회 함수
bool isEmailUsedByAnotherUser(MYSQL* connection,
                              const std::string& user_id,
                              const std::string& email,
                              bool* out_used) {
    if (!connection || !out_used) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "SELECT 1 FROM users WHERE email = ? AND id <> ? LIMIT 1");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[2] = {};
    unsigned long email_length = static_cast<unsigned long>(email.size());
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(email.c_str());
    param_bind[0].buffer_length = email_length;
    param_bind[0].length = &email_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(user_id.c_str());
    param_bind[1].buffer_length = user_id_length;
    param_bind[1].length = &user_id_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind isEmailUsedByAnotherUser params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute isEmailUsedByAnotherUser: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    signed char exists_value = 0;
    MysqlBindFlag exists_is_null = 0;
    MysqlBindFlag exists_bind_error = 0;
    MYSQL_BIND result_bind[1] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_TINY;
    result_bind[0].buffer = &exists_value;
    result_bind[0].is_null = &exists_is_null;
    result_bind[0].error = &exists_bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind isEmailUsedByAnotherUser result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to store isEmailUsedByAnotherUser result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        *out_used = false;
        return true;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED || exists_bind_error) {
        std::cerr << "[AUTH][DB] Failed to fetch isEmailUsedByAnotherUser result" << std::endl;
        return false;
    }

    *out_used = !exists_is_null && exists_value != 0;
    return true;
}

// 사용자 이메일 인증 상태 갱신 함수
bool updateUserEmailState(MYSQL* connection,
                          const std::string& user_id,
                          const std::string& email,
                          bool is_email_verified) {
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "UPDATE users SET email = ?, is_email_verified = ? WHERE id = ?");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[3] = {};
    unsigned long email_length = static_cast<unsigned long>(email.size());
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    signed char verified_value = is_email_verified ? 1 : 0;

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(email.c_str());
    param_bind[0].buffer_length = email_length;
    param_bind[0].length = &email_length;

    param_bind[1].buffer_type = MYSQL_TYPE_TINY;
    param_bind[1].buffer = &verified_value;

    param_bind[2].buffer_type = MYSQL_TYPE_STRING;
    param_bind[2].buffer = const_cast<char*>(user_id.c_str());
    param_bind[2].buffer_length = user_id_length;
    param_bind[2].length = &user_id_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind updateUserEmailState params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute updateUserEmailState: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

// 사용자 이메일 인증 완료 처리 함수
bool markUserEmailVerified(MYSQL* connection, const std::string& user_id) {
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "UPDATE users SET is_email_verified = 1 WHERE id = ?");
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
        std::cerr << "[AUTH][DB] Failed to bind markUserEmailVerified param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute markUserEmailVerified: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

// 비밀번호 해시 DB 갱신 함수
bool updateStoredPasswordHash(MYSQL* connection,
                              const std::string& user_id,
                              const std::string& password_hash) {
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(
        connection,
        "UPDATE users SET password = ? WHERE id = ?");
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
        std::cerr << "[AUTH][DB] Failed to bind updateStoredPasswordHash params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute updateStoredPasswordHash: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

// 복구 토큰 저장 함수
bool insertRecoveryToken(MYSQL* connection,
                         const std::string& table_name,
                         const std::string& user_id,
                         const std::string& token_hash,
                         long long expires_at,
                         const std::string& request_ip,
                         const std::string& user_agent) {
    if (!connection) {
        return false;
    }

    const std::string query =
        "INSERT INTO " + table_name +
        " (user_id, token_hash, expires_at, request_ip, user_agent) "
        "VALUES (?, ?, FROM_UNIXTIME(?), ?, ?)";
    auto statement = prepareStatement(connection, query);
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[5] = {};
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    unsigned long token_hash_length = static_cast<unsigned long>(token_hash.size());
    long long expires_at_value = expires_at;

    unsigned long request_ip_length = static_cast<unsigned long>(request_ip.size());
    MysqlBindFlag request_ip_is_null = request_ip.empty() ? 1 : 0;
    unsigned long user_agent_length = static_cast<unsigned long>(user_agent.size());
    MysqlBindFlag user_agent_is_null = user_agent.empty() ? 1 : 0;

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(user_id.c_str());
    param_bind[0].buffer_length = user_id_length;
    param_bind[0].length = &user_id_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(token_hash.c_str());
    param_bind[1].buffer_length = token_hash_length;
    param_bind[1].length = &token_hash_length;

    param_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[2].buffer = &expires_at_value;

    param_bind[3].buffer_type = MYSQL_TYPE_STRING;
    param_bind[3].buffer = request_ip_is_null ? nullptr : const_cast<char*>(request_ip.c_str());
    param_bind[3].buffer_length = request_ip_length;
    param_bind[3].length = &request_ip_length;
    param_bind[3].is_null = &request_ip_is_null;

    param_bind[4].buffer_type = MYSQL_TYPE_STRING;
    param_bind[4].buffer = user_agent_is_null ? nullptr : const_cast<char*>(user_agent.c_str());
    param_bind[4].buffer_length = user_agent_length;
    param_bind[4].length = &user_agent_length;
    param_bind[4].is_null = &user_agent_is_null;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind insertRecoveryToken params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute insertRecoveryToken: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return true;
}

// 활성 토큰의 사용자 ID 조회 함수
bool loadActiveTokenUserId(MYSQL* connection,
                           const std::string& table_name,
                           const std::string& token_hash,
                           std::string* out_user_id) {
    if (!connection || !out_user_id) {
        return false;
    }

    const std::string query =
        "SELECT user_id FROM " + table_name +
        " WHERE token_hash = ? AND used_at IS NULL AND expires_at > NOW() LIMIT 1";
    auto statement = prepareStatement(connection, query);
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[1] = {};
    unsigned long token_hash_length = static_cast<unsigned long>(token_hash.size());
    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(token_hash.c_str());
    param_bind[0].buffer_length = token_hash_length;
    param_bind[0].length = &token_hash_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind loadActiveTokenUserId param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute loadActiveTokenUserId: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    std::array<char, kUserIdBufferBytes> user_id_buffer{};
    unsigned long user_id_length = 0;
    MysqlBindFlag user_id_is_null = 0;
    MysqlBindFlag user_id_bind_error = 0;

    MYSQL_BIND result_bind[1] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = user_id_buffer.data();
    result_bind[0].buffer_length = static_cast<unsigned long>(user_id_buffer.size());
    result_bind[0].length = &user_id_length;
    result_bind[0].is_null = &user_id_is_null;
    result_bind[0].error = &user_id_bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind loadActiveTokenUserId result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to store loadActiveTokenUserId result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA || user_id_is_null) {
        return false;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED || user_id_bind_error) {
        std::cerr << "[AUTH][DB] Failed to fetch loadActiveTokenUserId result" << std::endl;
        return false;
    }

    out_user_id->assign(user_id_buffer.data(), user_id_length);
    return true;
}

// 토큰 사용 완료 처리 함수
bool markTokenUsed(MYSQL* connection, const std::string& table_name, const std::string& token_hash) {
    if (!connection) {
        return false;
    }

    const std::string query =
        "UPDATE " + table_name +
        " SET used_at = NOW() WHERE token_hash = ? AND used_at IS NULL";
    auto statement = prepareStatement(connection, query);
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[1] = {};
    unsigned long token_hash_length = static_cast<unsigned long>(token_hash.size());
    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(token_hash.c_str());
    param_bind[0].buffer_length = token_hash_length;
    param_bind[0].length = &token_hash_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind markTokenUsed param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute markTokenUsed: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

// Delete unused recovery code record
bool deleteRecoveryTokenByHash(MYSQL* connection,
                               const std::string& table_name,
                               const std::string& token_hash) {
    if (!connection) {
        return false;
    }

    const std::string query =
        "DELETE FROM " + table_name +
        " WHERE token_hash = ? AND used_at IS NULL LIMIT 1";
    auto statement = prepareStatement(connection, query);
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[1] = {};
    unsigned long token_hash_length = static_cast<unsigned long>(token_hash.size());
    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(token_hash.c_str());
    param_bind[0].buffer_length = token_hash_length;
    param_bind[0].length = &token_hash_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[AUTH][DB] Failed to bind deleteRecoveryTokenByHash param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }
    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[AUTH][DB] Failed to execute deleteRecoveryTokenByHash: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

// 디버그 토큰 응답 노출 여부 확인 함수
bool shouldExposeDebugToken() {
    const char* env = std::getenv("AUTH_DEBUG_TOKEN_RESPONSE");
    return env && std::string(env) == "1";
}

// 요청 IP 문자열 추출 함수
std::string resolveRequestIp(const crow::request& req) {
    const std::string forwarded_for = req.get_header_value("X-Forwarded-For");
    if (!forwarded_for.empty()) {
        return forwarded_for;
    }
    return {};
}

}  // namespace

// 이메일 인증/비밀번호 재설정 라우트 등록 함수
void registerAuthRecoveryRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/auth/email/verify/request").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("id") || !x.has("email")) {
            return crow::response(400, "id 또는 email 값이 없습니다.");
        }

        const std::string user_id = x["id"].s();
        const std::string email = x["email"].s();
        if (const auto user_id_error = validateUserId(user_id)) {
            return crow::response(400, *user_id_error);
        }
        if (const auto email_error = validateEmail(email)) {
            return crow::response(400, *email_error);
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "데이터베이스 연결에 실패했습니다.");
        }

        bool id_exists = false;
        if (!isUserIdAlreadyRegistered(connection.get(), user_id, &id_exists)) {
            return crow::response(500, "아이디 중복 확인 중 오류가 발생했습니다.");
        }
        if (id_exists) {
            return crow::response(409, "이미 사용 중인 아이디입니다.");
        }

        bool email_exists = false;
        if (!isEmailAlreadyRegistered(connection.get(), email, &email_exists)) {
            return crow::response(500, "이메일 중복 확인 중 오류가 발생했습니다.");
        }
        if (email_exists) {
            return crow::response(409, "이미 사용 중인 이메일입니다.");
        }

        SignupEmailVerificationState latest_state;
        if (!loadLatestSignupEmailVerificationState(connection.get(), user_id, email, &latest_state)) {
            return crow::response(500, "기존 인증 상태 조회에 실패했습니다.");
        }

        const long long now = static_cast<long long>(std::time(nullptr));
        if (latest_state.found && !latest_state.consumed) {
            const long long elapsed_seconds = now - latest_state.created_at;
            if (elapsed_seconds < kEmailVerifyResendCooldownSeconds) {
                const int resend_after =
                    static_cast<int>(kEmailVerifyResendCooldownSeconds - elapsed_seconds);
                crow::json::wvalue res;
                res["status"] = "cooldown";
                res["message"] = "인증 코드를 다시 보내려면 잠시 후에 시도해 주세요.";
                res["resend_after"] = resend_after;
                return crow::response(429, res);
            }
        }

        const std::string code = generateEmailVerificationCode(kEmailVerifyCodeDigits);
        const std::string code_hash = sha256Hex(code);
        if (code.empty() || code_hash.empty()) {
            return crow::response(500, "인증 코드 생성에 실패했습니다.");
        }

        const long long expires_at = now + kEmailVerifyCodeTtlSeconds;
        const std::string request_ip = resolveRequestIp(req);
        const std::string user_agent = req.get_header_value("User-Agent");
        unsigned long long record_id = 0;
        if (!insertSignupEmailVerificationCode(connection.get(),
                                               user_id,
                                               email,
                                               code_hash,
                                               expires_at,
                                               request_ip,
                                               user_agent,
                                               &record_id)) {
            return crow::response(500, "인증 코드 저장에 실패했습니다.");
        }

        std::string mail_error;
        if (!sendAppScriptMail(email, code, "signup_verify", &mail_error)) {
            deleteSignupEmailVerificationRecord(connection.get(), record_id);
            return crow::response(
                502,
                mail_error.empty()
                    ? "인증 메일 발송에 실패했습니다."
                    : mail_error);
        }

        crow::json::wvalue res;
        res["status"] = "pending";
        res["message"] = "이메일 인증 코드가 발급되었습니다.";
        res["expires_in"] = kEmailVerifyCodeTtlSeconds;
        res["resend_after"] = kEmailVerifyResendCooldownSeconds;
        return crow::response(200, res);
    });

    CROW_ROUTE(app, "/auth/email/verify/confirm").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("id") || !x.has("email") || (!x.has("code") && !x.has("token"))) {
            return crow::response(400, "id, email, code 값이 필요합니다.");
        }

        const std::string user_id = x["id"].s();
        const std::string email = x["email"].s();
        const std::string code = x.has("code") ? x["code"].s() : x["token"].s();
        if (const auto user_id_error = validateUserId(user_id)) {
            return crow::response(400, *user_id_error);
        }
        if (const auto email_error = validateEmail(email)) {
            return crow::response(400, *email_error);
        }
        if (const auto code_error = validateEmailVerificationCode(code)) {
            return crow::response(400, *code_error);
        }

        const std::string code_hash = sha256Hex(code);
        if (code_hash.empty()) {
            return crow::response(500, "인증 코드 해시 계산에 실패했습니다.");
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "데이터베이스 연결에 실패했습니다.");
        }

        SignupEmailVerificationRecord verification_record;
        if (!loadActiveSignupEmailVerificationByCode(connection.get(),
                                                     user_id,
                                                     email,
                                                     code_hash,
                                                     &verification_record)) {
            return crow::response(500, "인증 코드 조회 중 오류가 발생했습니다.");
        }
        if (!verification_record.found) {
            return crow::response(400, "유효하지 않거나 만료된 인증 코드입니다.");
        }

        if (!markSignupEmailVerificationConfirmed(connection.get(), verification_record.id)) {
            return crow::response(500, "이메일 인증 처리에 실패했습니다.");
        }

        crow::json::wvalue res;
        res["status"] = "success";
        res["verified"] = true;
        res["id"] = verification_record.user_id;
        res["email"] = verification_record.email;
        res["message"] = "이메일 인증이 완료되었습니다.";
        return crow::response(200, res);
    });
    CROW_ROUTE(app, "/auth/password/forgot").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("id") || !x.has("email")) {
            return crow::response(400, "id 또는 email 값이 없습니다.");
        }

        const std::string user_id = x["id"].s();
        const std::string email = x["email"].s();
        if (const auto user_id_error = validateUserId(user_id)) {
            return crow::response(400, *user_id_error);
        }
        if (const auto email_error = validateEmail(email)) {
            return crow::response(400, *email_error);
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "데이터베이스 연결에 실패했습니다.");
        }

        bool can_issue_token = false;
        UserEmailInfo user_info;
        if (loadUserEmailInfo(connection.get(), user_id, &user_info) &&
            user_info.found &&
            !user_info.email.empty() &&
            user_info.email == email) {
            can_issue_token = true;
        }

        std::optional<std::string> debug_code;
        if (can_issue_token) {
            const std::string code = generateEmailVerificationCode(kEmailVerifyCodeDigits);
            const std::string code_hash = sha256Hex(code);
            if (code.empty() || code_hash.empty()) {
                return crow::response(500, "비밀번호 재설정 코드 생성에 실패했습니다.");
            }

            const long long expires_at =
                static_cast<long long>(std::time(nullptr)) + kPasswordResetCodeTtlSeconds;
            const std::string request_ip = resolveRequestIp(req);
            const std::string user_agent = req.get_header_value("User-Agent");
            if (!insertRecoveryToken(connection.get(),
                                     "password_reset_tokens",
                                     user_id,
                                     code_hash,
                                     expires_at,
                                     request_ip,
                                     user_agent)) {
                return crow::response(500, "비밀번호 재설정 코드 저장에 실패했습니다.");
            }

            std::string mail_error;
            if (!sendAppScriptMail(email, code, "password_reset", &mail_error)) {
                deleteRecoveryTokenByHash(connection.get(), "password_reset_tokens", code_hash);
                return crow::response(
                    502,
                    mail_error.empty()
                        ? "비밀번호 재설정 메일 발송에 실패했습니다."
                        : mail_error);
            }

            if (shouldExposeDebugToken()) {
                debug_code = code;
            }
        }

        crow::json::wvalue res;
        res["status"] = "success";
        res["message"] = "입력한 정보가 일치하면 비밀번호 재설정 코드가 메일로 전송됩니다.";
        res["expires_in"] = kPasswordResetCodeTtlSeconds;
        if (debug_code.has_value()) {
            res["debug_code"] = *debug_code;
        }
        return crow::response(200, res);
    });

    CROW_ROUTE(app, "/auth/password/reset").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x || !x.has("new_password") || (!x.has("code") && !x.has("token"))) {
            return crow::response(400, "code 또는 new_password 값이 없습니다.");
        }

        const bool is_legacy_token_request = x.has("token") && !x.has("code");
        const std::string code = x.has("code") ? x["code"].s() : x["token"].s();
        const std::string new_password = x["new_password"].s();
        if (code.empty()) {
            return crow::response(400, "재설정 코드를 입력해 주세요.");
        }
        if (new_password.empty()) {
            return crow::response(400, "새 비밀번호를 입력해 주세요.");
        }
        if (const auto complexity_error = validatePasswordComplexity(new_password)) {
            return crow::response(400, *complexity_error);
        }
        if (!is_legacy_token_request) {
            if (const auto code_error = validatePasswordResetCode(code)) {
                return crow::response(400, *code_error);
            }
        }

        const std::string code_hash = sha256Hex(code);
        if (code_hash.empty()) {
            return crow::response(500, "재설정 코드 해시 계산에 실패했습니다.");
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "데이터베이스 연결에 실패했습니다.");
        }

        std::string user_id;
        if (!loadActiveTokenUserId(connection.get(), "password_reset_tokens", code_hash, &user_id)) {
            return crow::response(400, "유효하지 않거나 만료된 재설정 코드입니다.");
        }

        const std::string new_password_hash = hashPassword(new_password);
        if (new_password_hash.empty()) {
            return crow::response(500, "비밀번호 해시 생성에 실패했습니다.");
        }
        if (!updateStoredPasswordHash(connection.get(), user_id, new_password_hash)) {
            return crow::response(500, "비밀번호 변경에 실패했습니다.");
        }
        if (!markTokenUsed(connection.get(), "password_reset_tokens", code_hash)) {
            return crow::response(500, "재설정 코드 해시 계산에 실패했습니다.");
        }

        crow::json::wvalue res;
        res["status"] = "success";
        res["changed"] = true;
        res["message"] = "비밀번호가 성공적으로 재설정되었습니다.";
        return crow::response(200, res);
    });
}

// 회원가입 전 이메일 인증 완료 여부 확인 함수
bool checkSignupEmailVerified(const std::string& user_id,
                              const std::string& email,
                              std::string* error_message) {
    auto connection = openDatabaseConnection();
    if (!connection) {
        if (error_message) {
            *error_message = "데이터베이스 연결에 실패했습니다.";
        }
        return false;
    }

    bool is_verified = false;
    if (!hasVerifiedSignupEmailVerification(connection.get(), user_id, email, &is_verified)) {
        if (error_message) {
            *error_message = "이메일 인증 상태를 확인하지 못했습니다.";
        }
        return false;
    }
    if (!is_verified) {
        if (error_message) {
            *error_message = "이메일 인증이 완료되지 않았습니다. 이메일 인증 후 다시 시도해 주세요.";
        }
        return false;
    }

    return true;
}

// 회원가입 완료 시 이메일 인증 토큰 사용 처리 함수
bool consumeSignupEmailVerification(const std::string& user_id,
                                    const std::string& email,
                                    std::string* error_message) {
    auto connection = openDatabaseConnection();
    if (!connection) {
        if (error_message) {
            *error_message = "데이터베이스 연결에 실패했습니다.";
        }
        return false;
    }

    if (!consumeVerifiedSignupEmailVerification(connection.get(), user_id, email)) {
        if (error_message) {
            *error_message = "이메일 인증 사용 처리에 실패했습니다.";
        }
        return false;
    }

    return true;
}
