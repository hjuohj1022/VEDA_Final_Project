#include "crow.h"
#include "features/auth/AuthRecoveryRoutes.h"
#include "features/cctv/CctvManager.h"
#include "features/cctv/CctvProxy.h"
#include "features/esp/EspHealthManager.h"
#include "features/event_log/EventLogRoutes.h"
#include "features/media/RecordingRoutes.h"
#include "features/motor/MotorManager.h"
#include "features/sunapi/SunapiProxy.h"
#include "features/sunapi/SunapiWsProxy.h"
#include "features/system/SystemRoutes.h"
#include "features/thermal/ThermalProxy.h"
#include "infra/mqtt/MqttManager.h"
#include <jwt-cpp/jwt.h> 
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cstdlib> 
#include <iomanip>
#include <iostream>
#include <memory>
#include <mysql/mysql.h>
#include <optional>
#include <sstream> 
#include <algorithm>
#include <cctype>
#include <chrono>
#include <type_traits>
#include <utility>
#include <vector>

// crow_server의 메인 엔트리 파일이다.
// 인증, 계정 관리, 2단계 인증, 외부 서비스 초기화처럼 여러 기능을 하나의 앱으로 조립하며,
// 세부 네트워크 처리나 개별 기능 구현은 각 기능 모듈에 위임하고 여기서는 공통 보안 흐름과 부트스트랩에 집중한다.

std::string getJwtSecret();

namespace {
bool verifyTokenStage(const std::string& token,
                      const std::string& expected_stage,
                      std::string* user_id_out);
// 인증, DB, 파일 스트리밍 관련 보조 함수를 라우트 바깥에 모아 둔다.
// 이렇게 하면 main()은 서비스 등록 흐름과 상위 초기화 순서에 더 집중할 수 있다.
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
// TOTP 기반 2단계 인증은 인증 앱과 서버가 동일한 규칙을 공유해야 하므로 관련 값을 상수로 고정한다.
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

// users 테이블의 2단계 인증 관련 상태를 한 번에 읽어 라우트 분기에서 재사용한다.
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

// OTP는 실제 검증 로직에 들어가기 전에 숫자 6자리 형식인지 먼저 확인한다.
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

// 인증 앱과의 호환을 위해 비밀키는 Base32 문자열 형태로 저장하고 전달한다.
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

// RFC 6238 방식으로 현재 시간 구간에 대응하는 6자리 OTP를 계산한다.
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

    // 시간 오차는 조금 허용하되, 이미 사용한 구간 이하의 OTP는 다시 쓰지 못하게 막는다.
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

// 로그인과 /2fa 라우트에서 공통으로 쓰는 2단계 인증 상태 조회를 준비된 질의문으로 묶는다.
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

// setup/init 단계에서는 아직 활성화하지 않고 임시 비밀키 상태로만 저장한다.
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

// 첫 OTP 검증이 끝난 시점에만 임시 비밀키를 실제 로그인용 비밀키로 승격한다.
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

// 같은 30초 구간의 OTP 재사용을 막기 위해 마지막으로 승인한 시간 구간을 기록한다.
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

// 2단계 인증 비활성화 시에는 활성/대기 상태를 모두 지워 다음 설정을 깨끗하게 시작한다.
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

// 계정 삭제 자체는 별도 헬퍼로 분리해 둔다.
// 라우트 본문은 "인증된 사용자 확인 -> 비밀번호 재입력 확인 -> 필요 시 OTP 검증 -> 실제 삭제"라는
// 보안 흐름에만 집중하고, 실제 삭제 질의문의 세부 구현은 이 함수가 맡는다.
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

// 계정 삭제가 지원되는 시점부터는 "서명된 JWT"만으로는 충분하지 않다.
// 보호 라우트는 토큰 검증 외에도 실제 사용자 행이 아직 남아 있는지 다시 확인해야 하며,
// 그렇지 않으면 삭제 전에 발급된 예전 토큰이 만료 시점까지 계속 살아 있을 수 있다.
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

// 정식 접근 토큰과 2차 인증 전 임시 토큰을 같은 형식으로 발급하고, 클레임 값으로만 역할을 구분한다.
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
            // 구형 토큰과의 호환을 위해 auth_stage가 없으면 정식 접근 토큰으로만 간주한다.
            return expected_stage == kJwtStageFull;
        }
    } catch (const std::exception& e) {
        std::cerr << "[JWT Verify Error] " << e.what() << std::endl;
        return false;
    }
}

// 보호 라우트는 정식 단계 JWT만 통과시키고 pre_auth 토큰은 막는다.
std::optional<std::string> getAuthorizedUserId(const crow::request& req) {
    const auto token = extractBearerToken(req);
    if (!token) {
        return std::nullopt;
    }

    std::string user_id;
    // 보호 라우트에서는 "정식 권한 JWT"이면서, 그 토큰의 소유자 계정도 실제로 존재해야 한다.
    // 이렇게 해야 삭제된 계정이 예전 토큰만으로 만료 시점까지 계속 API를 호출하는 틈을 막을 수 있다.
    if (!loadExistingFullJwtUserId(*token, &user_id)) {
        return std::nullopt;
    }

    return user_id;
}
}  // 익명 네임스페이스

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
// MariaDB에서 아이디/비밀번호를 확인하는 구형 호환용 함수다.
// -------------------------------------------------------
[[maybe_unused]] bool checkUserFromDBLegacy(std::string inputId, std::string inputPw) {
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;
    
    // 환경 변수에서 접속 정보를 가져온다.
    const char* db_host = std::getenv("DB_HOST");     // mariadb-service
    const char* db_user = std::getenv("DB_USER");     // veda_user
    const char* db_pass = std::getenv("DB_PASSWORD"); // secret에서 가져옴
    const char* db_name = "veda_db";                  // DB 이름 (YAML에 설정 필요, 없으면 기본값)

    // 필수 환경 변수가 없으면 즉시 실패 처리한다.
    if(!db_host || !db_user || !db_pass) {
        std::cerr << "[Error] DB Environment variables are missing!" << std::endl;
        return false;
    }

    conn = mysql_init(NULL);

    // 데이터베이스 연결을 시도한다.
    if (!mysql_real_connect(conn, db_host, db_user, db_pass, "veda_db", 3306, NULL, 0)) {
        std::cerr << "[DB Error] " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return false;
    }

    // 쿼리를 직접 문자열로 조립해 실행하는 단순 구현이다.
    // 실제 운영 코드에서는 준비된 질의문을 사용해 SQL 삽입 공격을 반드시 막아야 한다.
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
        // count(*)가 1이면 로그인 성공으로 판단한다.
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
    // 신규 계정은 준비된 질의문과 PBKDF2 해시 형태로 저장한다.
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
    // mosquitto 전역 초기화는 앱 수명 동안 한 번만 유지한다.
    MqttLibraryGuard mqtt_library_guard;
    crow::SimpleApp app;

    // SUNAPI 프록시 라우트를 등록한다. (/sunapi/stw-cgi/*)
    registerSunapiProxyRoutes(app);
    // SUNAPI StreamingServer 웹소켓 프록시를 등록한다. (/sunapi/StreamingServer)
    registerSunapiWsProxyRoutes(app);

    // ==========================================
    // CCTV 매니저를 초기화하고 관련 라우트를 등록한다.
    // ==========================================
    const char* cctv_host = std::getenv("CCTV_BACKEND_HOST") ? std::getenv("CCTV_BACKEND_HOST") : "127.0.0.1";
    int cctv_port = std::getenv("CCTV_BACKEND_PORT") ? std::atoi(std::getenv("CCTV_BACKEND_PORT")) : 9090;
    
    // CCTV 백엔드 접속에 쓸 인증서 경로의 기본값을 잡는다.
    std::string cert_dir = "/app/certs";
    CctvManager cctv_mgr(
        cctv_host, cctv_port,
        cert_dir + "/rootCA.crt",
        cert_dir + "/cctv.crt", 
        cert_dir + "/cctv.key"
    );
    
    // CCTV 관련 라우트를 등록한다.
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
    const char* laser_control_topic = std::getenv("LASER_CONTROL_TOPIC") ? std::getenv("LASER_CONTROL_TOPIC") : "laser/control";
    registerLaserRoutes(app, motor_mgr, laser_control_topic);

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

        // setup/init은 비밀키를 발급만 하고 confirm 전까지 임시 상태로 유지한다.
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
        // 첫 OTP가 맞아야만 임시 비밀키를 실제 비밀키로 활성화한다.
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
        // 계정 삭제는 일반 보호 API보다 한 단계 더 강한 확인 절차를 둔다.
        // 세션 토큰이 탈취되더라도 현재 비밀번호를 다시 알아야 하고,
        // 2FA가 켜져 있으면 실시간 OTP까지 있어야 삭제가 가능하도록 만든다.
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

        // 먼저 로그인된 사용자가 현재 비밀번호를 실제로 다시 알고 있는지 확인한다.
        // 이렇게 해야 이미 열린 세션만으로 단번에 계정을 지워 버리는 위험을 줄일 수 있다.
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
            // 2FA가 활성화된 계정은 삭제 직전에도 인증 앱의 최신 OTP를 다시 요구한다.
            // 또한 last_used_step을 재사용해 로그인/비활성화와 동일한 재사용 방지 규칙을 적용한다.
            if (!verifyTotpCode(two_factor_info.secret, otp, two_factor_info.last_used_step, &matched_step)) {
                return crow::response(401, "Invalid OTP");
            }
        }

        // 여기까지 통과했다면 요청자는 구성된 모든 신원 확인 절차를 마친 상태이므로,
        // 이제 users 테이블의 실제 계정 행을 삭제해도 된다.
        if (!deleteUserAccount(connection.get(), *user_id)) {
            return crow::response(500, "Failed to delete user");
        }

        crow::json::wvalue res;
        res["status"] = "success";
        res["deleted"] = true;
        return crow::response(res);
    });

    // ==========================================
    // 로그인한 계정의 비밀번호 변경 API
    // ==========================================
    CROW_ROUTE(app, "/account/password/change").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        // 로그인 완료 상태의 JWT를 가진 사용자만 비밀번호 변경을 허용한다.
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

        // 새 비밀번호에는 회원가입과 동일한 복잡도 정책을 적용한다.
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

        // mTLS 정보는 운영 로그 확인용으로만 읽는다.
        std::string device_id = req.get_header_value("X-Device-ID");
        if (!device_id.empty()) std::cout << "[mTLS Device] " << device_id << std::endl;

        // 사용자 확인이 끝난 뒤 토큰을 생성한다.
        auto connection = openDatabaseConnection();
        if (!connection) {
            return crow::response(500, "데이터베이스 연결에 실패했습니다.");
        }

        if (!verifyUserPassword(connection.get(), id, pw)) {
            return crow::response(401, "아이디 또는 비밀번호가 올바르지 않습니다.");
        }

        UserEmailVerificationState email_state;
        // 이메일이 등록된 계정은 인증 완료 전 로그인을 막고, 이메일이 없는 레거시 계정은 허용한다.
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
            // 2FA가 꺼져 있으면 기존과 동일하게 바로 접근 토큰을 내려준다.
            res["status"] = "success";
            res["requires_2fa"] = false;
            res["token"] = generateJWT(id);
            return crow::response(res);
        }

        // 2FA 사용자는 접근 토큰 대신 수명이 짧은 pre_auth_token으로 OTP 단계로 넘긴다.
        res["status"] = "2fa_required";
        res["requires_2fa"] = true;
        res["pre_auth_token"] = generatePreAuthJWT(id);
        res["expires_in"] = kPreAuthTokenTtlSeconds;
        return crow::response(res);
    });

    // ==========================================
    // 토큰 검증 보조 함수를 람다로 정의해 하위 라우트 등록에 재사용한다.
    // ==========================================
    auto is_authorized = [](const crow::request& req) {
        const auto token = extractBearerToken(req);
        // 기존 보호 API는 정식 단계 JWT만 허용한다.
        return token && verifyJWT(*token);
    };

    registerEventLogRoutes(app, is_authorized);
    registerRecordingRoutes(app, is_authorized);
    registerSystemRoutes(app, is_authorized);
        
    app.port(8080).multithreaded().run();
    shutdownThermalProxy();
    shutdownCctvProxyWorker();
}
