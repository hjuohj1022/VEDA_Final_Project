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

    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);
}

void CctvManager::cleanupSsl() {
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
}

bool CctvManager::readExact(void* buf, size_t len) {
    size_t total_read = 0;
    char* p = static_cast<char*>(buf);
    while (total_read < len && !stop_thread_) {
        int r = SSL_read(ssl_, p + total_read, len - total_read);
        if (r <= 0) return false;
        total_read += r;
    }
    return total_read == len;
}

bool CctvManager::connect() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (connected_) return true;

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) return false;

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr);

    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd_);
        return false;
    }

    ssl_ = SSL_new(ssl_ctx_);
    SSL_set_fd(ssl_, socket_fd_);

    if (SSL_connect(ssl_) <= 0) {
        SSL_free(ssl_);
        close(socket_fd_);
        return false;
    }

    connected_ = true;
    stop_thread_ = false;
    reader_thread_ = std::thread(&CctvManager::streamLoop, this);
    return true;
}

void CctvManager::disconnect() {
    stop_thread_ = true;
    if (reader_thread_.joinable()) reader_thread_.join();

    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
    if (socket_fd_ >= 0) { close(socket_fd_); socket_fd_ = -1; }
    connected_ = false;
    stream_mode_ = CctvStreamMode::NONE;
}

std::string CctvManager::sendCommand(const std::string& command) {
    if (!connected_ && !connect()) return "Error: Not connected";

    std::lock_guard<std::mutex> lock(socket_mutex_);
    std::string full_cmd = command + "\n";
    SSL_write(ssl_, full_cmd.c_str(), full_cmd.length());

    if (command == "pc_stream") {
        stream_mode_ = CctvStreamMode::PC_IMAGE;
        return "OK pc_stream";
    } else if (command == "rgbd_stream") {
        stream_mode_ = CctvStreamMode::RGBD_RAW;
        return "OK rgbd_stream";
    }

    char buf[1024];
    int n = SSL_read(ssl_, buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return std::string(buf); }
    return "OK";
}

void CctvManager::streamLoop() {
    while (!stop_thread_) {
        if (stream_mode_ == CctvStreamMode::NONE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::vector<uint8_t> full_data;
        size_t header_size = (stream_mode_ == CctvStreamMode::PC_IMAGE) ? sizeof(FrameHeader) : sizeof(RgbdHeader);
        full_data.resize(header_size);

        // 1. 헤더 읽기
        if (!readExact(full_data.data(), header_size)) {
            connected_ = false; break;
        }

        // 2. 페이로드 크기 계산
        size_t payload_total = 0;
        if (stream_mode_ == CctvStreamMode::PC_IMAGE) {
            payload_total = reinterpret_cast<FrameHeader*>(full_data.data())->payload_size;
        } else {
            auto h = reinterpret_cast<RgbdHeader*>(full_data.data());
            payload_total = h->depth_size + h->bgr_size;
        }

        // 3. 페이로드 읽기
        if (payload_total > 0) {
            size_t current_size = full_data.size();
            full_data.resize(current_size + payload_total);
            if (!readExact(full_data.data() + current_size, payload_total)) {
                connected_ = false; break;
            }
        }

        // 4. 콜백 호출
        if (stream_cb_) stream_cb_(full_data);
    }
}
