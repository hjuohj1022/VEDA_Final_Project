#include "features/media/RecordingRoutes.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

// 녹화 파일 관리용 REST 엔드포인트 구현 파일이다.
// 녹화 목록 조회, 파일 삭제, 바이트 범위 기반 스트리밍 다운로드가
// 동일한 경로 검증 규칙과 인증 규칙을 따르도록 한 곳에 모아뒀다.
namespace {
constexpr char kRecordingDirectory[] = "/app/recordings";
constexpr long long kMaxStreamResponseBytes = 8LL * 1024LL * 1024LL;

// HTTP 범위 헤더에 들어온 숫자 문자열을 64비트 정수로 안전하게 변환한다.
// 숫자 외 문자가 섞여 있거나 파싱이 끝까지 되지 않으면 잘못된 범위로 간주한다.
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

// 디렉터리 탈출(`..`, `/`, `\\`) 시도를 막고 recordings 루트 아래 경로만 허용한다.
// 삭제 경로와 스트리밍 경로가 모두 이 검사를 거치므로 파일 시스템 접근 보안의 핵심 지점이다.
std::string resolveSafeRecordingPath(const std::string& filename) {
    if (filename.find("..") != std::string::npos ||
        filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos) {
        return {};
    }

    return (fs::path(kRecordingDirectory) / filename).string();
}
}  // 익명 네임스페이스

// 인증된 웹 클라이언트가 사용하는 녹화 관리 라우트를 한 번에 등록한다.
// 목록 조회, 삭제, 스트리밍이 같은 경로 보안 정책을 공유하도록 이 파일에 묶어둔다.
void registerRecordingRoutes(crow::SimpleApp& app, const RequestAuthorizer& is_authorized) {
    CROW_ROUTE(app, "/recordings")
    ([is_authorized](const crow::request& req) {
        if (!is_authorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        crow::json::wvalue response;
        response["files"] = std::vector<std::string>();

        // 현재 녹화 디렉터리의 mp4 파일만 노출해 화면에 보여줄 목록을 구성한다.
        if (fs::exists(kRecordingDirectory) && fs::is_directory(kRecordingDirectory)) {
            int index = 0;
            for (const auto& entry : fs::directory_iterator(kRecordingDirectory)) {
                if (entry.path().extension() == ".mp4") {
                    response["files"][index]["name"] = entry.path().filename().string();
                    response["files"][index]["size"] = static_cast<long long>(entry.file_size());
                    ++index;
                }
            }
        }

        return crow::response(response);
    });

    CROW_ROUTE(app, "/recordings").methods(crow::HTTPMethod::DELETE)
    ([is_authorized](const crow::request& req) {
        if (!is_authorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        const char* file_param = req.url_params.get("file");
        if (!file_param) {
            return crow::response(400, "Missing file parameter");
        }

        const std::string safe_path = resolveSafeRecordingPath(file_param);
        if (safe_path.empty()) {
            return crow::response(403, "Invalid filename");
        }
        if (!fs::exists(safe_path) || !fs::is_regular_file(safe_path)) {
            return crow::response(404, "File not found");
        }

        std::error_code ec;
        fs::remove(safe_path, ec);
        if (ec) {
            std::cerr << "[Delete Error] " << ec.message() << std::endl;
            return crow::response(500, "Failed to delete file");
        }

        std::cout << "[Delete] Removed file: " << safe_path << std::endl;
        return crow::response(200, "File deleted");
    });

    CROW_ROUTE(app, "/stream")
    ([is_authorized](const crow::request& req, crow::response& res) {
        if (!is_authorized(req)) {
            res.code = 401;
            res.write("Unauthorized");
            res.end();
            return;
        }

        const char* file_param = req.url_params.get("file");
        if (!file_param) {
            res.code = 400;
            res.write("Missing file parameter");
            res.end();
            return;
        }

        const std::string safe_path = resolveSafeRecordingPath(file_param);
        if (safe_path.empty()) {
            res.code = 403;
            res.write("Invalid filename");
            res.end();
            return;
        }

        std::ifstream input(safe_path, std::ios::binary | std::ios::ate);
        if (!input.is_open()) {
            std::cerr << "[Error] Cannot open file: " << safe_path << std::endl;
            res.code = 404;
            res.write("File not found or cannot open");
            res.end();
            return;
        }

        const long long file_size = static_cast<long long>(input.tellg());
        input.seekg(0, std::ios::beg);

        // 기본은 전체 파일 응답이고, Range 헤더가 있으면 부분 전송으로 바꾼다.
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

        if (start < 0 || start > end || start >= file_size) {
            res.code = 416;
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

        res.add_header("Accept-Ranges", "bytes");
        res.add_header("Content-Type", "video/mp4");
        res.add_header("Content-Length", std::to_string(content_length));

        if (is_range) {
            res.code = 206;
            res.add_header(
                "Content-Range",
                "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size));
        } else {
            res.code = 200;
        }

        std::vector<char> buffer(static_cast<size_t>(content_length));
        input.seekg(start);
        input.read(buffer.data(), static_cast<std::streamsize>(content_length));
        if (input.gcount() != static_cast<std::streamsize>(content_length)) {
            res.code = 500;
            res.write("Failed to read requested range");
            res.end();
            return;
        }

        res.body.assign(buffer.begin(), buffer.end());
        res.end();
    });
}
