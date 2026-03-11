# K3s Cluster Architecture

`RaspberryPi/k3s-cluster`는 Raspberry Pi 4 여러 대를 K3s 클러스터로 묶고, CCTV 관제에 필요한 백엔드 서비스를 역할별로 분산 배치하기 위한 매니페스트와 구성 파일을 모아둔 디렉터리입니다. 이 문서는 각 컴포넌트가 어떤 역할을 맡고, 어떤 경로로 트래픽이 흐르며, 어떤 순서로 배포되는지를 상세히 설명합니다.

## Overview
| Component | Role | Main Files |
|---|---|---|
| Nginx Gateway | 외부 진입점, TLS/mTLS 종료, 내부 서비스 라우팅 | `nginx/nginx.conf`, `nginx/nginx-deployment.yaml` |
| Crow Server | REST API, SUNAPI 프록시, MQTT 연동, CCTV 백엔드 제어 | `crow_server/crow-server.yaml` |
| MediaMTX | RTSP/RTSPS/HLS/WebRTC 스트리밍 및 녹화 | `mediamtx/mediamtx.yaml`, `mediamtx/mediamtx.yml` |
| Mosquitto | MQTT 브로커, mTLS 기반 장치 인증 | `mosquitto/mqtt.yaml`, `mosquitto/mosquitto.conf` |
| MariaDB | 사용자 및 서비스 데이터 저장 | `mariadb/mariadb-deploy.yaml`, `mariadb/mariadb-init.yaml` |
| MetalLB | 베어메탈 환경에서 LoadBalancer IP 제공 | `metallb/metallb-config.yaml` |
| Security Assets | Root CA, 서버/클라이언트 인증서 생성 | `security/generate_certs.sh` |

## Architecture Diagram
```text
                                  External Clients
                  +------------------------------------------------+
                  | Qt Client / Device / CCTV / Admin Tools        |
                  +------------------------+-----------------------+
                                           |
                         HTTPS(443), RTSPS(8555), MQTTS(8883)
                                           |
                                   +-------v--------+
                                   | Nginx Gateway  |
                                   | TLS / mTLS     |
                                   | LoadBalancer   |
                                   +---+--------+---+
                                       |        |
                         HTTP proxy ----+        +---- TCP stream proxy
                                       |        |
             +-------------------------+        +-------------------------+
             |                                                          |
     +-------v--------+                                        +--------v--------+
     | Crow Server    |                                        | MediaMTX / MQTT |
     | API / Proxy    |                                        | Stream / Broker |
     +---+--------+---+                                        +-----+-------+---+
         |        |                                                  |       |
         |        +--> MariaDB                                       |       +--> Mosquitto
         |                                                           |
         +--> CCTV Backend (depth / inference server)                +--> Camera RTSP ingest
         |
         +--> SUNAPI camera HTTP / WebSocket proxy

      K3s cluster schedules these workloads across Raspberry Pi master/worker nodes.
```

## Cluster Topology
이 디렉터리는 단일 장비 서버가 아니라 여러 Raspberry Pi 노드를 하나의 클러스터로 운영하는 전제를 가집니다.

- Master Node
  K3s control plane 역할을 수행합니다.
- Worker Node
  실제 애플리케이션 Pod를 실행합니다.
- Node-pinned workloads
  일부 서비스는 `nodeSelector`로 특정 노드에 고정됩니다.

현재 매니페스트 기준 예시:
- `crow_server`는 `pi-worker1`에 고정
- `mediamtx`는 `pi-worker1`에 고정
- `mariadb`는 `pi-worker2`에 고정

이 구조는 기능별 책임을 분리해 CPU, 네트워크, 스토리지 부하를 한 장비에 몰지 않도록 하는 데 목적이 있습니다.

## Service Responsibilities
### Nginx Gateway
`nginx`는 외부에서 들어오는 모든 주요 트래픽의 진입점입니다.

- `443/tcp`
  HTTPS 및 WebSocket 요청 수신
- `8554/tcp`
  내부 RTSP 프록시
- `8555/tcp`
  mTLS 기반 RTSPS 프록시
- `8883/tcp`
  mTLS 기반 MQTTS 프록시

`nginx/nginx.conf` 기준 주요 역할:
- HTTP 80 포트를 443으로 리다이렉트
- `/` 요청을 `crow-server-service:8080`으로 프록시
- `/sunapi/` 및 `/sunapi/StreamingServer` 요청을 Crow Server로 프록시
- `/hls/` 요청을 `mediamtx-service:8888`로 프록시
- `8554` RTSP 트래픽을 MediaMTX로 포워딩
- `8883` MQTTS 트래픽을 Mosquitto로 포워딩

보안 관점에서 Nginx는 다음을 수행합니다.
- 서버 인증서 적용
- Root CA 기반 클라이언트 인증서 검증
- `ssl_verify_client on`을 통한 mTLS 강제
- 장치 식별 결과를 `X-Device-Verify`, `X-Device-ID` 헤더로 내부 서비스에 전달

### Crow Server
`crow_server`는 애플리케이션 레벨 제어 서버입니다.

주요 역할:
- REST API 제공
- SUNAPI HTTP 프록시
- SUNAPI WebSocket 프록시
- MQTT 브로커와 연동
- CCTV 백엔드 서버와 연동
- DB 조회 및 사용자 데이터 처리

`crow_server/crow-server.yaml` 기준 의존성:
- DB: `mariadb-service`
- SUNAPI 설정: `crow-sunapi-config`, `crow-sunapi-secret`
- CCTV 백엔드 설정: `crow-cctv-ip-config`
- 인증서: `crow-certs`
- 녹화 디렉터리 공유: `/home/pi/cctv-recordings`

즉, Crow Server는 클러스터 내부 서비스와 외부 CCTV/카메라 제어 계층을 연결하는 중앙 백엔드 역할을 맡습니다.

### MediaMTX
`mediamtx`는 스트리밍 허브입니다.

주요 기능:
- 카메라 RTSP 소스를 받아 내부 재배포
- `main`, `sub` 스트림 분리
- 녹화 파일 생성
- HLS/WebRTC/RTSP/RTSPS 제공

`mediamtx/mediamtx.yaml` 기준 특징:
- 채널 `0~3`에 대해 `main`, `sub` 스트림 정의
- `runOnInit`에서 FFmpeg로 카메라 스트림을 pull
- `main` 스트림은 녹화 활성화
- 녹화 파일은 `/recordings/%path_%Y-%m-%d_%H-%M-%S`
- 인증서 적용 시 `8555` RTSPS 제공
- Service 타입은 `LoadBalancer`

외부 접근 경로:
- Nginx 경유 HLS: `https://<LB_IP>/hls/...`
- 직접 RTSP: `<LB_IP>:8554`
- 직접 RTSPS: `<LB_IP>:8555`

### Mosquitto
`mosquitto`는 장치 이벤트와 서버 간 메시징을 담당합니다.

설정 특징:
- `1883`: 클러스터 내부 비암호화 MQTT
- `8883`: 인증서 기반 MQTT over TLS
- `require_certificate true`
- `use_identity_as_username true`

즉, 외부 장치가 MQTT에 접근할 때는 단순 계정/비밀번호가 아니라 클라이언트 인증서 자체를 식별자로 사용하는 구조입니다.

### MariaDB
`mariadb`는 영속 데이터 저장소입니다.

`mariadb/mariadb-deploy.yaml` 기준:
- `pi-worker2`에 고정 배치
- `local-path` 스토리지를 사용하는 PVC 연결
- 기본 DB 이름: `veda_db`
- 기본 애플리케이션 사용자: `veda_user`
- 초기 SQL은 `mariadb-init.yaml`에서 주입

초기화 스크립트에는 현재 `users` 테이블 생성과 테스트 계정 삽입이 포함되어 있습니다.

### MetalLB
베어메탈 K3s 환경에서는 클라우드 LoadBalancer가 없으므로 `metallb`로 외부 IP를 제공합니다.

`metallb/metallb-config.yaml` 기준:
- IP 풀: `192.168.55.200-192.168.55.210`

이 IP 대역에서 `nginx-service`, `mediamtx-service` 같은 `LoadBalancer` 타입 서비스가 외부 IP를 할당받습니다.

## Traffic Flows
### 1. API / WebSocket
1. 외부 클라이언트가 `https://<LB_IP>/...`로 접속합니다.
2. Nginx가 TLS/mTLS를 종료합니다.
3. 요청을 `crow-server-service:8080`으로 프록시합니다.
4. Crow Server가 DB, MQTT, CCTV 백엔드와 연동해 응답합니다.

### 2. SUNAPI Proxy
1. 클라이언트가 `/sunapi/...` 또는 `/sunapi/StreamingServer`로 요청합니다.
2. Nginx가 장치 인증 결과를 헤더에 포함해 Crow Server로 전달합니다.
3. Crow Server가 카메라 SUNAPI HTTP/WebSocket으로 중계합니다.

### 3. Streaming
1. MediaMTX가 카메라 RTSP를 FFmpeg로 ingest합니다.
2. 내부에서 `main/sub` 경로로 재게시합니다.
3. 외부 사용자는 Nginx 또는 MediaMTX Service를 통해 스트림에 접근합니다.
4. `main` 스트림은 녹화 파일을 `/recordings`에 저장합니다.

### 4. MQTT
1. 장치 또는 클라이언트가 `8883`으로 접속합니다.
2. Nginx가 mTLS를 검증합니다.
3. 트래픽을 내부 `mqtt-service:1883`으로 포워딩합니다.
4. Mosquitto가 메시지를 중계합니다.
5. Crow Server는 내부 브로커에 클라이언트로 붙어 이벤트를 처리합니다.

## Storage Model
현재 저장소/매니페스트 기준으로 스토리지는 두 종류가 섞여 있습니다.

- `local-path` PVC
  MariaDB와 MediaMTX용 PVC 정의가 있습니다.
- `hostPath`
  Crow Server와 MediaMTX는 `/home/pi/cctv-recordings`를 직접 마운트합니다.

주의할 점:
- `hostPath`는 특정 노드 로컬 디스크에 강하게 결합됩니다.
- Pod가 다른 노드로 이동하면 데이터 경로가 달라질 수 있습니다.
- 그래서 현재 매니페스트는 `nodeSelector`와 함께 사용하는 구조입니다.

## Security Model
`security/generate_certs.sh`는 다음 인증서를 생성합니다.

- Root CA
- Nginx 서버 인증서
- CCTV 장치용 클라이언트 인증서
- STM32 장치용 클라이언트 인증서
- Qt 클라이언트용 인증서

보안 흐름:
- Nginx는 서버 인증서를 제시합니다.
- 클라이언트는 Root CA로 서명된 인증서를 제시해야 합니다.
- MQTTS, RTSPS, HTTPS 모두 mTLS 적용 대상이 될 수 있습니다.
- 내부 서비스는 ClusterIP로 숨겨 외부 직접 접근을 제한합니다.

운영 시 유의점:
- 현재 `security/certs/`에 실제 인증서 파일이 존재합니다.
- 실서비스에서는 민감 파일을 저장소에 직접 두지 않고 Kubernetes Secret 또는 외부 비밀 관리 체계로 옮기는 편이 안전합니다.

## Deployment Order
권장 배포 순서는 다음과 같습니다.

1. K3s 클러스터와 기본 네트워크를 준비합니다.
2. MetalLB를 설치하고 IP 풀을 적용합니다.
3. 인증서와 Secret, ConfigMap을 준비합니다.
4. MariaDB PVC, init ConfigMap, Secret, Deployment를 배포합니다.
5. Mosquitto ConfigMap, Secret, Deployment를 배포합니다.
6. MediaMTX ConfigMap, Secret, Deployment를 배포합니다.
7. Crow Server용 ConfigMap, Secret, Deployment를 배포합니다.
8. Nginx Gateway Deployment와 Service를 배포합니다.
9. 외부 IP가 할당되면 HTTPS, MQTTS, RTSPS 동작을 점검합니다.

## Operational Procedure
아래 절차는 현재 저장소에 있는 매니페스트 기준으로 클러스터를 실제 배포할 때 사용할 수 있는 예시입니다. 운영 환경에 맞게 IP, 계정, 인증서 파일명은 반드시 교체해야 합니다.

### 1. Prerequisites
- K3s 클러스터가 준비되어 있어야 합니다.
- `kubectl`이 현재 클러스터를 가리켜야 합니다.
- MetalLB가 설치 가능해야 합니다.
- 각 워커 노드의 hostname이 매니페스트의 `nodeSelector`와 일치해야 합니다.
- 인증서 파일이 `RaspberryPi/k3s-cluster/security/certs/` 아래에 준비되어 있어야 합니다.

기본 확인:
```bash
kubectl get nodes -o wide
kubectl get pods -A
```

### 2. MetalLB Apply
먼저 LoadBalancer IP를 줄 수 있도록 MetalLB를 준비합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/metallb/metallb-config.yaml
```

적용 후 확인:
```bash
kubectl get ipaddresspools.metallb.io -A
kubectl get l2advertisements.metallb.io -A
```

### 3. Certificates And Secrets
인증서가 없다면 먼저 생성합니다.

```bash
cd RaspberryPi/k3s-cluster/security
chmod +x ./generate_certs.sh
SERVER_DNS=<LB_DNS_OR_IP> SERVER_IP=<LB_IP> ./generate_certs.sh
cd ../../..
```

다음 Secret들이 필요합니다.
- `nginx-certs`
- `mtls-ca`
- `mqtt-certs`
- `crow-certs`
- `mariadb-secret`
- `crow-sunapi-secret`

파일 기반 Secret 생성 예시:
```bash
kubectl create secret generic nginx-certs \
  --from-file=server.crt=RaspberryPi/k3s-cluster/security/certs/server.crt \
  --from-file=server.key=RaspberryPi/k3s-cluster/security/certs/server.key

kubectl create secret generic mtls-ca \
  --from-file=rootCA.crt=RaspberryPi/k3s-cluster/security/certs/rootCA.crt

kubectl create secret generic mqtt-certs \
  --from-file=ca.crt=RaspberryPi/k3s-cluster/security/certs/rootCA.crt \
  --from-file=server.crt=RaspberryPi/k3s-cluster/security/certs/server.crt \
  --from-file=server.key=RaspberryPi/k3s-cluster/security/certs/server.key

kubectl create secret generic crow-certs \
  --from-file=rootCA.crt=RaspberryPi/k3s-cluster/security/certs/rootCA.crt \
  --from-file=cctv.crt=RaspberryPi/k3s-cluster/security/certs/cctv.crt \
  --from-file=cctv.key=RaspberryPi/k3s-cluster/security/certs/cctv.key
```

문자열 기반 Secret 생성 예시:
```bash
kubectl create secret generic mariadb-secret \
  --from-literal=root-password='<DB_ROOT_PASSWORD>' \
  --from-literal=user-password='<DB_USER_PASSWORD>'

kubectl create secret generic crow-sunapi-secret \
  --from-literal=SUNAPI_USER='<CAMERA_USER>' \
  --from-literal=SUNAPI_PASSWORD='<CAMERA_PASSWORD>'
```

기존 example YAML을 복사해 적용하는 방식도 가능합니다.
- `RaspberryPi/k3s-cluster/mariadb/mariadb-secret.example.yaml`
- `RaspberryPi/k3s-cluster/crow_server/crow-sunapi-secret.example.yaml`

### 4. ConfigMaps
현재 매니페스트 기준으로 별도 준비가 필요한 ConfigMap은 다음과 같습니다.
- `crow-sunapi-config`
- `crow-cctv-ip-config`
- `mariadb-init-sql`
- `mediamtx-config`
- `mosquitto-config`

`mariadb-init-sql`, `mediamtx-config`, `mosquitto-config`는 각 YAML 내부에 이미 포함되어 있어 `kubectl apply` 시 함께 생성됩니다.

운영 환경 값으로 교체가 필요한 ConfigMap은 예제 파일을 복사해 적용하는 방식이 가장 안전합니다.

예시:
```bash
cp RaspberryPi/k3s-cluster/crow_server/crow-sunapi-config.example.yaml /tmp/crow-sunapi-config.yaml
cp RaspberryPi/k3s-cluster/crow_server/crow-cctv-ip-config.example.yaml /tmp/crow-cctv-ip-config.yaml
```

교체해야 하는 값:
- `SUNAPI_BASE_URL`
- `SUNAPI_WS_URL`
- `SUNAPI_INSECURE`
- `SUNAPI_TIMEOUT_MS`
- `CCTV_BACKEND_HOST`
- `CCTV_BACKEND_PORT`

적용:
```bash
kubectl apply -f /tmp/crow-sunapi-config.yaml
kubectl apply -f /tmp/crow-cctv-ip-config.yaml
```

직접 명령으로 생성하는 예시:
```bash
kubectl create configmap crow-sunapi-config \
  --from-literal=SUNAPI_BASE_URL='https://<CAMERA_HOST>' \
  --from-literal=SUNAPI_WS_URL='ws://<CAMERA_HOST>/StreamingServer' \
  --from-literal=SUNAPI_INSECURE='true' \
  --from-literal=SUNAPI_TIMEOUT_MS='12000'

kubectl create configmap crow-cctv-ip-config \
  --from-literal=CCTV_BACKEND_HOST='<CCTV_BACKEND_IP>' \
  --from-literal=CCTV_BACKEND_PORT='9090'
```

### 5. Database Apply
MariaDB부터 올리는 것이 안전합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-pvc.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-init.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-deploy.yaml
```

확인:
```bash
kubectl get pvc
kubectl get pods -l app=mariadb -o wide
kubectl get svc mariadb-service
```

### 6. MQTT Apply
```bash
kubectl apply -f RaspberryPi/k3s-cluster/mosquitto/mqtt.yaml
```

확인:
```bash
kubectl get pods -l app=mqtt -o wide
kubectl get svc mqtt-service
```

### 7. Streaming Apply
`mediamtx.yaml` 안에는 ConfigMap, Deployment, Service가 같이 포함되어 있습니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml
```

확인:
```bash
kubectl get pods -l app=mediamtx -o wide
kubectl get svc mediamtx-service
```

### 8. Crow Server Apply
Crow Server는 DB, SUNAPI ConfigMap, CCTV backend ConfigMap, 인증서 Secret이 준비된 뒤 배포해야 합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/crow_server/crow-server.yaml
```

확인:
```bash
kubectl get pods -l app=crow-server -o wide
kubectl get svc crow-server-service
```

### 9. Nginx Gateway Apply
마지막으로 외부 진입점을 올립니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml
```

확인:
```bash
kubectl get pods -l app=nginx-gateway -o wide
kubectl get svc nginx-service
```

`nginx-service`가 `LoadBalancer` 타입이므로 외부 IP가 붙는지 확인합니다.

### 10. Full Apply Example
한 번에 순서대로 적용하는 예시는 다음과 같습니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/metallb/metallb-config.yaml

kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-pvc.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-init.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-deploy.yaml

kubectl apply -f RaspberryPi/k3s-cluster/mosquitto/mqtt.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml
kubectl apply -f RaspberryPi/k3s-cluster/crow_server/crow-server.yaml
kubectl apply -f RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml
```

### 11. Post-Deployment Checks
전체 상태 확인:
```bash
kubectl get pods -o wide
kubectl get svc
kubectl get endpoints
```

로그 확인:
```bash
kubectl logs deploy/mariadb
kubectl logs deploy/mqtt-broker
kubectl logs deploy/mediamtx-server
kubectl logs deploy/crow-server
kubectl logs deploy/nginx-gateway
```

외부 접근 점검 예시:
```bash
curl -vk https://<LB_IP>/
openssl s_client -connect <LB_IP>:8883 -cert client-qt.crt -key client-qt.key -CAfile rootCA.crt
openssl s_client -connect <LB_IP>:8555 -cert client-qt.crt -key client-qt.key -CAfile rootCA.crt
```

### 12. Update And Restart
설정 변경 후 재적용:
```bash
kubectl apply -f <updated-yaml>
```

강제 재시작:
```bash
kubectl rollout restart deploy/mariadb
kubectl rollout restart deploy/mqtt-broker
kubectl rollout restart deploy/mediamtx-server
kubectl rollout restart deploy/crow-server
kubectl rollout restart deploy/nginx-gateway
```

롤아웃 상태 확인:
```bash
kubectl rollout status deploy/mariadb
kubectl rollout status deploy/mqtt-broker
kubectl rollout status deploy/mediamtx-server
kubectl rollout status deploy/crow-server
kubectl rollout status deploy/nginx-gateway
```

## Directory Layout
```text
RaspberryPi/k3s-cluster/
  README.md
  crow_server/
    crow-server.yaml
    Dockerfile
    swagger/
  mariadb/
    mariadb-deploy.yaml
    mariadb-init.yaml
    mariadb-pvc.yaml
  mediamtx/
    mediamtx.yaml
    mediamtx.yml
    recording-pvc.yaml
  mosquitto/
    mqtt.yaml
    mosquitto.conf
  nginx/
    nginx.conf
    nginx-deployment.yaml
  metallb/
    metallb-config.yaml
  security/
    generate_certs.sh
    certs/
```
