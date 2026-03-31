# VEDA Final Project

VEDA는 CCTV 관제, 열화상 모니터링, 원격 모터/레이저 제어를 하나의 시스템으로 묶은 통합 프로젝트입니다. 이 저장소는 Qt 클라이언트, Raspberry Pi 기반 백엔드, 딥러닝 CCTV 서버, 하드웨어 펌웨어, 배포 문서를 함께 관리하는 모노레포입니다.

루트 `README.md`는 저장소 전체를 빠르게 이해하기 위한 허브 문서입니다. 실제 빌드, 배포, 설정, 운영 절차는 각 하위 디렉터리의 README를 기준으로 확인하는 것을 권장합니다.

## 프로젝트 개요

이 저장소는 아래 네 축으로 구성됩니다.

- `Qt_Client/`: Windows용 Qt 6 기반 관제 클라이언트
- `RaspberryPi/`: Raspberry Pi K3s 클러스터 기반 백엔드 및 배포 자산
- `CCTV/`: TensorRT/CUDA 기반 CCTV Depth Server
- `Hardware/`: ESP32, STM32, Teensy 기반 열화상/제어 하드웨어 펌웨어

시스템을 큰 흐름으로 보면 아래와 같습니다.

```text
[ Qt Client ]
     |
     | HTTPS / WSS / RTSPS / MQTT
     v
[ Raspberry Pi K3s Backend ]
  - Crow Server
  - Nginx Gateway
  - MediaMTX
  - Mosquitto
  - MariaDB
  - Thermal DTLS Gateway
     |
     +--> [ CCTV Depth Server ]
     |
     \--> [ Thermal / Motor Hardware ]
           - ESP32-C5
           - STM32F401RE
           - Teensy 4.1
```

## 어디서부터 읽으면 좋은가

관심 있는 역할에 따라 시작점을 나누는 것이 가장 빠릅니다.

| 목적 | 먼저 읽을 문서 |
| --- | --- |
| 저장소 전체 구조 파악 | [README.md](README.md) |
| Qt 클라이언트 개발 | [Qt_Client/README.md](Qt_Client/README.md) |
| Raspberry Pi 서버/배포 | [RaspberryPi/README.md](RaspberryPi/README.md) |
| CCTV 추론 서버 개발 | [CCTV/README.md](CCTV/README.md) |
| 열화상/모터 하드웨어 개발 | [Hardware/README.md](Hardware/README.md) |
| CI/CD 파이프라인 이해 | [docs/jenkins/README.md](docs/jenkins/README.md) |
| 2FA 흐름 이해 | [docs/2fa_qr_setup_flow.md](docs/2fa_qr_setup_flow.md) |
| 열화상 이벤트 알고리즘 이해 | [docs/thermal_event_algorithm_current.md](docs/thermal_event_algorithm_current.md) |

## 디렉터리 구조

| 경로 | 역할 |
| --- | --- |
| `Qt_Client/` | AEGIS Vision VMS Qt 애플리케이션. Live, Playback, Thermal, 3D Map, 인증/2FA UI 포함 |
| `RaspberryPi/` | K3s 클러스터 운영 허브. Crow API, MediaMTX, Mosquitto, MariaDB, Nginx, Thermal Gateway 배포 문서 포함 |
| `CCTV/` | RTSP 입력을 받아 depth 추론과 스트리밍을 수행하는 Windows 서버 |
| `Hardware/` | ESP32, STM32, Teensy 펌웨어와 하드웨어 통신 구조 문서 |
| `docs/` | Jenkins, 2FA, 열화상 이벤트 알고리즘 등 보조 문서 |
| `Jenkinsfile` | 저장소 전체 CI/CD 파이프라인 정의 |
| `Dockerfile.base` | 일부 빌드/배포용 공통 베이스 이미지 정의 |

## 하위 시스템 요약

### 1. Qt Client

- Qt 6 C++/QML 기반 Windows 관제 애플리케이션
- Live 뷰, Playback, Thermal 모니터링, 3D Map, 이벤트 알림, 계정 관리, 2FA 지원
- Crow API, MediaMTX, SUNAPI, MQTT 연동

문서: [Qt_Client/README.md](Qt_Client/README.md)

### 2. Raspberry Pi Backend

- Raspberry Pi K3s 환경에 백엔드 서비스를 배포
- Crow Server가 인증, 2FA, 이벤트 로그, CCTV/열화상/모터/레이저 제어를 통합 제공
- MediaMTX, Mosquitto, MariaDB, Nginx, Thermal DTLS Gateway가 함께 동작

문서: [RaspberryPi/README.md](RaspberryPi/README.md)

### 3. CCTV Depth Server

- Windows 전용 TensorRT/CUDA 기반 추론 서버
- RTSP CCTV 영상을 받아 depth 추론 후 TCP 제어 및 스트리밍 인터페이스 제공
- 3D Map용 RGBD 스트림 연동

문서: [CCTV/README.md](CCTV/README.md)

### 4. Hardware

- ESP32-C5: MQTT/TLS 및 UDP 브리지
- STM32F401RE: 모터/레이저 제어
- Teensy 4.1: FLIR Lepton 열화상 프레임 캡처
- 제어 평면과 데이터 평면을 분리한 하드웨어 구조

문서: [Hardware/README.md](Hardware/README.md)

## 개발 전 확인하면 좋은 것

이 저장소는 하나의 명령으로 전체를 빌드하는 구조가 아닙니다. 작업 대상에 따라 필요한 환경이 다릅니다.

- Qt 클라이언트: Qt 6, CMake, MinGW 또는 팀에서 사용하는 Qt Kit
- Raspberry Pi 백엔드: K3s, Docker, `kubectl`, Secret/인증서 준비
- CCTV 서버: Windows, CUDA, TensorRT, OpenCV, CMake
- 하드웨어: ESP-IDF, STM32CubeIDE, Zephyr/`west`

세부 요구사항과 실행 절차는 반드시 하위 README를 기준으로 확인하세요.

## 문서 작성 원칙

루트 README에는 아래 내용이 들어가는 것이 좋습니다.

- 프로젝트가 무엇인지 한 문장으로 설명하는 소개
- 저장소가 어떤 하위 시스템으로 나뉘는지에 대한 개요
- 역할별로 어디 문서부터 읽어야 하는지에 대한 길잡이
- 주요 디렉터리와 책임 범위
- 팀이 함께 쓰는 브랜치/커밋/머지 규칙

반대로, 아래 내용은 루트보다 하위 README로 내리는 편이 좋습니다.

- 환경별 상세 설치 절차
- 서비스별 모든 API 설명
- 하드웨어 핀맵, 인증서 생성 절차, 배포 명령 전체
- 특정 모듈 내부 구조에 대한 긴 설명

## Git 규칙

### 1. Branch

| 브랜치 | 용도 |
| --- | --- |
| `main` | 성공적으로 배포된 안정 버전을 보관하는 브랜치 |
| `develop` | 기능 통합과 테스트 배포가 진행되는 기본 개발 브랜치 |
| `feature/*` | 기능 개발용 브랜치 |
| `hotfix/*` | 운영 중 발생한 긴급 수정용 브랜치 |
| `release/*` | 배포 직전 검증 및 릴리즈 정리용 브랜치 |

예시:

- `feature/connect-mqtt`
- `feature/S12P21A705-5`
- `hotfix/db-fix`
- `hotfix/S12P21A705-13`

### 2. Commit

기본 형식:

```text
[head] commit-message
```

| Head | 설명 |
| --- | --- |
| `[feat]` | 새로운 기능 |
| `[fix]` | 버그 수정 |
| `[test]` | 테스트 |
| `[docs]` | 문서 추가 및 수정 |
| `[add]` | 파일 추가 |
| `[move]` | 파일 이동 |
| `[remove]` | 파일 삭제 |
| `[style]` | 코드 스타일 수정 |
| `[perf]` | 성능 개선 |

### 3. Merge

- `feature/*` -> `develop`: 기능 개발 완료 후 머지합니다. 최소 1명 이상의 리뷰 승인을 권장합니다.
- `hotfix/*` -> `develop`: 긴급 수정 사항을 반영할 때 사용합니다.
- `release/*` -> `main`: 배포가 성공적으로 완료된 뒤 아카이브 목적의 릴리즈 반영에 사용합니다.

## 관련 문서

- [Qt_Client/README.md](Qt_Client/README.md)
- [RaspberryPi/README.md](RaspberryPi/README.md)
- [RaspberryPi/k3s-cluster/README.md](RaspberryPi/k3s-cluster/README.md)
- [CCTV/README.md](CCTV/README.md)
- [Hardware/README.md](Hardware/README.md)
- [docs/jenkins/README.md](docs/jenkins/README.md)
- [docs/2fa_qr_setup_flow.md](docs/2fa_qr_setup_flow.md)
- [docs/thermal_event_algorithm_current.md](docs/thermal_event_algorithm_current.md)

