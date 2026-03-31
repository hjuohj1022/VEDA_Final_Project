# RaspberryPi

`RaspberryPi/` 디렉터리는 VEDA 시스템의 Raspberry Pi 기반 서버 환경을 정리한 상위 허브 문서입니다.
현재 저장소 기준으로 이 영역은 크게 두 축으로 나뉩니다.

- `yocto/`: Raspberry Pi 노드용 공통 Yocto 이미지와 K3s 노드 초기 설정
- `k3s-cluster/`: 실제 서비스 배포용 Kubernetes manifest, 서버 코드, 보안 자산, 운영 문서

루트 README는 "어디서부터 읽어야 하는지"와 "현재 Raspberry Pi 쪽에서 어떤 기능을 담당하는지"를 빠르게 파악하는 용도이며,
세부 설정과 실행 절차는 각 하위 README를 기준 문서로 따라가는 것을 권장합니다.

## 현재 담당 기능

Raspberry Pi 영역은 현재 다음 서버 기능을 담당합니다.

- Crow Server 기반 통합 API
  - 로그인, 회원가입, 이메일 인증
  - 2FA(TOTP) 등록, 확인, 비활성화
  - 비밀번호 재설정, 관리자 잠금 해제, 계정 삭제
- CCTV 제어 및 스트리밍 연동
  - SUNAPI proxy
  - CCTV backend 제어 API
  - 3D Map용 WebSocket 스트림 브리지
- 열화상 수집 및 이벤트 처리
  - Thermal DTLS Gateway
  - Crow Server `/thermal/*` API, WebSocket 스트림
  - 서버 측 열화상 이벤트 감지 및 MQTT 연동
- MQTT 기반 장치 제어
  - 모터 제어
  - 레이저 제어
  - ESP32 watchdog 상태 조회
- 미디어 및 기록 관리
  - MediaMTX 기반 RTSP/HLS/recording 경로
  - Crow Server `/recordings`, `/stream`, `/system/storage`
- 운영 인프라
  - Nginx Gateway(HTTPS/WSS, RTSPS, MQTTS, UDP 진입점)
  - MariaDB(계정/2FA/복구/이벤트 로그 저장)
  - Mosquitto(MQTT broker)
  - MetalLB(LoadBalancer IP 할당)
  - Security 자산 및 Secret 매핑

## 디렉터리 구조

| 경로 | 역할 | 기준 문서 |
| --- | --- | --- |
| `yocto/` | Raspberry Pi 노드용 Yocto 이미지 빌드와 K3s 노드 초기 설정 | `yocto/README.md` |
| `k3s-cluster/` | 전체 K3s 서비스 스택 운영 허브 문서 | `k3s-cluster/README.md` |
| `k3s-cluster/crow_server/` | 인증, 2FA, 계정 복구, CCTV, Thermal, MQTT 브리지, 이벤트 로그 API | `k3s-cluster/crow_server/README.md` |
| `k3s-cluster/nginx/` | 외부 TLS/mTLS 진입점과 프록시 구성 | `k3s-cluster/nginx/README.md` |
| `k3s-cluster/thermal_dtls_gateway/` | DTLS 또는 plain UDP thermal frame 수신 후 Crow로 전달 | `k3s-cluster/thermal_dtls_gateway/README.md` |
| `k3s-cluster/mediamtx/` | CCTV RTSP ingest, HLS/RTSP 서비스, 녹화 저장 | `k3s-cluster/mediamtx/README.md` |
| `k3s-cluster/mosquitto/` | MQTT broker 및 TLS 설정 | `k3s-cluster/mosquitto/README.md` |
| `k3s-cluster/mariadb/` | 계정/2FA/복구/이벤트 로그 저장용 DB와 백업 설정 | `k3s-cluster/mariadb/README.md` |
| `k3s-cluster/metallb/` | LoadBalancer 외부 IP 풀 설정 | `k3s-cluster/metallb/README.md` |
| `k3s-cluster/security/` | mTLS 인증서 생성과 Secret 매핑 가이드 | `k3s-cluster/security/README.md` |
| `cctv-certs/` | 과거 CCTV 제어 인증서 자산 보관 디렉터리 | `k3s-cluster/security/README.md` 참고 |

## 권장 읽기 순서

### 1. 노드 준비가 먼저인 경우

1. `yocto/README.md`
2. `k3s-cluster/README.md`
3. `k3s-cluster/security/README.md`

### 2. 이미 K3s 노드가 있고 서비스만 올릴 경우

1. `k3s-cluster/README.md`
2. `k3s-cluster/security/README.md`
3. `k3s-cluster/mariadb/README.md`
4. `k3s-cluster/mosquitto/README.md`
5. `k3s-cluster/mediamtx/README.md`
6. `k3s-cluster/crow_server/README.md`
7. `k3s-cluster/thermal_dtls_gateway/README.md`
8. `k3s-cluster/nginx/README.md`

### 3. API/기능 구현을 확인하려는 경우

1. `k3s-cluster/crow_server/README.md`
2. `k3s-cluster/crow_server/swagger/swagger.yaml`
3. `../Qt_Client/README.md`
4. `../docs/2fa_qr_setup_flow.md`

## 배포 흐름 요약

현재 저장소 기준 권장 흐름은 아래와 같습니다.

1. Raspberry Pi 노드 이미지를 준비하고 K3s 클러스터를 구성합니다.
2. `security/` 기준으로 인증서와 Secret을 준비합니다.
3. `mariadb/`, `mosquitto/`, `mediamtx/`를 먼저 배포합니다.
4. `crow_server/`와 필요 시 DB migration job을 배포합니다.
5. `thermal_dtls_gateway/`를 배포합니다.
6. 마지막으로 `nginx/`와 `metallb/`를 통해 외부 진입점을 연결합니다.

## 확인 결과

기존 루트 README는 상위 구조만 아주 간단히 소개하고 있어,
현재 실제로 운영 중인 Raspberry Pi 기능 범위(2FA, 계정 복구, 이벤트 로그, Thermal Gateway, MQTT 장치 제어, 보안 자산, MediaMTX 등)를 파악하기에는 정보가 부족했습니다.

이번 업데이트에서는 다음 내용을 반영했습니다.

- 현재 Raspberry Pi 영역이 담당하는 기능 범위 정리
- 실제 하위 디렉터리와 문서 진입점 정리
- 읽기 순서와 배포 흐름 요약 추가
- `cctv-certs/`의 위치와 성격 명시

마지막 검증일: 2026-03-31
