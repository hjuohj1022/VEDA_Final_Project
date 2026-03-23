### 컴포넌트 명칭

Crow Server는 Raspberry Pi K3s 클러스터 내부에서 동작하는 REST/WebSocket 게이트웨이입니다. 사용자 인증과 계정 관리, SUNAPI 프록시, CCTV 제어, 열화상 스트림 제어, MQTT 기반 모터/레이저/ESP32 상태 제어를 통합 제공하는 서버 컴포넌트입니다.

**주요 환경 및 버전**
- 운영체제 및 최소 버전: Ubuntu 22.04 LTS 계열 컨테이너 런타임, K3s 클러스터
- 컴파일/도구체인: CMake 3.11+, C++17, GCC 계열 빌드 환경
- 핵심 의존성 및 버전: Crow 1.2.0, jwt-cpp 0.6.0, Boost system/thread, OpenSSL, libcurl, mysqlclient, mosquitto/mosquittopp

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

이 섹션은 Crow Server의 설계 역할과 주요 모듈 책임을 정의합니다.

###### 설계 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** API Gateway + Proxy + MQTT Bridge + WebSocket Streaming Bridge
- **설명:** Crow Server는 외부 요청을 직접 처리하는 애플리케이션 게이트웨이 역할을 하며, 일부 기능은 자체 처리하고 일부 기능은 SUNAPI, CCTV backend, MQTT broker, MariaDB, Thermal UDP 경로로 중계합니다. 인증/계정 관리처럼 서버 내부에서 완결되는 기능과, CCTV/열화상/모터/레이저처럼 외부 시스템과 협력하는 기능을 한 프로세스에서 통합하지만, 기능별 매니저와 프록시 모듈로 책임을 분리해 유지보수성을 확보합니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 컴포넌트의 주된 역할 | 주요 파일 경로 |
| --- | --- | --- |
| Crow Server Core | 라우트 등록, 서비스 초기화, 프로세스 진입점 | `src/main.cpp` |
| Auth / Account API | 로그인, 회원가입, 2FA, 계정 삭제/비밀번호 변경 | `src/main.cpp`, `src/AuthRecoveryRoutes.cpp` |
| SUNAPI Proxy | 카메라 SUNAPI HTTP 프록시 | `src/SunapiProxy.cpp` |
| SUNAPI WS Proxy | StreamingServer WebSocket 프록시 | `src/SunapiWsProxy.cpp` |
| CCTV Proxy | CCTV backend 제어 API와 WebSocket 스트림 연결 | `src/CctvProxy.cpp`, `src/CctvManager.cpp` |
| Thermal Proxy | 내부 UDP 열화상 수신과 WebSocket 브리지 | `src/ThermalProxy.cpp` |
| MQTT Device Bridge | 모터/레이저/ESP32 상태 제어와 응답 대기 | `src/MotorManager.cpp`, `src/MqttManager.cpp`, `src/EspHealthManager.cpp` |
| API Documentation | Swagger UI 및 OpenAPI 문서 제공 | `swagger/swagger.yaml`, `src/main.cpp` |
| Kubernetes Runtime | Deployment/Service 및 ConfigMap/Secret 연동 | `crow-server.yaml`, `crow-sunapi-config.example.yaml`, `crow-cctv-ip-config.example.yaml` |

###### 모듈 상세 (Module Detail)

| 모듈명 | 상세 책임 정의 |
| --- | --- |
| `src/main.cpp` | 모든 REST 라우트와 보조 서비스 초기화, HTTP 8080 포트 오픈, `/health`, `/docs`, `/swagger.yaml` 제공 |
| `src/AuthRecoveryRoutes.cpp` | 이메일 인증, 비밀번호 재설정 등 계정 복구 흐름 담당 |
| `src/SunapiProxy.cpp` | `/sunapi/stw-cgi/*` 및 SUNAPI 관련 보조 API를 실제 카메라 SUNAPI로 전달 |
| `src/SunapiWsProxy.cpp` | `/sunapi/StreamingServer` WebSocket 업그레이드와 중계 처리 |
| `src/CctvManager.cpp` | TLS 기반 CCTV backend 명령 송수신 관리 |
| `src/CctvProxy.cpp` | `/cctv/control/*`, `/cctv/status`, `/cctv/stream` 라우트 처리 |
| `src/ThermalProxy.cpp` | `/thermal/control/start`, `/thermal/control/stop`, `/thermal/status`, `/thermal/stream` 라우트 및 UDP 수신 상태 관리 |
| `src/MqttManager.cpp` | MQTT broker 연결, publish/subscribe 래퍼 |
| `src/MotorManager.cpp` | 모터와 레이저 명령을 MQTT request-response 흐름으로 변환하고 `motor/response`를 대기 |
| `src/EspHealthManager.cpp` | `system/control`, `system/status` 기반 ESP32 health 제어와 조회 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
External Client
  |
  | HTTPS/WSS via Nginx Gateway
  v
[ Crow Server :8080 ]
  | \
  |  \-- MariaDB (account/auth data)
  | 
  +-- SUNAPI HTTP/WS proxy ----------> Camera SUNAPI endpoint
  +-- CCTV control proxy ------------> CCTV backend / relay service
  +-- MQTT device bridge ------------> mqtt-service:1883
  |                                     |- motor/control
  |                                     |- laser/control
  |                                     |- system/control
  |                                     \- motor/response, system/status
  |
  +-- ThermalProxy (UDP :5005) <------- thermal-dtls-gateway
        \-- WebSocket /thermal/stream -> external client
```

###### Features

- **기능 1:** JWT 기반 로그인, 회원가입, 이메일 인증, 2FA, 계정 삭제/비밀번호 변경 API 제공
- **기능 2:** SUNAPI HTTP/WebSocket 프록시를 통해 카메라 기능을 중앙 API로 노출
- **기능 3:** CCTV backend 제어와 WebSocket 스트림을 프록시하여 외부 클라이언트와 연결
- **기능 4:** 내부 UDP 열화상 패킷을 WebSocket 스트림으로 브리지
- **기능 5:** MQTT를 통해 모터, 레이저, ESP32 watchdog 명령을 제어
- **기능 6:** `/health`, `/docs`, `/swagger.yaml`를 통해 운영 점검과 문서 접근 지원

###### Thermal Additions

- **열화상 입력 호환성:** `thermal-dtls-gateway`가 전달하는 내부 UDP 열화상 chunk에서 `10B(frameId 포함)`, `8B(range 포함)`, `4B(legacy)` 헤더를 모두 허용합니다.
- **WebSocket 출력 정규화:** 입력 헤더 변형과 관계없이 `/thermal/stream` 출력은 항상 `10B canonical header + thermal payload` 형식으로 브로드캐스트합니다.
- **Payload 인코딩 지원:** 열화상 payload는 하드웨어 설정에 따라 `raw 16-bit(38400B)` 또는 `normalized 8-bit(19200B)`가 될 수 있으며, 서버는 완료된 프레임 크기로 인코딩을 판별합니다.
- **서버 측 이벤트 감지:** 완료된 프레임 기준으로 hotspot threshold, baseline, cooldown, consecutive frame 정책을 적용해 MQTT 이벤트를 발행할 수 있습니다.
- **서버 측 actuation 연동:** 설정에 따라 이벤트 publish와 별도로 `motor/control`, `laser/control` 토픽으로 자동 제어 요청을 보낼 수 있습니다.
- **상시 모니터링 모드:** `THERMAL_MONITOR_ALWAYS_ON`이 켜져 있으면 WebSocket 클라이언트가 없어도 수신기와 이벤트 경로를 유지합니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **OS:** Ubuntu 22.04 LTS 계열 컨테이너 런타임 권장
- **Compiler/Toolchain:** CMake 3.11+, C++17, pkg-config, GCC 계열 툴체인
- **Essential Libraries:** Boost system/thread, OpenSSL, libcurl, mysqlclient, mosquitto, mosquittopp

###### 환경 변수 및 경로 설정 (Path Configurations)

Crow Server는 Kubernetes 환경 변수, Secret, ConfigMap, 볼륨 마운트를 동시에 사용합니다.

- **설정 파일명:**
  - `crow-server.yaml`
  - `crow-sunapi-config.example.yaml`
  - `crow-cctv-ip-config.example.yaml`
  - `swagger/swagger.yaml`
- **필수 환경 변수:**
  - `DB_HOST`, `DB_USER`, `DB_PASSWORD`
  - `ADMIN_UNLOCK_KEY` (`crow-admin-secret` Secret에서 주입)
  - `SUNAPI_BASE_URL`, `SUNAPI_WS_URL`, `SUNAPI_INSECURE`, `SUNAPI_TIMEOUT_MS`
  - `SUNAPI_USER`, `SUNAPI_PASSWORD`
  - `CCTV_BACKEND_HOST`, `CCTV_BACKEND_PORT`
  - `THERMAL_UDP_BIND_HOST`, `THERMAL_UDP_PORT`
- **선택 환경 변수 또는 기본값 제공 변수:**
  - `MQTT_HOST`, `MQTT_PORT`
  - `MOTOR_MQTT_CLIENT_ID`, `MOTOR_CONTROL_TOPIC`, `MOTOR_RESPONSE_TOPIC`
  - `MOTOR_COMMAND_TIMEOUT_MS`
  - `LASER_CONTROL_TOPIC`
  - `ESP32_WATCHDOG_CLIENT_ID`, `ESP32_SYSTEM_CONTROL_TOPIC`, `ESP32_SYSTEM_STATUS_TOPIC`
- **경로 주의사항:**
  - `/app/certs` 에는 `rootCA.crt`, `cctv.crt`, `cctv.key`가 필요합니다.
  - `/app/recordings` 는 `/home/pi/cctv-recordings` hostPath와 연결됩니다.
  - `/app/swagger` 아래 Swagger UI 파일과 `swagger.yaml`이 포함되어야 합니다.
- **Secret/ConfigMap 주의사항:**
  - `ADMIN_UNLOCK_KEY`는 클라이언트 `.env`가 아니라 Crow 서버 배포 시 `crow-admin-secret`에서 주입합니다.
  - `crow-server.yaml`은 `crow-admin-secret`의 `ADMIN_UNLOCK_KEY` 키를 참조해야 합니다.

###### Thermal Event / Monitor Variables

- `THERMAL_MONITOR_ALWAYS_ON`: WebSocket 클라이언트가 없어도 열화상 UDP 수신기와 이벤트 감지 경로를 유지합니다.
- `THERMAL_EVENT_ENABLED`, `THERMAL_EVENT_ACTUATE`, `THERMAL_EVENT_TOPIC`: MQTT 이벤트 publish와 서버 측 actuation 동작을 제어합니다.
- `THERMAL_EVENT_THRESHOLD_MAX_VALUE`, `THERMAL_EVENT_CONSECUTIVE_FRAMES`, `THERMAL_EVENT_COOLDOWN_MS`: hotspot 판정 민감도와 연속 프레임 조건, 재발행 간격을 조절합니다.
- `THERMAL_EVENT_BASELINE_ENABLED`, `THERMAL_EVENT_BASELINE_MARGIN`, `THERMAL_EVENT_BASELINE_WINDOW_MS`, `THERMAL_EVENT_BASELINE_MIN_SAMPLES`, `THERMAL_EVENT_BASELINE_GUARD_DELTA`: baseline 기반 adaptive threshold 계산에 사용됩니다.
- `THERMAL_EVENT_MQTT_CLIENT_ID`, `THERMAL_EVENT_SOURCE`, `THERMAL_EVENT_SEVERITY`, `THERMAL_EVENT_TITLE`: 이벤트 MQTT payload 메타데이터를 구성합니다.
- `THERMAL_EVENT_MOTOR1_ANGLE`, `THERMAL_EVENT_MOTOR2_ANGLE`, `THERMAL_EVENT_MOTOR3_ANGLE`, `THERMAL_EVENT_LASER_ENABLED`: actuation 활성화 시 motor/laser 제어값을 지정합니다.

###### Dependency Setup

로컬 빌드 시 필요한 패키지 예시는 다음과 같습니다.

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake pkg-config \
  libssl-dev libboost-system-dev libboost-thread-dev \
  libmysqlclient-dev libcurl4-openssl-dev \
  libmosquitto-dev libmosquittopp-dev
```

컨테이너 빌드는 `Dockerfile` 기준으로 수행합니다.

```bash
docker build -t local/crow-server:dev RaspberryPi/k3s-cluster/crow_server
```

##### 4. 설정 가이드 (Configuration)

이 섹션은 Crow Server 운영 시 필요한 주요 설정 파일과 파라미터를 정리합니다.

###### 설정 파일명 1: `crow-server.yaml`

- Deployment와 Service 정의
- 내부 HTTP 포트 `8080/TCP`
- 내부 Thermal UDP 포트 `5005/UDP`
- `crow-certs` Secret 마운트
- `crow-admin-secret`의 `ADMIN_UNLOCK_KEY` 환경 변수 주입
- `/app/recordings` hostPath 마운트
- `pi-worker1` 노드 고정 배치

###### 설정 파일명 2: `crow-sunapi-config.example.yaml`

- `SUNAPI_BASE_URL`
- `SUNAPI_WS_URL`
- `SUNAPI_INSECURE`
- `SUNAPI_TIMEOUT_MS`

###### 설정 파일명 3: `crow-cctv-ip-config.example.yaml`

- `CCTV_BACKEND_HOST`
- `CCTV_BACKEND_PORT`

###### 보안 및 통신 설정: 인증서와 MQTT

- 인증서 경로:
  - `/app/certs/rootCA.crt`
  - `/app/certs/cctv.crt`
  - `/app/certs/cctv.key`
- MQTT 제어 기본 토픽:
  - `motor/control`
  - `laser/control`
  - `motor/response`
  - `system/control`
  - `system/status`
- API 문서 경로:
  - `/docs`
  - `/swagger.yaml`

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

로컬 빌드 예시는 다음과 같습니다.

```bash
cd RaspberryPi/k3s-cluster/crow_server
mkdir -p build
cd build
cmake ..
cmake --build . --config Release -j
```

컨테이너 빌드 예시는 다음과 같습니다.

```bash
docker build -t local/crow-server:dev RaspberryPi/k3s-cluster/crow_server
```

Kubernetes 배포는 다음과 같습니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/crow_server/crow-server.yaml
```

###### Static Analysis (정적 검증)

현재 이 디렉터리에는 별도 `clang-tidy` 전용 스크립트는 포함되어 있지 않습니다. 대신 다음 수준의 검증을 권장합니다.

- **Local IDE Analysis:** CMake configure/build 단계에서 경고와 누락된 의존성 확인
- **Container Build Analysis:** `docker build` 단계에서 런타임 의존성 누락 여부 확인
- **Manifest Validation:** `kubectl apply --dry-run=client -f crow-server.yaml`

예시:

```bash
kubectl apply --dry-run=client -f RaspberryPi/k3s-cluster/crow_server/crow-server.yaml
```

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

로컬 바이너리 실행 예시:

```bash
./crow_server
```

Kubernetes 환경에서 로그 확인:

```bash
kubectl get pods -l app=crow-server -o wide
kubectl logs deploy/crow-server
```

문서와 헬스체크 확인:

```bash
curl --cert client-qt.crt --key client-qt.key --cacert rootCA.crt \
  https://<LB_IP>/health

curl --cert client-qt.crt --key client-qt.key --cacert rootCA.crt \
  https://<LB_IP>/docs
```

###### Test (검증 방법)

- **Unit Test:** 현재 전용 단위 테스트 바이너리는 이 디렉터리에 별도로 포함되어 있지 않습니다.
- **Smoke Test:**
  - `GET /health` 응답 확인
  - `GET /swagger.yaml` 또는 `GET /docs` 확인
  - `GET /laser/status` 확인
  - `POST /laser/control/on`, `POST /laser/control/off` 호출 후 실제 장치 반응 확인
  - `GET /thermal/status` 로 내부 UDP bind 상태 확인

- **열화상 추가 확인 포인트:**
  - `GET /thermal/status`에서 `last_frame_encoding=scaled8`이면 현재 하드웨어 기본 설정(`APP_UDP_FRAME_8BIT=1`)과 일치합니다.
  - `GET /thermal/status`에서 `last_frame_encoding=raw16`이면 raw 16-bit frame 경로로 수신 중입니다.
  - 이벤트 기능을 켠 경우 `event.published`, `event.actuation_requests`, `event.last_frame_max_value` 증가 여부를 함께 확인합니다.

레이저 API smoke test 예시:

```bash
curl --cert client-qt.crt --key client-qt.key --cacert rootCA.crt \
  https://<LB_IP>/laser/status

curl --cert client-qt.crt --key client-qt.key --cacert rootCA.crt \
  -X POST https://<LB_IP>/laser/control/on

curl --cert client-qt.crt --key client-qt.key --cacert rootCA.crt \
  -X POST https://<LB_IP>/laser/control/off
```

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Start/Channel Select:**
  - Crow Server 시작 시 SUNAPI, CCTV, Thermal, MQTT 관련 매니저를 초기화합니다.
  - `main.cpp`에서 `registerSunapiProxyRoutes`, `registerSunapiWsProxyRoutes`, `registerCctvProxyRoutes`, `registerThermalProxyRoutes`, `registerMotorRoutes`, `registerLaserRoutes`, `registerEspHealthRoutes` 순으로 라우트를 등록합니다.
- **Pause/Resume:**
  - CCTV backend는 `/cctv/control/pause`, `/cctv/control/resume`로 제어합니다.
  - 열화상 스트림은 `/thermal/control/start`, `/thermal/control/stop`과 WebSocket 연결 상태에 따라 유지됩니다.
- **Stop:**
  - 프로세스 종료 시 `shutdownThermalProxy()`를 호출해 Thermal 수신 스레드를 정리합니다.
  - 모터/레이저 제어는 REST 요청 단위로 MQTT publish 후 응답을 기다리고 종료됩니다.

###### Thermal Runtime Notes

- `/thermal/control/start`는 열화상 WebSocket 사용을 위한 준비 요청이며, `THERMAL_MONITOR_ALWAYS_ON=true` 환경에서는 수신기가 이미 실행 중일 수 있습니다.
- `/thermal/control/stop` 이후에도 `THERMAL_MONITOR_ALWAYS_ON=true`이면 서버 이벤트 감지를 위해 UDP 수신기와 MQTT 경로가 유지될 수 있습니다.
- `/thermal/status`는 단순 UDP bind 상태뿐 아니라 마지막 완성 프레임의 인코딩(`raw16`, `scaled8`)과 이벤트 감지 상태를 함께 노출합니다.

###### Command Reference

주요 제어 API 예시는 다음과 같습니다.

| 기능 | 메서드 | 설명 |
| --- | --- | --- |
| `/health` | `GET` | Crow Server 상태와 Nginx mTLS 헤더 상태 조회 |
| `/docs` | `GET` | Swagger UI HTML 제공 |
| `/swagger.yaml` | `GET` | OpenAPI YAML 제공 |
| `/motor/control/command` | `POST` | 원시 모터 ASCII 명령 publish |
| `/motor/ping` | `GET` | 모터 응답 경로 확인 |
| `/laser/control/on` | `POST` | `laser/control` 토픽으로 `laser on` publish |
| `/laser/control/off` | `POST` | `laser/control` 토픽으로 `laser off` publish |
| `/laser/control/command` | `POST` | `on`, `off`, `laser on`, `laser off` 정규화 전송 |
| `/laser/status` | `GET` | 레이저 MQTT 브리지 상태 조회 |
| `/thermal/control/start` | `POST` | 열화상 수신 시작 보장 및 WebSocket 메타데이터 반환 |
| `/thermal/status` | `GET` | Thermal UDP 수신 상태 조회 |
| `/auth/admin/unlock` | `POST` | 관리자 잠금 해제 키 검증 후 클라이언트 로그인 잠금 해제에 사용할 성공/실패 결과 반환 |
| `/cctv/control/start` | `POST` | CCTV backend 처리 시작 |
| `/cctv/status` | `GET` | CCTV backend 연결 상태 조회 |

- `GET /thermal/status` 응답에는 `last_frame_encoding`, `last_frame_payload_bytes`, `event.*`, `monitor_always_on` 필드가 포함되어 현재 열화상 입력 형식과 이벤트 상태를 함께 확인할 수 있습니다.

레이저 제어 요청 예시:

```json
{
  "command": "laser on"
}
```

정상 응답 예시:

```json
{
  "status": "OK",
  "ok": true,
  "command": "laser on",
  "published": true,
  "timed_out": false,
  "broker_connected": true,
  "response": "LED ON"
}
```

###### Stream/Data Format (데이터 규격)

Crow Server가 직접 다루는 주요 데이터 포맷은 다음과 같습니다.

| 데이터 구분 | ACK 메시지 | Header 구조 | Payload 데이터 |
| --- | --- | --- | --- |
| 모터/레이저 제어 REST | HTTP JSON 응답 | HTTP 헤더 + JSON 본문 | MQTT publish 결과와 장치 응답 문자열 |
| MQTT 장치 응답 | `motor/response` 수신 | 별도 바이너리 헤더 없음 | ASCII line 기반 응답 (`READY`, `OK ...`, `LED ON`, `ERR` 등) |
| Thermal WebSocket | WebSocket binary | 10-byte thermal chunk header | 열화상 payload (`u16be`) |
| Swagger 문서 | HTTP 200 | text/html 또는 text/yaml | UI HTML 또는 OpenAPI YAML |

- **Byte Order:**
  - Thermal UDP/WebSocket header는 big-endian 16-bit 필드를 사용합니다.
  - 모터/레이저 응답은 ASCII 문자열 기반입니다.
- **Header Alignment:**
  - Thermal chunk header는 고정 10 bytes입니다.
  - MQTT 모터/레이저 응답은 별도 정렬 개념 없이 line protocol입니다.

###### Thermal Compatibility Notes

- 내부 UDP 입력은 `10B(frameId + chunkIndex + totalChunks + minValue + maxValue)`, `8B(chunkIndex + totalChunks + minValue + maxValue)`, `4B(chunkIndex + totalChunks)` 헤더를 모두 허용합니다.
- WebSocket 출력은 항상 `10B canonical header + thermal payload`로 정규화됩니다.
- thermal payload는 하드웨어 설정에 따라 `raw 16-bit(38400B)` 또는 `normalized 8-bit(19200B)`가 될 수 있습니다.
- `4B legacy` 입력도 전체 raw16 프레임이 완성되면 서버가 payload에서 실제 range를 계산해 이벤트 판정에 사용할 수 있습니다.

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting (자주 발생하는 문제)

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| `/docs` 또는 `/swagger.yaml`가 열리지 않음 | Swagger 파일 번들 누락 또는 Nginx 경로 문제 | `/app/swagger` 복사 여부와 Nginx 프록시 경로를 확인 |
| `503 MQTT_UNAVAILABLE` | MQTT broker 연결 실패 또는 publish 실패 | `MQTT_HOST`, `MQTT_PORT`, `mqtt-broker` 상태, `/laser/status`의 `broker_connected` 확인 |
| `504 TIMEOUT` | `motor/response` 응답 미수신 | ESP32의 `laser/control` 구독 상태, UART 연결, STM32 응답 경로 확인 |
| `GET /thermal/status`에서 수신이 늘지 않음 | Thermal DTLS Gateway 또는 Crow Server UDP bind 문제 | `THERMAL_UDP_BIND_HOST`, `THERMAL_UDP_PORT`, `thermal-dtls-gateway` 로그 확인 |
| DB 연결 실패 | MariaDB Secret 또는 Service 문제 | `DB_HOST`, `mariadb-service`, `mariadb-secret` 키 이름 확인 |
| CCTV 제어 실패 | CCTV backend 주소 또는 인증서 문제 | `CCTV_BACKEND_HOST`, `CCTV_BACKEND_PORT`, `/app/certs` 내부 인증서 확인 |
| `/auth/admin/unlock`가 항상 실패함 | `crow-admin-secret` 누락, `ADMIN_UNLOCK_KEY` 미주입, 또는 잘못된 secret 값 | `kubectl get secret crow-admin-secret`, `crow-server.yaml`의 `secretKeyRef`, Crow 배포 로그를 확인 |

###### Operational Checklist (배포 전 최종 확인)

- `crow-server.yaml`의 `nodeSelector`가 실제 `pi-worker1` 노드와 일치하는가
- `crow-certs` Secret에 `rootCA.crt`, `cctv.crt`, `cctv.key`가 모두 포함되어 있는가
- `crow-sunapi-config`, `crow-sunapi-secret`, `crow-cctv-ip-config`, `mariadb-secret`, `crow-admin-secret`이 준비되어 있는가
- `MQTT_HOST`, `MOTOR_RESPONSE_TOPIC`, `LASER_CONTROL_TOPIC` 기본값 또는 override 값이 환경과 맞는가
- `/home/pi/cctv-recordings` 경로가 쓰기 가능한가
- `GET /health`, `GET /docs`, `GET /laser/status`, `GET /thermal/status`가 정상 응답하는가

**작성자:** VEDA Team  
**마지막 업데이트:** 2026-03-22
