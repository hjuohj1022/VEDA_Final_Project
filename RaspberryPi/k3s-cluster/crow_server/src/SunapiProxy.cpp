#include "../include/SunapiProxy.h"

#include <curl/curl.h>
#include <boost/asio.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <random>
#include <sstream>
#include <string>

// main.cpp에 정의된 JWT 검증 함수
extern bool verifyJWT(const std::string& token);

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

std::string envOrDefault(const char* key, const std::string& defaultValue) {
    const char* value = std::getenv(key);
    return value ? std::string(value) : defaultValue;
}

bool envToBool(const char* key, bool defaultValue) {
    const char* value = std::getenv(key);
    if (!value) return defaultValue;
    std::string s(value);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (s == "1" || s == "true" || s == "yes" || s == "on");
}

int envToInt(const char* key, int defaultValue) {
    const char* value = std::getenv(key);
    if (!value) return defaultValue;
    try {
        return std::stoi(value);
    } catch (...) {
        return defaultValue;
    }
}

std::string trimTrailingSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string extractQuery(const std::string& rawUrl) {
    const std::size_t pos = rawUrl.find('?');
    if (pos == std::string::npos) return {};
    return rawUrl.substr(pos);
}

std::string urlEncode(const std::string& input) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() * 3);
    for (unsigned char c : input) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::string makeQuery(const std::initializer_list<std::pair<std::string, std::string>>& items) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& kv : items) {
        if (!first) oss << "&";
        first = false;
        oss << urlEncode(kv.first) << "=" << urlEncode(kv.second);
    }
    return oss.str();
}

std::string normalizeWsPath(std::string path) {
    if (path.empty()) {
        path = "/StreamingServer";
    }
    if (!path.empty() && path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    return path;
}

std::string resolveRtspHost() {
    std::string rtspHost = envOrDefault("SUNAPI_RTSP_HOST", "");
    if (!rtspHost.empty()) {
        return rtspHost;
    }

    const std::string base = trimTrailingSlash(envOrDefault("SUNAPI_BASE_URL", ""));
    static const std::regex baseRe("^(https?)://([^/:]+)(?::([0-9]+))?$", std::regex_constants::icase);
    std::smatch m;
    if (std::regex_match(base, m, baseRe) && m.size() > 2) {
        return m[2].str();
    }
    return {};
}

bool toCompactDateTime(const std::string& dateTimeText, std::string* compact) {
    static const std::regex dtRe("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$");
    if (!std::regex_match(dateTimeText, dtRe)) {
        return false;
    }
    std::string out;
    out.reserve(14);
    for (char c : dateTimeText) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            out.push_back(c);
        }
    }
    if (out.size() != 14) {
        return false;
    }
    if (compact) {
        *compact = out;
    }
    return true;
}

std::string randomHexString(std::size_t length) {
    static thread_local std::mt19937 rng(std::random_device{}());
    static const char* hex = "0123456789ABCDEF";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        out.push_back(hex[dist(rng)]);
    }
    return out;
}

bool isLeapYear(int y) {
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

int daysInMonth(int y, int m) {
    static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12) return 30;
    if (m == 2 && isLeapYear(y)) return 29;
    return d[m - 1];
}

size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t total = size * nmemb;
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, total);
    return total;
}

size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    const size_t total = size * nitems;
    std::string line(buffer, total);
    auto* contentType = static_cast<std::string*>(userdata);
    const std::string prefix = "Content-Type:";
    if (line.size() > prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), line.begin(),
                   [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
        std::string value = line.substr(prefix.size());
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) value.pop_back();
        *contentType = value;
    }
    return total;
}

bool isAuthorized(const crow::request& req) {
    const std::string auth = req.get_header_value("Authorization");
    if (!(auth.size() > 7 && auth.rfind("Bearer ", 0) == 0)) return false;
    return verifyJWT(auth.substr(7));
}

bool fetchRtspDigestChallenge(const std::string& host,
                              int port,
                              const std::string& uri,
                              std::string* realm,
                              std::string* nonce,
                              std::string* error) {
    try {
        asio::io_context io;
        tcp::resolver resolver(io);
        tcp::socket socket(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        asio::connect(socket, endpoints);

        std::string req;
        req += "OPTIONS " + uri + " RTSP/1.0\r\n";
        req += "CSeq: 1\r\n";
        req += "User-Agent: Crow-SUNAPI\r\n";
        req += "\r\n";
        asio::write(socket, asio::buffer(req));

        std::string resp;
        boost::system::error_code ec;
        char buf[4096];
        for (;;) {
            std::size_t n = socket.read_some(asio::buffer(buf), ec);
            if (n > 0) {
                resp.append(buf, n);
            }
            if (ec == asio::error::eof) break;
            if (ec) break;
            if (resp.find("\r\n\r\n") != std::string::npos) break;
            if (resp.size() > 32768) break;
        }

        static const std::regex realmRe("realm\\s*=\\s*\"([^\"]+)\"", std::regex_constants::icase);
        static const std::regex nonceRe("nonce\\s*=\\s*\"([^\"]+)\"", std::regex_constants::icase);
        std::smatch m;
        std::string localRealm;
        std::string localNonce;
        if (std::regex_search(resp, m, realmRe) && m.size() > 1) {
            localRealm = m[1].str();
        }
        if (std::regex_search(resp, m, nonceRe) && m.size() > 1) {
            localNonce = m[1].str();
        }

        if (localRealm.empty() || localNonce.empty()) {
            if (error) *error = "realm/nonce parse failed";
            return false;
        }
        if (realm) *realm = localRealm;
        if (nonce) *nonce = localNonce;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

crow::response forwardToSunapi(const crow::request& req, const std::string& forwardPathWithQuery) {
    if (!isAuthorized(req)) {
        return crow::response(401, "Unauthorized");
    }

    const std::string baseUrl = trimTrailingSlash(envOrDefault("SUNAPI_BASE_URL", ""));
    const std::string username = envOrDefault("SUNAPI_USER", "");
    const std::string password = envOrDefault("SUNAPI_PASSWORD", "");
    const bool insecure = envToBool("SUNAPI_INSECURE", true);
    const long timeoutMs = static_cast<long>(envToInt("SUNAPI_TIMEOUT_MS", 12000));

    if (baseUrl.empty()) {
        return crow::response(500, "SUNAPI_BASE_URL is not configured");
    }

    if (username.empty() || password.empty()) {
        return crow::response(500, "SUNAPI credentials are not configured");
    }

    const std::string fullUrl = baseUrl + forwardPathWithQuery;

    CURL* curl = curl_easy_init();
    if (!curl) {
        return crow::response(500, "curl init failed");
    }

    std::string responseBody;
    std::string responseContentType;
    long httpCode = 502;

    curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseContentType);

    if (insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    const auto method = req.method;
    if (method == crow::HTTPMethod::Post) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    } else if (method == crow::HTTPMethod::Put) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    } else if (method == crow::HTTPMethod::Delete) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (method == crow::HTTPMethod::Patch) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    } else if (method == crow::HTTPMethod::Options) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
    }

    struct curl_slist* headers = nullptr;
    const std::string reqContentType = req.get_header_value("Content-Type");
    if (!reqContentType.empty()) {
        headers = curl_slist_append(headers, ("Content-Type: " + reqContentType).c_str());
    }
    const std::string reqAccept = req.get_header_value("Accept");
    if (!reqAccept.empty()) {
        headers = curl_slist_append(headers, ("Accept: " + reqAccept).c_str());
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    const CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        std::cerr << "[SUNAPI_PROXY] curl failed: " << curl_easy_strerror(rc) << ", url=" << fullUrl << std::endl;
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return crow::response(502, std::string("SUNAPI proxy failed: ") + curl_easy_strerror(rc));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    crow::response res(static_cast<int>(httpCode), responseBody);
    if (!responseContentType.empty()) {
        res.set_header("Content-Type", responseContentType);
    }
    return res;
}

bool requestDigestResponse(const crow::request& req,
                           const std::string& method,
                           const std::string& realm,
                           const std::string& nonce,
                           const std::string& uri,
                           const std::string& nc,
                           const std::string& cnonce,
                           std::string* digestResponse,
                           std::string* usernameOut,
                           std::string* error,
                           int* errorCode) {
    const std::string username = envOrDefault("SUNAPI_USER", "");
    if (username.empty()) {
        if (error) *error = "SUNAPI_USER is not configured";
        if (errorCode) *errorCode = 500;
        return false;
    }

    const std::string q = makeQuery({
        {"msubmenu", "digestauth"},
        {"action", "view"},
        {"Method", method},
        {"Realm", realm},
        {"Nonce", nonce},
        {"Uri", uri},
        {"username", username},
        {"password", ""},
        {"Nc", nc},
        {"Cnonce", cnonce}
    });

    crow::response forwarded = forwardToSunapi(req, "/stw-cgi/security.cgi?" + q);
    if (forwarded.code < 200 || forwarded.code >= 300) {
        if (errorCode) *errorCode = forwarded.code;
        if (error) {
            *error = forwarded.body.empty() ? "digestauth forward failed" : forwarded.body;
        }
        return false;
    }

    auto parsed = crow::json::load(forwarded.body);
    if (!parsed || !parsed.has("Response")) {
        if (errorCode) *errorCode = 502;
        if (error) *error = "digest response parse failed";
        return false;
    }

    const std::string response = parsed["Response"].s();
    if (response.empty()) {
        if (errorCode) *errorCode = 502;
        if (error) *error = "digest response is empty";
        return false;
    }

    if (digestResponse) *digestResponse = response;
    if (usernameOut) *usernameOut = username;
    return true;
}

std::string buildExportForwardPath(const crow::request& req,
                                   const std::string& phase,
                                   std::string* error) {
    const char* ch = req.url_params.get("channel");
    const char* startTime = req.url_params.get("start_time");
    const char* endTime = req.url_params.get("end_time");
    const char* format = req.url_params.get("format");
    const char* jobId = req.url_params.get("job_id");
    const char* mode = req.url_params.get("mode");

    const std::string modeText = mode ? std::string(mode) : std::string("default");
    std::string cgi;
    std::string submenu;
    std::string action;
    std::string extraQuery;

    if (phase == "create") {
        if (!ch || !startTime || !endTime) {
            if (error) *error = "missing query: channel, start_time, end_time";
            return {};
        }
        if (modeText == "backup") {
            cgi = "recording.cgi";
            submenu = "backup";
            action = "create";
        } else if (modeText == "start") {
            cgi = "recording.cgi";
            submenu = "export";
            action = "start";
        } else {
            cgi = envOrDefault("SUNAPI_EXPORT_CREATE_CGI", "recording.cgi");
            submenu = envOrDefault("SUNAPI_EXPORT_CREATE_SUBMENU", "export");
            action = envOrDefault("SUNAPI_EXPORT_CREATE_ACTION", "create");
            extraQuery = envOrDefault("SUNAPI_EXPORT_CREATE_QUERY", "");
        }

        const std::string fmt = (format && *format) ? std::string(format) : std::string("AVI");
        std::string q = makeQuery({
            {"msubmenu", submenu},
            {"action", action},
            {"Channel", ch},
            {"ChannelIDList", ch},
            {"StartTime", startTime},
            {"EndTime", endTime},
            {"FromDate", startTime},
            {"ToDate", endTime},
            {"Type", fmt},
            {"FileType", fmt}
        });
        if (!extraQuery.empty()) {
            q += "&" + extraQuery;
        }
        return "/stw-cgi/" + cgi + "?" + q;
    }

    if (!jobId || std::string(jobId).empty()) {
        if (error) *error = "missing query: job_id";
        return {};
    }

    if (phase == "status") {
        if (modeText == "backup") {
            cgi = "recording.cgi";
            submenu = "backup";
            action = "status";
        } else if (modeText == "start") {
            cgi = "recording.cgi";
            submenu = "export";
            action = "status";
        } else {
            cgi = envOrDefault("SUNAPI_EXPORT_POLL_CGI", "recording.cgi");
            submenu = envOrDefault("SUNAPI_EXPORT_POLL_SUBMENU", "export");
            action = envOrDefault("SUNAPI_EXPORT_POLL_ACTION", "status");
            extraQuery = envOrDefault("SUNAPI_EXPORT_POLL_QUERY", "");
        }
    } else if (phase == "download") {
        if (modeText == "backup") {
            cgi = "recording.cgi";
            submenu = "backup";
            action = "download";
        } else if (modeText == "start") {
            cgi = "recording.cgi";
            submenu = "export";
            action = "download";
        } else {
            cgi = envOrDefault("SUNAPI_EXPORT_DOWNLOAD_CGI", "recording.cgi");
            submenu = envOrDefault("SUNAPI_EXPORT_DOWNLOAD_SUBMENU", "export");
            action = envOrDefault("SUNAPI_EXPORT_DOWNLOAD_ACTION", "download");
            extraQuery = envOrDefault("SUNAPI_EXPORT_DOWNLOAD_QUERY", "");
        }
    } else {
        if (error) *error = "invalid export phase";
        return {};
    }

    std::string q = makeQuery({
        {"msubmenu", submenu},
        {"action", action},
        {"JobID", jobId},
        {"ExportID", jobId}
    });
    if (!extraQuery.empty()) {
        q += "&" + extraQuery;
    }
    return "/stw-cgi/" + cgi + "?" + q;
}

} // namespace

void registerSunapiProxyRoutes(crow::SimpleApp& app) {
    static const bool curlReady = []() {
        return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }();
    if (!curlReady) {
        std::cerr << "[SUNAPI_PROXY] curl_global_init failed" << std::endl;
        return;
    }

    // /sunapi/stw-cgi/... 형태로 들어온 요청을 실제 카메라 SUNAPI로 전달
    CROW_ROUTE(app, "/sunapi/stw-cgi/<path>")
        .methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post, crow::HTTPMethod::Put,
                 crow::HTTPMethod::Delete, crow::HTTPMethod::Patch, crow::HTTPMethod::Options)
    ([](const crow::request& req, const std::string& tailPath) {
        const std::string query = extractQuery(req.raw_url);
        const std::string forwardPath = "/stw-cgi/" + tailPath + query;
        return forwardToSunapi(req, forwardPath);
    });

    CROW_ROUTE(app, "/sunapi/stw-cgi")
        .methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post, crow::HTTPMethod::Put,
                 crow::HTTPMethod::Delete, crow::HTTPMethod::Patch, crow::HTTPMethod::Options)
    ([](const crow::request& req) {
        const std::string query = extractQuery(req.raw_url);
        const std::string forwardPath = "/stw-cgi" + query;
        return forwardToSunapi(req, forwardPath);
    });

    // Crow 고정 스펙: 저장소 조회
    CROW_ROUTE(app, "/api/sunapi/storage")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        const std::string q = makeQuery({
            {"msubmenu", "storageinfo"},
            {"action", "view"}
        });
        return forwardToSunapi(req, "/stw-cgi/system.cgi?" + q);
    });

    // Crow 고정 스펙: 일자 타임라인 조회
    CROW_ROUTE(app, "/api/sunapi/timeline")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        const char* ch = req.url_params.get("channel");
        const char* date = req.url_params.get("date"); // YYYY-MM-DD
        if (!ch || !date) {
            return crow::response(400, "missing query: channel, date");
        }
        const std::string ds(date);
        if (ds.size() != 10) {
            return crow::response(400, "invalid date format");
        }
        const std::string q = makeQuery({
            {"msubmenu", "timeline"},
            {"action", "view"},
            {"FromDate", ds + " 00:00:00"},
            {"ToDate", ds + " 23:59:59"},
            {"ChannelIDList", ch},
            {"Type", "All"},
            {"OverlappedID", "0"}
        });
        return forwardToSunapi(req, "/stw-cgi/recording.cgi?" + q);
    });

    // Crow 고정 스펙: 월별 녹화일 조회
    CROW_ROUTE(app, "/api/sunapi/month-days")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        const char* ch = req.url_params.get("channel");
        const char* ys = req.url_params.get("year");
        const char* ms = req.url_params.get("month");
        if (!ch || !ys || !ms) {
            return crow::response(400, "missing query: channel, year, month");
        }

        int year = 0;
        int month = 0;
        try {
            year = std::stoi(ys);
            month = std::stoi(ms);
        } catch (...) {
            return crow::response(400, "invalid year/month");
        }
        if (year < 2000 || year > 2100 || month < 1 || month > 12) {
            return crow::response(400, "invalid year/month range");
        }

        const int last = daysInMonth(year, month);
        std::ostringstream m2;
        if (month < 10) m2 << "0";
        m2 << month;
        const std::string mm = m2.str();
        std::ostringstream l2;
        if (last < 10) l2 << "0";
        l2 << last;

        const std::string fromDate = std::to_string(year) + "-" + mm + "-01 00:00:00";
        const std::string toDate = std::to_string(year) + "-" + mm + "-" + l2.str() + " 23:59:59";

        const std::string q = makeQuery({
            {"msubmenu", "timeline"},
            {"action", "view"},
            {"FromDate", fromDate},
            {"ToDate", toDate},
            {"ChannelIDList", ch},
            {"Type", "All"},
            {"OverlappedID", "0"}
        });
        return forwardToSunapi(req, "/stw-cgi/recording.cgi?" + q);
    });

    // Crow fixed spec: export create request
    CROW_ROUTE(app, "/api/sunapi/export/create")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        std::string err;
        const std::string path = buildExportForwardPath(req, "create", &err);
        if (path.empty()) {
            return crow::response(400, err.empty() ? "invalid export create query" : err);
        }
        return forwardToSunapi(req, path);
    });

    // Crow fixed spec: export status polling request
    CROW_ROUTE(app, "/api/sunapi/export/status")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        std::string err;
        const std::string path = buildExportForwardPath(req, "status", &err);
        if (path.empty()) {
            return crow::response(400, err.empty() ? "invalid export status query" : err);
        }
        return forwardToSunapi(req, path);
    });

    // Crow fixed spec: export download request
    CROW_ROUTE(app, "/api/sunapi/export/download")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        std::string err;
        const std::string path = buildExportForwardPath(req, "download", &err);
        if (path.empty()) {
            return crow::response(400, err.empty() ? "invalid export download query" : err);
        }
        return forwardToSunapi(req, path);
    });

    // Crow fixed spec: PTZ/Focus control
    CROW_ROUTE(app, "/api/sunapi/ptz/focus")
        .methods(crow::HTTPMethod::Post)
    ([](const crow::request& req) {
        const char* ch = req.url_params.get("channel");
        const char* command = req.url_params.get("command");
        if (!ch || !command) {
            return crow::response(400, "missing query: channel, command");
        }

        std::string key;
        std::string value;
        const std::string cmd(command);
        if (cmd == "zoom_in") {
            key = "ZoomContinuous";
            value = "In";
        } else if (cmd == "zoom_out") {
            key = "ZoomContinuous";
            value = "Out";
        } else if (cmd == "zoom_stop") {
            key = "ZoomContinuous";
            value = "Stop";
        } else if (cmd == "focus_near") {
            key = "FocusContinuous";
            value = "Near";
        } else if (cmd == "focus_far") {
            key = "FocusContinuous";
            value = "Far";
        } else if (cmd == "focus_stop") {
            key = "FocusContinuous";
            value = "Stop";
        } else if (cmd == "autofocus") {
            key = "Mode";
            value = "SimpleFocus";
        } else {
            return crow::response(400, "invalid command");
        }

        const std::string q = makeQuery({
            {"msubmenu", "focus"},
            {"action", "control"},
            {"Channel", ch},
            {key, value}
        });
        return forwardToSunapi(req, "/stw-cgi/image.cgi?" + q);
    });

    CROW_ROUTE(app, "/api/sunapi/playback/challenge")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        if (!isAuthorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        const char* uri = req.url_params.get("uri");
        if (!uri || std::string(uri).empty()) {
            return crow::response(400, "missing query: uri");
        }

        int rtspPort = 554;
        const char* portStr = req.url_params.get("rtsp_port");
        if (portStr) {
            try {
                rtspPort = std::stoi(portStr);
            } catch (...) {
                return crow::response(400, "invalid rtsp_port");
            }
        }
        if (rtspPort <= 0 || rtspPort > 65535) {
            return crow::response(400, "invalid rtsp_port range");
        }

        std::string rtspHost = resolveRtspHost();
        if (rtspHost.empty()) {
            return crow::response(500, "SUNAPI_RTSP_HOST or SUNAPI_BASE_URL host is not configured");
        }

        std::string realm;
        std::string nonce;
        std::string err;
        if (!fetchRtspDigestChallenge(rtspHost, rtspPort, uri, &realm, &nonce, &err)) {
            return crow::response(502, std::string("RTSP challenge failed: ") + err);
        }

        crow::json::wvalue out;
        out["Realm"] = realm;
        out["Nonce"] = nonce;
        crow::response res;
        res.code = 200;
        res.set_header("Content-Type", "application/json");
        res.write(out.dump());
        return res;
    });

    // Crow 고정 스펙: Playback digestauth response 생성
    CROW_ROUTE(app, "/api/sunapi/playback/digestauth")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        const char* method = req.url_params.get("method");
        const char* realm = req.url_params.get("realm");
        const char* nonce = req.url_params.get("nonce");
        const char* uri = req.url_params.get("uri");
        const char* cnonce = req.url_params.get("cnonce");
        const char* nc = req.url_params.get("nc");

        if (!method || !realm || !nonce || !uri || !cnonce || !nc) {
            return crow::response(400, "missing query: method, realm, nonce, uri, cnonce, nc");
        }

        const std::string username = envOrDefault("SUNAPI_USER", "");
        if (username.empty()) {
            return crow::response(500, "SUNAPI_USER is not configured");
        }

        const std::string q = makeQuery({
            {"msubmenu", "digestauth"},
            {"action", "view"},
            {"Method", method},
            {"Realm", realm},
            {"Nonce", nonce},
            {"Uri", uri},
            {"username", username},
            {"password", ""},
            {"Nc", nc},
            {"Cnonce", cnonce}
        });

        crow::response res = forwardToSunapi(req, "/stw-cgi/security.cgi?" + q);
        if (res.code >= 200 && res.code < 300) {
            res.set_header("X-SUNAPI-USER", username);
        }
        return res;
    });

    CROW_ROUTE(app, "/api/sunapi/playback/session")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        if (!isAuthorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        const char* ch = req.url_params.get("channel");
        const char* date = req.url_params.get("date");
        const char* time = req.url_params.get("time");
        if (!ch || !date || !time) {
            return crow::response(400, "missing query: channel, date, time");
        }

        int channel = -1;
        try {
            channel = std::stoi(ch);
        } catch (...) {
            return crow::response(400, "invalid channel");
        }
        if (channel < 0) {
            return crow::response(400, "invalid channel range");
        }

        std::string compactTs;
        if (!toCompactDateTime(std::string(date) + " " + std::string(time), &compactTs)) {
            return crow::response(400, "invalid date/time format");
        }

        int rtspPort = 554;
        const char* portStr = req.url_params.get("rtsp_port");
        if (portStr) {
            try {
                rtspPort = std::stoi(portStr);
            } catch (...) {
                return crow::response(400, "invalid rtsp_port");
            }
        }
        if (rtspPort <= 0 || rtspPort > 65535) {
            return crow::response(400, "invalid rtsp_port range");
        }

        const std::string rtspHost = resolveRtspHost();
        if (rtspHost.empty()) {
            return crow::response(500, "SUNAPI_RTSP_HOST or SUNAPI_BASE_URL host is not configured");
        }

        const std::string uri = "rtsp://" + rtspHost + ":" + std::to_string(rtspPort)
                              + "/" + std::to_string(channel) + "/recording/" + compactTs + "/play.smp";

        std::string realm;
        std::string nonce;
        std::string challengeErr;
        if (!fetchRtspDigestChallenge(rtspHost, rtspPort, uri, &realm, &nonce, &challengeErr)) {
            return crow::response(502, std::string("RTSP challenge failed: ") + challengeErr);
        }

        const std::string method = "OPTIONS";
        const std::string nc = "00000001";
        const std::string cnonce = randomHexString(8);
        std::string digestResponse;
        std::string username;
        std::string digestErr;
        int digestErrCode = 502;
        if (!requestDigestResponse(req, method, realm, nonce, uri, nc, cnonce,
                                   &digestResponse, &username, &digestErr, &digestErrCode)) {
            return crow::response(digestErrCode, digestErr);
        }

        crow::json::wvalue out;
        out["Uri"] = uri;
        out["Realm"] = realm;
        out["Nonce"] = nonce;
        out["Method"] = method;
        out["Nc"] = nc;
        out["Cnonce"] = cnonce;
        out["Response"] = digestResponse;
        out["Username"] = username;
        out["WsPath"] = normalizeWsPath(envOrDefault("SUNAPI_STREAMING_WS_PATH", "/StreamingServer"));
        return crow::response(200, out);
    });

    CROW_ROUTE(app, "/api/sunapi/export/session")
        .methods(crow::HTTPMethod::Get)
    ([](const crow::request& req) {
        if (!isAuthorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        const char* ch = req.url_params.get("channel");
        const char* date = req.url_params.get("date");
        const char* startTime = req.url_params.get("start_time");
        const char* endTime = req.url_params.get("end_time");
        if (!ch || !date || !startTime || !endTime) {
            return crow::response(400, "missing query: channel, date, start_time, end_time");
        }

        int channel = -1;
        try {
            channel = std::stoi(ch);
        } catch (...) {
            return crow::response(400, "invalid channel");
        }
        if (channel < 0) {
            return crow::response(400, "invalid channel range");
        }

        std::string tsStart;
        std::string tsEnd;
        if (!toCompactDateTime(std::string(date) + " " + std::string(startTime), &tsStart)
            || !toCompactDateTime(std::string(date) + " " + std::string(endTime), &tsEnd)) {
            return crow::response(400, "invalid date/time format");
        }

        int rtspPort = 554;
        const char* portStr = req.url_params.get("rtsp_port");
        if (portStr) {
            try {
                rtspPort = std::stoi(portStr);
            } catch (...) {
                return crow::response(400, "invalid rtsp_port");
            }
        }
        if (rtspPort <= 0 || rtspPort > 65535) {
            return crow::response(400, "invalid rtsp_port range");
        }

        const std::string rtspHost = resolveRtspHost();
        if (rtspHost.empty()) {
            return crow::response(500, "SUNAPI_RTSP_HOST or SUNAPI_BASE_URL host is not configured");
        }

        const std::string uri = "rtsp://" + rtspHost + "/"
                              + std::to_string(channel) + "/recording/"
                              + tsStart + "-" + tsEnd + "/OverlappedID=0/backup.smp";

        std::string realm;
        std::string nonce;
        std::string challengeErr;
        if (!fetchRtspDigestChallenge(rtspHost, rtspPort, uri, &realm, &nonce, &challengeErr)) {
            return crow::response(502, std::string("RTSP challenge failed: ") + challengeErr);
        }

        const std::string method = "OPTIONS";
        const std::string nc = "00000001";
        const std::string cnonce = randomHexString(8);
        std::string digestResponse;
        std::string username;
        std::string digestErr;
        int digestErrCode = 502;
        if (!requestDigestResponse(req, method, realm, nonce, uri, nc, cnonce,
                                   &digestResponse, &username, &digestErr, &digestErrCode)) {
            return crow::response(digestErrCode, digestErr);
        }

        crow::json::wvalue out;
        out["Uri"] = uri;
        out["Realm"] = realm;
        out["Nonce"] = nonce;
        out["Method"] = method;
        out["Nc"] = nc;
        out["Cnonce"] = cnonce;
        out["Response"] = digestResponse;
        out["Username"] = username;
        out["WsPath"] = normalizeWsPath(envOrDefault("SUNAPI_STREAMING_WS_PATH", "/StreamingServer"));
        return crow::response(200, out);
    });
}
