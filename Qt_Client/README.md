# AEGIS Vision VMS

이 프로젝트는 **Qt 6 (C++/QML)** 기반 **AEGIS Vision VMS** CCTV 관제 클라이언트입니다.  
실시간 Live(4채널), 카메라 SD 저장소 표시, SUNAPI 기반 Playback(타임라인/구간 탐색), CCTV 3D Map 1차 연동(API/WS 수신 + 로컬 렌더링), 그리고 **Playback WebSocket 제어 송신 + 로컬 UDP RTP 송출** 경로를 제공합니다.

## 주요 기능

### 1. Live 모니터링
- 2x2 그리드 4채널 동시 표시
- MediaMTX 경유 RTSP/RTSPS 재생
- 우측 사이드 패널 제공
  - 기본 Live 화면: `System Metrics` / `H/W Control` 탭 전환
  - 상단: `FPS`, `LATENCY` 차트
  - 하단: `ACTIVE`, `STORAGE`, `CLIENT`, `SERVER` 4개 카드 세로 배치(패널 높이 전체 사용)
  - `CLIENT`는 실시간 CPU 사용률/메모리 사용률/GPU 정보 표시
    - 주기 갱신 시 무거운 시스템 모델 조회(CPU/GPU/DirectX)는 캐시 사용
    - 반복 PowerShell/WMI 호출로 인한 UI 프레임 드랍/4채널 FPS 튐 현상 완화
  - `SERVER`는 단일 상태가 아니라 `API`, `RTSP`, `MQTT`를 각각 독립 상태로 표시
    - 상단: `GOOD / DEGRADED / DOWN` (각 항목별 상태값)
    - 하단: `API / RTSP / MQTT` 라벨
  - `LATENCY`는 RTSP 연결 지표 기반으로 표시
    - 스토리지 API 응답시간으로 latency를 덮어쓰지 않음
    - 단발성 스파이크 완화를 위해 내부 스무딩(EMA) 적용
  - 기존 `AI STATUS` 카드는 제거

### 2. Playback
- 채널/날짜/시간 지정 재생
- SUNAPI 타임라인 조회 후 녹화 구간 표시
- 하단 타임라인(녹화 구간 초록색), 시각 이동(seek), 줌(휠) 지원
- Play / Pause / Resume 제어
- 화면 전환 시 Playback 세션 정리(disconnect)
- Playback 제어 패킷을 WebSocket Binary로 송신(`OPTIONS/SETUP/PLAY/GET_PARAMETER`)
- WebSocket interleaved RTP 수신 데이터를 로컬 UDP(`127.0.0.1:5004`)로 송출
- Playback Controls 내 Export(내보내기) 지원
  - 선택 시각 기준 시작/종료 구간 지정
  - 기본 자동 범위는 5분이며, 선택 시각이 실제 녹화 구간 안에 있을 때는 종료 시각이 해당 녹화 구간 끝을 넘지 않도록 자동 보정
  - SUNAPI `export/create` 우선 시도
  - 장비가 `Error 608`(미지원)일 경우 RTSP `backup.smp` + ffmpeg fallback
  - 결과물 저장: 기본 AVI(실패 시 원인 로그 출력)

### 3. 저장소/상태 정보
- 카메라 SUNAPI 응답 기반 SD 저장소 용량 표시

### 3-1. 카메라 표시(Image Enhancements) 제어
- Camera Controls 패널에서 채널별 표시값 제어
  - 확대 화면 우측 패널에서 `Camera Controls` / `H/W Control` 탭 전환 가능
  - 대비(1~100), 밝기(1~100), 윤곽 조정(Enable + Level 1~32), 컬러 레벨(1~100)
  - 각 항목 값은 3자리 입력칸으로 직접 수정 가능(예: `100`)
  - 윤곽 활성화(SharpnessEnable) on/off
  - 초기화 버튼(기본값 50/50/12/50)
- Qt는 Crow 고정 API만 호출
  - `GET /api/sunapi/display/settings`
  - `POST /api/sunapi/display/settings`
  - `POST /api/sunapi/display/reset`

### 3-2. H/W Control (모터 + 레이저)
- 헤더 버튼 대신 우측 사이드 패널 탭에서 H/W 제어 제공
  - 기본 Live 화면: `System Metrics` / `H/W Control`
  - 확대 화면: `Camera Controls` / `H/W Control`
- 모터 제어
  - 패널에서 `Target motor(1~3)`, `Direction(Left/Right)`, `angle(0~180)` 선택 후 제어
  - 지원 동작
    - `Hold` / `Stop` / `Set` / `Center All` / `Stop All`
    - `Hold` 버튼: 누르는 동안 `press`, 손을 떼면 자동 `release`
    - 선택된 모터에 실제 hold가 걸려 있는 동안에만 버튼이 주황색으로 표시
    - `Stop` 버튼: 선택한 `Target` 모터 즉시 정지
- 레이저 제어
  - 같은 패널 안에서 `On` / `Off` / `Status` 버튼 제공
  - `On`: Crow `POST /laser/control/on`
  - `Off`: Crow `POST /laser/control/off`
  - `Status`: Crow `GET /laser/status`
  - `On` 버튼은 레이저가 켜진 상태일 때만 주황색으로 유지
  - `Status` 응답의 최근 명령/응답(`last=laser on/off`, `response=LED ON/OFF`)을 기준으로 레이저 상태를 동기화
- 관련 구현:
  - `src/ui/qml/sidebar/Sidebar.qml`
  - `src/ui/qml/sidebar/SidebarMotorControlPanel.qml`
  - `src/core/cctv/BackendMotorControl.cpp`
  - `src/core/cctv/BackendMotorControlService.cpp`

### 3-3. 이벤트 알림 및 비상 제어
- MQTT 이벤트 알림 연동
  - `MQTT_EVENT_ALERT_TOPIC`(기본 `system/event`)을 구독해 이벤트 메시지를 수신
  - 이벤트 payload는 JSON 우선 처리
    - 지원 키: `source`, `severity`, `title`, `message`, `autoControl`
    - `control.*` 필드는 하위 호환용으로 파싱은 유지하지만, 현재 수동 적용 흐름은 고정 비상 제어 시퀀스를 사용
  - JSON이 아니어도 raw 문자열을 이벤트 메시지로 표시
- 서버 이벤트 이력 연동
  - 로그인 성공 후 `GET /events?limit=50`를 호출해 서버에 저장된 최근 이벤트 로그를 불러옴
  - Qt를 재실행해도 로그인 후 이전 이벤트 목록을 다시 복원할 수 있음
  - 로그아웃 시 클라이언트에 캐시된 이벤트 목록/현재 이벤트 상태를 초기화
- 헤더 이벤트 알림 버튼
  - 로그인 상태에서만 노출
  - 읽지 않은 이벤트가 있으면 빨간 점 배지 표시
  - 읽지 않은 이벤트가 있으면 버튼 아이콘 강조 + 펄스 애니메이션으로 가시성 강화
  - 클릭 시 이벤트 알림 다이얼로그 오픈 + 읽음 처리
- 이벤트 알림 다이얼로그
  - 가로형 2열 레이아웃으로 구성
    - 좌측: 이벤트 히스토리 목록 또는 선택 로그 상세
    - 우측: 최신 이벤트 카드
  - 이벤트 목록은 수신 시각 기준으로 최근 항목이 위에 표시
  - 초기 목록은 서버 `/events` 조회 결과로 채워지고, 이후 실시간 MQTT 이벤트가 맨 위에 추가됨
  - 목록 항목 더블클릭 시 같은 영역에서 상세 로그 보기로 전환
  - 최신 이벤트 카드는 `제목`, `수신 시각`, `출처`, `Severity`, `AUTO ON/OFF`, 메시지를 표시
  - 이벤트가 없을 때는 빈 상태 카드로 유지
  - 하단에는 상태 바를 두어 최근 적용/초기화 결과를 표시
- 이벤트 대응 버튼
  - `현재 이벤트 적용`: 레이저 ON 요청 후 `POST /motor/emergency` 호출
  - `비상 동작 정지`: `POST /motor/control/stopall` 호출
  - `이벤트 초기화`, `닫기` 버튼 제공
- 자동제어 동작
  - `autoControl=true` 이벤트를 받으면 현재 이벤트 적용과 같은 비상 제어 흐름을 즉시 요청
  - 제어 흐름:
    - `laserSetEnabled(true)`
    - `motorEmergency()`
- 관련 구현:
  - `src/ui/qml/common/Header.qml`
  - `src/ui/qml/dialogs/EventAlertDialog.qml`
  - `src/ui/qml/Main.qml`
  - `src/core/core/BackendCoreEvent.cpp`
  - `src/core/core/BackendCoreEventService.cpp`
  - `src/core/core/BackendCoreEventLogService.cpp`
  - `src/core/cctv/BackendMotorControl.cpp`
  - `src/core/cctv/BackendMotorControlService.cpp`
  - `src/core/core/BackendCoreMqttService.cpp`

### 4. Thermal 모니터링
- 상단 탭의 `Thermal` 진입 시 열화상 스트림 시작, 이탈 시 자동 중지
- 전송 경로 선택 지원: `THERMAL_TRANSPORT=ws|mqtt` (기본 `ws`)
- `ws` 모드: `POST /thermal/control/start` -> `ws(s)://<API_HOST>/thermal/stream` 수신 -> `POST /thermal/control/stop`
- `mqtt` 모드: MQTT 청크 토픽 구독 후 프레임 재조립
- 팔레트(`Jet`/`Gray`/`Iron`) 및 Auto/Manual range 제어 지원
- 관련 구현: `src/core/thermal/BackendThermal.cpp`, `src/ui/qml/thermal/ThermalViewer.qml`

### 4-1. Thermal 화면 UI 구성
- Thermal 탭 진입 시 우측 패널은 `System Metrics` 대신 `Thermal Panel`로 전환
- `Return to Live View` 버튼 제거
- `Start/Stop` 버튼을 `Thermal Controls` 헤더 우측으로 이동
- Thermal Controls 패널 내부 간격/정렬을 재조정해 세로 공간을 균등 사용
- Palette 콤보박스/Auto Range 토글/슬라이더 스타일을 다크 테마 기준으로 정리
- 하단 상태 표시를 단일 라인으로 유지하고, 중복 대형 에러 텍스트 노출 제거

### 5. CCTV 3D Map (1차: API + WS 수신/로컬 렌더)
- Camera Controls에서 `줌- + 오토포커스` 준비 버튼, `3D Map 모드 ON/OFF` 버튼으로 시작/중지
- 3D Map 전용 새 창(프레임리스, UI 통일)에서 수신 프레임 표시 및 드래그 회전
- 3D Map 창/사이드바에서 `일시정지`/`재개` 버튼 지원
  - `POST /cctv/control/pause`
  - `POST /cctv/control/resume`
- ON 시 시퀀스:
  - 줌 아웃 -> 줌 정지 -> 오토포커스 -> `POST /cctv/control/start` -> `POST /cctv/control/stream`(`rgbd_stream`) -> `wss://<API_HOST>/cctv/stream`
- OFF 시 즉시 중지:
  - `POST /cctv/control/stop` 호출 + WS 종료
- 현재 단계(1차):
  - WS 바이너리 프레임을 수신해 클라이언트에서 RGBD 포인트클라우드(와이어프레임 오버레이) 렌더링
  - 드래그 회전은 서버 view API 호출 없이 로컬 재렌더링
  - OpenCL 기반 고성능 렌더링 파이프라인은 후속 단계
- 관련 구현:
  - `src/core/cctv/BackendCctv3dMap.cpp`
  - `src/core/cctv/BackendCctv3dMapService.cpp`
  - `src/ui/qml/sidebar/SidebarCameraControlsPanel.qml`
  - `src/ui/qml/Main.qml` (3D Map 전용 창)

### 6. 인증 (Login / Sign Up)
- 로그인 화면에서 `Sign In`/`Sign Up` 모드 전환 지원
- Sign Up 전용 입력 폼(`ID`, `Email`, `Password`) 제공
- 회원가입 전 이메일 인증 플로우 지원
  - `인증` 버튼으로 이메일 인증 코드 요청
  - 6자리 인증 코드 입력 후 `확인`으로 이메일 인증 완료
  - 이메일 인증이 완료된 `ID + Email` 조합에서만 회원가입 진행 가능
- 회원가입 시 클라이언트에서 비밀번호 복잡도 규칙 사전 검증
  - 8~16자
  - 숫자 1개 이상
  - 특수문자 1개 이상
  - 공백 불가
- 빈 입력값 클라이언트 검증
- 이메일 형식 클라이언트 검증
- `POST /register` 성공 시 로그인 화면으로 자동 복귀 및 입력값 초기화
- `Back to Sign In` 클릭 시 회원가입 입력값 초기화
- 로그인 전에는 검색창/화면 안내 툴팁 아이콘 비노출
- `/login` 응답이 `token`이면 즉시 로그인 완료, `pre_auth_token + requires_2fa=true`이면 OTP 입력 단계로 전환
- 로그인 화면에서 OTP 6자리 입력 후 `POST /2fa/verify`로 최종 JWT 발급
- 로그인 잠금 해제는 더 이상 클라이언트 `.env` 값을 직접 비교하지 않고 `POST /auth/admin/unlock`로 Crow 서버 검증을 사용
- 로그인 잠금/세션 타이머/SSL 설정은 기존 인증 흐름과 동일하게 유지

### 6-1. 2FA 및 계정 관리
- 우측 상단 프로필 버튼에 현재 로그인한 계정 ID를 표시
- 프로필 메뉴를 열 때마다 `GET /2fa/status`를 호출해 서버 기준 2FA 상태를 동기화
- 2FA 미사용 계정은 `OTP 생성`만, 사용 중인 계정은 `OTP 삭제`만 노출
- `OTP 생성` 다이얼로그
  - `POST /2fa/setup/init`으로 `manual_key`를 수신
  - Authenticator 앱 등록 후 `POST /2fa/setup/confirm`으로 활성화 완료
  - 성공 후 현재 로그인은 유지되고, 메뉴 상태만 다시 동기화
- `OTP 삭제` 다이얼로그
  - 현재 OTP 6자리를 입력받아 `POST /2fa/disable` 호출
  - 성공 후 현재 로그인은 유지되고, 메뉴 상태만 다시 동기화
- `회원탈퇴` 다이얼로그
  - 비밀번호를 다시 입력받아 `POST /account/delete` 호출
  - 2FA 사용 계정이면 OTP 6자리도 함께 요구
  - 성공 시 현재 세션을 정리하고 로그인 화면으로 복귀
- 관련 구현:
  - `src/ui/qml/common/LoginScreen.qml`
  - `src/ui/qml/common/Header.qml`
  - `src/ui/qml/dialogs/TwoFactorDialog.qml`
  - `src/ui/qml/dialogs/AccountDeleteDialog.qml`
  - `src/core/auth/BackendAuthRequestService.cpp`

### 6-2. 로그인 인터페이스 개선
- 비밀번호 입력칸 우측에 표시/숨김 토글(눈 아이콘) 추가
  - 숨김 상태는 슬래시 오버레이(가리기 표시)로 시각화
- 비밀번호 입력칸 아래에 클릭형 텍스트 `비밀번호를 잊으셨나요?` 제공
- `비밀번호를 잊으셨나요?` 클릭 시 비밀번호 재설정 1단계 다이얼로그 오픈
  - `ID`, `Email` 입력 후 `POST /auth/password/forgot` 호출
  - 입력한 정보가 일치하면 메일로 재설정 코드가 전송되는 구조로 안내
- 비밀번호 재설정 2단계 다이얼로그 제공
  - 메일로 받은 재설정 코드와 새 비밀번호 입력
  - `POST /auth/password/reset` 호출로 비밀번호 변경
  - 새 비밀번호 입력칸 표시/숨김(눈 아이콘) 토글 지원
- ID/Password 입력 필드 크기/정렬을 동일 기준으로 맞춤
- 회원가입 화면도 로그인 화면과 동일한 기준 폭으로 정렬
- 회원가입 이메일 인증 버튼/인증코드 확인 버튼은 입력칸 바깥 오른쪽에 배치
- 회원가입 화면에 비밀번호 규칙 안내 문구(2줄) 표시
- 입력 중 `CAPS LOCK ON` 상태를 실시간 표시
  - Windows API 기반 실제 키보드 상태 조회 사용
- `Clear`, `Back to Sign In` 버튼 텍스트 대비를 높여 가독성 개선

### 6-3. 로그인 전 비밀번호 재설정
- 비밀번호 찾기 1단계 다이얼로그
  - 입력 항목: `ID`, `Email`
  - 클라이언트에서 이메일 형식 검증 수행
  - 성공 시 안내 문구와 함께 재설정 코드 입력 단계로 이동
- 비밀번호 재설정 2단계 다이얼로그
  - 입력 항목: `재설정 코드`, `새 비밀번호`
  - 새 비밀번호 복잡도 규칙을 클라이언트에서 사전 검증
  - 성공 시 다이얼로그 종료 후 완료 메시지 표시
- 관련 구현:
  - `src/ui/qml/dialogs/PasswordForgotDialog.qml`
  - `src/ui/qml/dialogs/PasswordResetDialog.qml`
  - `src/core/auth/BackendAuthRecovery.cpp`
  - `src/core/auth/BackendAuthRequestService.cpp`

### 6-4. 로그인 후 비밀번호 변경
- 우측 상단 프로필 메뉴에서 `비밀번호 변경` 버튼으로 다이얼로그 오픈
- 다이얼로그 입력 항목
  - `현재 비밀번호`, `새 비밀번호`
  - 각 입력칸 표시/숨김(눈 아이콘) 토글 지원
- 클라이언트 사전 검증
  - 현재/새 비밀번호 빈 값 차단
  - 현재/새 비밀번호 동일 입력 차단
  - 새 비밀번호 복잡도 검증(8~16자, 숫자 1개 이상, 특수문자 1개 이상, 공백 불가)
- 서버 연동
  - `POST /account/password/change`
  - 성공 시 다이얼로그 닫힘 + 성공 상태 표시
  - 실패 시 서버 응답/네트워크 상태에 따라 오류 문구 표시
- 로그아웃/세션 종료 시 비밀번호 변경 진행 요청은 안전하게 중단 처리

### 7. 클라이언트 사양(System Specs) 팝업
- 헤더 좌측 홈 아이콘 클릭 시 클라이언트 사양 다이얼로그 표시
- 홈 아이콘 툴팁 문구: `클라이언트 사양`
- 다이얼로그에서 `Refresh` 버튼 제거(`Close`만 유지)
- 사양 정보는 팝업 호출 시 최초 1회 조회 후 캐시 재사용(반복 조회로 인한 UI 멈춤 완화)

### 8. 프로그램명 및 실행파일 아이콘(Windows)
- 사용자 표시 프로그램명은 `AEGIS Vision VMS`로 통일
- Windows 빌드 출력 실행파일 이름은 `AEGIS.exe`
- 실행파일 아이콘은 `SVG`가 아닌 `ICO` 리소스로 적용
- 아이콘 파일:
  - `src/ui/assets/icons/Hanwha_logo.ico`
  - 원본 벡터/소스는 현재 AEGIS 브랜드 아이콘 기준으로 관리
- 로그인 화면/시작 오버레이의 화면 표시용 로고는 별도 `PNG` 리소스 사용
  - `src/ui/assets/icons/AEGIS_logo.png`
- Windows 리소스 파일:
  - `src/app/app_icon.rc`
- CMake에서 `WIN32` 빌드 시 `app_icon.rc`를 타깃 소스로 포함해 exe 아이콘과 파일 메타데이터를 반영

### 8-1. 창 라운드 처리(Windows)
- 프레임리스 메인 창과 3D Map 보조 창은 직각 창 대신 둥근 모서리로 표시
- 초기 Win32 `SetWindowRgn(CreateRoundRectRgn(...))` 방식은 듀얼 모니터 이동 시 모서리 복원/계단 현상 이슈가 있어 제거
- 현재는 `QML transparent window + OpacityMask` 기반 라운드 마스킹 사용
  - 메인 창: `src/ui/qml/Main.qml`
  - 3D Map 창: `src/ui/qml/Main.qml` 내부 `cctv3dMapDebugWindow`
- 장점
  - 듀얼 모니터 이동 시 region이 풀리며 직각으로 돌아가는 문제 완화
  - Win32 region 방식보다 더 부드러운 모서리 표현
- 빌드 시 `Qt5Compat.GraphicalEffects` 사용을 위해 `Qt::Core5Compat` 링크 필요

### 9. 앱 시작 로딩 오버레이
- 앱 시작 직후 풀스크린 로딩 오버레이(스플래시) 표시
- 고정 시간만으로 종료하지 않고, 카메라 준비 상태를 함께 반영해 종료
  - 종료 조건: `초기 UI 준비` + `최소 표시 시간` + (`4채널 준비 완료` 또는 `최대 대기시간 만료`)
  - 로딩 문구 예시: `카메라 스트림 준비 중... (x/4)`
- 최대 대기시간 만료 시 일부 채널 미준비여도 로그인 화면으로 진행
- 오버레이는 한 번 닫히면 다시 표시되지 않음
- 시작 오버레이도 메인 창과 동일한 라운드 기준을 사용
- 종료 후 기존 로그인 화면 노출(로그인/세션 동작 변경 없음)
- 관련 구현: `src/ui/qml/Main.qml`

## Qt Client Architecture Diagram

```text
                 +----------------------------------------------+
                 |              QML UI Layer                    |
                 | Main.qml + common/view/playback/sidebar/*    |
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
   - Qt 클라이언트에서 수신 RTP를 로컬 UDP(`127.0.0.1:5004`)로 writeDatagram 송출
7. `GET_PARAMETER` keepalive로 세션 유지  
8. `PAUSE/PLAY`로 일시정지/재개 제어

Live와 Playback은 제어 경로가 다릅니다. Playback은 단순 RTSP URL 1회 호출이 아니라 WS + RTSP 제어 시퀀스가 핵심입니다.

## UDP + WebSocket 데이터 경로 요약

- Playback 제어 송신(클라이언트 -> 서버)
  - `BackendStreamingWsService::streamingWsSendHex()`에서 RTSP 텍스트를 Hex Binary로 WebSocket 송신
- Playback 미디어 전달(클라이언트 내부)
  - WebSocket interleaved RTP 수신 후 `BackendPlaybackWsRuntimeService::forwardPlaybackInterleavedRtp()`에서 로컬 UDP 포트로 송출
  - 기본 포트: `127.0.0.1:5004`
- Thermal 스트림
  - `THERMAL_TRANSPORT=ws`이면 Thermal WS 연결로 바이너리 청크 수신
  - `THERMAL_TRANSPORT=mqtt`이면 MQTT thermal topic 수신 경로 사용

## Playback Export 작동 원리

1. 사용자가 Playback 화면에서 내보내기 구간(시작/끝)을 지정  
   - 자동 범위 사용 시 시작 시각 기준 5분을 기본값으로 잡되, 실제 녹화 구간 끝이 더 이르면 종료 시각을 그 구간 끝으로 자동 조정  
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
- Protocol: RTSP/RTSPS, HTTP/HTTPS, WebSocket, UDP

## 환경 설정 (.env)

실행 파일과 같은 경로의 `.env`를 로드합니다.  
기본 템플릿은 `example.env`를 사용하세요.

참고:
- 관리자 잠금 해제 키는 클라이언트 `.env`에서 읽지 않습니다.
- 관리자 잠금 해제는 Crow 서버의 `ADMIN_UNLOCK_KEY`(배포 시 `crow-admin-secret`에서 주입) 기준으로 검증됩니다.

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
- Thermal 전송 경로
  - `THERMAL_TRANSPORT` (`ws` 기본, `mqtt` 선택)
  - `THERMAL_START_PATH` (기본 `/thermal/control/start`)
  - `THERMAL_STOP_PATH` (기본 `/thermal/control/stop`)
  - `THERMAL_WS_PATH` (기본 `/thermal/stream`)

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
| `POST` | `/auth/email/verify/request` | 회원가입 이메일 인증 코드 요청 |
| `POST` | `/auth/email/verify/confirm` | 회원가입 이메일 인증 코드 확인 |
| `POST` | `/auth/password/forgot` | 로그인 전 비밀번호 재설정 코드 요청 |
| `POST` | `/auth/password/reset` | 로그인 전 재설정 코드로 비밀번호 변경 |
| `POST` | `/auth/admin/unlock` | 로그인 잠금 상태에서 관리자 해제 키 검증 |
| `POST` | `/2fa/verify` | 비밀번호 로그인 후 OTP 검증 및 최종 JWT 발급 |
| `GET` | `/2fa/status` | 현재 로그인 사용자 2FA 상태 조회 |
| `GET` | `/events` | 로그인 후 서버 이벤트 로그 목록 조회 (`limit` 지원) |
| `DELETE` | `/events` | 이벤트 로그 전체 삭제 또는 `id` 지정 단건 삭제 |
| `POST` | `/2fa/setup/init` | OTP 등록용 `manual_key`/`otpauth_url` 발급 |
| `POST` | `/2fa/setup/confirm` | OTP 등록 완료(첫 OTP 검증) |
| `POST` | `/2fa/disable` | 현재 OTP로 2FA 비활성화 |
| `POST` | `/account/delete` | 비밀번호 재입력 기반 회원탈퇴(2FA 계정은 OTP 추가) |
| `POST` | `/account/password/change` | 로그인 사용자 비밀번호 변경(현재 비밀번호 검증 + 새 비밀번호 정책 검증) |
| `POST` | `/thermal/control/start` | Thermal 스트림 시작 (`THERMAL_TRANSPORT=ws`일 때 사용) |
| `POST` | `/thermal/control/stop` | Thermal 스트림 중지 (`THERMAL_TRANSPORT=ws`일 때 사용) |
| `WS` | `/thermal/stream` | Thermal 바이너리 청크 수신 (`THERMAL_TRANSPORT=ws`) |
| `GET` | `/api/sunapi/storage` | 카메라 SD 저장소 조회 (Crow 프록시) |
| `GET` | `/api/sunapi/timeline` | Playback 타임라인 조회 |
| `GET` | `/api/sunapi/month-days` | Playback 월 단위 녹화일 조회 |
| `POST` | `/api/sunapi/ptz/focus` | PTZ/Focus 제어 (`zoom_*`, `focus_*`, `autofocus`) |
| `GET` | `/api/sunapi/display/settings` | 표시 설정 조회 (대비/밝기/윤곽/컬러) |
| `POST` | `/api/sunapi/display/settings` | 표시 설정 적용 (채널별) |
| `POST` | `/api/sunapi/display/reset` | 표시 설정 초기화 (50/50/12/50) |
| `POST` | `/motor/control/press` | 모터 press 시작 (`motor`, `direction`) |
| `POST` | `/motor/control/release` | 모터 press 해제 (`motor`) |
| `POST` | `/motor/control/stop` | 단일 모터 정지 (`motor`) |
| `POST` | `/motor/control/set` | 단일 모터 각도 설정 (`motor`, `angle`) |
| `POST` | `/motor/control/speed` | 단일 모터 속도 설정 (`motor`, `speed`, 1~10) |
| `POST` | `/motor/control/center` | 전체 모터 동일 각도 센터 정렬 (`angle`, optional) |
| `POST` | `/motor/control/stopall` | 전체 모터 일괄 정지 |
| `POST` | `/motor/emergency` | 비상 대피 유도용 고정 모터 시퀀스 실행 |
| `POST` | `/laser/control/on` | 레이저 켜기 |
| `POST` | `/laser/control/off` | 레이저 끄기 |
| `GET` | `/laser/status` | 레이저 MQTT 브리지 상태 조회 |
| `POST` | `/cctv/control/start` | 3D Map 처리 시작 (channel/mode) |
| `POST` | `/cctv/control/stream` | 3D Map 스트림 모드 요청 (`rgbd_stream`) |
| `POST` | `/cctv/control/pause` | 3D Map 처리 일시정지 |
| `POST` | `/cctv/control/resume` | 3D Map 처리 재개 |
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
- Windows 실행파일 이름은 `AEGIS.exe`이며, 아이콘/파일 메타데이터는 `src/app/app_icon.rc` + `src/ui/assets/icons/Hanwha_logo.ico` 조합으로 설정됩니다.

## Qt -> Crow API 전환 현황 (한글)

- 적용 완료
  - Playback WS 준비: Qt direct challenge/digest 호출 -> Crow `/api/sunapi/playback/session`
  - Export WS 준비: Qt direct challenge/digest 호출 -> Crow `/api/sunapi/export/session`
  - PTZ/Focus: Qt direct CGI -> Crow `/api/sunapi/ptz/focus`
  - 표시 설정(대비/밝기/윤곽/컬러): Qt direct CGI -> Crow `/api/sunapi/display/*`
  - 관리자 잠금 해제: Qt local `.env` 비교 -> Crow `/auth/admin/unlock`
  - H/W Control 레이저 제어: Qt -> Crow `/laser/control/on`, `/laser/control/off`, `/laser/status`
  - 3D Map 1차: Qt ON/OFF/Pause/Resume -> Crow `/cctv/control/*` + `/cctv/stream` WS 수신 + 클라이언트 로컬 렌더
  - Export HTTP(create/status/download): Qt direct CGI -> Crow `/api/sunapi/export/*`
  - Error 608 장비에서 기본 경로를 WS export로 우선 전환 (`PLAYBACK_EXPORT_USE_FFMPEG_BACKUP=0`)

- 현재 상태
  - Qt는 Playback/Export 준비 + PTZ/Focus + Export HTTP에서 Crow API를 사용합니다.
- Qt는 카메라 계정(`SUNAPI_USER/SUNAPI_PASSWORD`)을 더 이상 사용하지 않습니다.
- Qt는 관리자 잠금 해제 키(`ADMIN_UNLOCK_KEY`)를 더 이상 로컬 `.env`에서 사용하지 않습니다.

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
│     ├─ Backend.h                         # QML 공개 퍼사드(API 계약)
│     └─ internal/
│        ├─ auth/
│        ├─ cctv/
│        ├─ core/
│        ├─ media/
│        ├─ rtsp/
│        ├─ stream/
│        ├─ sunapi/
│        └─ thermal/
├─ src/
│  ├─ app/
│  │  └─ main.cpp
│  ├─ core/
│  │  ├─ auth/
│  │  ├─ cctv/
│  │  ├─ core/
│  │  ├─ media/
│  │  ├─ rtsp/
│  │  ├─ stream/
│  │  ├─ sunapi/
│  │  └─ thermal/
│  └─ ui/
│     └─ qml/
│        ├─ common/
│        ├─ view/
│        ├─ playback/
│        ├─ thermal/
│        ├─ sidebar/
│        ├─ dialogs/
│        ├─ components/
│        └─ Main.qml
├─ .editorconfig
├─ .env
├─ .gitignore
├─ CMakeLists.txt
├─ example.env
└─ README.md
```

## 헤더 리팩토링(friend 제거) 요약

`Backend`를 QML용 퍼사드(API 계약)로 유지하고 내부 구현을 서비스 계층으로 분리했습니다.

- `include/core/Backend.h`
  - `Q_PROPERTY`, `Q_INVOKABLE`, `signals` 중심 공개 인터페이스 유지
- 내부 상태/헬퍼
  - `include/core/internal/core/Backend_p.h`로 이동
- 도메인별 서비스 헤더
  - `include/core/internal/<domain>/*Service.h`
- 구현 파일은 도메인 폴더로 정리
  - `src/core/<domain>/*.cpp`
- `friend` 선언 제거
  - 서비스 클래스는 `Backend`의 서비스용 헬퍼 API를 통해 동작
  - 클래스 간 결합도를 낮추고 접근 경로를 명시적으로 정리
- 공개 인터페이스와 내부 구현의 의존성 경계 명확화
  - 빌드 영향 범위 축소
  - 헤더 include 충돌 감소

## QML 디렉토리 개편 요약

동작 변경 없이 기능 기준으로 폴더를 재구성했습니다.

- 진입점
  - `src/ui/qml/Main.qml`
- 공통
  - `src/ui/qml/common/*`
- 화면 콘텐츠
  - `src/ui/qml/view/*`
  - `src/ui/qml/playback/*`
  - `src/ui/qml/thermal/*`
- 사이드바
  - `src/ui/qml/sidebar/*`
- 다이얼로그
  - `src/ui/qml/dialogs/*`
- 재사용 컴포넌트
  - `src/ui/qml/components/*`

참고:
- QML 파일 이동에 맞춰 `CMakeLists.txt`의 `qt_add_qml_module(... QML_FILES ...)` 경로를 함께 갱신했습니다.
- 폴더 간 참조는 상대 import 경로로 정리했습니다.

## 빌드

필수:
1. Qt 6 SDK (Qt Quick, Qt Multimedia, Qt Network, Qt WebSockets, Qt5Compat/Core5Compat)
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

- 한글 문자열 깨짐(소스/로그)
  - 저장소 규칙: `.gitattributes` + `.editorconfig` 기준 UTF-8 유지
  - 에디터 권장: UTF-8 고정(`autoGuessEncoding` 비활성)
  - Git 권장 설정: `core.autocrlf=input`, `core.eol=lf`
  - 문자열 리터럴 수정 후 빌드 에러 발생 시 따옴표/이스케이프(`\"`, `\n`) 먼저 확인

- Windows 실행 중 아이콘이 기본 아이콘으로 보일 때
  - 아이콘 리소스(`.ico`, `.rc`, CMake`) 변경 후에는 `Clean`만으로 반영이 안 될 수 있음
  - `build/Desktop_Qt_6_10_2_MinGW_64_bit-Release` 폴더 삭제 후 재빌드 권장
  - 작업표시줄 고정 아이콘 사용 중이면 고정 해제 후 재실행/재고정 필요

- 프레임리스 창 모서리가 직각/계단처럼 보일 때
  - 현재 라운드 처리는 Win32 region이 아닌 `QML OpacityMask` 기반
  - `Qt::Core5Compat` 또는 관련 QML 모듈이 배포 누락되면 창 외곽 표시가 기대와 다를 수 있음
  - `windeployqt` 재실행 후 `Qt5Compat.GraphicalEffects` 관련 QML 리소스 포함 여부 확인

- 관리자 잠금 해제가 항상 실패할 때
  - 클라이언트 `.env`가 아니라 Crow 서버의 `ADMIN_UNLOCK_KEY`를 사용합니다
  - `crow-admin-secret`가 정상 생성되었는지, `crow-server.yaml`의 `secretKeyRef`가 맞는지 확인 필요
  - 직접 API 확인 시 `POST /auth/admin/unlock` 응답 코드(`200/403/500`)를 먼저 점검

## License

교육/프로젝트 목적 샘플 코드입니다.


