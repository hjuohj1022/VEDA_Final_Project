#include "../include/CctvManager.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

CctvManager::CctvManager(const std::string& host, int port,
                        const std::string& ca_path,
                        const std::string& cert_path,
                        const std::string& key_path)
    : host_(host), port_(port), ca_path_(ca_path), cert_path_(cert_path), key_path_(key_path) {
    initSsl();
}

CctvManager::~CctvManager() {
    disconnect();
    cleanupSsl();
}

void CctvManager::initSsl() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx_) {
        std::cerr << "[CCTV] Failed to create SSL context" << std::endl;
        return;
    }

    if (SSL_CTX_load_verify_locations(ssl_ctx_, ca_path_.c_str(), nullptr) <= 0) {
        std::cerr << "[CCTV] Failed to load CA certificate: " << ca_path_ << std::endl;
    }

    if (SSL_CTX_use_certificate_file(ssl_ctx_, cert_path_.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::cerr << "[CCTV] Failed to load client certificate: " << cert_path_ << std::endl;
    }

    if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, key_path_.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::cerr << "[CCTV] Failed to load client key: " << key_path_ << std::endl;
    }

    if (!SSL_CTX_check_private_key(ssl_ctx_)) {
        std::cerr << "[CCTV] Private key does not match the certificate" << std::endl;
    }

    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);
}

void CctvManager::cleanupSsl() {
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
}

bool CctvManager::connect() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (connected_) return true;

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "[CCTV] Failed to create socket" << std::endl;
        return false;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[CCTV] Invalid address: " << host_ << std::endl;
        close(socket_fd_);
        return false;
    }

    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[CCTV] Failed to connect to " << host_ << ":" << port_ << std::endl;
        close(socket_fd_);
        return false;
    }

    ssl_ = SSL_new(ssl_ctx_);
    SSL_set_fd(ssl_, socket_fd_);

    if (SSL_connect(ssl_) <= 0) {
        std::cerr << "[CCTV] SSL handshake failed" << std::endl;
        ERR_print_errors_fp(stderr);
        SSL_free(ssl_);
        close(socket_fd_);
        return false;
    }

    connected_ = true;
    stop_thread_ = false;
    reader_thread_ = std::thread(&CctvManager::streamLoop, this);
    
    std::cout << "[CCTV] Connected to " << host_ << ":" << port_ << " with mTLS" << std::endl;
    return true;
}

void CctvManager::disconnect() {
    stop_thread_ = true;
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    connected_ = false;
    streaming_ = false;
}

std::string CctvManager::sendCommand(const std::string& command) {
    if (!connected_ && !connect()) return "Error: Not connected";

    std::lock_guard<std::mutex> lock(socket_mutex_);
    std::string full_cmd = command + "\n";
    int ret = SSL_write(ssl_, full_cmd.c_str(), full_cmd.length());
    if (ret <= 0) {
        connected_ = false;
        return "Error: SSL_write failed";
    }

    if (command == "pc_stream") {
        streaming_ = true;
        return "OK pc_stream";
    }

    // 간단한 명령 응답 읽기 (필요 시)
    char buf[1024];
    int n = SSL_read(ssl_, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return std::string(buf);
    }

    return "OK";
}

void CctvManager::streamLoop() {
    while (!stop_thread_) {
        if (!streaming_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        FrameHeader header;
        int n = 0;
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            n = SSL_read(ssl_, &header, sizeof(header));
        }

        if (n <= 0) {
            std::cerr << "[CCTV] Read error or connection closed" << std::endl;
            connected_ = false;
            streaming_ = false;
            break;
        }

        if (n < (int)sizeof(header)) {
            // 헤더가 한 번에 다 안 읽힌 경우 처리 (단순화를 위해 여기서는 로그만)
            continue;
        }

        std::vector<uint8_t> payload(header.payload_size);
        int total_read = 0;
        while (total_read < (int)header.payload_size && !stop_thread_) {
            int r = 0;
            {
                std::lock_guard<std::mutex> lock(socket_mutex_);
                r = SSL_read(ssl_, payload.data() + total_read, header.payload_size - total_read);
            }
            if (r <= 0) break;
            total_read += r;
        }

        if (total_read == (int)header.payload_size && stream_cb_) {
            stream_cb_(header, payload);
        }
    }
}
