#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <openssl/ssl.h>
#include <openssl/err.h>

#pragma pack(push, 1)
// 1.1 서버 렌더링 이미지용 헤더 (16 bytes)
struct FrameHeader {
    uint32_t frame_idx;
    uint32_t width;
    uint32_t height;
    uint32_t payload_size;
};

// 1.2 클라이언트 렌더링 RGBD용 헤더 (20 bytes)
struct RgbdHeader {
    uint32_t frame_idx;
    uint32_t width;
    uint32_t height;
    uint32_t depth_size;
    uint32_t bgr_size;
};
#pragma pack(pop)

enum class CctvStreamMode {
    NONE,
    PC_IMAGE, // 16B Header
    RGBD_RAW, // 20B Header
    DEPTH_RAW // 16B Header
};

class CctvManager {
public:
    // 콜백: 헤더를 포함한 전체 바이너리 데이터를 전달
    using RawStreamCallback = std::function<void(const std::vector<uint8_t>&)>;

    CctvManager(const std::string& host, int port,
                const std::string& ca_path,
                const std::string& cert_path,
                const std::string& key_path);
    ~CctvManager();

    bool connect();
    void disconnect();
    
    std::string sendCommand(const std::string& command);
    void setStreamCallback(RawStreamCallback cb) { stream_cb_ = cb; }
    
    bool isConnected() const { return connected_; }
    CctvStreamMode getStreamMode() const { return stream_mode_; }

private:
    void streamLoop();
    void initSsl();
    void cleanupSsl();
    bool readExact(void* buf, size_t len);
    std::string readLine();
    bool setSocketRecvTimeoutMs(int timeout_ms);
    bool openTlsConnection(SSL** out_ssl, int* out_socket_fd);
    void closeTlsConnection(SSL* ssl, int socket_fd);
    std::string sendControlCommand(const std::string& command);
    std::string startStreamCommand(const std::string& command);

    std::string host_;
    int port_;
    std::string ca_path_;
    std::string cert_path_;
    std::string key_path_;

    SSL_CTX* ssl_ctx_ = nullptr;
    SSL* ssl_ = nullptr;
    int socket_fd_ = -1;

    std::atomic<bool> connected_{false};
    std::atomic<CctvStreamMode> stream_mode_{CctvStreamMode::NONE};
    std::atomic<bool> stop_thread_{false};

    std::thread reader_thread_;
    std::mutex socket_mutex_;
    RawStreamCallback stream_cb_;
};
