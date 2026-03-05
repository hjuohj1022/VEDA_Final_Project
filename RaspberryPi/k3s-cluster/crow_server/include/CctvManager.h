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
struct FrameHeader {
    uint32_t frame_idx;
    uint32_t width;
    uint32_t height;
    uint32_t payload_size;
};
#pragma pack(pop)

class CctvManager {
public:
    using StreamCallback = std::function<void(const FrameHeader&, const std::vector<uint8_t>&)>;

    CctvManager(const std::string& host, int port,
                const std::string& ca_path,
                const std::string& cert_path,
                const std::string& key_path);
    ~CctvManager();

    bool connect();
    void disconnect();
    
    std::string sendCommand(const std::string& command);
    void setStreamCallback(StreamCallback cb) { stream_cb_ = cb; }
    
    bool isConnected() const { return connected_; }
    bool isStreaming() const { return streaming_; }

private:
    void streamLoop();
    void initSsl();
    void cleanupSsl();

    std::string host_;
    int port_;
    std::string ca_path_;
    std::string cert_path_;
    std::string key_path_;

    SSL_CTX* ssl_ctx_ = nullptr;
    SSL* ssl_ = nullptr;
    int socket_fd_ = -1;

    std::atomic<bool> connected_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> stop_thread_{false};

    std::thread reader_thread_;
    std::mutex socket_mutex_;
    StreamCallback stream_cb_;
};
