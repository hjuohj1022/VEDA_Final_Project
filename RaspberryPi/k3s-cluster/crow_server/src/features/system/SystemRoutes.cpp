#include "features/system/SystemRoutes.h"

#include <fstream>
#include <sstream>
#include <string>
#include <sys/statvfs.h>

// 시스템 점검용 REST 엔드포인트와 스웨거 정적 문서 서빙을 담당하는 구현 파일이다.
// 운영자가 현재 서버 상태와 저장소 여유 공간을 확인하거나,
// API 문서를 브라우저에서 바로 열 수 있게 하는 비교적 가벼운 기능을 모아둔다.
namespace {
constexpr char kRecordingDirectory[] = "/app/recordings";
constexpr char kSwaggerIndexPath[] = "/app/swagger/index.html";
constexpr char kSwaggerSpecPath[] = "/app/swagger/swagger.yaml";

// 컨테이너 내부의 정적 파일을 읽어 HTTP 응답으로 반환한다.
// 스웨거 UI HTML, OpenAPI YAML처럼 서버에 포함된 문서 자원을 그대로 서빙할 때 사용한다.
crow::response serveFile(const char* path, const char* content_type, const char* not_found_message) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return crow::response(404, not_found_message);
    }

    std::stringstream buffer;
    buffer << input.rdbuf();

    crow::response response(buffer.str());
    response.add_header("Content-Type", content_type);
    return response;
}
}  // 익명 네임스페이스

// 시스템 점검용 REST와 문서 서빙 라우트를 한 번에 등록한다.
// 보호가 필요한 저장소 조회와 공개해도 되는 헬스체크/문서 경로를 같은 모듈 안에서 함께 관리한다.
void registerSystemRoutes(crow::SimpleApp& app, const RequestAuthorizer& is_authorized) {
    CROW_ROUTE(app, "/system/storage")
    ([is_authorized](const crow::request& req) {
        if (!is_authorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        struct statvfs stat {};
        crow::json::wvalue response;

        // 녹화 디렉터리가 올라가 있는 파일시스템 기준으로 전체/가용/사용 바이트를 계산한다.
        if (statvfs(kRecordingDirectory, &stat) != 0) {
            response["error"] = "Failed to get storage info";
            return crow::response(500, response);
        }

        const unsigned long long total = static_cast<unsigned long long>(stat.f_blocks) * stat.f_frsize;
        const unsigned long long available = static_cast<unsigned long long>(stat.f_bavail) * stat.f_frsize;
        response["total_bytes"] = total;
        response["used_bytes"] = total - available;
        response["available_bytes"] = available;
        return crow::response(response);
    });

    CROW_ROUTE(app, "/health")
    ([](const crow::request& req) {
        crow::json::wvalue response;
        // 서버 자체 상태와 Nginx mTLS 헤더를 한 번에 확인할 수 있는 간단한 헬스체크 응답이다.
        response["crow_server"]["status"] = "OK";
        response["crow_server"]["message"] = "Crow server is receiving requests successfully.";

        const std::string device_id = req.get_header_value("X-Device-ID");
        const std::string device_verify = req.get_header_value("X-Device-Verify");
        if (device_verify == "SUCCESS") {
            response["nginx_mtls"]["status"] = "VERIFIED";
            response["nginx_mtls"]["device_id"] = device_id;
        } else {
            response["nginx_mtls"]["status"] = "UNVERIFIED/NOT_PRESENT";
            response["nginx_mtls"]["message"] = "Client certificate was not verified by Nginx.";
        }

        response["services_info"]["mqtt"] = "Port 8883 (MQTTS) is exposed via Nginx Gateway.";
        response["services_info"]["mediamtx_rtsp"] = "Port 8554 (RTSP) / 8555 (RTSPS) are available.";
        response["services_info"]["mediamtx_hls"] = "/hls/ path is available on port 443.";
        return crow::response(response);
    });

    CROW_ROUTE(app, "/docs")
    ([]() {
        return serveFile(kSwaggerIndexPath, "text/html; charset=utf-8", "Swagger UI file not found");
    });

    CROW_ROUTE(app, "/swagger.yaml")
    ([]() {
        return serveFile(kSwaggerSpecPath, "text/yaml; charset=utf-8", "Swagger YAML file not found");
    });
}
