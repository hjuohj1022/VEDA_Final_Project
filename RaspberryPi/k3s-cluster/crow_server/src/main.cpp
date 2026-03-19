#include "crow.h"
#include "../include/SunapiProxy.h"
#include "../include/SunapiWsProxy.h"
#include "../include/CctvManager.h"
#include "../include/CctvProxy.h"
#include "../include/EspHealthManager.h"
#include "../include/MqttManager.h"
#include "../include/MotorManager.h"
#include "../include/ThermalProxy.h"
#include "../include/AuthRecoveryRoutes.h"
#include <jwt-cpp/jwt.h> 
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <cstdlib> 
#include <fstream> 
#include <iomanip>
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

std::string getJwtSecret();

namespace {
bool verifyTokenStage(const std::string& token,
                      const std::string& expected_stage,
                      std::string* user_id_out);
// 인증/DB/파일 스트리밍 헬퍼의 라우트 바깥 배치.
// main()의 서비스 등록 흐름 집중 목적 구조.
constexpr char kDatabaseName[] = "veda_db";
constexpr size_t kMaxUserIdLength = 64;
constexpr size_t kMaxPasswordLength = 128;
constexpr size_t kPasswordPolicyMinLength = 8;
constexpr size_t kPasswordPolicyMaxLength = 16;
constexpr size_t kStoredPasswordBufferBytes = 256;
constexpr int kPasswordHashIterations = 120000;
constexpr size_t kPasswordSaltBytes = 16;
constexpr size_t kPasswordHashBytes = 32;
constexpr char kPasswordHashPrefix[] = "pbkdf2_sha256";
constexpr unsigned int kMysqlErrDuplicateEntry = 1062;
constexpr long long kMaxStreamResponseBytes = 8LL * 1024LL * 1024LL;
// TOTP 2FA는 Authenticator 앱과 서버가 동일한 규칙을 공유해야 하므로 상수로 고정한다.
constexpr size_t kTotpSecretBufferBytes = 128;
constexpr size_t kTotpSecretBytes = 20;
constexpr int kTotpDigits = 6;
constexpr int kTotpPeriodSeconds = 30;
constexpr int kTotpAllowedSkewSteps = 1;
constexpr int kPreAuthTokenTtlSeconds = 300;
constexpr int kPendingTotpTtlSeconds = 300;
constexpr char kJwtStageFull[] = "full";
constexpr char kJwtStagePre2fa[] = "pre_2fa";
constexpr char kOtpIssuer[] = "VEDA";

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

// users 테이블의 2FA 관련 상태를 한 번에 읽어 라우트 분기에서 재사용한다.
struct UserTwoFactorInfo {
    bool found = false;
    bool enabled = false;
    std::string secret;
    std::string pending_secret;
    long long pending_expires_at = 0;
    long long last_used_step = -1;
};

enum class RegisterUserStatus {
    Success,
    DuplicateId,
    DuplicateEmail,
    InvalidInput,
    DbError,
};

struct UserEmailVerificationState {
    bool found = false;
    bool has_email = false;
    bool is_email_verified = false;
};

std::optional<std::string> validateCredentials(const std::string& user_id,
                                               const std::string& password) {
    if (user_id.empty() || password.empty()) {
        return "아이디와 비밀번호를 입력해 주세요.";
    }
    if (user_id.size() > kMaxUserIdLength) {
        return "아이디 길이가 너무 깁니다.";
    }
    if (password.size() > kMaxPasswordLength) {
        return "비밀번호 길이가 너무 깁니다.";
    }
    return std::nullopt;
}

// 회원가입 비밀번호 복잡도 규칙 검증 함수
std::optional<std::string> validatePasswordComplexity(const std::string& password) {
    if (password.size() < kPasswordPolicyMinLength) {
        return "비밀번호는 8자 이상이어야 합니다.";
    }
    if (password.size() > kPasswordPolicyMaxLength) {
        return "비밀번호는 16자 이하여야 합니다.";
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
        if (!std::isalnum(ch) && !std::isspace(ch)) {
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

// 회원가입 입력값 종합 검증 함수
std::optional<std::string> validateRegistrationCredentials(const std::string& user_id,
                                                           const std::string& password) {
    if (const auto basic_error = validateCredentials(user_id, password)) {
        return basic_error;
    }
    return validatePasswordComplexity(password);
}

// 회원가입 이메일 형식 검증 함수
std::optional<std::string> validateRegistrationEmail(const std::string& email) {
    if (email.empty()) {
        return "email 값을 입력해 주세요.";
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

// OTP는 검증 로직에 들어가기 전에 숫자 6자리 형식인지 먼저 확인한다.
std::optional<std::string> validateOtpCode(const std::string& code) {
    if (code.size() != static_cast<size_t>(kTotpDigits)) {
        return "OTP must be 6 digits";
    }

    if (!std::all_of(code.begin(), code.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        return "OTP must contain only digits";
    }

    return std::nullopt;
}

std::optional<std::string> extractBearerToken(const crow::request& req) {
    const std::string auth_header = req.get_header_value("Authorization");
    if (auth_header.size() <= 7 || auth_header.rfind("Bearer ", 0) != 0) {
        return std::nullopt;
    }
    return auth_header.substr(7);
}

std::string urlEncodeComponent(const std::string& input) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex;

    for (unsigned char ch : input) {
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            oss << static_cast<char>(ch);
        } else {
            oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }

    return oss.str();
}

// Authenticator 앱과의 호환을 위해 secret은 Base32 문자열로 저장하고 전달한다.
std::string base32Encode(const unsigned char* data, size_t size) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    if (!data || size == 0) {
        return {};
    }

    std::string encoded;
    encoded.reserve((size * 8 + 4) / 5);

    std::uint32_t buffer = 0;
    int bits_left = 0;
    for (size_t index = 0; index < size; ++index) {
        buffer = (buffer << 8) | data[index];
        bits_left += 8;

        while (bits_left >= 5) {
            encoded.push_back(kAlphabet[(buffer >> (bits_left - 5)) & 0x1F]);
            bits_left -= 5;
        }
    }

    if (bits_left > 0) {
        encoded.push_back(kAlphabet[(buffer << (5 - bits_left)) & 0x1F]);
    }

    return encoded;
}

bool base32Decode(const std::string& text, std::vector<unsigned char>* out) {
    if (!out) {
        return false;
    }

    out->clear();
    if (text.empty()) {
        return false;
    }

    std::uint32_t buffer = 0;
    int bits_left = 0;

    for (unsigned char raw_ch : text) {
        if (raw_ch == '=' || raw_ch == ' ' || raw_ch == '-') {
            continue;
        }

        const unsigned char ch = static_cast<unsigned char>(std::toupper(raw_ch));
        int value = -1;
        if (ch >= 'A' && ch <= 'Z') {
            value = ch - 'A';
        } else if (ch >= '2' && ch <= '7') {
            value = ch - '2' + 26;
        } else {
            out->clear();
            return false;
        }

        buffer = (buffer << 5) | static_cast<std::uint32_t>(value);
        bits_left += 5;
        while (bits_left >= 8) {
            out->push_back(static_cast<unsigned char>((buffer >> (bits_left - 8)) & 0xFF));
            bits_left -= 8;
        }
    }

    return !out->empty();
}

std::string generateRandomBase32Secret() {
    std::array<unsigned char, kTotpSecretBytes> secret_bytes{};
    if (RAND_bytes(secret_bytes.data(), static_cast<int>(secret_bytes.size())) != 1) {
        std::cerr << "[AUTH] RAND_bytes failed for TOTP secret" << std::endl;
        return {};
    }

    return base32Encode(secret_bytes.data(), secret_bytes.size());
}

std::string buildOtpAuthUrl(const std::string& issuer,
                            const std::string& user_id,
                            const std::string& secret) {
    const std::string label = issuer + ":" + user_id;
    return "otpauth://totp/" + urlEncodeComponent(label) +
           "?secret=" + secret +
           "&issuer=" + urlEncodeComponent(issuer) +
           "&algorithm=SHA1&digits=" + std::to_string(kTotpDigits) +
           "&period=" + std::to_string(kTotpPeriodSeconds);
}

// RFC 6238 방식으로 현재 time-step에 대응하는 6자리 OTP를 계산한다.
bool generateTotpCodeAtStep(const std::string& base32_secret,
                            long long step,
                            std::string* out_code) {
    if (!out_code || step < 0) {
        return false;
    }

    std::vector<unsigned char> secret_bytes;
    if (!base32Decode(base32_secret, &secret_bytes)) {
        return false;
    }

    std::array<unsigned char, 8> counter{};
    std::uint64_t value = static_cast<std::uint64_t>(step);
    for (int index = static_cast<int>(counter.size()) - 1; index >= 0; --index) {
        counter[static_cast<size_t>(index)] = static_cast<unsigned char>(value & 0xFF);
        value >>= 8;
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_length = 0;
    if (!HMAC(EVP_sha1(),
              secret_bytes.data(),
              static_cast<int>(secret_bytes.size()),
              counter.data(),
              static_cast<int>(counter.size()),
              digest.data(),
              &digest_length) || digest_length < 20) {
        return false;
    }

    const int offset = digest[digest_length - 1] & 0x0F;
    if (offset + 3 >= static_cast<int>(digest_length)) {
        return false;
    }

    const int binary_code =
        ((digest[static_cast<size_t>(offset)] & 0x7F) << 24) |
        ((digest[static_cast<size_t>(offset + 1)] & 0xFF) << 16) |
        ((digest[static_cast<size_t>(offset + 2)] & 0xFF) << 8) |
        (digest[static_cast<size_t>(offset + 3)] & 0xFF);

    const int otp = binary_code % 1000000;
    std::ostringstream oss;
    oss << std::setw(kTotpDigits) << std::setfill('0') << otp;
    *out_code = oss.str();
    return true;
}

bool verifyTotpCode(const std::string& base32_secret,
                    const std::string& input_code,
                    long long last_used_step,
                    long long* matched_step) {
    if (matched_step) {
        *matched_step = -1;
    }
    if (validateOtpCode(input_code)) {
        return false;
    }

    const long long now = static_cast<long long>(std::time(nullptr));
    const long long current_step = now / kTotpPeriodSeconds;

    // 시간 오차를 조금 허용하되, 이미 사용한 step 이하의 OTP는 재사용하지 않는다.
    for (int delta = -kTotpAllowedSkewSteps; delta <= kTotpAllowedSkewSteps; ++delta) {
        const long long candidate_step = current_step + delta;
        if (candidate_step < 0 || candidate_step <= last_used_step) {
            continue;
        }

        std::string expected_code;
        if (!generateTotpCodeAtStep(base32_secret, candidate_step, &expected_code)) {
            continue;
        }

        if (expected_code.size() == input_code.size() &&
            CRYPTO_memcmp(expected_code.data(), input_code.data(), input_code.size()) == 0) {
            if (matched_step) {
                *matched_step = candidate_step;
            }
            return true;
        }
    }

    return false;
}

// 로그인과 /2fa 라우트에서 공통으로 쓰는 2FA 상태 조회를 prepared statement로 묶는다.
bool loadUserTwoFactorInfo(MYSQL* connection,
                           const std::string& user_id,
                           UserTwoFactorInfo* out) {
    if (!connection || !out) {
        return false;
    }

    *out = {};

    auto statement = prepareStatement(
        connection,
        "SELECT two_factor_enabled, "
        "IFNULL(totp_secret, ''), "
        "IFNULL(totp_pending_secret, ''), "
        "IFNULL(UNIX_TIMESTAMP(totp_pending_expires_at), 0), "
        "IFNULL(totp_last_used_step, -1) "
        "FROM users WHERE id = ? LIMIT 1");
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
        std::cerr << "[DB Error] Failed to bind 2FA lookup param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to execute 2FA lookup: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    signed char enabled_value = 0;
    std::array<char, kTotpSecretBufferBytes> secret_buffer{};
    std::array<char, kTotpSecretBufferBytes> pending_secret_buffer{};
    unsigned long secret_length = 0;
    unsigned long pending_secret_length = 0;
    long long pending_expires_at = 0;
    long long last_used_step = -1;
    MysqlBindFlag is_null_flags[5] = {};
    MysqlBindFlag error_flags[5] = {};

    MYSQL_BIND result_bind[5] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_TINY;
    result_bind[0].buffer = &enabled_value;
    result_bind[0].is_null = &is_null_flags[0];
    result_bind[0].error = &error_flags[0];

    result_bind[1].buffer_type = MYSQL_TYPE_STRING;
    result_bind[1].buffer = secret_buffer.data();
    result_bind[1].buffer_length = static_cast<unsigned long>(secret_buffer.size());
    result_bind[1].length = &secret_length;
    result_bind[1].is_null = &is_null_flags[1];
    result_bind[1].error = &error_flags[1];

    result_bind[2].buffer_type = MYSQL_TYPE_STRING;
    result_bind[2].buffer = pending_secret_buffer.data();
    result_bind[2].buffer_length = static_cast<unsigned long>(pending_secret_buffer.size());
    result_bind[2].length = &pending_secret_length;
    result_bind[2].is_null = &is_null_flags[2];
    result_bind[2].error = &error_flags[2];

    result_bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[3].buffer = &pending_expires_at;
    result_bind[3].is_null = &is_null_flags[3];
    result_bind[3].error = &error_flags[3];

    result_bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[4].buffer = &last_used_step;
    result_bind[4].is_null = &is_null_flags[4];
    result_bind[4].error = &error_flags[4];

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind 2FA lookup result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to store 2FA lookup result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        return true;
    }

    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED ||
        std::any_of(std::begin(error_flags), std::end(error_flags), [](MysqlBindFlag flag) {
            return flag != 0;
        })) {
        std::cerr << "[DB Error] Failed to fetch 2FA state" << std::endl;
        return false;
    }

    out->found = true;
    out->enabled = enabled_value != 0;
    out->secret.assign(secret_buffer.data(), secret_length);
    out->pending_secret.assign(pending_secret_buffer.data(), pending_secret_length);
    out->pending_expires_at = pending_expires_at;
    out->last_used_step = last_used_step;
    return true;
}

// setup/init 단계에서는 아직 활성화하지 않고 pending secret으로만 저장한다.
bool savePendingTotpSecret(MYSQL* connection,
                           const std::string& user_id,
                           const std::string& pending_secret,
                           long long expires_at) {
    auto statement = prepareStatement(
        connection,
        "UPDATE users SET totp_pending_secret = ?, "
        "totp_pending_expires_at = FROM_UNIXTIME(?) WHERE id = ?");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[3] = {};
    unsigned long pending_secret_length = static_cast<unsigned long>(pending_secret.size());
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    long long expires_at_value = expires_at;

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(pending_secret.c_str());
    param_bind[0].buffer_length = pending_secret_length;
    param_bind[0].length = &pending_secret_length;

    param_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[1].buffer = &expires_at_value;

    param_bind[2].buffer_type = MYSQL_TYPE_STRING;
    param_bind[2].buffer = const_cast<char*>(user_id.c_str());
    param_bind[2].buffer_length = user_id_length;
    param_bind[2].length = &user_id_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind pending TOTP params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to save pending TOTP secret: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return true;
}

// 첫 OTP 검증이 끝난 시점에만 pending secret을 실제 로그인용 secret으로 승격한다.
bool activatePendingTotpSecret(MYSQL* connection,
                               const std::string& user_id,
                               long long step) {
    auto statement = prepareStatement(
        connection,
        "UPDATE users SET two_factor_enabled = 1, "
        "totp_secret = totp_pending_secret, "
        "totp_pending_secret = NULL, "
        "totp_pending_expires_at = NULL, "
        "totp_last_used_step = ? "
        "WHERE id = ? AND totp_pending_secret IS NOT NULL");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[2] = {};
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    long long step_value = step;

    param_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[0].buffer = &step_value;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(user_id.c_str());
    param_bind[1].buffer_length = user_id_length;
    param_bind[1].length = &user_id_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind TOTP activation params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to activate TOTP secret: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

// 같은 30초 window의 OTP 재사용을 막기 위해 마지막으로 승인한 step을 기록한다.
bool updateLastUsedTotpStep(MYSQL* connection,
                            const std::string& user_id,
                            long long step) {
    auto statement = prepareStatement(connection, "UPDATE users SET totp_last_used_step = ? WHERE id = ?");
    if (!statement) {
        return false;
    }

    MYSQL_BIND param_bind[2] = {};
    unsigned long user_id_length = static_cast<unsigned long>(user_id.size());
    long long step_value = step;

    param_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param_bind[0].buffer = &step_value;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(user_id.c_str());
    param_bind[1].buffer_length = user_id_length;
    param_bind[1].length = &user_id_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind last-used-step params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to update last TOTP step: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return true;
}

// 2FA 비활성화 시에는 active/pending 상태를 모두 지워 다음 설정을 깨끗하게 시작한다.
bool disableUserTwoFactor(MYSQL* connection,
                          const std::string& user_id) {
    auto statement = prepareStatement(
        connection,
        "UPDATE users SET two_factor_enabled = 0, "
        "totp_secret = NULL, "
        "totp_pending_secret = NULL, "
        "totp_pending_expires_at = NULL, "
        "totp_last_used_step = NULL "
        "WHERE id = ?");
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
        std::cerr << "[DB Error] Failed to bind 2FA disable params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to disable 2FA: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return true;
}

// Account deletion is intentionally a separate helper so the route can stay focused
// on security checks: authenticated user, password re-entry, optional OTP, then delete.
bool deleteUserAccount(MYSQL* connection,
                       const std::string& user_id) {
    auto statement = prepareStatement(connection, "DELETE FROM users WHERE id = ? LIMIT 1");
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
        std::cerr << "[DB Error] Failed to bind account delete params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to delete account: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    return mysql_stmt_affected_rows(statement.get()) > 0;
}

bool verifyUserPassword(MYSQL* connection,
                        const std::string& inputId,
                        const std::string& inputPw) {
    if (!connection) {
        return false;
    }

    std::string stored_password;
    if (!loadStoredPassword(connection, inputId, &stored_password)) {
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
        updateStoredPasswordHash(connection, inputId, upgraded_hash);
    }
    return true;
}

// 로그인 시 이메일 인증 상태 조회 함수
bool loadUserEmailVerificationState(MYSQL* connection,
                                    const std::string& user_id,
                                    UserEmailVerificationState* out) {
    if (!connection || !out) {
        return false;
    }

    *out = {};

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
        std::cerr << "[DB Error] Failed to bind email verify lookup param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to execute email verify lookup: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    std::array<char, 320> email_buffer{};
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
        std::cerr << "[DB Error] Failed to bind email verify lookup result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to store email verify lookup result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA) {
        return true;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED || email_bind_error || verified_bind_error) {
        std::cerr << "[DB Error] Failed to fetch email verify lookup result" << std::endl;
        return false;
    }

    out->found = true;
    const std::string email_value = email_is_null ? std::string{} : std::string(email_buffer.data(), email_length);
    out->has_email = !email_value.empty();
    out->is_email_verified = !verified_is_null && verified_value != 0;
    return true;
}

// A signed JWT is not enough on its own once account deletion is supported.
// Protected APIs should also confirm that the user row still exists, otherwise
// an old token issued before deletion could continue to work until expiry.
bool doesUserExist(MYSQL* connection,
                   const std::string& user_id) {
    if (!connection) {
        return false;
    }

    auto statement = prepareStatement(connection, "SELECT 1 FROM users WHERE id = ? LIMIT 1");
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
        std::cerr << "[DB Error] Failed to bind user-exists param: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to execute user-exists lookup: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    int exists_value = 0;
    MysqlBindFlag is_null = 0;
    MysqlBindFlag bind_error = 0;
    MYSQL_BIND result_bind[1] = {};
    result_bind[0].buffer_type = MYSQL_TYPE_LONG;
    result_bind[0].buffer = &exists_value;
    result_bind[0].is_null = &is_null;
    result_bind[0].error = &bind_error;

    if (mysql_stmt_bind_result(statement.get(), result_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind user-exists result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    if (mysql_stmt_store_result(statement.get()) != 0) {
        std::cerr << "[DB Error] Failed to store user-exists result: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(statement.get());
    if (fetch_result == MYSQL_NO_DATA || is_null) {
        return false;
    }
    if (fetch_result == 1 || fetch_result == MYSQL_DATA_TRUNCATED || bind_error) {
        std::cerr << "[DB Error] Failed to fetch user-exists result" << std::endl;
        return false;
    }

    return exists_value == 1;
}

bool loadExistingFullJwtUserId(const std::string& token,
                               std::string* user_id_out) {
    std::string user_id;
    if (!verifyTokenStage(token, kJwtStageFull, &user_id)) {
        return false;
    }

    auto connection = openDatabaseConnection();
    if (!connection) {
        std::cerr << "[AUTH] Failed to verify JWT user existence because DB connection failed" << std::endl;
        return false;
    }

    if (!doesUserExist(connection.get(), user_id)) {
        std::cerr << "[AUTH] JWT rejected because user no longer exists: " << user_id << std::endl;
        return false;
    }

    if (user_id_out) {
        *user_id_out = user_id;
    }
    return true;
}

// full access token과 pre_2fa token을 같은 포맷으로 발급하고 claim으로만 역할을 구분한다.
std::string generateToken(const std::string& user_id,
                          const std::string& auth_stage,
                          std::chrono::seconds ttl) {
    return jwt::create()
        .set_issuer("veda_auth_server")
        .set_type("JWS")
        .set_payload_claim("user_id", jwt::claim(user_id))
        .set_payload_claim("auth_stage", jwt::claim(auth_stage))
        .set_issued_at(std::chrono::system_clock::now())
        .set_expires_at(std::chrono::system_clock::now() + ttl)
        .sign(jwt::algorithm::hs256{getJwtSecret()});
}

bool verifyTokenStage(const std::string& token,
                      const std::string& expected_stage,
                      std::string* user_id_out) {
    try {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{getJwtSecret()})
            .with_issuer("veda_auth_server");

        verifier.verify(decoded);

        const std::string user_id = decoded.get_payload_claim("user_id").as_string();
        if (user_id_out) {
            *user_id_out = user_id;
        }

        try {
            const std::string auth_stage = decoded.get_payload_claim("auth_stage").as_string();
            return auth_stage == expected_stage;
        } catch (const std::exception&) {
            // 구형 토큰과의 호환을 위해 auth_stage가 없으면 full 토큰으로만 간주한다.
            return expected_stage == kJwtStageFull;
        }
    } catch (const std::exception& e) {
        std::cerr << "[JWT Verify Error] " << e.what() << std::endl;
        return false;
    }
}

// 보호 라우트는 full stage JWT만 통과시키고 pre_auth 토큰은 막는다.
std::optional<std::string> getAuthorizedUserId(const crow::request& req) {
    const auto token = extractBearerToken(req);
    if (!token) {
        return std::nullopt;
    }

    std::string user_id;
    // The token must be a full-access JWT and its owner must still exist.
    // This closes the gap where a deleted account could keep using an old JWT
    // until the token expiration time.
    if (!loadExistingFullJwtUserId(*token, &user_id)) {
        return std::nullopt;
    }

    return user_id;
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
    return generateToken(userId, kJwtStageFull, std::chrono::hours{24});
}

std::string generatePreAuthJWT(const std::string& userId) {
    return generateToken(userId, kJwtStagePre2fa, std::chrono::seconds{kPreAuthTokenTtlSeconds});
}

bool verifyJWT(const std::string& token) {
    return loadExistingFullJwtUserId(token, nullptr);
}

bool verifyPreAuthJWT(const std::string& token, std::string* user_id_out) {
    return verifyTokenStage(token, kJwtStagePre2fa, user_id_out);
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

    return verifyUserPassword(connection.get(), inputId, inputPw);
}

RegisterUserStatus registerUserToDBSecure(const std::string& inputId,
                                          const std::string& inputPw,
                                          const std::string& inputEmail) {
    // 신규 계정의 prepared statement + PBKDF2 해시 형태 저장.
    if (validateRegistrationCredentials(inputId, inputPw)) {
        return RegisterUserStatus::InvalidInput;
    }
    if (validateRegistrationEmail(inputEmail)) {
        return RegisterUserStatus::InvalidInput;
    }

    auto connection = openDatabaseConnection();
    if (!connection) {
        return RegisterUserStatus::DbError;
    }

    const std::string password_hash = hashPassword(inputPw);
    if (password_hash.empty()) {
        return RegisterUserStatus::DbError;
    }

    auto statement = prepareStatement(
        connection.get(),
        "INSERT INTO users (id, email, is_email_verified, password) VALUES (?, ?, 1, ?)");
    if (!statement) {
        return RegisterUserStatus::DbError;
    }

    MYSQL_BIND param_bind[3] = {};
    unsigned long user_id_length = static_cast<unsigned long>(inputId.size());
    unsigned long email_length = static_cast<unsigned long>(inputEmail.size());
    unsigned long password_hash_length = static_cast<unsigned long>(password_hash.size());

    param_bind[0].buffer_type = MYSQL_TYPE_STRING;
    param_bind[0].buffer = const_cast<char*>(inputId.c_str());
    param_bind[0].buffer_length = user_id_length;
    param_bind[0].length = &user_id_length;

    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = const_cast<char*>(inputEmail.c_str());
    param_bind[1].buffer_length = email_length;
    param_bind[1].length = &email_length;

    param_bind[2].buffer_type = MYSQL_TYPE_STRING;
    param_bind[2].buffer = const_cast<char*>(password_hash.c_str());
    param_bind[2].buffer_length = password_hash_length;
    param_bind[2].length = &password_hash_length;

    if (mysql_stmt_bind_param(statement.get(), param_bind) != 0) {
        std::cerr << "[DB Error] Failed to bind register params: "
                  << mysql_stmt_error(statement.get()) << std::endl;
        return RegisterUserStatus::DbError;
    }

    if (mysql_stmt_execute(statement.get()) != 0) {
        const unsigned int error_code = mysql_stmt_errno(statement.get());
        std::cerr << "[DB Error] Failed to register user (" << error_code << "): "
                  << mysql_stmt_error(statement.get()) << std::endl;
        if (error_code == kMysqlErrDuplicateEntry) {
            const std::string db_error = mysql_stmt_error(statement.get());
            if (db_error.find("uq_users_email") != std::string::npos) {
                return RegisterUserStatus::DuplicateEmail;
            }
            return RegisterUserStatus::DuplicateId;
        }
        return RegisterUserStatus::DbError;
    }

    return RegisterUserStatus::Success;
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
    registerThermalProxyRoutes(app);

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
        if (!x) return crow::response(400, "잘못된 JSON 형식입니다.");

        if (!x.has("id") || !x.has("password") || !x.has("email")) {
            return crow::response(400, "id, password 또는 email 값이 없습니다.");
        }

        std::string id = x["id"].s();
        std::string pw = x["password"].s();
        std::string email = x["email"].s();

        if (const auto credential_error = validateRegistrationCredentials(id, pw)) {
            return crow::response(400, *credential_error);
        }
        if (const auto email_error = validateRegistrationEmail(email)) {
            return crow::response(400, *email_error);
        }

        // 가입 전 이메일 인증 완료 여부 검증
        std::string verify_error_message;
        if (!checkSignupEmailVerified(id, email, &verify_error_message)) {
            const std::string response_message = verify_error_message.empty()
                ? "이메일 인증이 완료되지 않았습니다. 이메일 인증 후 다시 시도해 주세요."
                : verify_error_message;
            return crow::response(403, response_message);
        }

        const RegisterUserStatus register_status = registerUserToDBSecure(id, pw, email);
        if (register_status == RegisterUserStatus::Success) {
            // 가입 성공 후 인증 상태를 소모 처리
            std::string consume_error_message;
            if (!consumeSignupEmailVerification(id, email, &consume_error_message)) {
                std::cerr << "[AUTH][REGISTER] consumeSignupEmailVerification failed: "
                          << consume_error_message << std::endl;
            }

            crow::json::wvalue res;
            res["status"] = "success";
            res["message"] = "회원가입이 완료되었습니다. 로그인해 주세요.";
            res["requires_email_verification"] = false;
            return crow::response(201, res);
        }

        if (register_status == RegisterUserStatus::DuplicateId) {
            return crow::response(409, "이미 사용 중인 아이디입니다.");
        }

        if (register_status == RegisterUserStatus::DuplicateEmail) {
            return crow::response(409, "이미 사용 중인 이메일입니다.");
        }

        if (register_status == RegisterUserStatus::InvalidInput) {
            return crow::response(400, "아이디 또는 비밀번호 형식이 올바르지 않습니다.");
        }

        return crow::response(500, "회원가입 처리 중 데이터베이스 오류가 발생했습니다.");
    });

    // 이메일 인증/비밀번호 재설정 라우트 등록
    registerAuthRecoveryRoutes(app);

    // ==========================================
    // TOTP 2FA 설정/검증 API
    // ==========================================
    CROW_ROUTE(app, "/2fa/status").methods(crow::HTTPMethod::GET)
    ([](const crow::request& req){
        const auto user_id = getAuthorizedUserId(req);
        if (!user_id) {
            return crow::response(401, "Unauthorized");
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "Database connection failed");
        }

        UserTwoFactorInfo two_factor_info;
        if (!loadUserTwoFactorInfo(connection.get(), *user_id, &two_factor_info) || !two_factor_info.found) {
            return crow::response(404, "User not found");
        }

        const long long now = static_cast<long long>(std::time(nullptr));
        const bool has_pending_setup = !two_factor_info.pending_secret.empty()
                                       && (two_factor_info.pending_expires_at <= 0
                                           || now <= two_factor_info.pending_expires_at);
        const long long pending_expires_in = has_pending_setup && two_factor_info.pending_expires_at > 0
                                             ? std::max(0LL, two_factor_info.pending_expires_at - now)
                                             : 0LL;

        crow::json::wvalue res;
        res["status"] = "success";
        res["two_factor_enabled"] = two_factor_info.enabled && !two_factor_info.secret.empty();
        res["has_pending_setup"] = has_pending_setup;
        res["pending_expires_in"] = pending_expires_in;
        return crow::response(res);
    });

    CROW_ROUTE(app, "/2fa/setup/init").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        const auto user_id = getAuthorizedUserId(req);
        if (!user_id) {
            return crow::response(401, "Unauthorized");
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "Database connection failed");
        }

        UserTwoFactorInfo two_factor_info;
        if (!loadUserTwoFactorInfo(connection.get(), *user_id, &two_factor_info) || !two_factor_info.found) {
            return crow::response(404, "User not found");
        }
        if (two_factor_info.enabled) {
            return crow::response(409, "2FA is already enabled");
        }

        // setup/init은 secret을 발급만 하고 confirm 전까지 pending 상태로 유지한다.
        const std::string secret = generateRandomBase32Secret();
        if (secret.empty()) {
            return crow::response(500, "Failed to generate TOTP secret");
        }

        const long long expires_at = static_cast<long long>(std::time(nullptr)) + kPendingTotpTtlSeconds;
        if (!savePendingTotpSecret(connection.get(), *user_id, secret, expires_at)) {
            return crow::response(500, "Failed to save pending TOTP secret");
        }

        crow::json::wvalue res;
        res["status"] = "pending";
        res["manual_key"] = secret;
        res["otpauth_url"] = buildOtpAuthUrl(kOtpIssuer, *user_id, secret);
        res["expires_in"] = kPendingTotpTtlSeconds;
        return crow::response(res);
    });

    CROW_ROUTE(app, "/2fa/setup/confirm").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        const auto user_id = getAuthorizedUserId(req);
        if (!user_id) {
            return crow::response(401, "Unauthorized");
        }

        auto x = crow::json::load(req.body);
        if (!x || !x.has("otp")) {
            return crow::response(400, "Missing otp");
        }

        const std::string otp = x["otp"].s();
        if (const auto otp_error = validateOtpCode(otp)) {
            return crow::response(400, *otp_error);
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "Database connection failed");
        }

        UserTwoFactorInfo two_factor_info;
        if (!loadUserTwoFactorInfo(connection.get(), *user_id, &two_factor_info) || !two_factor_info.found) {
            return crow::response(404, "User not found");
        }
        if (two_factor_info.pending_secret.empty()) {
            return crow::response(400, "No pending 2FA setup found");
        }

        const long long now = static_cast<long long>(std::time(nullptr));
        if (two_factor_info.pending_expires_at > 0 && now > two_factor_info.pending_expires_at) {
            return crow::response(410, "Pending 2FA setup expired");
        }

        long long matched_step = -1;
        // 첫 OTP가 맞아야만 pending secret을 실제 secret으로 활성화한다.
        if (!verifyTotpCode(two_factor_info.pending_secret, otp, -1, &matched_step)) {
            return crow::response(401, "Invalid OTP");
        }

        if (!activatePendingTotpSecret(connection.get(), *user_id, matched_step)) {
            return crow::response(500, "Failed to activate 2FA");
        }

        crow::json::wvalue res;
        res["status"] = "success";
        res["two_factor_enabled"] = true;
        return crow::response(res);
    });

    CROW_ROUTE(app, "/2fa/verify").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        auto x = crow::json::load(req.body);
        if (!x || !x.has("pre_auth_token") || !x.has("otp")) {
            return crow::response(400, "Missing pre_auth_token or otp");
        }

        const std::string pre_auth_token = x["pre_auth_token"].s();
        const std::string otp = x["otp"].s();
        if (const auto otp_error = validateOtpCode(otp)) {
            return crow::response(400, *otp_error);
        }

        std::string user_id;
        // pre_auth_token은 비밀번호 1차 인증까지만 통과한 임시 토큰이다.
        if (!verifyPreAuthJWT(pre_auth_token, &user_id)) {
            return crow::response(401, "Invalid pre-auth token");
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "Database connection failed");
        }

        UserTwoFactorInfo two_factor_info;
        if (!loadUserTwoFactorInfo(connection.get(), user_id, &two_factor_info) || !two_factor_info.found) {
            return crow::response(404, "User not found");
        }
        if (!two_factor_info.enabled || two_factor_info.secret.empty()) {
            return crow::response(400, "2FA is not enabled");
        }

        long long matched_step = -1;
        if (!verifyTotpCode(two_factor_info.secret, otp, two_factor_info.last_used_step, &matched_step)) {
            return crow::response(401, "Invalid OTP");
        }
        if (!updateLastUsedTotpStep(connection.get(), user_id, matched_step)) {
            return crow::response(500, "Failed to update 2FA state");
        }

        // 최종 JWT는 OTP까지 통과한 뒤에만 발급한다.
        crow::json::wvalue res;
        res["status"] = "success";
        res["requires_2fa"] = false;
        res["token"] = generateJWT(user_id);
        return crow::response(res);
    });

    CROW_ROUTE(app, "/2fa/disable").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        const auto user_id = getAuthorizedUserId(req);
        if (!user_id) {
            return crow::response(401, "Unauthorized");
        }

        auto x = crow::json::load(req.body);
        if (!x || !x.has("otp")) {
            return crow::response(400, "Missing otp");
        }

        const std::string otp = x["otp"].s();
        if (const auto otp_error = validateOtpCode(otp)) {
            return crow::response(400, *otp_error);
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "Database connection failed");
        }

        UserTwoFactorInfo two_factor_info;
        if (!loadUserTwoFactorInfo(connection.get(), *user_id, &two_factor_info) || !two_factor_info.found) {
            return crow::response(404, "User not found");
        }
        if (!two_factor_info.enabled || two_factor_info.secret.empty()) {
            return crow::response(400, "2FA is already disabled");
        }

        long long matched_step = -1;
        // 비활성화도 현재 OTP를 한 번 더 확인한 뒤 진행한다.
        if (!verifyTotpCode(two_factor_info.secret, otp, two_factor_info.last_used_step, &matched_step)) {
            return crow::response(401, "Invalid OTP");
        }
        if (!disableUserTwoFactor(connection.get(), *user_id)) {
            return crow::response(500, "Failed to disable 2FA");
        }

        crow::json::wvalue res;
        res["status"] = "success";
        res["two_factor_enabled"] = false;
        return crow::response(res);
    });

    CROW_ROUTE(app, "/account/delete").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        // Account deletion is protected by the normal full JWT and then one more
        // local proof step. Even if a session token is stolen, the attacker still
        // needs the current password and, when 2FA is enabled, the live OTP code.
        const auto user_id = getAuthorizedUserId(req);
        if (!user_id) {
            return crow::response(401, "Unauthorized");
        }

        auto x = crow::json::load(req.body);
        if (!x || !x.has("password")) {
            return crow::response(400, "Missing password");
        }

        const std::string password = x["password"].s();
        if (password.empty()) {
            return crow::response(400, "Password is required");
        }
        if (password.size() > kMaxPasswordLength) {
            return crow::response(400, "Password is too long");
        }

        const bool has_otp_field = x.has("otp");
        const std::string otp = has_otp_field ? std::string(x["otp"].s()) : std::string{};

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "Database connection failed");
        }

        // First confirm that the caller still knows the current password for the
        // authenticated account. This prevents "click once and delete" behavior
        // from an already-open session.
        if (!verifyUserPassword(connection.get(), *user_id, password)) {
            return crow::response(401, "Invalid password");
        }

        UserTwoFactorInfo two_factor_info;
        if (!loadUserTwoFactorInfo(connection.get(), *user_id, &two_factor_info) || !two_factor_info.found) {
            return crow::response(404, "User not found");
        }

        const bool requires_otp = two_factor_info.enabled && !two_factor_info.secret.empty();
        if (requires_otp) {
            if (!has_otp_field) {
                return crow::response(400, "Missing otp");
            }

            if (const auto otp_error = validateOtpCode(otp)) {
                return crow::response(400, *otp_error);
            }

            long long matched_step = -1;
            // When 2FA is enabled we require a fresh OTP from the authenticator app
            // before deleting the account. The same replay protection rule used by
            // login/disable is reused here through last_used_step.
            if (!verifyTotpCode(two_factor_info.secret, otp, two_factor_info.last_used_step, &matched_step)) {
                return crow::response(401, "Invalid OTP");
            }
        }

        // At this point the requester passed all configured identity checks, so
        // the actual row can be removed from the users table.
        if (!deleteUserAccount(connection.get(), *user_id)) {
            return crow::response(500, "Failed to delete user");
        }

        crow::json::wvalue res;
        res["status"] = "success";
        res["deleted"] = true;
        return crow::response(res);
    });

    // ==========================================
    // 로그인 계정 비밀번호 변경 API
    // ==========================================
    CROW_ROUTE(app, "/account/password/change").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        // 로그인 완료(JWT full stage) 사용자만 비밀번호 변경 허용
        const auto user_id = getAuthorizedUserId(req);
        if (!user_id) {
            return crow::response(401, "Unauthorized");
        }

        auto x = crow::json::load(req.body);
        if (!x || !x.has("current_password") || !x.has("new_password")) {
            return crow::response(400, "current_password 또는 new_password 값이 없습니다.");
        }

        const std::string current_password = x["current_password"].s();
        const std::string new_password = x["new_password"].s();
        if (current_password.empty()) {
            return crow::response(400, "현재 비밀번호를 입력해 주세요.");
        }
        if (new_password.empty()) {
            return crow::response(400, "새 비밀번호를 입력해 주세요.");
        }
        if (current_password.size() > kMaxPasswordLength || new_password.size() > kMaxPasswordLength) {
            return crow::response(400, "비밀번호 길이가 너무 깁니다.");
        }
        if (current_password == new_password) {
            return crow::response(400, "새 비밀번호는 현재 비밀번호와 달라야 합니다.");
        }

        // 신규 비밀번호는 회원가입과 동일한 복잡도 정책 적용
        if (const auto complexity_error = validatePasswordComplexity(new_password)) {
            return crow::response(400, *complexity_error);
        }

        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "Database connection failed");
        }

        if (!verifyUserPassword(connection.get(), *user_id, current_password)) {
            return crow::response(401, "현재 비밀번호가 올바르지 않습니다.");
        }

        const std::string new_password_hash = hashPassword(new_password);
        if (new_password_hash.empty()) {
            return crow::response(500, "비밀번호 해시 생성에 실패했습니다.");
        }
        if (!updateStoredPasswordHash(connection.get(), *user_id, new_password_hash)) {
            return crow::response(500, "비밀번호 변경에 실패했습니다.");
        }

        std::cout << "[AUTH] Password changed user_id=" << *user_id << std::endl;

        crow::json::wvalue res;
        res["status"] = "success";
        res["changed"] = true;
        return crow::response(res);
    });

    CROW_ROUTE(app, "/login").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "잘못된 JSON 형식입니다.");

        if (!x.has("id") || !x.has("password")) {
            return crow::response(400, "id 또는 password 값이 없습니다.");
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
        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "데이터베이스 연결에 실패했습니다.");
        }

        if (!verifyUserPassword(connection.get(), id, pw)) {
            return crow::response(401, "아이디 또는 비밀번호가 올바르지 않습니다.");
        }

        UserEmailVerificationState email_state;
        // 이메일이 등록된 계정은 인증 완료 전 로그인 차단, 이메일 미등록(레거시) 계정은 허용
        if (!loadUserEmailVerificationState(connection.get(), id, &email_state)) {
            return crow::response(500, "이메일 인증 상태를 불러오지 못했습니다.");
        }
        if (!email_state.found) {
            return crow::response(401, "아이디 또는 비밀번호가 올바르지 않습니다.");
        }
        if (email_state.has_email && !email_state.is_email_verified) {
            return crow::response(403, "이메일 인증이 완료되지 않았습니다. 이메일 인증 후 로그인해 주세요.");
        }

        UserTwoFactorInfo two_factor_info;
        if (!loadUserTwoFactorInfo(connection.get(), id, &two_factor_info) || !two_factor_info.found) {
            return crow::response(500, "2FA 상태 정보를 불러오지 못했습니다.");
        }

        crow::json::wvalue res;
        if (!two_factor_info.enabled) {
            // 2FA가 꺼져 있으면 기존과 동일하게 바로 access token을 내려준다.
            res["status"] = "success";
            res["requires_2fa"] = false;
            res["token"] = generateJWT(id);
            return crow::response(res);
        }

        // 2FA 사용자는 access token 대신 짧게 사는 pre_auth_token으로 OTP 단계로 넘긴다.
        res["status"] = "2fa_required";
        res["requires_2fa"] = true;
        res["pre_auth_token"] = generatePreAuthJWT(id);
        res["expires_in"] = kPreAuthTokenTtlSeconds;
        return crow::response(res);
    });

    // ==========================================
    // 토큰 검증 헬퍼 (캡처를 위해 람다로 정의)
    // ==========================================
    auto is_authorized = [](const crow::request& req) {
        const auto token = extractBearerToken(req);
        // 기존 보호 API는 full stage JWT만 허용한다.
        return token && verifyJWT(*token);
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
    shutdownThermalProxy();
    shutdownCctvProxyWorker();
}
