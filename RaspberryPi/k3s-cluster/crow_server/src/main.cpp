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
    // 영상 스트리밍 (직접 읽어서 전송) 
    // ==========================================
    CROW_ROUTE(app, "/stream")
    ([](const crow::request& req, crow::response& res){
        char* file_param = req.url_params.get("file");
        if (!file_param) {
            res.code = 400;
            res.write("Error: Missing 'file' parameter");
            res.end();
            return;
        }

        std::string filename = std::string(file_param);
        
        // 경로 조작 방지
        if (filename.find("..") != std::string::npos || filename.find("/") != std::string::npos) {
             res.code = 403;
             res.end();
             return;
        }

        std::string file_path = "/recordings/" + filename;
        std::cout << "[Stream Request] Path: " << file_path << std::endl;

        // 파일 존재 확인
        if (!fs::exists(file_path)) {
            std::cerr << "[Error] File not found: " << file_path << std::endl;
            res.code = 404;
            res.write("File not found on Server");
            res.end();
            return;
        }

        // 파일 열기
        std::ifstream file(file_path, std::ios::binary | std::ios::ate); // 끝으로 이동해서 크기 확인
        if (!file.is_open()) {
            std::cerr << "[Error] Permission denied: " << file_path << std::endl;
            res.code = 500;
            res.write("Error: Could not open file");
            res.end();
            return;
        }

        // 파일 크기 구하기
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg); // 다시 처음으로

        // 내용 읽기
        std::vector<char> buffer(size);
        if (file.read(buffer.data(), size)) {
            // string으로 변환하여 body에 할당
            std::string body(buffer.begin(), buffer.end());
            res.write(body);
            
            // 플레이어에게 파일 정보를 정확히 알려줌
            res.add_header("Content-Length", std::to_string(size));
            res.add_header("Content-Type", "video/mp4");
            
            res.end();
        } else {
            res.code = 500;
            res.end();
        }
    });

    // 서버 시작 (포트 8080)
    app.port(8080).multithreaded().run();
}