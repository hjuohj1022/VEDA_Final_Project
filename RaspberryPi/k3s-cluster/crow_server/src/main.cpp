#include "../include/crow_all.h"
#include <jwt-cpp/jwt.h> 
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstdlib> 
#include <mysql/mysql.h> 
#include <fstream> 
#include <sstream> 
#include <sys/statvfs.h> 
#include <chrono>
#include <optional>
#include <system_error>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

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
bool checkUserFromDB(std::string inputId, std::string inputPw) {
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

int main()
{
    crow::SimpleApp app;

    // ==========================================
    // 로그인 API (실제 JWT 발급)
    // ==========================================
    CROW_ROUTE(app, "/login").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid JSON");

        std::string id = x["id"].s();
        std::string pw = x["password"].s();

        // mTLS 정보 확인 (로깅용)
        std::string device_id = req.get_header_value("X-Device-ID");
        if (!device_id.empty()) std::cout << "[mTLS Device] " << device_id << std::endl;

        // DB 확인 후 토큰 생성
        if (checkUserFromDB(id, pw)) {
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
        
        long long file_size = static_cast<long long>(ifs.tellg());
        ifs.seekg(0, std::ios::beg); // 다시 처음으로 돌림

        // Range 헤더 파싱
        long long start = 0;
        long long end = file_size - 1;
        bool is_range = false;

        std::string range_header = req.get_header_value("Range");
        if (!range_header.empty()) {
            is_range = true;
            // 예: "bytes=1024-" 또는 "bytes=1024-2048"
            if (range_header.find("bytes=") == 0) {
                std::string range_val = range_header.substr(6);
                size_t dash_pos = range_val.find('-');
                if (dash_pos != std::string::npos) {
                    std::string start_str = range_val.substr(0, dash_pos);
                    std::string end_str = range_val.substr(dash_pos + 1);
                    
                    if (!start_str.empty()) start = std::stoll(start_str);
                    if (!end_str.empty()) end = std::stoll(end_str);
                }
            }
        }

        // 범위 유효성 검사
        if (start < 0 || start > end || start >= file_size) {
            res.code = 416; // Range Not Satisfiable
            res.end();
            return;
        }

        long long content_length = end - start + 1;

        // 응답 헤더 설정
        res.add_header("Accept-Ranges", "bytes");
        res.add_header("Content-Type", "video/mp4");
        
        if (is_range) {
            res.code = 206; // Partial Content
            std::string content_range = "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size);
            res.add_header("Content-Range", content_range);
        } else {
            res.code = 200; // OK
        }
        
        // 요청된 부분만 읽어서 전송
        std::vector<char> buffer(static_cast<size_t>(content_length));
        ifs.seekg(start);
        ifs.read(buffer.data(), content_length);

        res.body = std::string(buffer.begin(), buffer.end());
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
}
