#include "../include/SunapiProxy.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
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
}
