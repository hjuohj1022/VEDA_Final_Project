#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>
#include <thread>
#include <vector>

#pragma pack(push, 1)

// Binary frame header used for single-payload CCTV streams.
struct FrameHeader {
    uint32_t frame_idx;
    uint32_t width;
    uint32_t height;
    uint32_t payload_size;
};

// Binary frame header used for RGBD streams with split depth and BGR payloads.
struct RgbdHeader {
    uint32_t frame_idx;
    uint32_t width;
    uint32_t height;
    uint32_t depth_size;
    uint32_t bgr_size;
};

#pragma pack(pop)

// Describes the payload format currently emitted by the CCTV backend stream.
enum class CctvStreamMode {
    NONE,
    PC_IMAGE,
    RGBD_RAW,
    DEPTH_RAW
};

// Owns the TLS connection to the CCTV backend and delivers stream frames to Crow-side consumers.
class CctvManager {
public:
    using RawStreamCallback = std::function<void(const std::vector<uint8_t>&)>;

    // Stores the backend connection parameters and certificate paths.
    CctvManager(const std::string& host,
                int port,
                const std::string& ca_path,
                const std::string& cert_path,
                const std::string& key_path);
    // Releases the stream socket, SSL context, and reader thread.
    ~CctvManager();

    // Opens the long-lived CCTV stream connection and starts the reader thread.
    bool connect();
    // Stops the reader thread and closes any active TLS sockets.
    void disconnect();

    // Sends a control or stream command to the backend and returns the backend response text.
    std::string sendCommand(const std::string& command);
    // Registers the callback that receives raw frame packets including backend headers.
    void setStreamCallback(RawStreamCallback cb) { stream_cb_ = cb; }

    // Reports whether the streaming connection is currently established.
    bool isConnected() const { return connected_; }
    // Reports which binary stream format the backend is currently sending.
    CctvStreamMode getStreamMode() const { return stream_mode_; }

private:
    // Continuously reads binary frames from the backend and forwards them to the callback.
    void streamLoop();
    // Creates the SSL context used for CCTV backend connections.
    void initSsl();
    // Frees the SSL context and any OpenSSL resources owned by this manager.
    void cleanupSsl();
    // Reads exactly len bytes from the active backend stream socket.
    bool readExact(void* buf, size_t len);
    // Reads a newline-terminated response from the active control socket.
    std::string readLine();
    // Applies a receive timeout to the active backend socket.
    bool setSocketRecvTimeoutMs(int timeout_ms);
    // Opens a one-off TLS connection for control commands or stream startup.
    bool openTlsConnection(SSL** out_ssl, int* out_socket_fd);
    // Closes a TLS connection previously opened by openTlsConnection().
    void closeTlsConnection(SSL* ssl, int socket_fd);
    // Executes a short-lived control command over a temporary TLS connection.
    std::string sendControlCommand(const std::string& command);
    // Starts a streaming command and hands the resulting socket to the reader thread.
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
