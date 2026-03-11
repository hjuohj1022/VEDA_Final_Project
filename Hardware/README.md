# Thermal Imaging & Remote Motor Control System

이 프로젝트는 두 개의 주요 경로로 구성된다.

- 열화상 경로: `Teensy -> ESP32 -> MQTT broker -> Python viewer`
- 모터 제어 경로: `GUI/Server -> MQTT broker -> ESP32 -> UART -> STM32 -> PCA9685 -> Servo`

현재 README는 최근 작업 기준으로 핀맵, MQTT 토픽, STM32 명령 체계, 그리고 ESP32 watchdog/health topic을 중심으로 정리한다.

## System Overview

```text
[ GUI / Python Client ]
          |
          | MQTT (`motor/control`, `motor/response`, `system/status`, `lepton/frame/chunk`)
          v
[ MQTT Broker ] <----TLS----> [ ESP32-C3 ]
                                  | \
                                  |  \ SPI frame link
                                  |   \----------------> thermal frame handling
                                  |
                                  | UART1 (GPIO21/GPIO20)
                                  v
                            [ STM32F401RE ]
                                  |
                                  | I2C1 (PB8/PB9)
                                  v
                              [ PCA9685 ]
                                  |
                                  | PWM x3
                                  v
                           [ Servo Motors x3 ]
```

## Pin Map

### 1. ESP32-C3 <-> STM32F401RE UART

현재 모터 제어 명령 경로는 ESP32와 STM32 사이를 UART로 연결한다.

| 기능 | ESP32-C3 | STM32F401RE |
| --- | --- | --- |
| UART TX | `GPIO21` | `PA10 (USART1_RX)` |
| UART RX | `GPIO20` | `PA9 (USART1_TX)` |
| GND | `GND` | `GND` |

배선 방향:

- `ESP32 GPIO21 (TX)` -> `STM32 PA10 (RX)`
- `ESP32 GPIO20 (RX)` <- `STM32 PA9 (TX)`
- `GND` 공통

UART 설정:

- `115200 8N1`
- line command (`\r`, `\n`, `\r\n`)

### 2. STM32F401RE <-> PCA9685 I2C

현재 PCA9685는 STM32의 `I2C1`로 연결한다.

| 기능 | STM32F401RE | PCA9685 |
| --- | --- | --- |
| I2C SCL | `PB8` | `SCL` |
| I2C SDA | `PB9` | `SDA` |
| Power | `3V3 / 5V` | `VCC` |
| GND | `GND` | `GND` |

참고:

- 현재 코드 기준 I2C 핀은 `PB8/PB9`다.
- 예전 `PB6/PB7` 기준 문서는 더 이상 현재 기준이 아니다.

### 3. PCA9685 <-> Servo

| PCA9685 Channel | Servo |
| --- | --- |
| `CH0` | `motor1` |
| `CH1` | `motor2` |
| `CH2` | `motor3` |

### 4. Teensy <-> ESP32 Thermal Frame Link

열화상 프레임 전달은 Teensy와 ESP32 사이 SPI frame link를 사용한다.

| 기능 | Teensy | ESP32-C3 |
| --- | --- | --- |
| CS | `pin 0` | `GPIO7` |
| MISO | `pin 1` | `GPIO5` |
| MOSI | `pin 26` | `GPIO6` |
| SCK | `pin 27` | `GPIO4` |
| GND | `GND` | `GND` |

## MQTT Topics

### Motor Control

| Topic | 방향 | 설명 |
| --- | --- | --- |
| `motor/control` | Client -> ESP32 | STM32 모터 제어 명령 |
| `motor/response` | STM32/ESP32 -> Client | STM32 응답 |
| `system/status` | ESP32 -> Client | ESP32 health / watchdog 상태 |

### Thermal Frame

| Topic | 방향 | 설명 |
| --- | --- | --- |
| `lepton/frame/chunk` | ESP32 -> Client | 열화상 프레임 청크 |
| `lepton/status` | ESP32 -> Client | 열화상 상태 메시지 |

## STM32 Command Protocol

현재 STM32는 UART line command를 사용한다.

지원 명령:

- `motor1 left press`
- `motor1 right press`
- `motor1 release`
- `motor1 stop`
- `motor1 set 90`
- `motor1 left 10`
- `motor1 right 10`
- `read`
- `ping`
- `stopall`
- `help`

응답 예:

- `READY`
- `OK 90 90 90`
- `ANGLES 90 90 90`
- `PONG`
- `ERR`
- `ERR overflow`

상세 명령 정리는 [command.md](VEDA_Final_Project/Hardware/command.md)에 있다.

## ESP32 Role

ESP32는 세 가지 역할을 동시에 수행한다.

### 1. Thermal Frame Bridge

- Teensy에서 SPI로 raw thermal frame 수신
- MQTT chunk로 `lepton/frame/chunk` publish

### 2. Motor Control Bridge

- `motor/control` command subscribe
- UART로 STM32에 전달
- STM32 응답을 `motor/response`로 재publish

### 3. Health Publisher

- 5초 주기로 `system/status` JSON publish
- 현재 장비 상태를 외부 subscriber가 바로 확인할 수 있도록 제공

## STM32 Role

STM32는 실제 하드웨어 제어를 담당한다.

- `USART1 (PA9/PA10)`으로 명령 수신
- `I2C1 (PB8/PB9)`으로 PCA9685 제어
- `PCA9685 CH0/CH1/CH2`로 3개 서보 제어
- `Motor_Update()` 루프로 continuous move 처리

## ESP32 Watchdog / Health Topic

### 목적

`system/status`는 단순 로그 대체가 아니라, 현재 ESP32가 어떤 상태인지 외부에서 지속적으로 관찰하기 위한 운영용 topic이다.

이 topic이 필요한 이유:

- Wi-Fi는 연결돼 있지만 MQTT가 끊긴 경우를 구분하기 위해
- thermal frame이 들어오는데 조립이 실패하는지 확인하기 위해
- command queue가 쌓여서 STM32 명령 전달이 밀리는지 보기 위해
- heap 부족, reconnect loop, SPI 오류 같은 문제를 장비 외부에서 바로 확인하기 위해

즉, `system/status`는 현재 시스템의 "살아있는 상태"와 "병목 위치"를 동시에 보여주는 health feed다.

### publish 주기

- 기본 주기: `5초`

### payload 형식

ESP32는 JSON 문자열을 publish한다.

예시:

```json
{
  "uptime_sec": 123,
  "wifi_connected": true,
  "wifi_rssi": -61,
  "mqtt_connected": true,
  "free_heap": 65280,
  "min_heap": 56596,
  "cmd_queue_depth": 0,
  "frame_packets": 3824,
  "frame_completed": 1,
  "frame_timeouts": 38,
  "frame_errors": 0,
  "bad_magic": 1169,
  "bad_checksum": 1064,
  "bad_len": 105,
  "seq_errors": 1006,
  "queue_full_drops": 0,
  "frame_ready": 0
}
```

### field 설명

#### 기본 상태

- `uptime_sec`
  - ESP32 부팅 후 경과 시간
  - 재부팅 여부를 확인할 때 유용

- `wifi_connected`
  - 현재 AP와 연결돼 있는지 여부
  - `false`면 MQTT 이전 단계에서 이미 문제가 있는 상태

- `wifi_rssi`
  - 현재 Wi-Fi 수신 감도
  - 일반적으로 `-60 ~ -65 dBm`이면 좋고, `-80 dBm` 이하로 내려가면 불안정 가능성이 커짐

- `mqtt_connected`
  - 현재 MQTT client가 broker와 연결된 상태인지 여부
  - `wifi_connected=true`인데 `mqtt_connected=false`면 TLS/broker/reconnect 문제 가능성이 큼

#### 메모리 / 리소스

- `free_heap`
  - 현재 남은 heap
  - 순간 상태 확인용

- `min_heap`
  - 부팅 후 가장 낮았던 heap watermark
  - 실제 worst-case 메모리 여유 판단에 더 중요

- `cmd_queue_depth`
  - ESP32 내부 STM32 command queue 적재 개수
  - 계속 증가하면 GUI/서버 명령은 오는데 STM32로 전달이 밀리고 있다는 뜻

#### thermal frame link 상태

- `frame_packets`
  - Teensy -> ESP32 SPI 패킷 총 수신 개수

- `frame_completed`
  - 완성된 thermal frame 개수
  - 이 값이 증가해야 실제 frame publish 후보가 생긴다

- `frame_timeouts`
  - SPI slave receive timeout 누적 수
  - idle 구간에서도 증가할 수 있으므로 단독으로 장애 지표는 아님

- `frame_errors`
  - SPI driver 레벨 오류 수
  - 증가하면 통신 계층 문제 가능성이 큼

- `bad_magic`
  - 헤더 magic mismatch 수
  - 패킷 경계가 틀렸거나 초반 sync가 맞지 않는 경우 증가

- `bad_checksum`
  - payload checksum mismatch 수
  - 실제 데이터 무결성 문제가 있는 경우 증가

- `bad_len`
  - payload length 비정상 수
  - 헤더 파싱이 깨졌거나 송신/수신 경계가 어긋난 상태일 수 있음

- `seq_errors`
  - chunk/frame 순서 mismatch 수
  - frame 조립 중간에 패킷이 누락되거나 꼬였을 때 증가

- `queue_full_drops`
  - frame buffer가 가득 차서 드롭된 개수
  - thermal path 처리 속도가 frame 입력 속도를 못 따라간다는 의미

- `frame_ready`
  - 현재 MQTT task가 아직 소비하지 않은 완성 frame 개수
  - 계속 높게 유지되면 backlog가 쌓이는 상태

### 해석 예시

#### 1. Wi-Fi 문제

예:

- `wifi_connected=false`
- `wifi_rssi=-90`
- `mqtt_connected=false`

의미:

- AP 연결 자체가 불안정
- 네트워크 환경부터 봐야 함

#### 2. MQTT / broker 문제

예:

- `wifi_connected=true`
- `wifi_rssi=-60`
- `mqtt_connected=false`

의미:

- Wi-Fi는 괜찮지만 broker/TLS/reconnect 경로 문제 가능성

#### 3. SPI 프레임 경계 문제

예:

- `bad_magic` 빠르게 증가
- `frame_completed` 증가 거의 없음

의미:

- Teensy -> ESP32 SPI 패킷 경계가 안정적이지 않음

#### 4. 데이터 무결성 문제

예:

- `bad_checksum` 증가
- `bad_len` 증가

의미:

- payload 자체가 깨지고 있음
- SPI clock / gap / wiring 쪽 확인 필요

#### 5. 처리량 부족

예:

- `queue_full_drops` 증가
- `frame_ready`가 계속 높음

의미:

- 입력 frame 생산 속도가 MQTT publish 처리 속도보다 빠름

#### 6. 명령 경로 병목

예:

- `cmd_queue_depth`가 계속 증가

의미:

- GUI/서버에서 오는 모터 명령은 들어오지만 STM32 UART 전달 또는 응답이 밀리고 있음

### watchdog 관점에서의 의미

이 프로젝트에서 별도의 하드 리셋 watchdog을 추가한 것은 아니다. 여기서 말하는 watchdog/health는 "장비가 살아 있는지"와 "어디서 막히는지"를 지속적으로 감시하는 운영용 상태 feed에 가깝다.

즉:

- `system/status`가 주기적으로 계속 오면 ESP32 app은 살아 있음
- `uptime_sec`가 자주 초기화되면 reboot 반복 의심
- `wifi_connected`, `mqtt_connected`, `frame_completed`, `cmd_queue_depth`를 같이 보면 어느 계층에서 문제인지 빠르게 좁힐 수 있음

## Python Tools

주요 Python 도구:

- `esp32/src/mqtt/thermal_view_test.py`
  - thermal frame viewer
- `esp32/src/mqtt/mqtt_chunk_dump.py`
  - thermal chunk dump subscriber
- `esp32/src/mqtt/motor_control_gui.py`
  - MQTT motor control GUI

## Quick Check

### Motor Path

정상이라면 아래 흐름이 보여야 한다.

1. STM32 부팅 UART:
- `BOOT USART1 PA9/PA10`
- `I2C scan start`
- `I2C 0x40`
- `READY`

2. ESP32 로그:
- `MQTT connected`
- `STM32 UART RX: READY`
- `Queued to STM32 UART: 'motor1 left press'`
- `STM32 UART TX: 'motor1 left press'`
- `STM32 UART RX: OK ...`

3. GUI 상태바:
- `MQTT connected`
- `TX: motor1 left press rc=0`
- `motor/response: OK ...`

### Thermal Path

정상이라면 아래 흐름이 보여야 한다.

- `>> SPI frame captured id=...`
- `Frame sent OK: id=...`
- Python viewer에서 `frame complete: id=...`

### Health Path

정상이라면 broker subscriber에서 아래 topic이 주기적으로 보여야 한다.

- `system/status`

그리고 JSON 안에서 보통 아래 조건을 기대할 수 있다.

- `wifi_connected=true`
- `mqtt_connected=true`
- `free_heap`가 급격히 0 근처로 내려가지 않음
- `cmd_queue_depth=0` 또는 낮은 값 유지

## Notes

- STM32 제어 UART는 현재 `USART1 (PA9/PA10)` 기준이다.
- PCA9685 I2C는 현재 `PB8/PB9` 기준이다.
- `motor/control`과 `lepton/frame/chunk`는 MQTT topic 수준에서는 충돌하지 않지만, 같은 ESP32 리소스를 공유하므로 부하 영향은 있을 수 있다.
- 현재 시스템은 열화상 표시와 모터 제어가 동시에 동작하도록 맞춰져 있지만, 고FPS thermal streaming에서는 ESP32-C3 자원 한계가 있다.
