#include "../include/SunapiProxy.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

// main.cpp에 정의된 JWT 검증 함수
extern bool verifyJWT(const std::string& token);

namespace {

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
}
