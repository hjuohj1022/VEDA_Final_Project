#include "../include/crow_all.h"
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstdlib> // getenv 사용
#include <mysql/mysql.h> // MariaDB C Connector
#include <fstream> // 파일 읽기용
#include <sstream> // 버퍼용
#include <sys/statvfs.h> // 리눅스 파일시스템 통계용

namespace fs = std::filesystem;

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
    // 로그인 API (DB 연동)
    // ==========================================
    CROW_ROUTE(app, "/login").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req){
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        std::string id = x["id"].s();
        std::string pw = x["password"].s();

        std::cout << "[Login Attempt] ID: " << id << std::endl;

        // DB 확인 함수 호출
        if (checkUserFromDB(id, pw)) {
            crow::json::wvalue res;
            res["status"] = "success";
            res["token"] = "valid-token-123"; 
            return crow::response(res);
        } else {
            return crow::response(401, "Login Failed: Check ID or Password");
        }
    });

    // ==========================================
    // 녹화된 파일 목록 조회
    // ==========================================
    CROW_ROUTE(app, "/recordings")
    ([](){
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
        return result; 
    });

    // ==========================================
    // 파일 삭제 API (DELETE /recordings?file=...)
    // Qt 앱이 이 주소로 삭제 요청을 보냄
    // ==========================================
    CROW_ROUTE(app, "/recordings").methods(crow::HTTPMethod::DELETE)
    ([](const crow::request& req){
        // 쿼리 파라미터(?file=...)에서 파일명 가져오기
        char* file_param = req.url_params.get("file");
        
        if (!file_param) {
            return crow::response(400, "Missing file parameter");
        }

        std::string filename = file_param;
        
        // 경로 조작 방지 (../ 같은 거 막기)
        if (filename.find("..") != std::string::npos) {
             return crow::response(403, "Invalid filename");
        }

        // 파일 경로 설정 (/app/recordings에 마운트 됨)
        std::string file_path = "/app/recordings/" + filename;

        if (fs::exists(file_path)) {
            try {
                fs::remove(file_path); // 실제 파일 삭제
                std::cout << "[Delete] Removed file: " << file_path << std::endl;
                return crow::response(200, "File deleted");
            } catch (const fs::filesystem_error& e) {
                std::cerr << "[Delete Error] " << e.what() << std::endl;
                return crow::response(500, "Failed to delete file");
            }
        } else {
            return crow::response(404, "File not found");
        }
    });

    // ==========================================
    // 파일 다운로드/재생 (Qt 요청: /stream?file=...)
    // 수동으로 파일 읽어서 전송 (404 해결)
    // ==========================================
    CROW_ROUTE(app, "/stream")
    ([](const crow::request& req, crow::response& res){
        // 쿼리 파라미터(?file=...)에서 파일명 가져오기
        char* file_param = req.url_params.get("file");
        
        if (!file_param) {
            res.code = 400;
            res.write("Missing file parameter");
            res.end();
            return;
        }

        std::string filename = file_param;
        
        // 경로 조작 방지 (../ 같은 거 막기)
        if (filename.find("..") != std::string::npos) {
             res.code = 403; 
             res.end();
             return;
        }

        // 파일 경로 설정 (/app/recordings에 마운트 됨)
        std::string file_path = "/app/recordings/" + filename;

        // [핵심 변경] set_static_file_info 대신 직접 읽기
        std::ifstream ifs(file_path, std::ios::binary);

        if (ifs.is_open()) {
            std::cout << "[Stream] Reading file manually: " << file_path << std::endl; // 로그 추가
            
            std::ostringstream oss;
            oss << ifs.rdbuf();
            
            res.body = oss.str();
            res.add_header("Content-Type", "video/mp4");
            res.code = 200;
            res.end();
        } else {
            std::cerr << "[Error] Cannot open file: " << file_path << std::endl; // 에러 로그
            res.code = 404;
            res.write("File not found or cannot open");
            res.end();
        }
    });
    
    // ==========================================
    // 시스템 저장소 용량 조회 (Qt 앱 요청)
    // ==========================================
    CROW_ROUTE(app, "/system/storage")
    ([](){
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

    app.port(8080).multithreaded().run();
}