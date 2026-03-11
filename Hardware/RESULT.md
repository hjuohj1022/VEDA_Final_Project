# RESULT

## 1. 목표

이 작업의 1차 목표는 아래 두 가지였다.

1. `Teensy -> ESP32 -> MQTT broker -> Python client` 경로로 열화상 프레임이 실제로 화면에 보이게 만들기
2. 가능한 한 높은 fps를 확보하되, 우선은 "안정적으로 화면이 보이는 상태"를 만들기

추가로 중간에 확인한 핵심 질문은 다음과 같았다.

- Teensy에서 최대 몇 fps까지 프레임을 만들 수 있는가
- Teensy에서 raw frame을 ESP32로 넘기는 구간의 병목이 어디인가
- UART 대신 SPI로 바꾸면 raw frame 유지가 가능한가
- ESP32-C3가 MQTT/TLS까지 감당 가능한가


## 2. 최종 결론 요약

### 2.1 현재 달성한 상태

- `TeensyC` 기반 펌웨어로 Lepton 프레임을 읽고 ESP32로 SPI 송신하는 경로를 만들었다.
- ESP32는 SPI slave로 프레임을 받아 MQTT broker로 청크 publish할 수 있다.
- Python 클라이언트는 broker에서 청크를 받아 화면을 띄울 수 있다.
- 즉, **클라이언트 화면 표시까지는 성공**했다.

### 2.2 현재 안정 운용점

현 시점에서 가장 현실적인 안정 설정은 아래와 같다.

- `TeensyC`
  - `FRAME_SPI_PKT_SIZE = 256`
  - `FRAME_SPI_CLOCK_HZ = 4000000`
  - `FRAME_SPI_GAP_US = 300`
  - `FRAME_POST_SEND_SLEEP_MS = 250`
- `ESP32`
  - SPI slave 수신
  - MQTT frame publish는 QoS 0
  - chunk 크기 1024B
- Python client
  - `thermal_view_test.py`로 화면 표시 가능

이 설정에서 체감 성능은 대략:

- **약 0.75 ~ 1.1 fps**

### 2.3 현재 한계

현 하드웨어와 프로토콜 조합에서는 다음이 핵심 한계다.

- `ESP32-C3 + raw frame + MQTT + TLS`는 여유가 매우 적다
- SPI 링크는 어느 정도 안정화했지만, sustained high fps에서는 다시 프레임 조립이 흔들린다
- 2 fps는 현재 구성에서 불안정
- 8 fps raw 지속 스트리밍은 현재 구성으로는 사실상 어렵다


## 3. 프로젝트 구조 이해

원래 프로젝트 데이터 흐름은 아래와 같다.

1. Teensy가 FLIR Lepton에서 raw thermal frame을 읽음
2. ESP32가 Teensy로부터 프레임을 받아 MQTT broker로 publish
3. Python 또는 다른 subscriber가 broker에서 frame chunk를 받아 화면 구성
4. 별도로 ESP32는 MQTT command를 STM32 SPI bridge로도 보내던 구조였음

핵심 문제는 2번 구간이었다.

- `Teensy -> ESP32` 링크
- `ESP32 -> broker` 전송


## 4. 초기에 확인한 병목

### 4.1 Teensy 캡처 성능

테스트 결과:

- `capture-only`는 대략 `8.8 fps`
- 즉, Lepton 캡처 자체는 센서 한계에 근접

해석:

- Teensy가 프레임을 못 만드는 게 아님
- 병목은 프레임 "수집"이 아니라 "전송"임

### 4.2 UART 전송 병목

raw frame 크기:

- `160 x 120 x 2 = 38,400 bytes`

기존 UART 방식(`921600 baud`) 테스트 결과:

- 실제 raw frame 송신 포함 fps는 약 `2.2 fps`

해석:

- raw 8~9 fps를 유지하면서 UART로 넘기는 건 사실상 불가
- 이 구간 때문에 UART를 버리고 SPI로 전환하기로 결정


## 5. SPI 전환 과정

### 5.1 테스트용 SPI 벤치마크

먼저 Teensy/ESP32 사이 SPI 링크 자체의 대역폭을 테스트했다.

테스트용 주요 파일:

- `teensy/TeensyIno/spi1_throughput_benchmark/spi1_throughput_benchmark.ino`
- `teensy/tools/teensy_spi_client.ps1`
- `esp32_spi_test_pio/src/main.c`

배선 기준:

- Teensy: `0(CS) / 1(MISO) / 26(MOSI) / 27(SCK)`
- ESP32: `GPIO7(CS) / GPIO5(MISO) / GPIO6(MOSI) / GPIO4(SCK)`

테스트 결과:

- 링크 자체는 raw frame 기준 약 `27~30 fps equivalent` 수준까지 가능

해석:

- 대역폭 자체는 충분
- UART 대신 SPI면 raw frame 전달 가능성 있음


## 6. TeensyC/ESP32 실 프로젝트에 SPI 적용

### 6.1 TeensyC 변경

`TeensyC`에서 Lepton frame을 읽고 SPI packet으로 전송하도록 수정했다.

주요 파일:

- `teensy/TeensyC/src/main.c`
- `teensy/TeensyC/zephyr/app.overlay`

핵심 구현 내용:

- Lepton용 `lpspi4`는 유지
- ESP32 전송용 별도 SPI 사용
- 패킷 헤더에 magic, frame id, chunk index, total chunks, payload length, checksum 포함
- 수동 CS 제어 적용
- 이후 payload 무결성 향상을 위해 packet size를 `1024 -> 256`으로 축소

### 6.2 ESP32 변경

ESP32는 UART 대신 SPI slave로 프레임을 받아 MQTT로 내보내도록 수정했다.

주요 파일:

- `esp32/src/device/frame_link.c`
- `esp32/src/device/frame_link.h`
- `esp32/src/main.c`
- `esp32/src/mqtt/mqtt.c`

핵심 구현 내용:

- SPI slave 수신
- 패킷 단위 frame reassembly
- 준비된 frame을 MQTT chunk로 publish
- frame progress log, error counters 추가


## 7. 중간에 발견된 실제 문제들

### 7.1 ESP32 메모리/Outbox 문제

초기 MQTT 설정에서 다음 문제가 발생했다.

- `outbox_enqueue(): Memory exhausted`

원인:

- 큰 chunk
- QoS 1
- TLS
- 작은 heap

대응:

- frame publish는 QoS 0
- MQTT out buffer 축소
- chunk payload 조정

### 7.2 frame_id와 수신 조립 문제

초기에는 청크가 섞일 수 있었다.

대응:

- chunk header에 `frame_id` 추가
- Python receiver에서 frame timeout 및 frame switch 처리 추가

### 7.3 `TeensyC`에서 Lepton 전원 배선 문제

중간에 중요한 실제 하드웨어 문제를 발견했다.

- Lepton의 `3V`와 `GND`가 반대로 연결되어 있었음

증상:

- `0x3F / 0x2F / 0x4F ... FF FF` 같은 discard packet만 반복
- `Capture timeout, resetting Lepton`

배선 수정 후:

- Teensy에서 `Frame sent over SPI: id=...`가 나오기 시작

### 7.4 `TeensyC` Lepton capture 불안정

배선 수정 후에도 `TeensyC`는 완전히 안정적이지 않았다.

증상:

- `Capture timeout, resetting Lepton (discards=...)`
- 그래도 간헐적으로는 frame send 성공

해석:

- Lepton capture 자체가 약간 불안정
- 하지만 완전히 죽은 건 아니고 실제 frame 송신은 가능

### 7.5 ESP32 SPI frame reassembly 불안정

여러 파라미터 조합에서 다음이 반복되었다.

- `bad_magic`
- `bad_checksum`
- `bad_len`
- `seq`

해석:

- Teensy -> ESP32 SPI 링크가 완전히 죽은 것은 아니지만
- sustained high rate에서는 frame packet 무결성이 흔들림


## 8. 파라미터 튜닝 과정

여러 조합을 시험했다.

### 8.1 시도한 주요 조합

- `10 MHz + gap 100us`
- `10 MHz + gap 200us`
- `10 MHz + gap 250us`
- `8 MHz + gap 200us`
- `4 MHz + gap 300us`
- packet size `1024`
- packet size `256`
- post-send sleep `0 / 20 / 150 / 200 / 250 / 400ms`

### 8.2 중요한 결과

#### `1024B packet`

- 무결성이 쉽게 깨짐
- `bad_checksum`, `seq`가 많이 증가

#### `256B packet`

- 크게 개선
- `bad_magic=0`, `bad_checksum=0` 구간도 나옴
- frame reassembly 성공률이 확실히 좋아짐

#### `post-send sleep 150ms`

- 순간 fps는 올랐지만 sustained 성능이 무너짐

#### `post-send sleep 200ms`

- 2fps 도전용으로는 너무 공격적
- 다시 `frames=0`, checksum/seq 증가

#### `post-send sleep 250ms`

- 가장 현실적인 타협점
- 화면이 안정적으로 보이고
- 약 `0.75 ~ 1.1 fps` 수준 확보

#### `post-send sleep 400ms`

- 화면 보이게 만들기에는 가장 쉬운 값
- 하지만 fps가 너무 낮음


## 9. Python 클라이언트 쪽 작업

### 9.1 기존 수신기

기존 파일:

- `esp32/src/mqtt/receive.py`

여기에 frame_id 조립, timeout 처리를 보강했다.

### 9.2 화면 표시용 테스트 뷰어

새로 만든 파일:

- `esp32/src/mqtt/thermal_view_test.py`

기능:

- broker 연결
- `lepton/frame/chunk` 구독
- frame chunk 조립
- OpenCV 화면 표시
- 마지막 정상 frame 유지
- status/chunk/frame count를 오버레이로 표시

### 9.3 메시지 덤프용 구독기

새로 만든 파일:

- `esp32/src/mqtt/mqtt_chunk_dump.py`

기능:

- `lepton/#` 구독
- `status`와 `frame/chunk` 메시지가 실제로 오는지 확인
- 디버그용

### 9.4 Python 측 최종 결론

Python 문제는 아니었다.

확인한 사실:

- subscriber 연결 정상
- `lepton/status` 수신 정상
- `lepton/frame/chunk`도 수신 가능
- 다만 frame chunk가 중간에 끊기면
  - `frame timeout`
  - `drop incomplete frame`
  가 발생

즉, Python 뷰어는 현재 상태를 정확히 보여주는 도구 역할을 하고 있다.


## 10. 현재 관측된 성능

### 10.1 현재 실사용 상태

현재 상태에서 가능한 것:

- 열화상 화면을 실제로 띄울 수 있음
- 간헐적이 아니라 어느 정도 지속적으로 볼 수 있음
- 최종 안정 운용점은 대략 `0.75 ~ 1.1 fps`

### 10.2 현재 불가능하거나 어려운 것

- `2 fps sustained`
- `8 fps raw + ESP32-C3 + MQTT/TLS`

### 10.3 2 fps가 어려운 이유

로그 기준으로 보면 2 fps에 가까워질수록:

- SPI frame reassembly 실패 증가
- `bad_checksum`, `seq` 증가
- 또는 ESP32 MQTT 송신 backlog 증가
- 결국 frame completion 비율이 급감

즉 현재 구조에서 2 fps를 넘기려 하면

- Teensy 생산 속도
- ESP32 SPI reassembly
- ESP32 MQTT/TLS transmit

이 세 단계 중 하나가 무너진다.

### 10.4 왜 2 fps 이상을 확보하지 못했는가

지금까지의 실험을 종합하면, `2 fps 이상`이 안 나오는 이유는 "한 군데 단일 병목"이 아니라 **여러 단계의 여유가 동시에 부족하기 때문**이다.

#### 1. Lepton capture 자체가 완전히 안정적이지 않음

`TeensyC`에서 실제로 반복 관측된 로그:

- `Capture timeout, resetting Lepton (discards=...)`

의미:

- Lepton 프레임이 항상 일정 주기로 완성되는 것이 아님
- 캡처 단계에서 이미 간헐적 reset이 들어감
- 즉 Teensy가 "계속 일정한 속도로 frame producer" 역할을 못 한다

이 문제는 `2 fps 이상`으로 갈수록 더 치명적이다.  
왜냐하면 생산 주기를 줄일수록 한 번의 timeout/reset이 전체 평균 fps에 주는 타격이 더 커지기 때문이다.

#### 2. Teensy -> ESP32 SPI는 고속일수록 frame 조립 실패가 다시 증가함

실험 결과:

- `1024B packet`에서는 무결성 문제가 컸음
- `256B packet`으로 줄인 뒤 많이 개선되었지만,
- frame post-send sleep을 너무 줄이면 다시 아래 값들이 빠르게 증가함

대표 지표:

- `bad_magic`
- `bad_checksum`
- `bad_len`
- `seq`

의미:

- SPI 링크가 완전히 죽는 것은 아니지만,
- 높은 생산 속도에서는 패킷 조립이 sustained 하게 유지되지 않음
- 즉 `2 fps`에 가까워질수록 frame 한 장 전체를 완주하기 전에 중간 청크들이 깨질 확률이 높아진다

실제로 `FRAME_POST_SEND_SLEEP_MS = 200ms` 같은 공격적인 설정에서는:

- `frames=1`만 간신히 성공
- 이후 `bad_checksum`, `seq`가 급증

반면 `250ms`에서는:

- 약 `0.75 ~ 1.11 fps`
- 상대적으로 지속 가능한 수준

즉 현재 `TeensyC` 기준으로는 `200ms` 부근부터 이미 안정 운용 한계를 넘는다는 뜻이다.

#### 3. ESP32-C3의 MQTT/TLS 송신이 sustained raw frame을 계속 소화하지 못함

SPI가 잘 붙은 구간에서도 로그는 아래 패턴을 보였다.

- `queue_full` 지속 증가
- `ready=2` 유지
- `Frame sent OK`는 간헐적
- `mqtt_client: Writing didn't complete in specified timeout`
- `MQTT disconnected, reconnecting...`

의미:

- ESP32가 SPI로 frame을 받아도,
- 그 frame을 MQTT chunk 38개로 잘라 TLS를 통해 broker로 밀어내는 속도가 충분하지 않음
- 그래서 ESP32 frame buffer가 차고, backlog가 생기고, 결국 publish timeout과 reconnect로 이어짐

즉 sustained `2 fps 이상`이 안 나오는 핵심 이유 중 하나는:

- **ESP32-C3가 raw frame + MQTT + TLS 조합을 장시간 여유 있게 감당하지 못함**

#### 4. 네트워크 품질이 결과를 더 흔듦

작업 중 반복적으로 관찰한 것:

- RSSI가 `-80 ~ -89 dBm` 구간이면
  - broker connect timeout
  - TLS open timeout
  - reconnect
  가 자주 발생

공유기 근처로 이동하여 RSSI가 `-61 dBm` 수준으로 좋아졌을 때는

- Wi‑Fi 자체는 확실히 안정화
- 그러나 여전히 sustained high fps는 확보되지 않음

이 의미는 중요하다.

- Wi‑Fi가 나쁘면 더 망가진다
- 하지만 Wi‑Fi가 좋아져도 `2 fps+`가 바로 되지는 않는다

즉 네트워크는 "보조 악화 요소"이고, 본질 병목은 여전히

- Lepton capture 불안정
- SPI frame reassembly 한계
- ESP32 MQTT/TLS 처리 여유 부족

에 있다.

#### 5. 현재 구조에서 실제로 성립한 타협점은 약 1 fps 전후

실제 튜닝 결과를 정리하면:

- `400ms sleep`: 화면 보이기 쉬움, fps 낮음
- `250ms sleep`: 대략 `0.75 ~ 1.11 fps`, 현재 최적 타협
- `200ms sleep`: 2 fps 도전용으로는 실패, sustained 안정성 무너짐

즉 현재 구조는:

- **1 fps 전후는 가능**
- **2 fps부터는 안정성 여유가 급격히 사라짐**

#### 6. 한 줄 요약

`2 fps 이상`을 확보하지 못한 이유는 아래 세 가지가 동시에 존재하기 때문이다.

1. `TeensyC`의 Lepton frame capture가 완전히 안정적이지 않다
2. frame 생성 주기를 줄이면 SPI 조립 무결성이 다시 무너진다
3. 설령 frame을 받아도 ESP32-C3의 MQTT/TLS 송신 여유가 부족하다

따라서 현재 구성에서 `2 fps+`를 못 만든 이유는 단순한 파라미터 미조정이 아니라,
**현 구조의 여유가 약 1 fps 부근에서 이미 소진되기 때문**이라고 정리할 수 있다.


## 11. 현재 가장 중요한 기술적 결론

### 11.1 가장 큰 구조적 병목

현재 가장 큰 병목은 `ESP32-C3가 raw frame을 안정적으로 MQTT/TLS로 지속 송신하는 부분`이다.

좀 더 구체적으로는:

- UART는 이미 포기하는 게 맞음
- SPI로 바꿔서 Teensy -> ESP32 구간은 "가능성"을 확보함
- 하지만 지속 스트리밍에서는 여전히 ESP32-C3 여유가 부족함

### 11.2 지금 구성에서의 현실적인 목표

- 화면 표시: 가능
- 1 fps 전후: 가능
- 2 fps: 도전 가능하지만 불안정
- 8 fps raw: 사실상 어려움


## 12. 보드 교체 관련 판단

작업 중간에 다음도 검토했다.

- `ESP32-C5`
- `ESP32-S3`

요약 판단:

- 현재 프로젝트 용도에서는 `ESP32-S3`가 가장 현실적
- 이유:
  - dual-core
  - 더 큰 메모리 여유
  - PSRAM 옵션
  - 주변장치 여유
- `ESP32-C5`는 무선 스펙은 좋지만 single-core라 전체 시스템 여유는 S3보다 덜 유리

정리:

- `ESP32-S3 > ESP32-C5 > ESP32-C3`
  - 이 프로젝트 기준


## 13. 최종 권장 설정

현재 이 저장소에서 "가장 실용적으로 돌아가는" `TeensyC` 기준 설정:

- `FRAME_SPI_PKT_SIZE = 256`
- `FRAME_SPI_CLOCK_HZ = 4000000`
- `FRAME_SPI_GAP_US = 300`
- `FRAME_POST_SEND_SLEEP_MS = 250`
- Python viewer는 `thermal_view_test.py`

이 조합은 다음을 목표로 하는 설정이다.

- 8 fps가 아니라
- **우선 화면이 보이고, 대략 1 fps 전후를 버티는 설정**


## 14. 지금 기준 다음 단계

### 14.1 현재 하드웨어 유지 시

가능한 현실적인 다음 단계:

1. 현재 설정 고정
2. 디버그 로그 정리
3. client 화면 표시 UX 개선
4. frame loss/timeout 표시 추가
5. sustained 1 fps 수준에서 시연용 정리

### 14.2 목표 fps를 더 올리고 싶으면

필요한 선택지:

1. `ESP32-S3`로 교체
2. 외부 안테나 사용
3. 가능하면 PSRAM 있는 모듈 사용
4. 장기적으로는 MQTT/TLS 대신 더 가벼운 전송 방식 고려


## 15. 핵심 파일 목록

### 15.1 Teensy

- `teensy/TeensyC/src/main.c`
- `teensy/TeensyC/zephyr/app.overlay`
- `teensy/TeensyC_TEST/src/main.c`
- `teensy/TeensyIno/spi1_throughput_benchmark/spi1_throughput_benchmark.ino`

### 15.2 ESP32

- `esp32/src/main.c`
- `esp32/src/device/frame_link.c`
- `esp32/src/device/frame_link.h`
- `esp32/src/mqtt/mqtt.c`
- `esp32/src/mqtt/mqtt.h`

### 15.3 Python

- `esp32/src/mqtt/receive.py`
- `esp32/src/mqtt/thermal_view_test.py`
- `esp32/src/mqtt/mqtt_chunk_dump.py`


## 16. 한 줄 결론

현재까지의 결과를 가장 짧게 요약하면 아래와 같다.

- **화면은 띄웠다**
- **현재 안정 fps는 대략 1fps 전후다**
- **2fps 이상부터는 ESP32-C3 기반 구조가 급격히 불안정해진다**
- **8fps raw가 목표라면 결국 ESP32-C3는 한계가 크고, S3급으로 가는 게 맞다**
## 부록. Lepton-ESP32 직접 연결 대안 검토

중간의 `Teensy -> ESP32` 링크를 없애기 위해, Lepton을 ESP32에 직접 연결하는 방안도 별도로 검토했다.

이 방식의 장점:

1. Teensy를 제거하므로 중간 SPI/UART 링크 병목이 사라진다
2. 현재처럼 `Teensy capture -> Teensy SPI 송신 -> ESP32 SPI 수신`을 모두 맞춰야 하는 복잡성이 줄어든다
3. 보드 수가 줄어들어 디버깅 포인트도 줄어든다

하지만 단점도 분명하다.

1. ESP32가 Lepton `VoSPI`를 직접 읽으면서 동시에 Wi-Fi, MQTT, TLS까지 처리해야 한다
2. Lepton은 discard packet과 segment timing을 맞춰야 해서 수신 타이밍이 까다롭다
3. `ESP32-C3`에서는 CPU, 메모리, 주변장치 여유가 적어서 raw frame을 직접 읽고 바로 네트워크로 내보내는 구조가 여전히 빡빡할 가능성이 높다

즉 이 대안의 의미는 다음과 같다.

- `Teensy <-> ESP32` 중간 링크 문제를 없애는 데는 도움이 된다
- 그러나 `ESP32 -> MQTT/TLS` 처리 한계를 없애지는 못한다
- 따라서 `ESP32-C3` 기준으로는 "구조 단순화" 효과는 있지만 "raw 8 fps 확보"의 근본 해결책이라고 보긴 어렵다

정리:

- `ESP32-C3`라면 Lepton 직결은 저속 미리보기나 단순 데모에는 도움이 될 수 있다
- 하지만 `raw 유지 + 안정적 8 fps + MQTT/TLS`까지 기대하긴 어렵다
- 이 방식을 진지하게 검토할 가치는 `ESP32-S3` 이상급 보드에서 더 크다
