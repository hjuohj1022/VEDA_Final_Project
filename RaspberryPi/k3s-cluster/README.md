### 컴포넌트 명칭

Raspberry Pi K3s Cluster는 VEDA 시스템의 백엔드 서비스를 Raspberry Pi 기반 K3s 환경에 배포하기 위한 통합 운영 문서입니다. 이 디렉터리는 외부 진입점인 Nginx Gateway부터 Crow Server, MediaMTX, Mosquitto, MariaDB, Thermal DTLS Gateway, MetalLB, 인증서 자산까지 서비스별 배포 파일과 운영 절차를 함께 관리합니다.

**주요 환경 및 버전**
- 운영 환경: Raspberry Pi 4 기반 K3s 클러스터
- 배포 방식: Kubernetes Manifest, Docker 이미지 기반 배포
- 외부 진입 포트: `443/TCP`, `8555/TCP`, `8883/TCP`, `5005/UDP`

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

###### 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** Edge K3s Multi-Service Platform
- **설명:** 이 디렉터리는 단일 애플리케이션이 아니라 여러 컨테이너형 서비스를 조합해 CCTV 제어, 열화상 수집, MQTT 장치 제어, DB 저장, 외부 TLS 진입을 제공하는 클러스터 배포 단위입니다. 서비스별 README는 개별 운영 문서 역할을 하고, 본 README는 전체 배치 구조와 배포 순서를 연결하는 허브 문서 역할을 합니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 역할 | 문서 |
| --- | --- | --- |
| Crow Server | REST API, WebSocket 스트림, MQTT 브리지, SUNAPI/CCTV/Thermal 통합 | `crow_server/README.md` |
| Nginx Gateway | HTTPS, RTSPS, MQTTS, DTLS/UDP 외부 진입점 | `nginx/README.md` |
| Thermal DTLS Gateway | DTLS PSK 종료 또는 plain UDP fallback 후 Crow Server로 UDP 전달 | `thermal_dtls_gateway/README.md` |
| MediaMTX | CCTV RTSP 수집, HLS/RTSP 제공, 녹화 저장 | `mediamtx/README.md` |
| Mosquitto | 내부 MQTT 브로커 | `mosquitto/README.md` |
| MariaDB | 계정, 인증, 복구 정보 저장소 | `mariadb/README.md` |
| MetalLB | `LoadBalancer` 외부 IP 할당 | `metallb/README.md` |
| Security Assets | mTLS 인증서 생성 및 Secret 매핑 가이드 | `security/README.md` |

###### 모듈 상세 (Module Detail)

| 디렉터리 | 상세 책임 |
| --- | --- |
| `crow_server/` | Crow 기반 API 서버 코드, Swagger 문서, K8s 배포 정의 |
| `nginx/` | 외부 포트 수신, mTLS 종료, HTTP/stream 프록시 |
| `thermal_dtls_gateway/` | OpenSSL 기반 DTLS 서버이자 plain UDP thermal chunk fallback 포워더 |
| `mediamtx/` | 카메라 RTSP ingest, HLS/RTSP 서비스, 녹화 경로 정의 |
| `mosquitto/` | MQTT 브로커 설정, TLS 인증서 마운트 |
| `mariadb/` | DB 초기화 SQL, PVC, 시크릿 기반 계정 구성 |
| `metallb/` | 외부 IP 풀과 L2 advertisement 정의 |
| `security/` | Nginx/MQTT/RTSPS용 CA, 서버/클라이언트 인증서 생성 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
External Client / Device
        |
        | 443 HTTPS/WSS, 8555 RTSPS, 8883 MQTTS, 5005 thermal UDP/DTLS
        v
   [ Nginx Gateway / LoadBalancer ]
        |
        +--> crow-server-service:8080/TCP
        +--> mediamtx-service:8554/TCP, 8888/TCP
        +--> mqtt-service:1883/TCP
        +--> thermal-dtls-gateway-service:5005/UDP
                         |
                         +--> crow-server-service:5005/UDP

Internal Backends
  - mariadb-service:3306/TCP
  - mqtt-service:1883/TCP
  - mediamtx-service:8554/TCP, 8888/TCP
```

###### Features

- **기능 1:** 외부 트래픽을 Nginx Gateway 한 곳으로 집중시켜 TLS/mTLS와 라우팅 정책을 일관되게 적용합니다.
- **기능 2:** Crow Server가 계정, CCTV, 열화상, MQTT 제어를 단일 API 계층으로 통합합니다.
- **기능 3:** MediaMTX가 RTSP ingest와 녹화 저장을 담당하고, Nginx가 이를 RTSPS/HLS로 외부에 노출합니다.
- **기능 4:** Thermal DTLS Gateway가 DTLS PSK 기반 센서 통신을 복호화하고, 필요 시 plain UDP thermal chunk도 Crow Server로 전달합니다.
- **기능 5:** Mosquitto가 모터, 레이저, ESP32 상태 제어를 위한 MQTT 허브 역할을 합니다.
- **기능 6:** MetalLB와 Security 자산이 외부 접속성과 인증서 신뢰 체인을 지원합니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **클러스터:** K3s가 설치된 Raspberry Pi 노드
- **도구:** `kubectl`, `docker` 또는 이미지 레지스트리 접근 권한, `openssl`
- **사전 준비:** MetalLB 설치, 기본 `default` 네임스페이스 사용, 서비스별 Secret 준비

###### 경로 및 설정 (Path Configurations)

- **핵심 배포 파일**
  - `metallb/metallb-config.yaml`
  - `mariadb/mariadb-deploy.yaml`
  - `mosquitto/mqtt.yaml`
  - `mediamtx/mediamtx.yaml`
  - `crow_server/crow-server.yaml`
  - `thermal_dtls_gateway/thermal-dtls-gateway.yaml`
  - `thermal_dtls_gateway/thermal-dtls-networkpolicy.yaml`
  - `nginx/nginx-deployment.yaml`
- **사전 생성 권장 Secret**
  - `mariadb-secret`
  - `mqtt-certs`
  - `nginx-certs`
  - `mtls-ca`
  - `crow-sunapi-secret`
  - `thermal-dtls-secret`
  - `crow-certs`
- **호스트 경로 주의사항**
  - `/home/pi/cctv-recordings`는 MediaMTX와 Crow Server에서 함께 참조할 수 있어야 합니다.

###### Dependency Setup

권장 사전 작업 순서는 다음과 같습니다.

```bash
cd RaspberryPi/k3s-cluster/security
chmod +x generate_certs.sh
./generate_certs.sh
```

이후 서비스별 Secret과 ConfigMap 값을 실제 운영 환경에 맞게 준비한 뒤 Kubernetes manifest를 적용합니다.

##### 4. 설정 가이드 (Configuration)

###### 설정 파일명 1: `metallb/metallb-config.yaml`

- `<METALLB_ADDRESS_RANGE>` 범위의 외부 IP 풀 정의
- `nginx-service`의 `LoadBalancer` 외부 IP 할당 기반 제공

###### 설정 파일명 2: 서비스별 배포 manifest

- `mariadb/mariadb-deploy.yaml`: DB Deployment, Service
- `mosquitto/mqtt.yaml`: ConfigMap, Deployment, Service
- `mediamtx/mediamtx.yaml`: ConfigMap, Deployment, Service
- `crow_server/crow-server.yaml`: Crow Server Deployment, Service
- `thermal_dtls_gateway/thermal-dtls-gateway.yaml`: Thermal Gateway Deployment, Service
- `nginx/nginx-deployment.yaml`: Nginx Deployment, `LoadBalancer` Service

###### 보안 및 통신 설정

- 외부 공개 포트: `443/TCP`, `8555/TCP`, `8883/TCP`, `5005/UDP`
- 내부 서비스 포트: `8080/TCP`, `3306/TCP`, `1883/TCP`, `8554/TCP`, `8888/TCP`, `5005/UDP`
- 인증서 생성은 `security/generate_certs.sh` 기준으로 관리
- MQTT, HTTPS, RTSPS는 인증서 기반 보호를 전제로 운영

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

이 디렉터리 자체에는 단일 바이너리 빌드 과정이 없고, 서비스별 이미지와 manifest 적용으로 운영합니다. 서비스별 이미지를 로컬에서 다시 빌드해야 한다면 각 하위 디렉터리의 `Dockerfile`을 사용합니다.

예시:

```bash
docker build -t local/nginx-gateway:dev RaspberryPi/k3s-cluster/nginx
docker build -t local/mqtt-broker:dev RaspberryPi/k3s-cluster/mosquitto
docker build -t local/thermal-dtls-gateway:dev RaspberryPi/k3s-cluster/thermal_dtls_gateway
```

###### Static Analysis

- `kubectl apply --dry-run=client -f <manifest>`
- 서비스별 YAML 문법 검토
- 노드 고정(`nodeSelector`)과 Secret 이름 일치 여부 검토

예시:

```bash
kubectl apply --dry-run=client -f RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml
kubectl apply --dry-run=client -f RaspberryPi/k3s-cluster/thermal_dtls_gateway/thermal-dtls-gateway.yaml
```

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

권장 배포 순서는 다음과 같습니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/metallb/metallb-config.yaml

kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-pvc.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-init.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-deploy.yaml

kubectl apply -f RaspberryPi/k3s-cluster/mosquitto/mqtt.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml
kubectl apply -f RaspberryPi/k3s-cluster/crow_server/crow-server.yaml
kubectl apply -f RaspberryPi/k3s-cluster/thermal_dtls_gateway/thermal-dtls-gateway.yaml
kubectl apply -f RaspberryPi/k3s-cluster/thermal_dtls_gateway/thermal-dtls-networkpolicy.yaml
kubectl apply -f RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml
```

###### Test (검증 방법)

- `kubectl get pods -o wide`
- `kubectl get svc`
- `curl --cert client-qt.crt --key client-qt.key --cacert rootCA.crt https://<LB_IP>/health`
- `GET /laser/status`, `GET /thermal/status` 확인
- `openssl s_client -connect <LB_IP>:8883 -cert client-qt.crt -key client-qt.key -CAfile rootCA.crt`

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Start:** MetalLB와 인증서 준비 후 DB, MQTT, MediaMTX, Crow, Thermal Gateway, Nginx 순으로 올립니다.
- **Steady State:** 외부 HTTPS/WSS, RTSPS, MQTTS, DTLS/UDP 또는 plain UDP thermal traffic이 Nginx를 통해 내부 서비스로 분기됩니다.
- **Device Control:** Crow Server가 `motor/control`, `laser/control`, `system/control` 등 MQTT 토픽에 명령을 publish합니다.
- **Streaming:** 열화상은 DTLS/UDP 또는 plain UDP thermal chunk -> Thermal Gateway -> Crow Server -> WebSocket으로, CCTV는 RTSP -> MediaMTX -> RTSPS/HLS로 전달됩니다.

###### Command Reference

| 구분 | 명령/경로 | 설명 |
| --- | --- | --- |
| 배포 | `kubectl apply -f .../metallb-config.yaml` | 외부 IP 풀 준비 |
| 배포 | `kubectl apply -f .../crow-server.yaml` | API 서버 배포 |
| 상태 확인 | `kubectl get svc nginx-service` | 외부 IP 할당 확인 |
| API | `GET /health` | Crow Server 헬스체크 |
| API | `GET /laser/status` | MQTT 레이저 브리지 상태 |
| API | `GET /thermal/status` | 열화상 UDP 수신 상태 |

###### Stream/Data Format

| 흐름 | 프로토콜 | 데이터 형식 |
| --- | --- | --- |
| REST/API | HTTPS | JSON |
| 장치 제어 | MQTT/MQTTS | ASCII 명령 문자열 |
| CCTV 스트림 | RTSP, RTSPS, HLS | 영상 스트림 |
| 열화상 센서 | DTLS/UDP 또는 plain UDP thermal chunk | 바이너리 thermal frame payload |

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| `nginx-service`에 `EXTERNAL-IP`가 없음 | MetalLB 미설치 또는 IP 풀 문제 | `metallb-system` 상태와 `metallb-config.yaml` 확인 |
| `https://<LB_IP>/health` 실패 | Nginx Secret 또는 Crow upstream 문제 | `nginx-certs`, `mtls-ca`, `crow-server` Pod 상태 확인 |
| `GET /laser/status`에서 브로커 미연결 | Mosquitto 또는 Crow MQTT 설정 문제 | `mqtt-service`, `MQTT_HOST`, `MQTT_PORT` 확인 |
| HLS/RTSPS 스트림 실패 | MediaMTX ingest 또는 Nginx 프록시 문제 | `CAMERA_*` 값, MediaMTX 로그, Nginx stream 설정 확인 |
| 열화상 수신 없음 | DTLS Secret, Nginx UDP, Crow UDP 바인드 문제 | `thermal-dtls-secret`, `thermal-dtls-gateway`, Crow thermal 상태 확인 |

###### Operational Checklist

- MetalLB가 설치되어 있고 IP 풀 범위가 네트워크와 충돌하지 않는가
- `mariadb-secret`, `mqtt-certs`, `nginx-certs`, `mtls-ca`, `thermal-dtls-secret`, `crow-certs`가 준비되었는가
- `pi-master`, `pi-worker1`, `pi-worker2` 노드명이 manifest의 `nodeSelector`와 일치하는가
- `/home/pi/cctv-recordings` 경로가 쓰기 가능한가
- `GET /health`, `GET /docs`, `GET /laser/status`, `GET /thermal/status`가 정상 응답하는가

**작성자:** VEDA Team  
**마지막 업데이트:** 2026-03-19
