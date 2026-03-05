# CCTV Crow 서버 API 명세서

## 개요
- **서비스**: CCTV 백엔드 서버 제어 및 스트리밍 중계
- **프로토콜**: REST API (HTTP) + WebSocket
- **인증**: mTLS (클라이언트 인증서)
- **기본 경로**: `/cctv`
- **백엔드 연결**: C++ 기반 Depth TRT 서버 (기본 Port 9090)

---

## 1. 제어 API (Control Endpoints)

### 1.1 CCTV 시작
**엔드포인트**: `POST /cctv/control/start`

**설명**: CCTV 백엔드 서버에서 특정 채널의 포인트클라우드 처리를 시작합니다.

**요청 헤더**:
```
Content-Type: application/json
```

**요청 본문**:
```json
{
  "channel": 0,
  "mode": "headless"
}
```

**요청 파라미터**:
| 파라미터 | 타입 | 필수 | 설명 |
|---------|------|------|------|
| channel | integer(0-3) | Yes | 채널 번호 (0: 메인, 1-3: 추가 채널) |
| mode | string | Yes | 실행 모드 ("headless" 또는 "gui") |

**응답 (성공 200)**:
```json
{
  "status": "OK",
  "message": "CCTV channel 0 started in headless mode",
  "timestamp": "2026-03-04T10:30:00Z"
}
```

**응답 (실패 400)**:
```json
{
  "error": "Invalid channel number",
  "details": "Channel must be between 0 and 3",
  "timestamp": "2026-03-04T10:30:00Z"
}
```

**백엔드 명령**: `channel=0 headless`

---

### 1.2 CCTV 중지
**엔드포인트**: `POST /cctv/control/stop`

**설명**: 현재 실행 중인 CCTV 처리를 중지합니다.

**요청 본문**: (비어있음)

**응답 (성공 200)**:
```json
{
  "status": "OK",
  "message": "CCTV stopped",
  "timestamp": "2026-03-04T10:30:00Z"
}
```

**백엔드 명령**: `stop`

---

### 1.3 CCTV 일시중지
**엔드포인트**: `POST /cctv/control/pause`

**설명**: CCTV 처리를 일시중지합니다. (데이터 처리 중단, 재개 가능)

**요청 본문**: (비어있음)

**응답 (성공 200)**:
```json
{
  "status": "OK",
  "message": "CCTV paused",
  "timestamp": "2026-03-04T10:30:00Z"
}
```

**백엔드 명령**: `pause`

---

### 1.4 CCTV 재개
**엔드포인트**: `POST /cctv/control/resume`

**설명**: 일시중지된 CCTV 처리를 재개합니다.

**요청 본문**: (비어있음)

**응답 (성공 200)**:
```json
{
  "status": "OK",
  "message": "CCTV resumed",
  "timestamp": "2026-03-04T10:30:00Z"
}
```

**백엔드 명령**: `resume`

---

### 1.5 뷰 설정 변경
**엔드포인트**: `POST /cctv/control/view`

**설명**: 포인트클라우드 시점(rotation, flip, wireframe 등)을 업데이트합니다.

**요청 헤더**:
```
Content-Type: application/json
```

**요청 본문**:
```json
{
  "rx": -20.0,
  "ry": 35.0,
  "flipx": false,
  "flipy": false,
  "flipz": false,
  "wire": false,
  "mesh": true
}
```

**요청 파라미터**:
| 파라미터 | 타입 | 필수 | 기본값 | 설명 |
|---------|------|------|--------|------|
| rx | float | Yes | -20.0 | X축 회전 각도 (-89 ~ 89도) |
| ry | float | Yes | 35.0 | Y축 회전 각도 (-180 ~ 180도) |
| flipx | boolean | No | false | X축 반전 여부 |
| flipy | boolean | No | false | Y축 반전 여부 |
| flipz | boolean | No | false | Z축 반전 여부 |
| wire | boolean | No | false | 와이어프레임 모드 |
| mesh | boolean | No | true | 메시 채우기 여부 |

**응답 (성공 200)**:
```json
{
  "status": "OK",
  "message": "View parameters updated",
  "parameters": {
    "rx": -20.0,
    "ry": 35.0,
    "flipx": false,
    "flipy": false,
    "flipz": false,
    "wire": false,
    "mesh": true
  },
  "timestamp": "2026-03-04T10:30:00Z"
}
```

**백엔드 명령**: `pc_view rx=-20.0 ry=35.0 flipx=0 flipy=0 flipz=0 wire=0 mesh=1`

**호출 예시** (JavaScript):
```javascript
fetch('/cctv/control/view', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    rx: -20.0,
    ry: 35.0,
    flipx: false,
    flipy: false,
    flipz: false,
    wire: true,
    mesh: false
  })
})
```

---

## 2. 스트리밍 API (Streaming Endpoints)

### 2.1 포인트클라우드 스트림 (WebSocket)
**엔드포인트**: `WS /cctv/stream`

**설명**: 백엔드 서버로부터 실시간 포인트클라우드 이미지를 수신합니다.
(서버-렌더링 방식 또는 클라이언트-렌더링 방식 지원)

**연결 프로토콜**:

#### 초기 핸드셰이크
```
Client -> Server: (WebSocket UPGRADE)
Server -> Client: HTTP 101 Switching Protocols
```

#### 데이터 흐름

**프레임 헤더** (16 바이트, Little-Endian):
```c
struct FrameHeader {
  uint32_t frame_idx;   // 4 바이트: 프레임 인덱스
  uint32_t width;       // 4 바이트: 이미지 폭
  uint32_t height;      // 4 바이트: 이미지 높이
  uint32_t payload;     // 4 바이트: 페이로드 데이터 크기
}
```

**데이터 구조**:
```
[프레임 헤더 16바이트] + [이미지 데이터 N바이트]
```

**예시 (초기 응답)**:
```
OK pc_stream\n
[바이너리 프레임 데이터 시작...]
```

**프레임 예시**:
- frame_idx: 1
- width: 640
- height: 480
- payload: 256000 (640×480 바이트 RGBA 이미지)
- 실제 데이터: RGBA 이미지 바이너리 + 추가 메타데이터

**호출 예시** (JavaScript):
```javascript
const ws = new WebSocket('ws://localhost:8080/cctv/stream');

ws.onopen = () => {
  console.log('Stream connected');
};

ws.onmessage = (event) => {
  const data = event.data;
  if (data instanceof ArrayBuffer) {
    // 16바이트 헤더 파싱
    const view = new DataView(data.slice(0, 16));
    const frameIdx = view.getUint32(0, true);
    const width = view.getUint32(4, true);
    const height = view.getUint32(8, true);
    const payloadSize = view.getUint32(12, true);
    
    console.log(`Frame ${frameIdx}: ${width}x${height}`);
    
    // 이미지 데이터 처리
    const imageData = data.slice(16, 16 + payloadSize);
    renderImage(imageData, width, height);
  }
};

ws.onerror = (error) => {
  console.error('Stream error:', error);
};

ws.onclose = () => {
  console.log('Stream disconnected');
};
```

**응답 상태**:
| 상태 코드 | 설명 |
|----------|------|
| 101 | Switching Protocols - WebSocket 연결 성공 |
| 400 | Bad Request - 잘못된 요청 |
| 503 | Service Unavailable - 백엔드 서버 연결 불가 |

---

## 3. 상태 조회 API (Status Endpoints)

### 3.1 CCTV 상태 조회
**엔드포인트**: `GET /cctv/status`

**설명**: 현재 CCTV의 실행 상태와 설정을 조회합니다.

**응답 (성공 200)**:
```json
{
  "status": "running",
  "channel": 0,
  "mode": "headless",
  "view": {
    "rx": -20.0,
    "ry": 35.0,
    "flipx": false,
    "flipy": false,
    "flipz": false,
    "wire": false,
    "mesh": true
  },
  "frame_count": 1234,
  "uptime_seconds": 3600,
  "backend_connected": true,
  "timestamp": "2026-03-04T10:30:00Z"
}
```

**상태 값**:
| 상태 | 설명 |
|------|------|
| idle | CCTV 미실행 상태 |
| running | CCTV 정상 실행 중 |
| paused | CCTV 일시중지 상태 |
| error | CCTV 오류 발생 |
| disconnected | 백엔드 서버 연결 끊김 |

---

## 4. 백엔드 소켓 통신 프로토콜

### 4.1 통신 방식
- **프로토콜**: TCP + mTLS (SSL/TLS)
- **기본 Port**: 9090
- **인증**: 클라이언트 인증서 기반
  - CA Certificate: `/certs/rootCA.crt`
  - Client Certificate: `/certs/cctv.crt`
  - Client Private Key: `/certs/cctv.key`
- **타임아웃**: 3초 (명령 응답), 5초 (스트밍)
- **인코딩**: UTF-8

### 4.2 명령 형식

#### 제어 명령
```
[명령] + [파라미터]\n
```

**예시**:
```
channel=0 headless
stop
pause
resume
pc_view rx=-20.0 ry=35.0 flipx=0 flipy=0 flipz=0 wire=0 mesh=1
```

#### 스트림 요청
```
pc_stream\n
```

**서버 응답**:
```
OK pc_stream\n
[바이너리 프레임 데이터...]
```

### 4.3 Crow 서버의 클라이언트 구현 (C++)

**필수 헤더**:
```cpp
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <string>
```

**주요 클래스**:
```cpp
class CctvClient {
  public:
    CctvClient(const std::string& host, int port,
               const std::string& ca_path,
               const std::string& cert_path,
               const std::string& key_path);
    
    // 명령 전송 및 응답 수신
    std::string send_command(const std::string& command);
    
    // 스트림 시작 (콜백 기반)
    void start_stream(
      std::function<void(const FrameHeader&, const std::vector<uint8_t>&)> callback
    );
    
    void stop_stream();
    
  private:
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::ssl::context> ssl_context_;
    // ...
};

struct FrameHeader {
  uint32_t frame_idx;
  uint32_t width;
  uint32_t height;
  uint32_t payload;
} __attribute__((packed));
```

---

## 5. MQTT 상태 발행 (선택 사항)

### 5.1 CCTV 상태 토픽
**토픽**: `v1/cctv/status`

**발행 조건**: 상태 변화 시

**발행 메시지**:
```json
{
  "status": "running",
  "channel": 0,
  "mode": "headless",
  "timestamp": "2026-03-04T10:30:00Z"
}
```

### 5.2 CCTV 뷰 설정 토픽
**토픽**: `v1/cctv/view`

**발행 조건**: 뷰 설정 변경 시

**발행 메시지**:
```json
{
  "rx": -20.0,
  "ry": 35.0,
  "flipx": false,
  "flipy": false,
  "flipz": false,
  "wire": false,
  "mesh": true,
  "timestamp": "2026-03-04T10:30:00Z"
}
```

---

## 6. 보안 및 인증

### 6.1 mTLS 설정
```bash
# 프로덕션 환경 (Kubernetes):
kubectl create secret tls cctv-tls \
  --cert=cctv.crt \
  --key=cctv.key \
  --ca-cert=rootCA.crt
```

### 6.2 환경 변수
```env
CCTV_BACKEND_HOST=127.0.0.1
CCTV_BACKEND_PORT=9090
CCTV_CERT_PATH=/certs
CCTV_USE_MTLS=true
CCTV_TIMEOUT_COMMAND=3
CCTV_TIMEOUT_STREAM=5
```

---

## 7. 에러 처리

### 7.1 HTTP 에러 코드
| 코드 | 설명 |
|------|------|
| 400 | Bad Request - 요청 파라미터 오류 |
| 401 | Unauthorized - 인증 실패 |
| 404 | Not Found - 엔드포인트 not found |
| 503 | Service Unavailable - 백엔드 서버 연결 불가 |
| 504 | Gateway Timeout - 백엔드 서버 응답 타임아웃 |

### 7.2 에러 응답 예시
```json
{
  "error": "Backend connection failed",
  "details": "Cannot connect to 127.0.0.1:9090",
  "code": "BACKEND_CONNECTION_ERROR",
  "timestamp": "2026-03-04T10:30:00Z"
}
```

---

## 8. 구현 순서 및 체크리스트

### Phase 1: 기본 소켓 클라이언트
- [ ] CctvClient 클래스 구현 (Boost.Asio SSL)
- [ ] mTLS 인증서 로딩 및 검증
- [ ] send_command() 함수 구현

### Phase 2: REST API 라우트
- [ ] POST /cctv/control/start
- [ ] POST /cctv/control/stop
- [ ] POST /cctv/control/pause
- [ ] POST /cctv/control/resume
- [ ] POST /cctv/control/view
- [ ] GET /cctv/status

### Phase 3: WebSocket 스트리밍
- [ ] WebSocket 엔드포인트 /cctv/stream 구현
- [ ] 바이너리 프레임 파싱 및 릴레이
- [ ] 다중 클라이언트 동시 연결 지원

### Phase 4: MQTT 통합 (선택)
- [ ] 상태 변화 시 MQTT 발행
- [ ] 토픽 매핑 및 메시지 형식

### Phase 5: 배포
- [ ] Docker 이미지 빌드
- [ ] Kubernetes 설정 (Secret, ConfigMap)
- [ ] 환경 변수 설정
- [ ] 통합 테스트

---

## 9. 테스트 예시

### cURL을 이용한 테스트
```bash
# CCTV 시작
curl -X POST http://localhost:8080/cctv/control/start \
  -H "Content-Type: application/json" \
  -d '{"channel": 0, "mode": "headless"}'

# 뷰 설정 변경
curl -X POST http://localhost:8080/cctv/control/view \
  -H "Content-Type: application/json" \
  -d '{"rx": -20.0, "ry": 35.0, "flipx": false, "flipy": false, "flipz": false, "wire": false, "mesh": true}'

# 상태 조회
curl http://localhost:8080/cctv/status

# CCTV 중지
curl -X POST http://localhost:8080/cctv/control/stop
```

### WebSocket을 이용한 스트림 테스트
```javascript
// JavaScript (브라우저)
const ws = new WebSocket('ws://localhost:8080/cctv/stream');
ws.binaryType = 'arraybuffer';

ws.onmessage = (event) => {
  const data = event.data;
  const view = new DataView(data.slice(0, 16));
  const frameIdx = view.getUint32(0, true);
  console.log(`Received frame ${frameIdx}`);
};
```

---

## 10. 참고 자료

### 백엔드 명령어 목록 (client_gui.py 기준)
```python
# 제어 명령
"channel=0 headless"  # 채널 0, headless 모드 시작
"channel=0 gui"       # 채널 0, GUI 모드 시작
"stop"                # 중지
"pause"               # 일시중지
"resume"              # 재개

# 뷰 명령
"pc_view rx=-20 ry=35 flipx=0 flipy=0 flipz=0 wire=0 mesh=1"

# 스트림 요청
"pc_stream\n"
```

### 프레임 데이터 구조
```python
import struct

# 프레임 헤더 파싱 (Python 예시)
header = s.recv(16)
frame_idx, w, h, payload = struct.unpack("<IIII", header)

# 데이터 수신
image_data = s.recv(payload)
```

---

**문서 버전**: 1.0  
**작성일**: 2026-03-04  
**최종 수정**: 2026-03-04
