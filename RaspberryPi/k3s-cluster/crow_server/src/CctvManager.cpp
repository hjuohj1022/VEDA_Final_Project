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

namespace {
constexpr int kCommandResponseTimeoutMs = 3000;
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
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (connected_) {
        return true;
    }

    if (!openTlsConnection(&ssl_, &socket_fd_)) {
        return false;
    }

    connected_ = true;
    stop_thread_ = false;
    reader_thread_ = std::thread(&CctvManager::streamLoop, this);
    return true;
}

void CctvManager::disconnect() {
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

    std::cout << "[CCTV] Sending control command: " << command << std::endl;
    const int write_result = SSL_write(control_ssl, command.c_str(), static_cast<int>(command.length()));
    if (write_result <= 0) {
        closeTlsConnection(control_ssl, control_fd);
        std::cerr << "[CCTV] Failed to write control command: " << command << std::endl;
        return "Error: Failed to send CCTV command";
    }

    char buf[1024];
    const int n = SSL_read(control_ssl, buf, sizeof(buf) - 1);
    closeTlsConnection(control_ssl, control_fd);

    if (n > 0) {
        buf[n] = '\0';
        std::cout << "[CCTV] Control response for [" << command << "]: " << buf << std::endl;
        return std::string(buf);
    }

    std::cerr << "[CCTV] Timed out waiting for control response: " << command << std::endl;
    return "Error: Timed out or failed while waiting for CCTV response";
}

std::string CctvManager::startStreamCommand(const std::string& command) {
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
            payload_total = reinterpret_cast<FrameHeader*>(full_data.data())->payload_size;
        } else {
            auto* h = reinterpret_cast<RgbdHeader*>(full_data.data());
            payload_total = h->depth_size + h->bgr_size;
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
