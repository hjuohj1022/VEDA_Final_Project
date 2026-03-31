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

// 단일 페이로드 기반 CCTV 스트림이 프레임 앞부분에 붙여 보내는 고정 길이 헤더다.
// 해상도와 페이로드 크기를 먼저 읽은 뒤, 뒤따르는 바이너리 프레임 본문을 안전하게 분리하는 데 사용한다.
struct FrameHeader {
    uint32_t frame_idx;
    uint32_t width;
    uint32_t height;
    uint32_t payload_size;
};

// RGBD 스트림이 깊이 정보와 BGR 페이로드를 나눠 보낼 때 사용하는 헤더다.
// 한 프레임 안에 두 개의 데이터 덩어리가 연속으로 들어오므로 각 크기를 따로 보관한다.
struct RgbdHeader {
    uint32_t frame_idx;
    uint32_t width;
    uint32_t height;
    uint32_t depth_size;
    uint32_t bgr_size;
};

#pragma pack(pop)

// 현재 백엔드가 보내는 CCTV 스트림 페이로드 형식을 나타낸다.
// 프레임 읽기 루프는 이 값을 기준으로 어떤 헤더를 먼저 읽고, 이어서 어느 길이만큼 본문을 읽을지 결정한다.
enum class CctvStreamMode {
    NONE,
    PC_IMAGE,
    RGBD_RAW,
    DEPTH_RAW
};

// CCTV 백엔드와의 실제 TLS 연결을 소유하고,
// 제어 명령 요청/응답과 장기 스트림 수신을 모두 담당하는 핵심 매니저다.
class CctvManager {
public:
    using RawStreamCallback = std::function<void(const std::vector<uint8_t>&)>;

    // 백엔드 주소와 TLS 인증서 경로를 보관해 이후 제어 연결/스트림 연결에 재사용한다.
    CctvManager(const std::string& host,
                int port,
                const std::string& ca_path,
                const std::string& cert_path,
                const std::string& key_path);
    // 살아 있는 스트림 연결과 수신 스레드를 정리하고, OpenSSL 컨텍스트를 해제한다.
    ~CctvManager();

    // 장기 스트림 연결을 만들고 수신 스레드를 시작한다.
    // 이미 연결 중이면 기존 상태를 정리한 뒤 다시 연결을 시도한다.
    bool connect();
    // 수신 스레드를 멈추고 현재 열린 소켓과 SSL 객체를 모두 닫는다.
    void disconnect();

    // 제어 명령 또는 스트림 시작 명령을 백엔드에 전달하고, 백엔드가 반환한 텍스트 응답을 돌려준다.
    std::string sendCommand(const std::string& command);
    // 스트림 수신 시 프레임 원본 바이트를 넘겨받을 콜백을 등록한다.
    // 프록시 계층은 이 콜백을 통해 웹소켓 브로드캐스트를 수행한다.
    void setStreamCallback(RawStreamCallback cb) { stream_cb_ = cb; }

    // 현재 장기 스트림 연결이 살아 있는지 확인한다.
    bool isConnected() const { return connected_; }
    // 현재 수신 중인 스트림의 바이너리 포맷을 확인한다.
    CctvStreamMode getStreamMode() const { return stream_mode_; }

private:
    // 장기 스트림 소켓에서 프레임 헤더와 본문 바이트를 반복해서 읽어 콜백으로 전달한다.
    void streamLoop();
    // CCTV 연결 전용 SSL_CTX를 초기화하고 인증서/키/CA 정보를 적재한다.
    void initSsl();
    // 이 매니저가 소유한 SSL 컨텍스트와 OpenSSL 관련 리소스를 해제한다.
    void cleanupSsl();
    // 활성 스트림 소켓에서 지정한 길이만큼 정확히 읽는다.
    bool readExact(void* buf, size_t len);
    // 제어용 소켓에서 개행 단위 텍스트 응답 한 줄을 읽는다.
    std::string readLine();
    // 백엔드 소켓에 수신 타임아웃을 적용해 영원히 블로킹되지 않도록 한다.
    bool setSocketRecvTimeoutMs(int timeout_ms);
    // 제어 명령 또는 스트림 시작용 단발성 TLS 연결을 생성한다.
    bool openTlsConnection(SSL** out_ssl, int* out_socket_fd);
    // openTlsConnection()으로 만든 TLS 연결을 정리한다.
    void closeTlsConnection(SSL* ssl, int socket_fd);
    // 짧게 연결해서 요청/응답만 처리하는 제어 명령 경로다.
    std::string sendControlCommand(const std::string& command);
    // 스트림 시작 명령을 보내고, 이후 지속 수신할 소켓을 수신 스레드에 넘긴다.
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
