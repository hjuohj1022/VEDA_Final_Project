# Thermal Streaming Network Protocol

## 1. 문서 목적

이 문서는 이번 열화상 화면 표시 작업을 네트워크 프로토콜 관점에서 정리한 문서다.
주요 목표는 다음과 같다.

- 하드웨어에서 생성한 열화상 프레임이 어떤 네트워크 경로를 타고 Qt 클라이언트 화면에 표시되는지 설명한다.
- 기존 `MQTT over TLS` 기반 방식에서 `UDP + WSS` 기반 방식으로 변경한 이유를 설명한다.
- 제어 채널과 데이터 채널이 어떻게 분리되어 있는지 설명한다.
- 각 계층에서 사용하는 프로토콜, 포트, 메시지 포맷, 인증 방식을 정리한다.
- 운영 중 발생할 수 있는 `404`, chunk drop, frame drop, black flicker 같은 현상을 어떻게 해석해야 하는지 설명한다.

---

## 2. 전체 구조 요약

현재 열화상 스트리밍은 아래와 같은 4단 구조를 가진다.

1. 하드웨어(ESP32/Lepton 등)가 열화상 raw frame을 `UDP` datagram으로 전송
2. `nginx`가 외부 게이트웨이로서 `5005/UDP`를 수신하고 내부 `crow-server`로 프록시
3. `crow-server`가 UDP datagram을 받아 WebSocket binary message로 재브로드캐스트
4. Qt 클라이언트가 `WSS`로 스트림을 수신하고 프레임을 조립한 뒤 QML 화면에 표시

즉, 외부에서 볼 때는 `nginx`만 공개되고, 내부에서는 `crow-server`가 열화상 프록시 역할을 수행한다.

---

## 3. 설계 변경 배경

초기 열화상 수신 구조는 `MQTT over TLS`를 사용했다.
이 방식은 제어/상태 전달에는 적합하지만, 열화상 프레임처럼 크기가 크고 연속성이 중요한 데이터를 다룰 때 다음 문제가 있었다.

- 프레임 chunk 수가 많을수록 broker와 subscriber를 거치며 지연이 누적됨
- 실시간 표시 시 FPS가 낮아짐
- 프레임이 늦게 도착하면 이미 다음 프레임으로 넘어가 조립 실패가 발생할 수 있음

이 문제를 줄이기 위해 열화상 프레임 자체는 `UDP`로 전환했다.
대신 Qt와 서버 사이에는 인증과 제어가 쉬운 `HTTPS + WSS`를 유지했다.

결과적으로 현재 구조는 다음처럼 채널이 분리된다.

- 제어 채널: `HTTPS`
- 스트림 채널: `WSS`
- 하드웨어 수신 채널: `UDP`

---

## 4. 프로토콜별 역할 분리

### 4.1 HTTPS

HTTPS는 제어 plane 역할을 한다.
Qt 클라이언트는 열화상 스트림 시작/중지와 상태 확인을 위해 REST API를 호출한다.

사용 API:

- `POST /thermal/control/start`
- `POST /thermal/control/stop`
- `GET /thermal/status`

역할:

- 스트리밍 의도 전달
- 서버 수신기 준비
- 인증 처리
- 운영 상태 조회

### 4.2 WSS

WSS는 서버에서 클라이언트로 열화상 binary frame chunk를 전달하는 data plane 역할을 한다.

사용 엔드포인트:

- `GET /thermal/stream` with WebSocket upgrade

역할:

- 서버가 받은 UDP datagram을 그대로 Qt 클라이언트에 binary message로 전달
- Qt가 binary chunk를 frame 단위로 재조립

### 4.3 UDP

UDP는 하드웨어에서 서버 쪽으로 열화상 raw 데이터를 전달하는 수단이다.

사용 포트:

- 외부 게이트웨이 수신 포트: `5005/UDP`
- 내부 `crow-server` 서비스 포트: `5005/UDP`

역할:

- 낮은 오버헤드로 열화상 chunk 전달
- 재전송/세션 유지보다 지연 최소화에 집중

---

## 5. 현재 네트워크 경로

### 5.1 제어 채널 경로

Qt 클라이언트는 로그인 후 `Authorization: Bearer <JWT>`를 포함한 HTTPS 요청으로 열화상 제어 API를 호출한다.

경로:

`Qt Client -> nginx:443/TCP -> crow-server-service:8080/TCP -> ThermalProxy`

### 5.2 스트림 채널 경로

Qt 클라이언트는 `wss://<API_URL host>/thermal/stream`로 접속한다.

경로:

`Qt Client -> nginx:443/TCP (WSS) -> crow-server-service:8080/TCP -> ThermalProxy`

### 5.3 하드웨어 수신 채널 경로

하드웨어는 외부에서 직접 `nginx`만 바라본다.

경로:

`Hardware -> nginx-service external IP:5005/UDP -> nginx stream proxy -> crow-server-service:5005/UDP -> ThermalProxy recvfrom()`

이 구조에서 외부에 직접 개방되는 것은 `nginx`뿐이다.
`crow-server`는 ClusterIP 내부 서비스로만 유지된다.

---

## 6. 구성 요소별 책임

### 6.1 Hardware

하드웨어는 열화상 프레임을 여러 개의 chunk로 분할하여 UDP datagram으로 전송한다.

하드웨어 책임:

- frame id 부여
- chunk index / total chunk count 기록
- min/max thermal range 기록
- 8-bit 또는 16-bit payload 생성

### 6.2 nginx

`nginx`는 외부 게이트웨이 역할을 한다.

HTTP/WSS 측 책임:

- TLS/mTLS termination
- `/thermal/stream` WebSocket upgrade 처리
- 내부 `crow-server-service:8080`로 프록시

UDP 측 책임:

- `5005/UDP` 수신
- 내부 `crow-server-service:5005/UDP`로 L4 프록시

즉, `nginx`는 열화상 payload 내용을 해석하지 않고 전달만 담당한다.

### 6.3 crow-server / ThermalProxy

`crow-server`의 `ThermalProxy`는 열화상 브리지 역할을 한다.

책임:

- JWT 인증 검증
- UDP 수신기 생성 및 관리
- 수신한 datagram 메타데이터 통계 업데이트
- WebSocket 클라이언트 관리
- 수신 datagram을 연결된 모든 WS 클라이언트에 binary로 브로드캐스트

중요한 점:

- 서버는 프레임을 재압축하지 않는다.
- UDP datagram을 그대로 WSS binary message로 전달한다.
- 이 구조는 지연을 최소화하는 데 초점을 둔다.

### 6.4 Qt Client

Qt 클라이언트는 열화상 소비자 역할을 한다.

책임:

- `POST /thermal/control/start` 호출
- `Authorization` 헤더를 포함해 WSS handshake 수행
- binary chunk 수신
- chunk header 파싱
- frame 단위 재조립
- 8-bit / 16-bit 자동 판별
- thermal palette 적용 후 화면 렌더링

---

## 7. 열화상 메시지 포맷

현재 서버와 Qt는 하드웨어가 보내는 UDP payload를 그대로 공유한다.
단, `THERMAL_HMAC_REQUIRED=true`가 활성화된 경우 서버는 하드웨어 UDP datagram 끝의 HMAC trailer를 먼저 검증하고, 검증이 끝난 뒤 원래 thermal payload만 Qt로 전달한다.

### 7.1 공통 header

기본 header는 big-endian 10바이트다.

```text
[0..1]  frame_id     uint16
[2..3]  chunk_index  uint16
[4..5]  total_chunks uint16
[6..7]  min_val      uint16
[8..9]  max_val      uint16
[10..]  payload
```

### 7.2 16-bit payload

16-bit 모드에서는 payload가 `uint16 big-endian` 픽셀 배열이다.

- 전체 픽셀 수: `160 x 120 = 19200`
- 전체 frame bytes: `38400`

### 7.3 8-bit payload

8-bit 모드에서는 payload가 `uint8` 픽셀 배열이다.

- 전체 픽셀 수: `160 x 120 = 19200`
- 전체 frame bytes: `19200`

Qt 클라이언트는 완성된 frame byte 길이를 보고 자동으로 모드를 판단한다.

- `38400` bytes -> `16-bit`
- `19200` bytes -> `8-bit`

8-bit일 경우 `min_val/max_val`을 이용해 thermal value 범위로 다시 매핑한 뒤 렌더링한다.

### 7.4 Optional HMAC trailer

보안 강화를 위해 하드웨어와 서버 사이 UDP datagram 끝에 HMAC trailer를 붙일 수 있다.

```text
[thermal payload...]
[magic "THS1" 4B]
[timestamp_sec 4B big-endian]
[counter 4B big-endian]
[hmac_sha256 32B]
```

서버 검증 순서는 다음과 같다.

1. trailer magic 확인
2. timestamp 허용 오차 확인
3. `thermal payload + magic + timestamp + counter`에 대한 HMAC-SHA256 재계산
4. 최근에 본 동일 digest인지 확인해 replay 여부 판단

검증에 실패한 패킷은 WebSocket으로 브로드캐스트하지 않고 drop한다.

---

## 8. API 계약

### 8.1 `POST /thermal/control/start`

역할:

- 열화상 스트림 시작 의도 전달
- 서버 수신기 준비

인증:

- `Authorization: Bearer <JWT>`

응답 예시:

```json
{
  "status": "accepted",
  "stream": "thermal16",
  "transport": "websocket",
  "ws_path": "/thermal/stream",
  "udp_bind_host": "0.0.0.0",
  "udp_port": 5005,
  "format": {
    "width": 160,
    "height": 120,
    "pixel": "u16be",
    "header_bytes": 10,
    "frame_bytes": 38400
  }
}
```

### 8.2 `POST /thermal/control/stop`

역할:

- 열화상 시청 종료 의도 전달
- WS 클라이언트가 모두 빠졌을 경우 UDP receiver 정리

### 8.3 `GET /thermal/status`

역할:

- 운영/디버깅 상태 확인

중요 필드:

- `udp_bound`
- `receiver_running`
- `ws_clients`
- `last_frame_id`
- `packets_received`
- `bytes_received`
- `last_error`

### 8.4 `GET /thermal/stream` (WebSocket)

역할:

- 열화상 binary stream 수신

인증:

- WSS handshake에 `Authorization: Bearer <JWT>` 포함

정상 응답:

- HTTP `101 Switching Protocols`

---

## 9. Qt 수신 및 재조립 절차

Qt 쪽 처리 순서는 다음과 같다.

1. 열화상 시작 버튼 클릭
2. `POST /thermal/control/start`
3. 성공 시 `wss://.../thermal/stream` 연결
4. binary message 수신
5. 10바이트 header 파싱
6. `frame_id` 기준으로 같은 프레임 chunk를 모음
7. 모든 chunk가 도착하면 full frame으로 합침
8. full frame byte 길이로 `8-bit`/`16-bit` 판별
9. thermal value range 계산
10. palette 적용 후 QImage 생성
11. PNG data URL로 QML에 반영

---

## 10. black flicker와 frame drop 해석

운영 중 자주 보게 되는 로그는 다음과 같다.

```text
[THERMAL] drop incomplete frame id=19180 chunks=15/16
```

이 로그는 "이전 프레임의 모든 chunk가 도착하기 전에 다음 frame id가 들어왔다"는 뜻이다.
즉, 네트워크 경로 중 어딘가에서 하나 이상의 chunk가 유실되었음을 의미한다.

중요한 해석:

- 이 로그는 payload bit depth mismatch만으로 발생하는 증상은 아니다.
- 실제 chunk 유실이 있을 때 더 직접적으로 나타난다.

화면이 검게 깜빡이는 현상은 두 가지 원인이 겹칠 수 있다.

1. 새 프레임 조립 실패
2. QML `Image`가 새 data URL을 로드하는 순간 기존 프레임을 잠깐 놓는 현상

이를 완화하기 위해 QML `Image`에 `retainWhileLoading: true`를 적용했다.
즉, 새 이미지가 완전히 준비될 때까지 이전 이미지를 유지한다.

---

## 11. 404 오류의 원인과 해결

초기에는 다음 오류가 발생했다.

```text
QWebSocketPrivate::processHandshake: Unhandled http status code: 404 (Not Found)
```

원인:

- `nginx`에 `/thermal/stream` WebSocket tunnel 설정이 없어서,
- WebSocket upgrade 요청이 일반 HTTP GET처럼 내부로 전달되었고,
- `crow-server`에서 일반 라우트로는 매칭되지 않아 `404`가 반환되었다.

해결:

- `nginx.conf`에 `/thermal/stream` 전용 WebSocket location 추가
- `Upgrade`, `Connection`, `proxy_http_version 1.1` 설정 반영

정상 동작 시 기대 로그:

- nginx access log: `GET /thermal/stream HTTP/1.1" 101`
- Qt log: `[THERMAL][WS] connected`

---

## 12. nginx-only gateway 구조

현재 구조에서는 외부 공개 지점을 모두 `nginx`로 통일했다.

### 외부 공개 포트

- `443/TCP`: HTTPS / WSS
- `5005/UDP`: thermal raw ingress

### 내부 서비스

- `crow-server-service:8080/TCP`
- `crow-server-service:5005/UDP`

### 장점

- 외부 보안 정책을 `nginx` 기준으로 일원화 가능
- `crow-server`를 내부망 서비스로 유지 가능
- 운영자가 외부 진입 경로를 한 곳에서 관리 가능

### 주의점

- UDP 쪽은 HTTP/JWT 인증이 없으므로 네트워크 차원의 통제가 필요함
- 필요 시 송신 IP 제한, VLAN, firewall rule, 별도 서명 등을 고려해야 함
- 현재 저장소 기준 보강 요소는 `JWT_SECRET` 필수 Secret 주입, `crow-server` NetworkPolicy, UDP HMAC 검증이다.
- 외부 하드웨어 IP allowlist는 `nginx/thermal-udp-allowlist.example.conf`를 운영 환경에 맞게 적용하는 방식을 권장한다.

---

## 13. 성능 및 화질 관점 분석

### 13.1 현재 병목

현재 Qt 클라이언트에서 가장 무거운 작업은 아래 단계다.

1. chunk 재조립
2. palette 적용
3. `QImage -> scaled(800x600)` 변환
4. PNG 인코딩
5. base64 data URL 생성

특히 `PNG + base64`는 네트워크가 아니라 UI 업데이트를 위한 내부 포맷 변환인데, CPU 비용이 크다.

### 13.2 화질 저하 원인

화질이 떨어져 보이는 원인은 여러 가지가 있다.

- 하드웨어가 8-bit 모드면 원본 정밀도가 이미 낮음
- 현재 C++에서 `Qt::FastTransformation`으로 스케일링 중
- 빠른 스케일링은 부드럽지 않고 계단 현상이 보일 수 있음

### 13.3 개선 가능 포인트

#### 낮은 리스크 개선

- QML `retainWhileLoading: true` 적용
- 8-bit / 16-bit 자동 판별
- `min/max` 기반 thermal range 복원

#### 중간 리스크 개선

- `Qt::FastTransformation` -> `Qt::SmoothTransformation`
- 또는 native 해상도 이미지를 유지하고 QML에서만 스케일링

#### 큰 폭의 개선

- PNG/base64 전달 방식을 제거
- `QQuickImageProvider`, texture provider, shared image buffer 방식 도입
- QML이 raw `QImage` 또는 GPU texture를 직접 소비하도록 변경

이 단계가 가장 큰 FPS 개선 포인트다.

---

## 14. 운영 시 확인 포인트

### 네트워크 레벨

- `nginx-service` 외부 IP에서 `443/TCP`, `5005/UDP` 노출 여부
- 하드웨어가 올바른 외부 IP와 `5005/UDP`로 보내는지

### 서버 레벨

- `/thermal/status`에서 `udp_bound=true`
- `ws_clients > 0`
- `packets_received` 증가 여부
- `last_frame_id` 갱신 여부

### 클라이언트 레벨

- `[THERMAL][WS] connected`
- `Format: 8-bit` 또는 `Format: 16-bit`
- `drop incomplete frame` 빈도

---

## 15. 관련 파일

### 서버

- `RaspberryPi/k3s-cluster/crow_server/src/ThermalProxy.cpp`
- `RaspberryPi/k3s-cluster/crow_server/include/ThermalProxy.h`
- `RaspberryPi/k3s-cluster/crow_server/src/main.cpp`
- `RaspberryPi/k3s-cluster/crow_server/crow-server.yaml`

### 게이트웨이

- `RaspberryPi/k3s-cluster/nginx/nginx.conf`
- `RaspberryPi/k3s-cluster/nginx/nginx-deployment.yaml`

### 클라이언트

- `Qt_Client/src/core/thermal/BackendThermalService.cpp`
- `Qt_Client/src/ui/qml/common/LoginScreen.qml`
- `Qt_Client/src/ui/qml/thermal/ThermalViewer.qml`
- `Qt_Client/src/core/core/BackendCoreMqttService.cpp`

---

## 16. 결론

이번 열화상 화면 작업은 네트워크 관점에서 보면 다음 한 줄로 요약할 수 있다.

`Hardware UDP ingress -> nginx UDP gateway -> crow-server passthrough bridge -> WSS binary stream -> Qt frame reassembly and render`

이 구조는 제어 채널과 데이터 채널을 분리하고, 외부 게이트웨이를 `nginx`로 단일화하면서도, 열화상 프레임 전송의 지연을 최소화하는 것을 목표로 한다.

현재 구조는 이미 실사용 가능한 수준이지만, 장기적으로는 `PNG/base64` 기반 UI 업데이트를 더 효율적인 이미지 전달 방식으로 바꾸는 것이 가장 큰 성능 개선 포인트다.
