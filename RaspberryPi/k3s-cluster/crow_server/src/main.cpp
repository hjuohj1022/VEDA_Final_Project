#include "../include/crow_all.h"
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstdlib> // getenv 사용
#include <mysql/mysql.h> // MariaDB C Connector

namespace fs = std::filesystem;

// -------------------------------------------------------
// MariaDB에서 ID/PW 확인
// -------------------------------------------------------
bool checkUserFromDB(std::string inputId, std::string inputPw) {
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;
    
    // 환경변수에서 접속 정보 가져오기 (k3s deployment.yaml의 env와 매칭)
    const char* db_host = std::getenv("DB_HOST");     // mariadb-service
    const char* db_user = std::getenv("DB_USER");     // veda_user
    const char* db_pass = std::getenv("DB_PASSWORD"); // secret
    const char* db_name = "veda_db";                  // 고정값

    // 환경변수 없으면 실패 처리 (안전장치)
    if(!db_host || !db_user || !db_pass) {
        std::cerr << "[Error] DB Environment variables are missing!" << std::endl;
        return false;
    }

    conn = mysql_init(NULL);

    // DB 연결
    if (!mysql_real_connect(conn, db_host, db_user, db_pass, db_name, 3306, NULL, 0)) {
        std::cerr << "[DB Error] " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return false;
    }

    // 쿼리 실행
    // 주의: 실제 서비스에선 PreparedStatement 사용 권장 (SQL Injection 방지)
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
    std::cout << "========== FINAL VERSION (STREAMING) ==========" << std::endl;
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
        // k3s yaml에서 volumeMounts로 연결한 경로와 일치해야 함
        std::string path = "/recordings"; 
        
        std::vector<crow::json::wvalue> file_list;

        if (fs::exists(path) && fs::is_directory(path)) {
            try {
                for (const auto& entry : fs::directory_iterator(path)) {
                    // .mp4 파일만 골라냄
                    if (entry.is_regular_file() && entry.path().extension() == ".mp4") {
                        crow::json::wvalue file_info;
                        file_info["name"] = entry.path().filename().string();
                        file_info["size"] = (long)entry.file_size();
                        file_list.push_back(file_info);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[Error] Reading directory: " << e.what() << std::endl;
            }
        }
        
        crow::json::wvalue result;
        result["files"] = std::move(file_list);
        return crow::response(200, result);
    });

    // ==========================================
    // 영상 스트리밍 (Range Request 지원) 
    // ==========================================
    CROW_ROUTE(app, "/stream")
    ([](const crow::request& req, crow::response& res){
        char* file_param = req.url_params.get("file");
        if (!file_param) {
            res.code = 400; res.end(); return;
        }

        std::string filename = std::string(file_param);
        
        // 경로 조작 방지
        if (filename.find("..") != std::string::npos || filename.find("/") != std::string::npos) {
             res.code = 403; res.end(); return;
        }

        std::string file_path = "/recordings/" + filename;
        std::cout << "[Stream] File: " << file_path << std::endl;

        if (!fs::exists(file_path)) {
            res.code = 404; res.end(); return;
        }

        // 파일 전체 크기 확인
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            res.code = 500; res.end(); return;
        }
        long file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Range 헤더 파싱 (플레이어가 "여기부터 주세요" 요청했는지 확인)
        long start = 0;
        long end = file_size - 1;
        bool is_range = false;

        if (req.headers.count("Range")) {
            std::string range_header = req.get_header_value("Range");
            // "bytes=1024-" 형식 파싱
            size_t eq_pos = range_header.find("=");
            size_t dash_pos = range_header.find("-");
            if (eq_pos != std::string::npos && dash_pos != std::string::npos) {
                try {
                    start = std::stol(range_header.substr(eq_pos + 1, dash_pos - eq_pos - 1));
                    if (dash_pos + 1 < range_header.length()) {
                        end = std::stol(range_header.substr(dash_pos + 1));
                    }
                    is_range = true;
                } catch (...) {
                    // 파싱 실패 시 그냥 전체 전송
                    start = 0; end = file_size - 1;
                }
            }
        }

        // 범위 보정
        if (start >= file_size) start = file_size - 1;
        if (end >= file_size) end = file_size - 1;
        long content_length = end - start + 1;

        // 해당 부분 읽기
        file.seekg(start, std::ios::beg);
        std::vector<char> buffer(content_length);
        file.read(buffer.data(), content_length);

        // 응답 헤더 설정
        std::string body(buffer.begin(), buffer.end());
        res.write(body);
        res.add_header("Content-Type", "video/mp4");
        res.add_header("Accept-Ranges", "bytes");

        if (is_range) {
            // 부분 전송 (206 Partial Content)
            res.code = 206;
            res.add_header("Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size));
            res.add_header("Content-Length", std::to_string(content_length));
        } else {
            // 전체 전송 (200 OK)
            res.code = 200;
            res.add_header("Content-Length", std::to_string(file_size));
        }
        
        res.end();
    });

    // ==========================================
    // 파일 삭제 API (DELETE /recordings?file=...)
    // ==========================================
    CROW_ROUTE(app, "/recordings").methods(crow::HTTPMethod::DELETE)
    ([](const crow::request& req){
        char* file_param = req.url_params.get("file");
        if (!file_param) return crow::response(400);

        std::string filename = std::string(file_param);
        // 보안 검사 (.. 포함 방지)
        if (filename.find("..") != std::string::npos || filename.find("/") != std::string::npos) 
            return crow::response(403);

        std::string file_path = "/recordings/" + filename;
        
        if (fs::exists(file_path)) {
            try {
                fs::remove(file_path);
                return crow::response(200, "Deleted");
            } catch(...) {
                return crow::response(500, "Delete failed");
            }
        }
        return crow::response(404, "File not found");
    });

    // 서버 시작 (포트 8080)
    app.port(8080).multithreaded().run();
}