#include "../include/CctvManager.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

// CCTV 백엔드와의 실제 TLS 입출력 담당부.
// 제어 명령은 짧은 연결, 스트림은 장기 연결 + reader thread 분리 처리 구조.
namespace {
// 일반 제어 명령 및 스트림 ACK의 단기 응답 시간 상한.
constexpr int kCommandResponseTimeoutMs = 3000;
// 비정상 헤더 기반 메모리 급증 방지용 상한.
constexpr size_t kMaxCctvFramePayloadBytes = 32U * 1024U * 1024U;
}

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
    // 프로세스 내 OpenSSL 사용 준비 및 매니저 전용 SSL_CTX 생성.
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

bool CctvManager::setSocketRecvTimeoutMs(int timeout_ms) {
    if (socket_fd_ < 0) {
        return false;
    }

    struct timeval tv;
    const int clamped_ms = std::max(timeout_ms, 0);
    tv.tv_sec = clamped_ms / 1000;
    tv.tv_usec = (clamped_ms % 1000) * 1000;
    return setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

bool CctvManager::openTlsConnection(SSL** out_ssl, int* out_socket_fd) {
    // 제어 명령용 단발 연결과 스트림용 장기 연결의 공통 생성 헬퍼.
    if (!out_ssl || !out_socket_fd || !ssl_ctx_) {
        std::cerr << "[CCTV] TLS connection setup skipped: SSL context is not ready" << std::endl;
        return false;
    }

    *out_ssl = nullptr;
    *out_socket_fd = -1;
    
    std::cout << "[CCTV] Opening TLS connection to " << host_ << ":" << port_ << std::endl;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    struct addrinfo* results = nullptr;
    const std::string port_str = std::to_string(port_);
    const int gai_result = getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &results);
    if (gai_result != 0) {
        std::cerr << "[CCTV] Failed to resolve backend host " << host_
                  << ": " << gai_strerror(gai_result) << std::endl;
        return false;
    }

    int fd = -1;
    for (struct addrinfo* rp = results; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, rp->ai_addr, static_cast<socklen_t>(rp->ai_addrlen)) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(results);

    if (fd < 0) {
        std::cerr << "[CCTV] TCP connect failed to " << host_ << ":" << port_ << std::endl;
        return false;
    }

    SSL* ssl = SSL_new(ssl_ctx_);
    if (!ssl) {
        std::cerr << "[CCTV] Failed to allocate SSL object" << std::endl;
        close(fd);
        return false;
    }

    if (!SSL_set_tlsext_host_name(ssl, host_.c_str())) {
        std::cerr << "[CCTV] Failed to set SNI host name" << std::endl;
        SSL_free(ssl);
        close(fd);
        return false;
    }

    SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) <= 0) {
        std::cerr << "[CCTV] TLS handshake failed for " << host_ << ":" << port_ << std::endl;
        SSL_free(ssl);
        close(fd);
        return false;
    }

    std::cout << "[CCTV] TLS connection established to " << host_ << ":" << port_ << std::endl;
    *out_ssl = ssl;
    *out_socket_fd = fd;
    return true;
}

void CctvManager::closeTlsConnection(SSL* ssl, int socket_fd) {
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (socket_fd >= 0) {
        close(socket_fd);
    }
}

bool CctvManager::readExact(void* buf, size_t len) {
    size_t total_read = 0;
    char* p = static_cast<char*>(buf);
    while (total_read < len && !stop_thread_) {
        const int r = SSL_read(ssl_, p + total_read, static_cast<int>(len - total_read));
        if (r <= 0) {
            return false;
        }
        total_read += static_cast<size_t>(r);
    }
    return total_read == len;
}

std::string CctvManager::readLine() {
    std::string line;
    char ch = '\0';
    while (!stop_thread_) {
        const int r = SSL_read(ssl_, &ch, 1);
        if (r <= 0) {
            break;
        }
        line.push_back(ch);
        if (ch == '\n') {
            break;
        }
    }
    return line;
}

bool CctvManager::connect() {
    std::unique_lock<std::mutex> lock(socket_mutex_);
    if (connected_) {
        return true;
    }

    // 이전 stream ACK/read 실패 뒤 남은 stale 상태 정리 목적.
    // 새 thread 생성 전 join 가능한 reader thread 정리 필요.
    stop_thread_ = true;
    lock.unlock();
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
    lock.lock();

    closeTlsConnection(ssl_, socket_fd_);
    ssl_ = nullptr;
    socket_fd_ = -1;
    stream_mode_ = CctvStreamMode::NONE;

    if (!openTlsConnection(&ssl_, &socket_fd_)) {
        connected_ = false;
        return false;
    }

    connected_ = true;
    stop_thread_ = false;
    reader_thread_ = std::thread(&CctvManager::streamLoop, this);
    return true;
}

void CctvManager::disconnect() {
    // 소멸자 경로 포함, thread 종료 후 소켓/SSL 리소스 추가 정리.
    stop_thread_ = true;
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    std::lock_guard<std::mutex> lock(socket_mutex_);
    closeTlsConnection(ssl_, socket_fd_);
    ssl_ = nullptr;
    socket_fd_ = -1;
    connected_ = false;
    stream_mode_ = CctvStreamMode::NONE;
}

std::string CctvManager::sendControlCommand(const std::string& command) {
    // 스트림 연결과 분리된 전용 소켓 기반 요청/응답 1회 처리.
    SSL* control_ssl = nullptr;
    int control_fd = -1;
    if (!openTlsConnection(&control_ssl, &control_fd)) {
        return "Error: Not connected";
    }

    struct timeval tv;
    tv.tv_sec = kCommandResponseTimeoutMs / 1000;
    tv.tv_usec = (kCommandResponseTimeoutMs % 1000) * 1000;
    setsockopt(control_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(control_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    const std::string full_cmd = command + "\n";
    std::cout << "[CCTV] Sending control command: " << command << std::endl;
    const int write_result = SSL_write(control_ssl, full_cmd.c_str(), static_cast<int>(full_cmd.length()));
    if (write_result <= 0) {
        closeTlsConnection(control_ssl, control_fd);
        std::cerr << "[CCTV] Failed to write control command: " << command << std::endl;
        return "Error: Failed to send CCTV command";
    }

    std::string line;
    char ch = '\0';
    while (true) {
        const int n = SSL_read(control_ssl, &ch, 1);
        if (n <= 0) {
            break;
        }
        line.push_back(ch);
        if (ch == '\n') {
            break;
        }
        if (line.size() >= 4096) {
            break;
        }
    }
    closeTlsConnection(control_ssl, control_fd);

    if (!line.empty()) {
        std::cout << "[CCTV] Control response for [" << command << "]: " << line;
        return line;
    }

    std::cerr << "[CCTV] Timed out waiting for control response: " << command << std::endl;
    return "Error: Timed out or failed while waiting for CCTV response";
}

std::string CctvManager::startStreamCommand(const std::string& command) {
    // 장기 연결 상태 진입 및 streamLoop 읽기 모드 결정.
    if (!connected_ && !connect()) {
        return "Error: Not connected";
    }

    std::lock_guard<std::mutex> lock(socket_mutex_);
    const std::string full_cmd = command + "\n";  
    std::cout << "[CCTV] Sending stream command: " << command << std::endl;
    const int write_result = SSL_write(ssl_, full_cmd.c_str(), static_cast<int>(full_cmd.length()));
    if (write_result <= 0) {
        connected_ = false;
        std::cerr << "[CCTV] Failed to write stream command: " << command << std::endl;
        return "Error: Failed to send stream command";
    }

    setSocketRecvTimeoutMs(kCommandResponseTimeoutMs);
    const std::string ack = readLine();
    setSocketRecvTimeoutMs(0);

    if (command == "pc_stream") {
        if (ack.rfind("OK pc_stream", 0) == 0) {
            stream_mode_ = CctvStreamMode::PC_IMAGE;
        }
    } else if (command == "rgbd_stream") {
        if (ack.rfind("OK rgbd_stream", 0) == 0) {
            stream_mode_ = CctvStreamMode::RGBD_RAW;
        }
    } else if (command == "depth_stream") {
        if (ack.rfind("OK depth_stream", 0) == 0) {
            stream_mode_ = CctvStreamMode::DEPTH_RAW;
        }
    }

    if (ack.empty()) {
        connected_ = false;
        stream_mode_ = CctvStreamMode::NONE;
        stop_thread_ = true;
        std::cerr << "[CCTV] Failed to receive stream ACK for: " << command << std::endl;
        return "Error: Failed to read stream ACK";
    }
    std::cout << "[CCTV] Stream ACK for [" << command << "]: " << ack;
    connected_ = true;
    return ack;
}

std::string CctvManager::sendCommand(const std::string& command) {
    if ((command == "pc_stream") || (command == "rgbd_stream") || (command == "depth_stream")) {
        return startStreamCommand(command);
    }
    return sendControlCommand(command);
}

void CctvManager::streamLoop() {
    // stream_mode_ 기준 헤더 선행 읽기 후 payload 연속 읽기.
    // reader thread의 역할은 해석이 아닌 프레임 단위 바이트 배열 전달.
    while (!stop_thread_) {
        if (stream_mode_ == CctvStreamMode::NONE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::vector<uint8_t> full_data;
        size_t header_size = sizeof(RgbdHeader);
        if ((stream_mode_ == CctvStreamMode::PC_IMAGE) || (stream_mode_ == CctvStreamMode::DEPTH_RAW)) {
            header_size = sizeof(FrameHeader);
        }
        full_data.resize(header_size);

        if (!readExact(full_data.data(), header_size)) {
            connected_ = false;
            break;
        }

        size_t payload_total = 0;
        if ((stream_mode_ == CctvStreamMode::PC_IMAGE) || (stream_mode_ == CctvStreamMode::DEPTH_RAW)) {
            payload_total = static_cast<size_t>(reinterpret_cast<FrameHeader*>(full_data.data())->payload_size);
        } else {
            auto* h = reinterpret_cast<RgbdHeader*>(full_data.data());
            const size_t depth_size = static_cast<size_t>(h->depth_size);
            const size_t bgr_size = static_cast<size_t>(h->bgr_size);
            if ((depth_size > kMaxCctvFramePayloadBytes) ||
                (bgr_size > kMaxCctvFramePayloadBytes) ||
                (depth_size + bgr_size > kMaxCctvFramePayloadBytes)) {
                connected_ = false;
                stream_mode_ = CctvStreamMode::NONE;
                std::cerr << "[CCTV] Rejected oversized RGBD payload: depth=" << depth_size
                          << ", bgr=" << bgr_size << std::endl;
                break;
            }
            payload_total = depth_size + bgr_size;
        }

        if (payload_total > kMaxCctvFramePayloadBytes) {
            connected_ = false;
            stream_mode_ = CctvStreamMode::NONE;
            std::cerr << "[CCTV] Rejected oversized stream payload: " << payload_total << std::endl;
            break;
        }

        if (payload_total > 0) {
            const size_t current_size = full_data.size();
            full_data.resize(current_size + payload_total);
            if (!readExact(full_data.data() + current_size, payload_total)) {
                connected_ = false;
                break;
            }
        }

        if (stream_cb_) {
            stream_cb_(full_data);
        }
    }
}
