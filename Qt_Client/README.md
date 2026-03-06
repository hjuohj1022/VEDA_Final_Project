# Qt CCTV Client (Live + Playback VMS)

이 프로젝트는 **Qt 6 (C++/QML)** 기반 CCTV 관제 클라이언트입니다.  
실시간 Live(4채널), 녹화 목록 API, 카메라 SD 저장소 표시, SUNAPI 기반 Playback(타임라인/구간 탐색)을 제공합니다.

## 주요 기능

### 1. Live 모니터링
- 2x2 그리드 4채널 동시 표시
- MediaMTX 경유 RTSP/RTSPS 재생
- FPS/Latency/Storage(카메라 SD 기준) 등 사이드바 메트릭 표시

### 2. Playback
- 채널/날짜/시간 지정 재생
- SUNAPI 타임라인 조회 후 녹화 구간 표시
- 하단 타임라인(녹화 구간 초록색), 시각 이동(seek), 줌(휠) 지원
- Play / Pause / Resume 제어
- 화면 전환 시 Playback 세션 정리(disconnect)
- Playback Controls 내 Export(내보내기) 지원
  - 선택 시각 기준 시작/종료 구간 지정
  - SUNAPI `export/create` 우선 시도
  - 장비가 `Error 608`(미지원)일 경우 RTSP `backup.smp` + ffmpeg fallback
  - 결과물 저장: 기본 AVI(실패 시 원인 로그 출력)

### 3. 녹화 관리 API 연동
- `/recordings` 목록 조회
- `/recordings?file=` 삭제
- `/stream?file=` 재생/다운로드
- 카메라 SUNAPI 응답 기반 SD 저장소 용량 표시

### 4. 인증 (Login / Sign Up)
- 로그인 화면에서 `Sign In`/`Sign Up` 모드 전환 지원
- Sign Up 전용 입력 폼(`ID`, `Password`, `Confirm Password`) 제공
- 비밀번호 확인 불일치/빈 입력값 클라이언트 검증
- `POST /register` 성공 시 로그인 화면으로 자동 복귀 및 입력값 초기화
- `Back to Sign In` 클릭 시 회원가입 입력값 초기화
- 로그인 전에는 검색창/화면 안내 툴팁 아이콘 비노출

## Qt Client Architecture Diagram

```text
                 +----------------------------------------------+
                 |              QML UI Layer                    |
                 | Main.qml / LoginScreen / VideoGrid / Sidebar |
                 | - Login / Live / Playback / Export controls  |
                 +------------------------+---------------------+
                                          | signals / bindings
                                          v
                 +------------------------+---------------------+
                 |             Backend Facade (QObject)         |
                 |                include/core/Backend.h        |
                 +-----------+----------------+-----------------+
                             |                |
                             |                +-----------------------------+
                             |                                              |
                             v                                              v
        +--------------------+-------------------+          +---------------+------------------+
        | Auth / Core / Session                  |          | Media / Playback / Export         |
        | BackendAuth* / BackendCore* / Init     |          | BackendRtsp* / BackendSunapi*     |
        | - JWT 저장, SSL 설정, API 요청 공통화  |          | - Live RTSP, WS RTSP 시퀀스, Export|
        +--------------------+-------------------+          +---------------+------------------+
                             |                                              |
                HTTPS API + Bearer                                          | RTSP/RTSPS, WS/WSS
                             |                                              |
                             v                                              v
          +------------------+--------------------+          +--------------+-------------------+
          | Crow API Gateway (외부)               |          | Media Endpoints (외부)           |
          | /login /recordings /api/sunapi/*      |          | MediaMTX / Camera StreamingServer|
          +---------------------------------------+          +----------------------------------+
```

- 핵심 원칙
  - Qt는 Crow API + Bearer 토큰 중심으로 동작
  - Playback/Export 세션 준비는 Crow 세션 API 응답을 사용
  - 카메라 계정/Digest 계산/CGI 상세는 클라이언트에서 직접 처리하지 않음

## Playback 작동 원리

1. 사용자가 채널/날짜/시간 선택 후 재생 요청  
2. SUNAPI `recording.cgi?msubmenu=timeline`으로 녹화 구간 조회  
3. 선택 시간이 녹화 구간인지 검증  
4. Crow 세션 API로 RTSP URI/challenge/digest 값을 한 번에 준비  
   - `GET /api/sunapi/playback/session`
5. `wss://<API_URL host><SUNAPI_STREAMING_WS_PATH>` 연결 후 WebSocket 바이너리 프레임으로 RTSP 시퀀스 전송  
   - `OPTIONS` (401) -> Digest 포함 `OPTIONS` (200)  
   - 다중 `SETUP` (track별 interleaved channel)  
   - `PLAY`  
6. RTP interleaved 수신 + 디코딩 경로로 전달  
7. `GET_PARAMETER` keepalive로 세션 유지  
8. `PAUSE/PLAY`로 일시정지/재개 제어

Live와 Playback은 제어 경로가 다릅니다. Playback은 단순 RTSP URL 1회 호출이 아니라 WS + RTSP 제어 시퀀스가 핵심입니다.

## Playback Export 작동 원리

1. 사용자가 Playback 화면에서 내보내기 구간(시작/끝)을 지정  
2. SUNAPI CGI export API 시도  
   - `recording.cgi?msubmenu=export&action=create`  
3. 장비가 export CGI를 지원하지 않으면(`Error 608`) RTSP backup fallback  
   - `rtsp://<ip>/<ch>/recording/<start>-<end>/OverlappedID=0/backup.smp`  
   - Web export와 동일한 다중 track SETUP + PLAY + keepalive 순서 사용  
4. 수집 완료 후 파일 저장  
   - AVI 요청 시 ffmpeg remux 수행  
  - ffmpeg 경로: `PLAYBACK_EXPORT_FFMPEG_PATH` -> 앱 폴더/`tools/ffmpeg.exe` -> PATH 순서 탐색

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
  - `LOGIN_TIMEOUT_MS`, `REGISTER_TIMEOUT_MS`
  - `SSL_CA_CERT`, `SSL_CLIENT_CERT`, `SSL_CLIENT_KEY`
  - `SSL_VERIFY_PEER`, `SSL_IGNORE_ERRORS`
- Live RTSP(MediaMTX)
  - `RTSP_SCHEME`, `RTSP_IP`, `RTSP_PORT`
  - `RTSP_USERNAME`, `RTSP_PASSWORD`
  - `RTSP_MAIN_PATH_TEMPLATE`, `RTSP_SUB_PATH_TEMPLATE`
- SUNAPI/Playback
  - `SUNAPI_RTSP_PORT`
  - `SUNAPI_STREAMING_WS_PATH` (세션 API 응답 WsPath가 없을 때 fallback)
  - PTZ/Focus 제어는 `POST /api/sunapi/ptz/focus` 고정 API 사용(클라이언트 CGI 조합 제거)
  - Storage 조회는 Crow 고정 API `GET /api/sunapi/storage` 사용
  - `SUNAPI_EXPORT_TYPE`, `SUNAPI_EXPORT_POLL_INTERVAL_MS`, `SUNAPI_EXPORT_POLL_TIMEOUT_MS`
  - `PLAYBACK_EXPORT_USE_FFMPEG_BACKUP` (608 장비에서 ffmpeg 백업 사용 여부)
  - `PLAYBACK_WS_AUTO_CONNECT`, `PLAYBACK_WS_SEND_DESCRIBE`
  - `SUNAPI_STREAMING_WS_PATH`, `PLAYBACK_WS_DIGEST_RESPONSE`
  - `PLAYBACK_WS_SDP_FILE_PROTOCOL`
  - `PLAYBACK_EXPORT_FFMPEG_PATH` (선택, 예: `C:/ffmpeg/bin/ffmpeg.exe`)

ffmpeg 배치/버전 관리:
- 저장소에는 `tools/ffmpeg.exe`를 커밋하지 않습니다(`.gitignore` 제외).
- 로컬 개발 환경에서는 프로젝트 루트 `tools/ffmpeg.exe`에 배치하거나 `PLAYBACK_EXPORT_FFMPEG_PATH`로 절대 경로를 지정하세요.
- CMake `POST_BUILD`는 로컬 `tools/ffmpeg.exe`가 있을 때 실행 폴더로 복사합니다.
- MQTT(선택)
  - `MQTT_ENABLED` 및 TLS 관련 변수

## 백엔드 API 요구사항

| Method | Endpoint | 설명 |
| :--- | :--- | :--- |
| `POST` | `/login` | 로그인 |
| `POST` | `/register` | 회원가입 |
| `GET` | `/recordings` | 녹화 목록 조회 |
| `DELETE` | `/recordings?file={name}` | 녹화 파일 삭제 |
| `GET` | `/stream?file={name}` | 녹화 파일 스트리밍 |
| `GET` | `/api/sunapi/storage` | 카메라 SD 저장소 조회 (Crow 프록시) |
| `GET` | `/api/sunapi/timeline` | Playback 타임라인 조회 |
| `GET` | `/api/sunapi/month-days` | Playback 월 단위 녹화일 조회 |
| `POST` | `/api/sunapi/ptz/focus` | PTZ/Focus 제어 (`zoom_*`, `focus_*`, `autofocus`) |
| `GET` | `/api/sunapi/export/create` | Playback Export 작업 생성 |
| `GET` | `/api/sunapi/export/status` | Playback Export 상태 조회 |
| `GET` | `/api/sunapi/export/download` | Playback Export 파일 다운로드 |
| `GET` | `/api/sunapi/playback/session` | Playback WS 세션 정보 생성(URI/challenge/digest) |
| `GET` | `/api/sunapi/export/session` | Export WS 세션 정보 생성(URI/challenge/digest) |

참고:
- Storage/Timeline/MonthDays/Playback digest는 Crow API를 통해 조회합니다.
- 빌드 시 로컬 `tools/ffmpeg.exe`가 있으면 실행 폴더로 자동 복사하도록 CMake POST_BUILD가 설정되어 있습니다.

## Qt -> Crow API 전환 현황 (한글)

- 적용 완료
  - Playback WS 준비: Qt direct challenge/digest 호출 -> Crow `/api/sunapi/playback/session`
  - Export WS 준비: Qt direct challenge/digest 호출 -> Crow `/api/sunapi/export/session`
  - PTZ/Focus: Qt direct CGI -> Crow `/api/sunapi/ptz/focus`
  - Export HTTP(create/status/download): Qt direct CGI -> Crow `/api/sunapi/export/*`
  - Error 608 장비에서 기본 경로를 WS export로 우선 전환 (`PLAYBACK_EXPORT_USE_FFMPEG_BACKUP=0`)

- 현재 상태
  - Qt는 Playback/Export 준비 + PTZ/Focus + Export HTTP에서 Crow API를 사용합니다.
  - Qt는 카메라 계정(`SUNAPI_USER/SUNAPI_PASSWORD`)을 더 이상 사용하지 않습니다.

- 최종 목표
  - Qt는 Crow API + Bearer 토큰만 사용
  - 카메라 Digest/계정/직접 CGI 호출은 Crow 내부 전용으로 완전 이관

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
│  │  ├─ BackendAuthLogin.cpp
│  │  ├─ BackendAuthRegister.cpp
│  │  ├─ BackendAuthSession.cpp
│  │  ├─ BackendCoreApi.cpp
│  │  ├─ BackendCoreEnv.cpp
│  │  ├─ BackendCoreMqtt.cpp
│  │  ├─ BackendCoreSsl.cpp
│  │  ├─ BackendCoreState.cpp
│  │  ├─ BackendInit.cpp
│  │  ├─ BackendMediaRecordings.cpp
│  │  ├─ BackendMediaStorage.cpp
│  │  ├─ BackendPlaybackWsRuntime.cpp
│  │  ├─ BackendRtspConfig.cpp
│  │  ├─ BackendRtspPlayback.cpp
│  │  ├─ BackendRtspProbe.cpp
│  │  ├─ BackendStreamingWs.cpp
│  │  ├─ BackendSunapiExportDownload.cpp
│  │  ├─ BackendSunapiExportFfmpeg.cpp
│  │  ├─ BackendSunapiExportHttp.cpp
│  │  ├─ BackendSunapiExportParse.cpp
│  │  ├─ BackendSunapiExportWsMux.cpp
│  │  ├─ BackendSunapiExportWsPrep.cpp
│  │  ├─ BackendSunapiExportWsRtsp.cpp
│  │  ├─ BackendSunapiExportWsSession.cpp
│  │  ├─ BackendSunapiPtz.cpp
│  │  ├─ BackendSunapiTimeline.cpp
│  │  └─ BackendSunapiTimelineMonth.cpp
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

## 소스 코드 2차 리팩토링 요약

대형 단일 파일을 기능 도메인 기준으로 분리해 책임 경계를 명확히 정리했습니다.

- `Backend.cpp` -> `BackendInit.cpp`, `BackendPlaybackWsRuntime.cpp`
- `BackendCore.cpp` -> `BackendCoreEnv.cpp`, `BackendCoreSsl.cpp`, `BackendCoreMqtt.cpp`, `BackendCoreApi.cpp`, `BackendCoreState.cpp`
- `BackendMedia.cpp` -> `BackendMediaRecordings.cpp`, `BackendMediaStorage.cpp`
- `BackendRtsp.cpp` -> `BackendRtspConfig.cpp`, `BackendRtspPlayback.cpp`, `BackendRtspProbe.cpp`
- `BackendSunapiExport.cpp` ->
  `BackendSunapiExportHttp.cpp`,
  `BackendSunapiExportParse.cpp`,
  `BackendSunapiExportFfmpeg.cpp`,
  `BackendSunapiExportDownload.cpp`,
  `BackendSunapiExportWsPrep.cpp`,
  `BackendSunapiExportWsSession.cpp`,
  `BackendSunapiExportWsRtsp.cpp`,
  `BackendSunapiExportWsMux.cpp`
- SUNAPI/PTZ/타임라인 분리
  - `BackendSunapiPtz.cpp`
  - `BackendSunapiTimeline.cpp`
  - `BackendSunapiTimelineMonth.cpp`

### 리팩토링 중 발생한 잔버그와 수정 내역

- 증상: Export 진행 중 컴파일 에러(람다 캡처 누락/미정의 심볼/헤더 누락)
  - 원인: 함수 분리 과정에서 의존 유틸 및 `this` 캡처, include 목록 누락
  - 조치: 분리된 파일별 include/시그니처/람다 캡처 재정렬

- 증상: Export 취소 시 파일 잠김 또는 삭제 실패
  - 원인: WebSocket/`QNetworkReply`/`QProcess` 종료 순서와 파일 핸들 해제 타이밍 충돌
  - 조치: 취소 시그널에서 네트워크/프로세스 선종료 후 파일 삭제 재시도(`removeFileWithRetry`) 적용

- 증상: 리팩토링 후 fallback 경로(FFmpeg -> WS)가 비정상 종료되거나 재시작 루프 발생
  - 원인: 실패/취소 상태 플래그와 fallback 트리거 경합
  - 조치: 취소 플래그 우선 처리, 완료/실패/취소 공통 종료 경로 단일화

- 증상: 주석 자동 치환 이후 의미 없는 주석/깨진 한글 발생
  - 원인: 기계적 주석 치환 및 인코딩 처리 실수
  - 조치: 핵심 분기(에러/fallback/리소스 정리) 중심 자연어 주석으로 재작성, 깨진 주석 전수 복구

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

- Export 시 `Error 608 (Feature Not Implemented OR Not Supported)`
  - 해당 장비의 SUNAPI CGI export 미지원 의미
  - RTSP `backup.smp` + ffmpeg fallback 경로 사용

- Export 시 `AVI 변환 실패 (ffmpeg 실행 오류/미설치)`
  - `PLAYBACK_EXPORT_FFMPEG_PATH` 설정 또는 `tools/ffmpeg.exe`/PATH 점검

- 타임라인 라벨 겹침
  - 기본/확대 라벨 Repeater 동시 렌더 문제
  - 조건부 `model` 렌더링으로 수정

## License

교육/프로젝트 목적 샘플 코드입니다.
