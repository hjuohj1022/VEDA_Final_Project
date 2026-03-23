### 컴포넌트 명칭

Thermal DTLS Gateway는 열화상 하드웨어가 보내는 DTLS/UDP 패킷을 PSK 기반으로 복호화해 Crow Server의 UDP 입력으로 전달하는 게이트웨이입니다. 현재 ESP32 펌웨어가 DTLS가 아닌 plain UDP thermal chunk를 송신하는 운영 상황도 확인되어, 이 게이트웨이는 DTLS 종료뿐 아니라 plain UDP thermal chunk fallback도 함께 처리합니다. 이 디렉터리는 C++ 소스, OpenSSL 기반 빌드 설정, Deployment/Service, NetworkPolicy, Secret 예시를 포함합니다.

**주요 환경 및 버전**
- 런타임 베이스: `ubuntu:22.04`
- 빌드 도구: CMake + OpenSSL
- 주요 포트: `5005/UDP`

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

###### 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** DTLS Termination + UDP Forwarder + Plain UDP Compatibility
- **설명:** 센서 장비와의 DTLS 세션은 이 게이트웨이에서 종료하고, 복호화된 thermal payload는 내부 평문 UDP로 Crow Server에 전달합니다. 또한 첫 datagram을 분류해 현재 ESP32 펌웨어가 보내는 plain UDP thermal chunk도 Crow로 직접 forward할 수 있습니다. 필요 시 cookie exchange와 프레임 통계 로깅을 활성화해 열화상 스트림 품질을 추적할 수 있습니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 역할 | 파일 |
| --- | --- | --- |
| Gateway Binary | DTLS 서버, UDP 포워딩, 통계 수집 | `src/main.cpp` |
| Build Definition | OpenSSL 링크와 바이너리 생성 | `CMakeLists.txt` |
| Deployment | 컨테이너 실행과 환경변수 주입 | `thermal-dtls-gateway.yaml` |
| Service | `5005/UDP` ClusterIP 노출 | `thermal-dtls-gateway.yaml` |
| NetworkPolicy | Nginx Pod에서만 ingress 허용 | `thermal-dtls-networkpolicy.yaml` |
| Secret Example | PSK와 cookie secret 예시 | `thermal-dtls-secret.example.yaml` |

###### 모듈 상세 (Module Detail)

| 파일 | 상세 책임 |
| --- | --- |
| `src/main.cpp` | DTLS 바인드, PSK 검증, cookie exchange, 첫 datagram 분류, plain UDP fallback, UDP forward, frame stats 처리 |
| `CMakeLists.txt` | `thermal_dtls_gateway` 실행 파일 생성 및 OpenSSL 연결 |
| `Dockerfile` | 멀티스테이지 빌드로 바이너리 생성 후 런타임 이미지에 복사 |
| `thermal-dtls-gateway.yaml` | Deployment와 `thermal-dtls-gateway-service` 정의 |
| `thermal-dtls-networkpolicy.yaml` | `app=nginx-gateway`에서 오는 `5005/UDP`만 허용 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
Thermal Sensor
      |
      | DTLS/UDP + PSK or plain UDP thermal chunk
      v
[ Thermal DTLS Gateway :5005/UDP ]
      |
      | decrypted or raw UDP forward
      v
[ crow-server-service :5005/UDP ]
      |
      \--> Crow Server thermal websocket bridge
```

###### Features

- **기능 1:** `DTLS_PSK_IDENTITY`, `DTLS_PSK_KEY_HEX`로 DTLS PSK 인증을 수행합니다.
- **기능 2:** `DTLS_USE_COOKIE_EXCHANGE`를 켜면 `DTLSv1_listen` 기반 cookie 검증을 수행합니다.
- **기능 3:** 첫 UDP datagram을 검사해 DTLS record와 plain thermal chunk를 자동 분류합니다.
- **기능 4:** plain thermal chunk가 감지되면 `DTLS_ALLOW_PLAIN_UDP_FALLBACK=true`일 때 DTLS handshake 없이 Crow로 직접 forward합니다.
- **기능 5:** 복호화되었거나 raw로 수신된 thermal payload를 `CROW_FORWARD_HOST:CROW_FORWARD_PORT`로 UDP 전송합니다.
- **기능 6:** 프레임 누락, 중복, incomplete frame 통계를 로그로 남길 수 있습니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **빌드 도구:** CMake, C++ 컴파일러, OpenSSL 개발 헤더
- **클러스터:** K3s 또는 Kubernetes
- **시크릿:** `thermal-dtls-secret`
- **상위 의존:** Crow Server가 UDP `5005`를 수신 중이어야 함

###### 경로 및 설정 (Path Configurations)

- **핵심 파일**
  - `src/main.cpp`
  - `CMakeLists.txt`
  - `thermal-dtls-gateway.yaml`
  - `thermal-dtls-networkpolicy.yaml`
  - `thermal-dtls-secret.example.yaml`
- **필수 환경변수**
  - `DTLS_BIND_HOST`
  - `DTLS_BIND_PORT`
  - `CROW_FORWARD_HOST`
  - `CROW_FORWARD_PORT`
  - `DTLS_PSK_IDENTITY`
  - `DTLS_PSK_KEY_HEX`
- **선택 환경변수**
  - `DTLS_USE_COOKIE_EXCHANGE`
  - `DTLS_ALLOW_PLAIN_UDP_FALLBACK`
  - `DTLS_COOKIE_SECRET_HEX`
  - `DTLS_ENABLE_FRAME_STATS`
  - `DTLS_UDP_RCVBUF_BYTES`
  - `DTLS_UDP_SNDBUF_BYTES`
  - `DTLS_STATS_LOG_INTERVAL_MS`
  - `DTLS_FRAME_TRACK_TIMEOUT_MS`
  - `DTLS_MAX_TRACKED_FRAMES`

###### Dependency Setup

Secret 준비 후 Deployment와 NetworkPolicy를 적용합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/thermal_dtls_gateway/thermal-dtls-gateway.yaml
kubectl apply -f RaspberryPi/k3s-cluster/thermal_dtls_gateway/thermal-dtls-networkpolicy.yaml
```

##### 4. 설정 가이드 (Configuration)

###### 설정 파일명 1: `thermal-dtls-gateway.yaml`

- Deployment 이름: `thermal-dtls-gateway`
- Service 이름: `thermal-dtls-gateway-service`
- 서비스 포트: `5005/UDP`
- 기본 바인드: `0.0.0.0:5005`
- 기본 포워드: `crow-server-service:5005`
- plain UDP fallback: `DTLS_ALLOW_PLAIN_UDP_FALLBACK=true`

###### 설정 파일명 2: `thermal-dtls-secret.example.yaml`

- `DTLS_PSK_IDENTITY`
- `DTLS_PSK_KEY_HEX`
- `DTLS_COOKIE_SECRET_HEX`

###### 설정 파일명 3: `thermal-dtls-networkpolicy.yaml`

- `default` 네임스페이스
- `app=nginx-gateway` Pod에서 들어오는 `5005/UDP`만 허용

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

로컬 빌드 예시:

```bash
cd RaspberryPi/k3s-cluster/thermal_dtls_gateway
mkdir -p build
cd build
cmake ..
cmake --build . --config Release -j
```

컨테이너 빌드 예시:

```bash
docker build -t local/thermal-dtls-gateway:dev RaspberryPi/k3s-cluster/thermal_dtls_gateway
```

###### Static Analysis

- `kubectl apply --dry-run=client -f thermal-dtls-gateway.yaml`
- `kubectl apply --dry-run=client -f thermal-dtls-networkpolicy.yaml`
- PSK 관련 Secret 키 존재 여부 확인

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

```bash
kubectl get pods -l app=thermal-dtls-gateway -o wide
kubectl get svc thermal-dtls-gateway-service
kubectl logs deploy/thermal-dtls-gateway
```

###### Test (검증 방법)

- DTLS handshake 또는 plain UDP fallback 로그 확인
- UDP forward 로그 확인
- Crow Server의 `GET /thermal/status`에서 수신 증가 확인

간접 점검 순서:

1. Nginx `5005/UDP` 서비스가 열려 있는지 확인
2. Thermal Gateway 로그에서 `Handshake complete` 또는 `Plain thermal UDP session active` 로그 확인
3. Crow Server thermal 상태 API 확인

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Bind:** 게이트웨이는 `DTLS_BIND_HOST:DTLS_BIND_PORT`에서 UDP 소켓을 엽니다.
- **Classify:** 첫 datagram을 보고 DTLS record인지, plain thermal chunk인지 분류합니다.
- **Handshake:** DTLS record면 센서가 DTLS PSK 핸드셰이크를 수행하고, 선택적으로 cookie exchange를 거칩니다.
- **Fallback Forward:** plain thermal chunk면 handshake 없이 Crow Server로 그대로 `sendto()`합니다.
- **Forward:** DTLS 세션이 성립하면 복호화된 payload를 `CROW_FORWARD_HOST:CROW_FORWARD_PORT`로 `sendto()`합니다.
- **Observe:** 통계가 활성화되면 패킷 수, 바이트 수, 중복 chunk, 누락 frame 등을 로그에 집계합니다.

###### Command Reference

| 구분 | 값/명령 | 설명 |
| --- | --- | --- |
| 바인드 | `DTLS_BIND_PORT=5005` | DTLS 수신 포트 |
| 포워드 | `CROW_FORWARD_HOST=crow-server-service` | Crow Server 전달 대상 |
| 인증 | `DTLS_PSK_IDENTITY` | 허용할 PSK identity |
| 인증 | `DTLS_PSK_KEY_HEX` | PSK hex 키 |
| 호환 | `DTLS_ALLOW_PLAIN_UDP_FALLBACK=true` | plain UDP thermal chunk를 DTLS 없이 직접 forward |
| 운영 | `kubectl logs deploy/thermal-dtls-gateway` | 핸드셰이크 및 통계 로그 확인 |

###### Stream/Data Format

| 항목 | 형식 |
| --- | --- |
| 외부 입력 | DTLS 1.2 over UDP 또는 plain UDP thermal chunk |
| 인증 | DTLS 사용 시 PSK, fallback plain UDP는 추가 인증 없음 |
| 내부 전달 | 평문 UDP |
| payload | thermal frame binary |
| 통계 필드 | packet/frame/chunk 기반 카운터 |

###### Compatibility Note

- 현재 저장소의 ESP32 펌웨어는 설정 이름에 `DTLS`가 남아 있지만 실제 전송은 `lwip_send()` 기반 plain UDP thermal chunk입니다.
- 따라서 gateway 로그에서 `Peer accepted for handshake` 뒤 `SSL_accept timed out`가 반복되면, DTLS 미구현 클라이언트를 handshake로 해석하고 있을 가능성이 큽니다.
- 이 경우 `DTLS_ALLOW_PLAIN_UDP_FALLBACK=true`를 유지하고, `Initial datagram looks like plain thermal UDP` 로그가 보이는지 확인합니다.

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| DTLS handshake 실패 | PSK identity/key 불일치 | `DTLS_PSK_IDENTITY`, `DTLS_PSK_KEY_HEX` 대조 |
| `Peer accepted for handshake` 뒤 `SSL_accept timed out` 반복 | 센서가 DTLS가 아니라 plain UDP를 전송하거나 DTLS record가 아닌 payload를 전송 | `DTLS_ALLOW_PLAIN_UDP_FALLBACK=true` 유지, ESP32 송신 구현 확인, plain UDP 분류 로그 확인 |
| cookie exchange 단계 실패 | cookie secret 또는 클라이언트 구현 문제 | `DTLS_USE_COOKIE_EXCHANGE`, `DTLS_COOKIE_SECRET_HEX` 확인 |
| Crow Server로 전달되지 않음 | 포워드 대상/포트 오기 또는 Crow UDP 미수신 | `CROW_FORWARD_HOST`, `CROW_FORWARD_PORT`, Crow 상태 확인 |
| 패킷 손실 통계 증가 | 네트워크 지연 또는 처리량 부족 | 버퍼 크기, stats 설정, 네트워크 경로 점검 |

###### Operational Checklist

- `thermal-dtls-secret`가 준비되었는가
- `thermal-dtls-gateway-service`가 `5005/UDP`를 노출하는가
- `thermal-dtls-networkpolicy.yaml`이 Nginx ingress만 허용하도록 적용되었는가
- Crow Server가 `5005/UDP`를 바인드하고 있는가
- 현재 ESP32 펌웨어가 DTLS가 아닌 plain UDP thermal chunk를 보내는지 운영 설정과 일치하는가
- `GET /thermal/status`와 게이트웨이 로그가 함께 증가하는가

**작성자:** VEDA Team  
**마지막 업데이트:** 2026-03-23
