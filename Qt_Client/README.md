# Qt CCTV Client (Live Streaming & Recording Manager)

이 프로젝트는 **Qt 6 (C++)**와 **libVLC**를 활용하여 개발된 CCTV 관제 클라이언트 프로그램입니다.
현대적인 **Dark Theme** UI를 적용하였으며, 실시간 RTSP 스트리밍(4채널 멀티뷰)과 REST API를 통한 녹화 영상 관리(재생, 목록 조회, 삭제) 기능을 제공합니다.

## Key Features (주요 기능)

### 1. Modern UI & UX
- **Vision VMS 디자인 포팅:** React 기반의 모던한 CCTV 관제 UI를 Qt Widgets으로 완벽하게 이식.
- **Dark/Light 테마 전환:** 헤더의 토글 버튼으로 실시간 테마 변경 지원 (Windows 타이틀바 색상 연동).
- **반응형 레이아웃:** 사이드바, 헤더, 메인 콘텐츠 영역이 유기적으로 동작.

### 2. Live View (실시간 라이브 뷰)
- **4채널 그리드 뷰:** 4개의 CCTV 화면을 동시에 모니터링 (2x2 Grid).
- **고성능 재생 엔진:** `libVLC`를 사용하여 지연 시간(Latency) 최소화 및 안정적인 재생.
- **RTSP over TCP 강제:** 패킷 손실 및 UDP 차단 환경 대응을 위해 TCP 전송 방식 강제 적용 (`--rtsp-tcp`).
- **실시간 상태 모니터링:** 
  - 각 채널별 연결 상태 (LIVE, BUFFERING, ERROR, OFFLINE) 표시.
  - 시스템 전체 FPS 및 활성 카메라 수 실시간 집계.

### 3. Recordings (녹화 영상 관리)
- **사용자 인증:** MariaDB 연동 로그인 시스템 (ID/PW).
- **영상 목록 조회:** 서버(`/app/recordings`)에 저장된 녹화 파일 리스트 및 용량 확인.
- **영상 재생 (Download & Play):** 
  - **끊김 없는 재생:** 녹화 파일을 임시 폴더로 다운로드 후 재생하여 버퍼링 문제 해결.
  - **정확한 탐색:** 전체 재생 시간 인식 및 즉각적인 탐색(Seek) 지원.
- **영상 삭제:** 권한이 있는 사용자의 녹화 파일 삭제 기능.

### 4. System Integration
- **Server Storage Monitoring:** Crow 서버 API(`/system/storage`)를 통해 원격 서버의 디스크 사용량 실시간 표시.

---

## Tech Stack (기술 스택)

- **Language:** C++17
- **Framework:** Qt 6 (Qt Multimedia, Qt Network, Qt Widgets)
- **Video Engine:** libVLC (VLC SDK)
- **Backend Server:** Crow (C++ Microframework)
- **Database:** MariaDB (Authentication)
- **Protocol:** RTSP (Live), HTTP (API & File Transfer)

---

## Configuration (환경 설정)

이 프로그램은 하드코딩된 설정을 피하기 위해 실행 파일과 같은 경로에 있는 `.env` 파일을 로드합니다.
프로그램 실행 전, 실행 파일과 같은 경로에 아래 내용으로 `.env` 파일을 생성해주세요.

**파일명:** `.env`

```ini
# 백엔드 API 서버 주소 (로그인 및 녹화 파일 관리)
API_URL=http://192.168.55.xxx:8080

# RTSP 스트리밍 서버 주소 (MediaMTX 프록시 서버)
RTSP_IP=192.168.55.xxx
RTSP_PORT=8554

# [주의] .env 파일이 없으면 프로그램이 정상적으로 서버에 접속할 수 없습니다.
```

---

## API Requirements (API 규격)

이 클라이언트가 정상 작동하기 위해서는 백엔드 서버가 다음 API를 제공해야 합니다.

| Method | Endpoint | Description | Request Body |
| :--- | :--- | :--- | :--- |
| `POST` | `/login` | 사용자 로그인 | `{"id": "...", "password": "..."}` |
| `GET` | `/recordings` | 녹화 목록 조회 | `N/A` |
| `DELETE` | `/recordings?file={name}` | 특정 녹화 파일 삭제 | `N/A` |
| `GET` | `/stream?file={name}` | 녹화 파일 다운로드/스트리밍 | `N/A` |
| `GET` | `/system/storage` | 서버 디스크 용량 조회 | `N/A` |

---

## Build & Run (빌드 및 실행 방법)

### Prerequisites (필수 요구 사항)
1.  **Qt 6 SDK** 설치 (Qt Multimedia, Qt Network 모듈 포함)
2.  **VLC Media Player** 설치 (실행 시 라이브러리 필요)
3.  **libVLC SDK** (헤더 파일 및 라이브러리 - `vlc/vlc.h`)

### Windows Configuration (DLL 배치)
빌드 후 실행 파일(`.exe`)이 있는 폴더에 다음 파일들이 있어야 합니다.
1.  `libvlc.dll`
2.  `libvlccore.dll`
3.  `plugins/` 폴더 (VLC 설치 경로에서 복사)

### RTSP URL Rule (주소 규칙)
MediaMTX 서버를 경유하기 위해 코드는 다음 규칙으로 RTSP 주소를 생성합니다.
`rtsp://{RTSP_IP}:{RTSP_PORT}/{Channel_Number}`
(예: `rtsp://192.168.55.xxx:8554/0`)

---

## Troubleshooting (트러블슈팅)

**Q. "VLC 인스턴스 생성 실패! DLL 파일을 확인하세요." 메시지가 뜹니다.**
> A. 실행 파일 경로에 `libvlc.dll`, `libvlccore.dll` 및 `plugins` 폴더가 올바르게 복사되었는지 확인하세요.

**Q. 라이브 뷰가 "BUFFERING"이나 "ERROR" 상태에서 멈춥니다.**
> A. `.env` 파일의 `RTSP_IP`와 `RTSP_PORT`가 정확한지 확인하세요. 방화벽이 8554 포트를 차단하고 있는지 확인하세요.

**Q. 녹화 영상 재생 시 "Player Error"가 발생합니다.**
> A. 서버(`API_URL`)가 정상 작동 중인지, 디스크 용량이 부족하여 다운로드에 실패한 것은 아닌지 확인하세요.

---

## License

This project is for educational purposes.
