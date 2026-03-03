# Qt CCTV Client (Live + Playback VMS)

이 프로젝트는 **Qt 6 (C++/QML)** 기반 CCTV 관제 클라이언트입니다.  
실시간 Live(4채널), 녹화 목록/저장소 API, SUNAPI 기반 Playback(타임라인/구간 탐색)을 제공합니다.

## 주요 기능

### 1. Live 모니터링
- 2x2 그리드 4채널 동시 표시
- MediaMTX 경유 RTSP/RTSPS 재생
- FPS/Latency/Storage 등 사이드바 메트릭 표시

### 2. Playback
- 채널/날짜/시간 지정 재생
- SUNAPI 타임라인 조회 후 녹화 구간 표시
- 하단 타임라인(녹화 구간 초록색), 시각 이동(seek), 줌(휠) 지원
- Play / Pause / Resume 제어
- 화면 전환 시 Playback 세션 정리(disconnect)

### 3. 녹화 관리 API 연동
- `/recordings` 목록 조회
- `/recordings?file=` 삭제
- `/stream?file=` 재생/다운로드
- `/system/storage` 저장소 용량 표시

## Playback 작동 원리

1. 사용자가 채널/날짜/시간 선택 후 재생 요청  
2. SUNAPI `recording.cgi?msubmenu=timeline`으로 녹화 구간 조회  
3. 선택 시간이 녹화 구간인지 검증  
4. `ws://<SUNAPI_IP>/StreamingServer` 연결  
5. WebSocket 바이너리 프레임으로 RTSP 시퀀스 전송  
   - `OPTIONS` (401) -> Digest 포함 `OPTIONS` (200)  
   - 다중 `SETUP` (track별 interleaved channel)  
   - `PLAY`  
6. RTP interleaved 수신 + 디코딩 경로로 전달  
7. `GET_PARAMETER` keepalive로 세션 유지  
8. `PAUSE/PLAY`로 일시정지/재개 제어

Live와 Playback은 제어 경로가 다릅니다. Playback은 단순 RTSP URL 1회 호출이 아니라 WS + RTSP 제어 시퀀스가 핵심입니다.

## 기술 스택

- Language: C++17
- UI: Qt Quick/QML
- Media: Qt Multimedia (FFmpeg backend)
- Network: Qt Network / WebSocket
- Backend API: Crow + MariaDB (외부 서버)
- Protocol: RTSP/RTSPS, HTTP/HTTPS, WebSocket

## 환경 설정 (.env)

실행 파일과 같은 경로의 `.env`를 로드합니다.  
기본 템플릿은 `example.env`를 사용하세요.

주요 항목:
- API/SSL
  - `API_URL`
  - `SSL_CA_CERT`, `SSL_CLIENT_CERT`, `SSL_CLIENT_KEY`
  - `SSL_VERIFY_PEER`, `SSL_IGNORE_ERRORS`
- Live RTSP(MediaMTX)
  - `RTSP_SCHEME`, `RTSP_IP`, `RTSP_PORT`
  - `RTSP_USERNAME`, `RTSP_PASSWORD`
  - `RTSP_MAIN_PATH_TEMPLATE`, `RTSP_SUB_PATH_TEMPLATE`
- SUNAPI/Playback
  - `SUNAPI_SCHEME`, `SUNAPI_IP`, `SUNAPI_PORT`
  - `SUNAPI_USER`, `SUNAPI_PASSWORD`, `SUNAPI_RTSP_PORT`
  - `PLAYBACK_WS_AUTO_CONNECT`, `PLAYBACK_WS_SEND_DESCRIBE`
- MQTT(선택)
  - `MQTT_ENABLED` 및 TLS 관련 변수

## 백엔드 API 요구사항

| Method | Endpoint | 설명 |
| :--- | :--- | :--- |
| `POST` | `/login` | 로그인 |
| `GET` | `/recordings` | 녹화 목록 조회 |
| `DELETE` | `/recordings?file={name}` | 녹화 파일 삭제 |
| `GET` | `/stream?file={name}` | 녹화 파일 스트리밍 |
| `GET` | `/system/storage` | 저장소 용량 조회 |

## 프로젝트 구조

```text
Team3VideoReceiver/
├─ build/
├─ certs/
├─ include/
│  └─ core/
│     └─ Backend.h
├─ src/
│  ├─ app/
│  │  └─ main.cpp
│  ├─ core/
│  │  ├─ Backend.cpp
│  │  ├─ BackendAuth.cpp
│  │  ├─ BackendCore.cpp
│  │  ├─ BackendMedia.cpp
│  │  ├─ BackendRtsp.cpp
│  │  ├─ BackendStreamingWs.cpp
│  │  └─ BackendSunapi.cpp
│  └─ ui/
│     └─ qml/
│        ├─ Header.qml
│        ├─ LoginScreen.qml
│        ├─ Main.qml
│        ├─ PlaybackScreen.qml
│        ├─ Sidebar.qml
│        ├─ VideoGrid.qml
│        └─ VideoPlayer.qml
├─ .editorconfig
├─ .env
├─ .gitignore
├─ CMakeLists.txt
├─ example.env
└─ README.md
```

## 빌드

필수:
1. Qt 6 SDK (Qt Quick, Qt Multimedia, Qt Network, Qt WebSockets)
2. CMake 3.19+
3. MinGW 64-bit (Qt Kit와 동일)

예시:
```bash
cmake -S . -B build
cmake --build build --config Release
```

## Wireshark 분석 기록(요약)

Playback 동작 검증 시 아래를 확인했습니다.

- 필터 예시
  - `ip.addr == <camera_ip> && (http || websocket || rtsp)`
  - `http.request.uri contains "recording.cgi" || http.request.uri contains "security.cgi"`
  - `tcp.stream eq <N>`
- 확인 포인트
  - `GET /StreamingServer` -> `101 Switching Protocols`
  - WS Binary payload 내부 RTSP 메시지 확인
  - `OPTIONS 401` -> Digest 포함 재시도 `200`
  - `SETUP` 다중 트랙 `200`
  - `PLAY 200` 이후 interleaved RTP 수신
- 결론
  - 웹 플레이백과 동일 동작을 위해 WS 세션 + RTSP 제어 순서 재현 필요

## 트러블슈팅

- `invalid JSON` (timeline)
  - 일부 장비 응답이 JSON이 아닌 key-value 형식
  - key-value fallback 파서로 처리

- `STORAGE_CHECK SSL host mismatch`
  - 접속 host와 인증서 SAN/CN 불일치
  - 인증서 재발급 또는 host 매칭 필요

- `MediaPlayer error / Invalid data / Invalid argument`
  - Playback 입력 경로/세션 상태/프로토콜 설정 점검 필요
  - WS 연결, RTSP 시퀀스, SDP/입력 URL 순서대로 확인

- 타임라인 라벨 겹침
  - 기본/확대 라벨 Repeater 동시 렌더 문제
  - 조건부 `model` 렌더링으로 수정

## License

교육/프로젝트 목적 샘플 코드입니다.
