### 컴포넌트 명칭

Mosquitto는 클러스터 내부 MQTT 브로커로서 Crow Server, ESP32 장치, 테스트 클라이언트 사이의 제어 메시지를 중계합니다. 이 디렉터리는 MQTT 리스너 설정, TLS 인증서 마운트, Kubernetes Deployment/Service 정의를 함께 관리합니다.

**주요 환경 및 버전**
- 베이스 이미지: `eclipse-mosquitto:latest`
- 배포 형태: ConfigMap + Deployment + ClusterIP Service
- 주요 포트: `1883/TCP`, `8883/TCP`

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

###### 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** Internal MQTT Broker with Optional Mutual TLS
- **설명:** Mosquitto는 내부 서비스 간 비동기 장치 제어를 위한 메시지 허브입니다. 내부 기본 포트 `1883`과 TLS 리스너 `8883`을 함께 제공하며, 외부 클라이언트는 일반적으로 Nginx의 `8883` 포트를 통해 접근합니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 역할 | 파일 |
| --- | --- | --- |
| Broker Config | 리스너, TLS 정책, 인증 설정 | `mosquitto.conf`, `mqtt.yaml` |
| Deployment | 브로커 실행과 Config/Cert 마운트 | `mqtt.yaml` |
| Service | 내부 `mqtt-service` 노출 | `mqtt.yaml` |
| TLS Secret | `ca.crt`, `server.crt`, `server.key` 제공 | `mqtt.yaml` 참조 |
| Docker Image | 로컬 빌드용 설정 복사 이미지 | `Dockerfile` |

###### 모듈 상세 (Module Detail)

| 파일 | 상세 책임 |
| --- | --- |
| `mosquitto.conf` | 로컬/이미지용 브로커 설정 |
| `mqtt.yaml` | ConfigMap, Deployment, Service를 한 파일에 정의 |
| `Dockerfile` | `mosquitto.conf`를 컨테이너 설정 경로로 복사 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
[ Crow Server ] --------\
[ ESP32 / STM32 ] ------+--> [ mqtt-service :1883 / :8883 ]
[ Test Client ] --------/           |
                                    +--> topic: motor/control
                                    +--> topic: laser/control
                                    +--> topic: system/control
                                    +--> topic: motor/response
                                    \--> topic: system/status

External MQTTS
  Client -> Nginx :8883 -> mqtt-service :1883
```

###### Features

- **기능 1:** 내부 기본 리스너 `1883`에서 일반 MQTT를 제공합니다.
- **기능 2:** `8883` 리스너에서 서버 인증서와 클라이언트 인증서를 요구하는 TLS 연결을 지원합니다.
- **기능 3:** `use_identity_as_username true`로 인증서 주체를 식별자로 활용합니다.
- **기능 4:** 모터, 레이저, 시스템 watchdog 제어용 토픽을 한 브로커에서 통합 관리합니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **클러스터:** K3s 또는 Kubernetes
- **시크릿:** `mqtt-certs`
- **필수 파일 키:** `ca.crt`, `server.crt`, `server.key`
- **클라이언트 도구:** `mosquitto_pub`, `mosquitto_sub`가 있으면 검증이 쉬움

###### 경로 및 설정 (Path Configurations)

- **핵심 파일**
  - `mqtt.yaml`
  - `mosquitto.conf`
- **서비스명**
  - `mqtt-service`
- **마운트 경로**
  - `/mosquitto/config/mosquitto.conf`
  - `/mosquitto/certs`

###### Dependency Setup

Secret을 준비한 뒤 배포합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/mosquitto/mqtt.yaml
```

##### 4. 설정 가이드 (Configuration)

###### 설정 파일명 1: `mqtt.yaml`

- ConfigMap 이름: `mosquitto-config`
- Deployment 이름: `mqtt-broker`
- Service 이름: `mqtt-service`
- 노출 포트
  - `1883/TCP`
  - `8883/TCP`

###### 설정 파일명 2: `mosquitto.conf`

- `listener 1883`
- `allow_anonymous true`
- `listener 8883`
- `cafile /mosquitto/certs/ca.crt`
- `certfile /mosquitto/certs/server.crt`
- `keyfile /mosquitto/certs/server.key`
- `require_certificate true`
- `use_identity_as_username true`
- `allow_anonymous false`

###### 보안 및 토픽

- 제어 토픽: `motor/control`, `laser/control`, `system/control`
- 상태/응답 토픽: `motor/response`, `system/status`
- 외부 TLS 종단은 보통 Nginx `8883`에서 수행

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

로컬 이미지가 필요하면 다음과 같이 빌드할 수 있습니다.

```bash
docker build -t local/mqtt-broker:dev RaspberryPi/k3s-cluster/mosquitto
```

Kubernetes 반영:

```bash
kubectl apply -f RaspberryPi/k3s-cluster/mosquitto/mqtt.yaml
```

###### Static Analysis

- `kubectl apply --dry-run=client -f mqtt.yaml`
- `mqtt-certs` Secret 키명 확인
- `mosquitto.conf`와 ConfigMap 본문이 동일한지 검토

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

```bash
kubectl get pods -l app=mqtt -o wide
kubectl get svc mqtt-service
kubectl logs deploy/mqtt-broker
```

###### Test (검증 방법)

내부 평문 MQTT 확인:

```bash
mosquitto_sub -h mqtt-service -p 1883 -t '#' -v
```

외부 MQTTS 확인:

```bash
openssl s_client -connect <LB_IP>:8883 \
  -cert client-qt.crt -key client-qt.key -CAfile rootCA.crt
```

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Start:** ConfigMap과 `mqtt-certs` Secret이 마운트된 뒤 브로커가 `1883`과 `8883` 리스너를 엽니다.
- **Publish/Subscribe:** Crow Server와 장치가 제어 토픽을 publish/subscribe 합니다.
- **Response Path:** 장치 응답은 `motor/response`와 `system/status`로 되돌아와 Crow Server가 이를 해석합니다.

###### Command Reference

| 구분 | 토픽/명령 | 설명 |
| --- | --- | --- |
| 제어 | `motor/control` | 모터 ASCII 명령 publish |
| 제어 | `laser/control` | `laser on`, `laser off` publish |
| 제어 | `system/control` | ESP32 watchdog/상태 제어 |
| 응답 | `motor/response` | 모터/레이저 응답 수신 |
| 응답 | `system/status` | ESP32 상태 보고 |

###### Stream/Data Format

| 항목 | 형식 |
| --- | --- |
| 프로토콜 | MQTT, MQTT over TLS |
| 페이로드 | ASCII 문자열 |
| 레이저 예시 | `laser on`, `laser off` |
| 응답 예시 | `READY`, `OK`, `LED ON`, `ERR` |

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| publish가 실패함 | 브로커 미기동 또는 서비스명 오기 | `mqtt-broker`, `mqtt-service` 확인 |
| TLS 리스너가 열리지 않음 | `mqtt-certs` Secret 키 불일치 | `ca.crt`, `server.crt`, `server.key` 확인 |
| 인증서 연결은 되지만 명령이 동작하지 않음 | 애플리케이션 토픽 문제 | `motor/control`, `laser/control`, `motor/response` 토픽 확인 |
| 외부 8883은 되는데 내부 1883이 안 됨 | Nginx 경로와 브로커 경로 혼동 | 내부는 `mqtt-service:1883`, 외부는 Nginx `8883`으로 구분 확인 |

###### Operational Checklist

- `mqtt-certs` Secret이 준비되었는가
- `mqtt-service`가 `1883`, `8883` 포트를 노출하는가
- Crow Server의 `MQTT_HOST`, `MQTT_PORT`가 브로커와 일치하는가
- 테스트 클라이언트로 publish/subscribe가 가능한가
- 레이저와 모터 응답 토픽 흐름이 확인되는가

**작성자:** VEDA Team  
**마지막 업데이트:** 2026-03-19
