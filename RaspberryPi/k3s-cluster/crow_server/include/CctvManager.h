#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <openssl/ssl.h>
#include <openssl/err.h>

// CCTV 백엔드 직접 통신용 바이너리 프로토콜 정의.
// 제어 명령은 텍스트 기반, 스트림 데이터는 아래 헤더 포함 바이너리 프레임 형태.
#pragma pack(push, 1)
// PC 이미지, Depth 같은 단일 payload 스트림용 바이너리 헤더.
struct FrameHeader {
    uint32_t frame_idx;
    uint32_t width;
    uint32_t height;
    uint32_t payload_size;
};

// Depth와 Color payload 분리 전송형 RGBD 스트림용 바이너리 헤더.
struct RgbdHeader {
    uint32_t frame_idx;
    uint32_t width;
    uint32_t height;
    uint32_t depth_size;
    uint32_t bgr_size;
};
#pragma pack(pop)

// 현재 CCTV 스트림의 바이너리 payload 형식 표현.
enum class CctvStreamMode {
    NONE,
    PC_IMAGE, // 16B Header
    RGBD_RAW, // 20B Header
    DEPTH_RAW // 16B Header
};

// 역할:
// - CCTV 백엔드와의 TLS 연결 개방 및 종료
// - 단발성 제어 명령과 장기 스트림 명령의 단일 인터페이스 제공
// - 스트림 프레임 읽기 및 등록된 콜백 전달
//
// 동작 방식:
// - 일반 제어 명령: 별도 TLS 연결 기반 요청/응답 1회 처리
// - 스트림 명령: 장기 연결 유지, reader_thread_ 기반 지속 수신
//
// 수명/동시성:
// - reader_thread_의 streamLoop() 실행 전용
// - 장기 연결 상태의 socket_mutex_ 보호, 소멸 시 disconnect() 정리
class CctvManager {
public:
    // 스트림 헤더 포함 전체 바이너리 프레임 전달용 콜백 형식.
    using RawStreamCallback = std::function<void(const std::vector<uint8_t>&)>;

    CctvManager(const std::string& host, int port,
                const std::string& ca_path,
                const std::string& cert_path,
                const std::string& key_path);
    ~CctvManager();

    // 스트림용 장기 연결과 reader thread 준비.
    bool connect();
    // 스트림용 장기 연결과 reader thread의 안전한 종료.
    void disconnect();

    // 명령 종류에 따른 제어 연결/스트림 연결 경로 선택.
    std::string sendCommand(const std::string& command);
    // 새 프레임 수신 시 호출할 콜백 등록.
    void setStreamCallback(RawStreamCallback cb) { stream_cb_ = cb; }

    bool isConnected() const { return connected_; }
    CctvStreamMode getStreamMode() const { return stream_mode_; }

private:
    // 장기 스트림 연결에서 헤더/페이로드 읽기 후 stream_cb_ 전달.
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
