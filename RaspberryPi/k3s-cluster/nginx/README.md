### 컴포넌트 명칭

Nginx Gateway는 클러스터의 외부 진입점으로서 HTTPS, WebSocket, RTSPS, MQTTS, DTLS/UDP 트래픽을 받아 내부 서비스로 프록시합니다. 이 디렉터리는 Nginx 설정 파일, 인증서 마운트, Kubernetes `LoadBalancer` Service 정의를 포함합니다.

**주요 환경 및 버전**
- 베이스 이미지: `nginx:latest`
- 배포 형태: Deployment + `LoadBalancer` Service
- 외부 포트: `80/TCP`, `443/TCP`, `8555/TCP`, `8883/TCP`, `5005/UDP`

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

###### 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** TLS Termination + Reverse Proxy + Stream Proxy Gateway
- **설명:** Nginx는 외부에서 들어오는 모든 주요 트래픽을 수신해 HTTP 계층은 Crow Server와 MediaMTX HLS로, stream 계층은 Mosquitto, MediaMTX RTSP, Thermal DTLS Gateway로 중계합니다. 또한 mTLS 검증을 통해 장치와 클라이언트 인증을 강제합니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 역할 | 파일 |
| --- | --- | --- |
| HTTP Proxy | Crow Server REST, Swagger, WebSocket, HLS 프록시 | `nginx.conf` |
| Stream Proxy | MQTTS, RTSPS, DTLS/UDP 프록시 | `nginx.conf` |
| Deployment | 컨테이너와 인증서 마운트 실행 | `nginx-deployment.yaml` |
| LoadBalancer Service | 외부 포트 공개 | `nginx-deployment.yaml` |
| Docker Image | 설정 템플릿 복사 이미지 | `Dockerfile` |

###### 모듈 상세 (Module Detail)

| 파일 | 상세 책임 |
| --- | --- |
| `nginx.conf` | HTTP server, upstream, stream block, mTLS 헤더 전달 정의 |
| `nginx-deployment.yaml` | `nginx-gateway` Deployment와 `nginx-service` LoadBalancer 정의 |
| `Dockerfile` | 기본 설정 제거 후 프로젝트 `nginx.conf` 템플릿 복사 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
External Client / Device
        |
        +--> 443 HTTPS/WSS ----> crow-server-service:8080
        |                     \-> mediamtx-service:8888 (/hls/)
        +--> 8555 RTSPS -------> mediamtx-service:8554
        +--> 8883 MQTTS -------> mqtt-service:1883
        \--> 5005 DTLS/UDP ----> thermal-dtls-gateway-service:5005
```

###### Features

- **기능 1:** `ssl_verify_client on`으로 HTTPS, RTSPS, MQTTS 접속에서 클라이언트 인증서를 요구합니다.
- **기능 2:** `/auth/*`, `/2fa/*`, `/events`, `/recordings`, `/system/storage`, `/sunapi/*`, `/docs`, `/swagger.yaml`를 포함한 일반 Crow REST 경로를 프록시합니다.
- **기능 3:** `/cctv/` 경로는 긴 처리 시간을 고려한 별도 timeout 정책으로 Crow Server에 프록시합니다.
- **기능 4:** `/cctv/stream`, `/thermal/stream`, `/sunapi/StreamingServer` WebSocket을 Crow Server로 터널링합니다.
- **기능 5:** `/hls/` 경로를 MediaMTX HLS upstream으로 연결합니다.
- **기능 6:** `8555`, `8883`, `5005/UDP`를 stream 계층에서 내부 서비스로 중계합니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **클러스터:** K3s 또는 Kubernetes
- **외부 IP:** MetalLB 또는 동등한 `LoadBalancer` 제공자
- **시크릿:** `nginx-certs`, `mtls-ca`
- **상위 의존 서비스:** `crow-server-service`, `mediamtx-service`, `mqtt-service`, `thermal-dtls-gateway-service`

###### 경로 및 설정 (Path Configurations)

- **핵심 파일**
  - `nginx-deployment.yaml`
  - `nginx.conf`
- **인증서 마운트 경로**
  - `/etc/nginx/certs/server.crt`
  - `/etc/nginx/certs/server.key`
  - `/etc/nginx/certs/rootCA.crt`
- **고정 노드**
  - `pi-master`

###### Dependency Setup

앞단 인증서와 upstream 서비스가 준비된 뒤 배포합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml
```

##### 4. 설정 가이드 (Configuration)

###### 설정 파일명 1: `nginx-deployment.yaml`

- Deployment 이름: `nginx-gateway`
- Service 이름: `nginx-service`
- Service 타입: `LoadBalancer`
- 노출 포트
  - `80/TCP`
  - `443/TCP`
  - `8555/TCP`
  - `8883/TCP`
  - `5005/UDP`

###### 설정 파일명 2: `nginx.conf`

- HTTP upstream
  - `crow_backend -> crow-server-service:8080`
  - `mediamtx_hls -> mediamtx-service:8888`
- Stream upstream
  - `mediamtx_rtsp -> mediamtx-service:8554`
  - `mosquitto_backend -> mqtt-service:1883`
  - `thermal_dtls_gateway_udp -> thermal-dtls-gateway-service:5005`

###### 보안 설정

- `ssl_protocols TLSv1.2 TLSv1.3`
- `ssl_verify_client on`
- 전달 헤더
  - `X-Device-Verify`
  - `X-Device-ID`

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

로컬 이미지 빌드:

```bash
docker build -t local/nginx-gateway:dev RaspberryPi/k3s-cluster/nginx
```

Kubernetes 적용:

```bash
kubectl apply -f RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml
```

###### Static Analysis

- `kubectl apply --dry-run=client -f nginx-deployment.yaml`
- `nginx.conf` upstream 이름과 서비스명 일치 여부 확인
- `nginx-certs`, `mtls-ca` Secret 존재 여부 확인

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

```bash
kubectl get pods -l app=nginx-gateway -o wide
kubectl get svc nginx-service
kubectl logs deploy/nginx-gateway
```

###### Test (검증 방법)

HTTPS 헬스체크:

```bash
curl --cert client-qt.crt --key client-qt.key --cacert rootCA.crt \
  https://<LB_IP>/health
```

MQTTS/RTSPS 확인:

```bash
openssl s_client -connect <LB_IP>:8883 \
  -cert client-qt.crt -key client-qt.key -CAfile rootCA.crt

openssl s_client -connect <LB_IP>:8555 \
  -cert client-qt.crt -key client-qt.key -CAfile rootCA.crt
```

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Start:** `nginx-certs`, `mtls-ca`가 마운트된 뒤 HTTP/stream 리스너가 열립니다.
- **Ingress Routing:** 경로 기반 HTTP 요청은 Crow/MediaMTX로, 포트 기반 stream 요청은 Mosquitto/MediaMTX/Thermal Gateway로 분배됩니다.
- **Identity Forwarding:** HTTPS/WebSocket 요청은 클라이언트 인증 결과와 주체 DN을 헤더로 Crow Server에 전달합니다.

###### Command Reference

| 구분 | 경로/포트 | 설명 |
| --- | --- | --- |
| HTTP | `https://<LB_IP>/health` | Crow Server 헬스체크 |
| HTTP | `https://<LB_IP>/docs` | Swagger UI |
| HTTP | `https://<LB_IP>/events` | Crow 이벤트 로그 조회 |
| HTTP | `https://<LB_IP>/recordings` | 녹화 파일 목록 조회 |
| HTTP | `https://<LB_IP>/system/storage` | 녹화 저장소 사용량 조회 |
| HTTP | `https://<LB_IP>/hls/` | MediaMTX HLS 접근 |
| WS | `wss://<LB_IP>/cctv/stream` | CCTV 바이너리 스트림 |
| WS | `wss://<LB_IP>/thermal/stream` | Thermal 바이너리 스트림 |
| WS | `wss://<LB_IP>/sunapi/StreamingServer` | SUNAPI StreamingServer 프록시 |
| Stream | `<LB_IP>:8883` | MQTTS |
| Stream | `<LB_IP>:8555` | RTSPS |
| Stream | `<LB_IP>:5005/UDP` | Thermal DTLS ingress |

###### Stream/Data Format

| 흐름 | 형식 |
| --- | --- |
| API | HTTPS + JSON |
| 스트림 터널 | WebSocket |
| MQTT | TLS over TCP |
| RTSP | TLS over TCP |
| Thermal | UDP pass-through |

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| `EXTERNAL-IP`가 없음 | MetalLB 할당 실패 | `nginx-service`와 MetalLB 상태 확인 |
| HTTPS는 되는데 WebSocket이 안 됨 | 업그레이드 헤더 또는 Crow upstream 문제 | `proxy_set_header Upgrade`, Crow 상태 확인 |
| `/cctv/*` 요청이 빨리 타임아웃됨 | 일반 `/` 프록시로 라우팅되거나 Crow backend 지연 | `location /cctv/` timeout 설정과 Crow/CCTV backend 상태 확인 |
| MQTTS 실패 | `mqtt-service` 또는 인증서 문제 | `nginx-certs`, `mtls-ca`, Mosquitto 상태 확인 |
| RTSPS 실패 | MediaMTX upstream 또는 인증서 문제 | `mediamtx-service:8554` 및 Nginx stream 설정 확인 |
| DTLS/UDP 전달 실패 | Thermal Gateway 미기동 | `thermal-dtls-gateway-service:5005` 확인 |

###### Operational Checklist

- `nginx-service`가 `LoadBalancer` 타입인가
- `nginx-certs`와 `mtls-ca`가 마운트 가능한가
- `pi-master` 노드가 존재하는가
- `crow-server-service`, `mediamtx-service`, `mqtt-service`, `thermal-dtls-gateway-service`가 모두 준비되었는가
- `https://<LB_IP>/health`, `https://<LB_IP>/events`, `https://<LB_IP>/recordings`와 `openssl s_client` 테스트가 성공하는가

**작성자:** VEDA Team  
**마지막 업데이트:** 2026-03-26
