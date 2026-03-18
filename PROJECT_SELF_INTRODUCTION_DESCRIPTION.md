# VEDA Project Description for Self-Introduction

## 문서 목적

이 문서는 `VEDA_Final_Project`를 자기소개서, 포트폴리오, 면접 답변에 활용할 수 있도록 프로젝트 설명을 정리한 문서이다.
단순 기능 나열이 아니라, 프로젝트가 해결하려는 문제, 시스템 구조, 핵심 기술, 어필 포인트를 중심으로 정리하였다.

자기소개서에 활용할 때는 아래 내용을 그대로 복사하기보다, 본인이 실제로 기여한 영역에 맞게 일부 문장을 선택하고 수정하는 것을 권장한다.

## 프로젝트 한 줄 소개

VEDA는 `Qt 기반 관제 클라이언트`, `Crow 기반 백엔드 서버`, `Raspberry Pi K3s 클러스터`, `MQTT/RTSP/WebSocket 기반 하드웨어 연동`을 통합해 실시간 CCTV 관제, 영상 재생, 장비 제어, 열화상 모니터링, 보안 인증을 제공하는 엣지 기반 통합 관제 시스템이다.

## 프로젝트 상세 설명

이 프로젝트는 단순히 CCTV 영상을 보여주는 수준을 넘어, 여러 종류의 장비와 프로토콜을 하나의 운영 흐름으로 묶는 것을 목표로 한다.

사용자는 Qt 클라이언트에서 실시간 라이브 영상을 모니터링하고, 과거 녹화 영상을 검색 및 재생하며, 필요한 구간을 export할 수 있다.
또한 카메라의 PTZ 및 표시 설정을 제어하고, 3D Map 기반 CCTV 데이터 시각화 기능과 열화상 스트림 확인 기능도 함께 사용할 수 있다.

백엔드 측에서는 Crow 서버가 인증, 사용자 관리, 카메라 SUNAPI 프록시, CCTV 백엔드 제어, MQTT 기반 모터 제어, ESP32 watchdog 상태 수집을 담당한다.
이 서버는 Raspberry Pi 기반 K3s 클러스터 위에 배포되며, MediaMTX는 RTSP/RTSPS/HLS/WebRTC 스트리밍과 녹화를 담당하고, Mosquitto는 장치 제어용 MQTT 브로커 역할을 수행한다.
Nginx 게이트웨이는 HTTPS, RTSPS, MQTTS의 외부 진입점으로 동작하며, mTLS 기반 장치 인증도 처리한다.

즉, 이 프로젝트는 `프론트엔드 UI`, `백엔드 API`, `스트리밍 서버`, `메시지 브로커`, `DB`, `엣지 클러스터`, `임베디드 장치`가 하나의 서비스 목적 아래 통합된 시스템이다.

## 프로젝트가 해결하려는 문제

- 여러 CCTV 카메라와 장치를 한 화면에서 통합적으로 관제할 수 있어야 한다.
- 실시간 스트리밍뿐 아니라 과거 녹화 영상 조회와 export까지 지원해야 한다.
- 장비 제어와 상태 수집이 네트워크 환경과 하드웨어 제약 속에서도 안정적으로 동작해야 한다.
- 외부 접속 환경에서는 단순 API 호출을 넘어서 HTTPS, JWT, TOTP 2FA, mTLS 같은 보안 체계가 필요하다.
- 서버, 클라이언트, 하드웨어가 각기 다른 프로토콜을 사용하더라도 운영 관점에서는 하나의 일관된 사용자 경험을 제공해야 한다.

## 전체 시스템 구성

### 1. Qt Client

- `Qt 6 + C++/QML` 기반 데스크톱 관제 애플리케이션
- 라이브 4채널 영상 모니터링
- Playback 타임라인 조회 및 과거 영상 재생
- Export 구간 저장
- CCTV 3D Map 시각화
- Thermal 모니터링
- 로그인, 회원가입, TOTP 2FA, 계정 관리 UI

### 2. Crow Server

- REST API 제공
- JWT 인증 및 사용자 관리
- TOTP 기반 2단계 인증 처리
- SUNAPI HTTP/WebSocket 프록시
- CCTV 백엔드 제어 및 WebSocket 중계
- MQTT 기반 모터 제어 API
- ESP32 watchdog 상태 조회 API
- 녹화 파일 목록 조회, 삭제, 스트리밍 API 제공

### 3. Raspberry Pi K3s Cluster

- Raspberry Pi 4 여러 대를 K3s 클러스터로 구성하여 서비스 분산 배치
- Crow Server, MediaMTX, Mosquitto, MariaDB, Nginx Gateway를 역할별로 배치
- MetalLB를 통해 베어메탈 환경에서도 LoadBalancer IP 제공

### 4. Streaming / Messaging / Security

- MediaMTX: RTSP/RTSPS/HLS/WebRTC 스트리밍 및 녹화
- Mosquitto: MQTT 브로커 및 장치 메시지 중계
- Nginx Gateway: HTTPS, RTSPS, MQTTS 진입점 및 TLS/mTLS 종료
- MariaDB: 사용자 인증 정보 및 서비스 데이터 저장

### 5. Hardware Integration

- ESP32: MQTT 브리지, watchdog 상태 publish, STM32와의 통신, 열화상 데이터 중계
- STM32: UART 기반 모터 명령 파싱 및 서보 제어
- Teensy: 열화상 프레임 캡처 및 ESP32 전달

## 주요 기능 정리

### 1. 실시간 CCTV 라이브 모니터링

- 2x2 그리드 기반 4채널 동시 관제
- MediaMTX를 통한 RTSP/RTSPS 스트리밍 재생
- FPS, latency, 저장소 상태 등 운영 메트릭 표시

### 2. Playback 및 영상 Export

- 날짜와 시간 기준으로 녹화 구간 조회
- SUNAPI 타임라인 정보를 기반으로 재생 구간 시각화
- RTSP over WebSocket 기반 Playback 제어
- 필요한 시간 구간을 export해 저장
- 장비 미지원 시 RTSP backup + ffmpeg fallback 경로 제공

### 3. CCTV 3D Map 기능

- RGBD 스트림을 받아 3D Map 형태로 시각화
- API와 WebSocket을 조합한 스트림 제어
- 로컬 렌더링을 통한 회전 및 시각적 상호작용 지원

### 4. Thermal 모니터링

- MQTT 청크 기반 열화상 프레임 수신
- 팔레트 변경 및 Auto/Manual 범위 조정 지원
- 단순 이미지 출력이 아니라 센서 데이터 기반 시각화 기능 제공

### 5. 모터 제어 및 장치 제어

- MQTT를 이용한 모터 제어 명령 발행
- `press`, `release`, `stop`, `set`, `center`, `stopall` 등 제어 지원
- 서버에서 REST API로 장치 제어를 추상화하고, ESP32와 STM32가 실제 하드웨어를 구동

### 6. 사용자 인증 및 보안

- 회원가입, 로그인, JWT 인증
- TOTP 기반 2단계 인증
- 계정 삭제 시 비밀번호 및 OTP 재확인
- Nginx와 인증서를 활용한 mTLS 기반 장치 인증

### 7. 운영 및 상태 모니터링

- ESP32 watchdog 상태 publish/subscribe 구조
- 서버 health check 및 상태 조회
- 장비 연결 상태와 최근 요청 메타데이터 확인 가능

## 기술 스택

### Application / Client

- C++
- Qt 6
- QML
- Qt Network
- Qt WebSocket
- Qt Multimedia

### Backend / Infra

- Crow C++
- MariaDB
- Nginx
- MediaMTX
- Mosquitto
- K3s
- Docker
- MetalLB

### Protocol / Security

- HTTP / HTTPS
- RTSP / RTSPS
- WebSocket / WSS
- MQTT / MQTTS
- JWT
- TOTP 2FA
- TLS / mTLS

### Embedded / Device

- ESP32
- STM32F401
- Teensy
- UART
- SPI
- I2C

## 프로젝트의 기술적 특징

### 1. 멀티 프로토콜 통합

이 프로젝트의 가장 큰 특징 중 하나는 서로 다른 계층의 프로토콜을 하나의 서비스 흐름으로 연결했다는 점이다.
클라이언트는 HTTP/HTTPS와 WebSocket을 사용하고, 영상 재생은 RTSP/RTSPS와 RTSP-over-WebSocket을 활용하며, 장치 제어는 MQTT를 사용한다.
또한 하드웨어 내부에서는 UART, SPI, I2C 같은 임베디드 통신 방식이 함께 사용된다.

즉, 단순 CRUD 중심 프로젝트가 아니라 `영상`, `장치`, `네트워크`, `보안`을 동시에 다루는 복합 시스템이라는 점이 강점이다.

### 2. 엣지 환경 중심의 시스템 설계

일반적인 클라우드 웹 서비스와 달리, 이 프로젝트는 Raspberry Pi 기반 K3s 클러스터 위에서 서비스가 배포되고 운영된다는 점이 특징이다.
제한된 자원 환경에서 스트리밍, 메시지 브로커, 데이터베이스, API 서버, 게이트웨이까지 함께 운영해야 하므로, 배포 구조와 자원 분산도 중요한 설계 요소가 된다.

### 3. 보안 강화를 고려한 설계

프로젝트에는 단순 로그인 기능뿐 아니라 JWT, TOTP 2FA, 인증서 기반 TLS/mTLS가 함께 적용되어 있다.
이는 사용자 인증뿐 아니라, 외부 장치와 서버 간 연결까지 신뢰 체계를 확장한 구조라는 점에서 의미가 있다.

### 4. 실시간성과 안정성을 동시에 고려

영상 스트리밍, 장비 제어, 열화상 데이터 전송은 모두 지연과 안정성에 민감한 기능이다.
이 프로젝트는 단순 기능 구현보다도, 실제 운영 환경에서 응답성, 타임아웃, 연결 상태, 재시도, 상태 확인 구조를 함께 고려한 설계라는 점을 강조할 수 있다.

### 5. 하드웨어 병목과 운영 한계까지 분석한 프로젝트

ESP32-C3 기반 열화상 전송 경로에서는 MQTT/TLS 처리 여유와 전송 backlog 같은 실제 병목 문제가 드러났고, 이를 통해 단순 구현에 그치지 않고 성능 한계와 대안 보드 선택까지 검토한 흔적이 있다.
이 점은 프로젝트가 단순히 "기능이 된다"에서 끝나지 않고, 운영 가능성과 확장성까지 고민한 사례로 설명하기 좋다.

## 자기소개서에서 강조하기 좋은 포인트

### 1. 단순 기능 구현이 아니라 시스템 통합 경험

이 프로젝트는 프론트엔드, 백엔드, 클러스터 배포, 스트리밍, 보안, 임베디드 장치 연동을 모두 포함한다.
따라서 자기소개서에서는 `여러 계층의 기술을 하나의 서비스로 연결한 경험`을 강조하기 좋다.

### 2. 실시간 영상과 장치 제어를 함께 다룬 경험

실시간 영상 재생과 장비 제어는 일반 웹 프로젝트보다 난도가 높다.
RTSP, WebSocket, MQTT, 하드웨어 제어가 함께 동작하는 구조를 이해하고 구현한 경험은 차별점이 될 수 있다.

### 3. 보안과 운영 관점까지 확장한 경험

JWT, TOTP 2FA, mTLS, health/watchdog 구조는 단순한 기능 추가가 아니라 운영 안정성과 보안 수준을 높이기 위한 설계 요소다.
따라서 자기소개서에서는 `기능 구현`뿐 아니라 `서비스 신뢰성`과 `보안 강화`를 함께 다뤘다고 설명할 수 있다.

### 4. 제약 환경에서의 문제 해결 경험

Raspberry Pi 기반 클러스터, ESP32 자원 한계, 실시간 스트리밍 병목 등은 모두 제한된 환경에서 발생하는 문제다.
이 점은 `제약된 환경에서도 현실적인 해결책을 찾는 문제 해결 능력`으로 연결해 설명하기 좋다.

## 자기소개서용 프로젝트 설명 예시

### 예시 1. 비교적 짧은 버전

`VEDA 프로젝트는 Qt 기반 관제 클라이언트와 Crow 기반 백엔드 서버, Raspberry Pi K3s 클러스터, MQTT/RTSP/WebSocket 기반 장치 연동을 결합한 통합 CCTV 관제 시스템입니다. 실시간 영상 모니터링, 과거 영상 Playback 및 Export, CCTV 3D Map, 열화상 모니터링, 모터 제어, JWT/TOTP 2FA 기반 인증 기능을 하나의 서비스 흐름으로 통합했습니다. 이 프로젝트를 통해 영상 스트리밍, 장치 제어, 보안 인증, 엣지 환경 배포까지 아우르는 시스템 통합 경험을 쌓았습니다.`

### 예시 2. 자기소개서 본문용 중간 길이 버전

`저는 VEDA 프로젝트에서 실시간 CCTV 관제와 장치 제어를 통합한 시스템 개발 경험을 쌓았습니다. 이 프로젝트는 Qt 기반 클라이언트에서 라이브 영상, Playback, Export, 3D Map, 열화상 모니터링 기능을 제공하고, Crow 기반 백엔드 서버가 SUNAPI 프록시, CCTV 제어, MQTT 기반 모터 제어, ESP32 watchdog 상태 관리, 사용자 인증을 담당하는 구조로 설계되었습니다. 또한 Raspberry Pi 기반 K3s 클러스터 위에 Nginx, MediaMTX, Mosquitto, MariaDB를 함께 배포해 엣지 환경에서도 스트리밍과 제어가 안정적으로 동작할 수 있도록 구성했습니다. 이를 통해 프론트엔드와 백엔드뿐 아니라 스트리밍, 보안, 인프라, 임베디드 연동까지 연결되는 전체 시스템 관점의 개발 역량을 키울 수 있었습니다.`

### 예시 3. 기술 강조형 버전

`VEDA는 HTTP/HTTPS, RTSP/RTSPS, WebSocket, MQTT, UART, SPI, I2C 등 다양한 프로토콜을 하나의 서비스로 통합한 엣지 기반 관제 플랫폼입니다. Qt/QML 클라이언트에서는 라이브 영상, 녹화 재생, RTSP-over-WebSocket 기반 Playback, 3D Map, Thermal 모니터링을 지원하고, Crow 서버는 JWT/TOTP 2FA 인증, SUNAPI 프록시, CCTV 스트림 중계, 모터 제어 API, ESP32 상태 수집을 담당합니다. 특히 Raspberry Pi K3s 클러스터와 mTLS 기반 게이트웨이를 통해 제한된 환경에서도 실시간성과 보안을 동시에 고려한 시스템을 구축한 점이 큰 특징입니다.`

## 본인 기여를 정리할 때 넣으면 좋은 항목

아래 항목 중 실제로 본인이 담당한 부분만 골라 자기소개서에 넣는 것이 좋다.

- Qt 클라이언트 UI 설계 및 QML 화면 구현
- Playback / Export 기능 구현
- Crow 서버 API 설계 및 구현
- JWT / TOTP 2FA 인증 기능 추가
- MQTT 기반 모터 제어 또는 ESP32 watchdog 연동
- CCTV 3D Map 스트리밍 및 WebSocket 처리
- 열화상 프레임 수신 및 시각화
- Raspberry Pi K3s 클러스터 배포 및 운영
- Nginx, MediaMTX, Mosquitto, MariaDB 환경 구성
- mTLS 및 인증서 체계 설계
- 하드웨어 병목 분석 및 성능 개선

## 작성 시 주의할 점

- 프로젝트 전체를 설명하되, 본인의 실제 기여 범위를 넘어서는 표현은 피하는 것이 좋다.
- "참여했다"보다 "어떤 문제를 해결했고 어떤 방식으로 구현했는지"를 쓰는 편이 더 설득력 있다.
- 가능하다면 본인이 맡은 기능, 개선한 구조, 해결한 문제를 중심으로 2~3문장 정도의 성과 서술을 추가하는 것이 좋다.
- 면접에서는 프로젝트 소개 후 `가장 어려웠던 점`, `왜 그 기술을 선택했는지`, `실제 장애나 병목이 무엇이었는지`까지 이어질 가능성이 높으므로 그 부분까지 준비해두는 것이 좋다.

## 추천 사용 방법

### 자기소개서 1문단용

- `프로젝트 한 줄 소개`
- `예시 1`

### 포트폴리오용

- `프로젝트 상세 설명`
- `전체 시스템 구성`
- `주요 기능 정리`
- `기술 스택`

### 면접 대비용

- `프로젝트의 기술적 특징`
- `자기소개서에서 강조하기 좋은 포인트`
- `본인 기여를 정리할 때 넣으면 좋은 항목`

