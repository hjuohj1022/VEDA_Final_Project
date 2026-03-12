# Qt CCTV Client (Live + Playback VMS)

이 프로젝트는 **Qt 6 (C++/QML)** 기반 CCTV 관제 클라이언트입니다.  
실시간 Live(4채널), 카메라 SD 저장소 표시, SUNAPI 기반 Playback(타임라인/구간 탐색), CCTV 3D Map 1차 연동(API/WS 수신)을 제공합니다.

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

### 3. 저장소/상태 정보
- 카메라 SUNAPI 응답 기반 SD 저장소 용량 표시
- 참고: `/recordings`, `/stream` 계열은 현재 UI에서 사용하지 않음(레거시)

### 3-1. 카메라 표시(Image Enhancements) 제어
- Camera Controls 패널에서 채널별 표시값 제어
  - 대비(1~100), 밝기(1~100), 윤곽 조정(Enable + Level 1~32), 컬러 레벨(1~100)
  - 각 항목 값은 3자리 입력칸으로 직접 수정 가능(예: `100`)
  - 윤곽 활성화(SharpnessEnable) on/off
  - 초기화 버튼(기본값 50/50/12/50)
- Qt는 Crow 고정 API만 호출
  - `GET /api/sunapi/display/settings`
  - `POST /api/sunapi/display/settings`
  - `POST /api/sunapi/display/reset`

### 4. Thermal 모니터링
- 상단 탭의 `Thermal` 진입 시 열화상 스트림 시작, 이탈 시 자동 중지
- MQTT 청크 프레임(160x120, 16-bit) 재조립 후 QML `Image`로 표시
- 팔레트(`Jet`/`Gray`/`Iron`) 및 Auto/Manual range 제어 지원
- 관련 구현: `src/core/BackendThermal.cpp`, `src/ui/qml/ThermalViewer.qml`

### 5. CCTV 3D Map (1차: API + WS 수신)
- Camera Controls의 `3D Map 모드 ON/OFF` 버튼으로 시작/중지
- ON 시 시퀀스:
  - 줌 아웃 -> 줌 정지 -> 오토포커스 -> `POST /cctv/control/start` -> `POST /cctv/control/stream` -> `wss://<API_HOST>/cctv/stream`
- OFF 시 즉시 중지:
  - `POST /cctv/control/stop` 호출 + WS 종료
- 현재 단계(1차):
  - WS 바이너리 프레임 수신/카운트까지 연동
  - OpenCL 포인트클라우드 렌더링은 후속 단계
- 관련 구현:
  - `src/core/BackendCctv3dMap.cpp`
  - `src/ui/qml/SidebarCameraControlsPanel.qml`

### 6. 인증 (Login / Sign Up)
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
          | /login /api/sunapi/*                  |          | MediaMTX / Camera StreamingServer|
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
  - `SUNAPI_STREAMING_WS_PATH` (세션 API 응답 `WsPath`가 없거나 비어 있을 때 fallback)
    - `example.env` 기본값: `/sunapi/StreamingServer`
    - 코드 내부 최종 fallback: `/StreamingServer`
  - PTZ/Focus 제어는 `POST /api/sunapi/ptz/focus` 고정 API 사용(클라이언트 CGI 조합 제거)
  - 표시 설정 제어는 `GET/POST /api/sunapi/display/settings`, `POST /api/sunapi/display/reset` 고정 API 사용
  - Storage 조회는 Crow 고정 API `GET /api/sunapi/storage` 사용
  - `SUNAPI_EXPORT_TYPE`, `SUNAPI_EXPORT_POLL_INTERVAL_MS`, `SUNAPI_EXPORT_POLL_TIMEOUT_MS`
  - `PLAYBACK_EXPORT_USE_FFMPEG_BACKUP` (608 장비에서 ffmpeg 백업 사용 여부)
  - `PLAYBACK_WS_AUTO_CONNECT`, `PLAYBACK_WS_SEND_DESCRIBE`
  - `PLAYBACK_WS_DIGEST_RESPONSE`
  - `PLAYBACK_WS_SDP_FILE_PROTOCOL`
  - `PLAYBACK_EXPORT_FFMPEG_PATH` (선택, 예: `C:/ffmpeg/bin/ffmpeg.exe`)
  - `THERMAL_DEBUG` (1이면 열화상 청크 조립 디버그 로그 활성화)

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
| `GET` | `/api/sunapi/storage` | 카메라 SD 저장소 조회 (Crow 프록시) |
| `GET` | `/api/sunapi/timeline` | Playback 타임라인 조회 |
| `GET` | `/api/sunapi/month-days` | Playback 월 단위 녹화일 조회 |
| `POST` | `/api/sunapi/ptz/focus` | PTZ/Focus 제어 (`zoom_*`, `focus_*`, `autofocus`) |
| `GET` | `/api/sunapi/display/settings` | 표시 설정 조회 (대비/밝기/윤곽/컬러) |
| `POST` | `/api/sunapi/display/settings` | 표시 설정 적용 (채널별) |
| `POST` | `/api/sunapi/display/reset` | 표시 설정 초기화 (50/50/12/50) |
| `POST` | `/cctv/control/start` | 3D Map 처리 시작 (channel/mode) |
| `POST` | `/cctv/control/stream` | 3D Map 스트림 모드 요청 (`pc_stream`) |
| `POST` | `/cctv/control/stop` | 3D Map 처리 중지 |
| `WS` | `/cctv/stream` | 3D Map 바이너리 프레임 수신 |
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
  - 표시 설정(대비/밝기/윤곽/컬러): Qt direct CGI -> Crow `/api/sunapi/display/*`
  - 3D Map 1차: Qt ON/OFF 버튼 -> Crow `/cctv/control/*` + `/cctv/stream` WS 수신
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
│  │  ├─ BackendCctv3dMap.cpp
│  │  ├─ BackendInit.cpp
│  │  ├─ BackendMediaRecordings.cpp
│  │  ├─ BackendMediaStorage.cpp
│  │  ├─ BackendPlaybackWsRuntime.cpp
│  │  ├─ BackendRtspConfig.cpp
│  │  ├─ BackendRtspPlayback.cpp
│  │  ├─ BackendRtspProbe.cpp
│  │  ├─ BackendStreamingWs.cpp
│  │  ├─ BackendSunapiDisplay.cpp
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
│  │  ├─ BackendSunapiTimelineMonth.cpp
│  │  └─ BackendThermal.cpp
│  └─ ui/
│     └─ qml/
│        ├─ components/
│        │  ├─ IconButton.qml
│        │  ├─ SidebarControlButton.qml
│        │  └─ SidebarDisplaySlider.qml
│        ├─ Header.qml
│        ├─ InlineMainViewContent.qml
│        ├─ LoginScreen.qml
│        ├─ Main.qml
│        ├─ PlaybackContent.qml
│        ├─ PlaybackExportSaveDialog.qml
│        ├─ PlaybackScreen.qml
│        ├─ RtspSettingsDialog.qml
│        ├─ Sidebar.qml
│        ├─ SidebarCameraControlsPanel.qml
│        ├─ SidebarPlaybackControlsPanel.qml
│        ├─ SidebarStore.qml
│        ├─ SidebarSystemMetricsPanel.qml
│        ├─ StatusDialog.qml
│        ├─ ThermalContent.qml
│        ├─ ThermalViewer.qml
│        ├─ ViewGridContent.qml
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
- `BackendSunapiDisplay.cpp` 분리로 표시 설정 API 처리 책임 분리
- `BackendThermal.cpp` 분리로 열화상 MQTT 프레임 처리/팔레트 렌더링 분리
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

## QML UI 분리(1차) 요약

동작 변경 없이 `Main.qml`의 책임을 줄이고 화면/패널/다이얼로그를 기능별로 분리했습니다.

- 화면 콘텐츠 분리
  - `ViewGridContent.qml`
  - `PlaybackContent.qml`
  - `ThermalContent.qml`
  - `InlineMainViewContent.qml`
- 다이얼로그 분리
  - `PlaybackExportSaveDialog.qml`
  - `RtspSettingsDialog.qml`
  - `StatusDialog.qml`
- Sidebar 하위 패널 분리
  - `SidebarSystemMetricsPanel.qml`
  - `SidebarCameraControlsPanel.qml`
  - `SidebarPlaybackControlsPanel.qml`
- 공통 상태 스토어 분리
  - `SidebarStore.qml`
- Sidebar -> Store 백엔드 전달 안정화
  - `backendObject` 명시 프로퍼티를 통해 전달
  - self-binding으로 인한 `undefined`/`Connections target` 경고 방지

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
  - `GET <SUNAPI_STREAMING_WS_PATH>` -> `101 Switching Protocols`
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
