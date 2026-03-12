# Thermal Imaging & Remote Motor Control System

열화상 전송과 원격 모터 제어를 하나의 파이프라인으로 묶은 하드웨어 프로젝트다.

- 열화상 경로: `Teensy -> ESP32 -> MQTT broker -> Python viewer`
- 모터 제어 경로: `GUI/Server -> MQTT broker -> ESP32 -> UART -> STM32 -> PCA9685 -> Servo`

현재 저장소 기준으로 문서는 다음 구현 상태를 반영한다.

- MQTT/TLS 유지
- ESP32 health topic 주기 발행 + 즉시 요청 응답
- STM32 모터 명령의 엄격한 숫자 파싱
- `release`와 `stop` 의미 분리
- 연속 `press` 동작의 끝각도 자동 해제

## 시스템 개요

```text
[ GUI / Python Client ]
          |
          | MQTT
          |  - motor/control
          |  - motor/response
          |  - system/status
          |  - system/request
          |  - lepton/frame/chunk
          v
[ MQTT Broker ] <---- TLS ----> [ ESP32-C3 ]
                                     | \
                                     |  \ SPI frame link
                                     |   \------> thermal frame handling
                                     |
                                     | UART1 (GPIO21 / GPIO20)
                                     v
                               [ STM32F401RE ]
                                     |
                                     | I2C1 (PB8 / PB9)
                                     v
                                 [ PCA9685 ]
                                     |
                                     | PWM x3
                                     v
                              [ Servo Motors x3 ]
```

## 핀맵

### 1. ESP32-C3 <-> STM32F401RE UART

| 기능 | ESP32-C3 | STM32F401RE |
| --- | --- | --- |
| UART TX | `GPIO21` | `PA10 (USART1_RX)` |
| UART RX | `GPIO20` | `PA9 (USART1_TX)` |
| GND | `GND` | `GND` |

배선:

- `ESP32 GPIO21 (TX)` -> `STM32 PA10 (RX)`
- `ESP32 GPIO20 (RX)` <- `STM32 PA9 (TX)`
- `GND` 공통

UART 설정:

- `115200 8N1`
- 라인 종료: `\r`, `\n`, `\r\n`

### 2. STM32F401RE <-> PCA9685 I2C

| 기능 | STM32F401RE | PCA9685 |
| --- | --- | --- |
| I2C SCL | `PB8` | `SCL` |
| I2C SDA | `PB9` | `SDA` |
| Power | `3V3 / 5V` | `VCC` |
| GND | `GND` | `GND` |

참고:

- 현재 코드 기준 I2C 핀은 `PB8/PB9`
- 예전 문서의 `PB6/PB7` 기준은 더 이상 사용하지 않음

### 3. PCA9685 <-> Servo

| PCA9685 채널 | Servo |
| --- | --- |
| `CH0` | `motor1` |
| `CH1` | `motor2` |
| `CH2` | `motor3` |

### 4. Teensy <-> ESP32 열화상 SPI 링크

| 기능 | Teensy | ESP32-C3 |
| --- | --- | --- |
| CS | `pin 0` | `GPIO7` |
| MISO | `pin 1` | `GPIO5` |
| MOSI | `pin 26` | `GPIO6` |
| SCK | `pin 27` | `GPIO4` |
| GND | `GND` | `GND` |

## MQTT 토픽

### 제어 / 응답

| 토픽 | 방향 | 설명 |
| --- | --- | --- |
| `motor/control` | Client -> ESP32 | STM32로 전달할 모터 제어 명령 |
| `motor/response` | STM32/ESP32 -> Client | STM32 응답 라인 |
| `system/status` | ESP32 -> Client | ESP32 health / watchdog JSON |
| `system/request` | Client -> ESP32 | 즉시 health snapshot 요청 |

### 열화상

| 토픽 | 방향 | 설명 |
| --- | --- | --- |
| `lepton/frame/chunk` | ESP32 -> Client | 열화상 프레임 chunk |
| `lepton/status` | ESP32 -> Client | 열화상 상태 메시지 |

## STM32 명령 프로토콜

STM32는 UART 라인 기반 ASCII 명령을 사용한다.

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

주요 응답:

- `READY`
- `OK 90 90 90`
- `ANGLES 90 90 90`
- `PONG`
- `ERR`
- `ERR overflow`

상세 명령 설명은 [command.md](/C:/Users/1-21/Desktop/MyGitMISRA/VEDA_Final_Project/Hardware/command.md)에 정리되어 있다.

## 각 보드 역할

### ESP32

- Teensy로부터 SPI로 열화상 frame 수신
- `lepton/frame/chunk` 토픽으로 MQTT publish
- `motor/control` 구독
- 수신한 명령을 UART로 STM32에 전달
- STM32 응답을 줄 단위로 `motor/response`에 재publish
- `system/status` health JSON 발행
- `system/request` 수신 시 즉시 health 응답

### STM32

- `USART1 (PA9/PA10)`로 명령 수신
- `I2C1 (PB8/PB9)`로 PCA9685 제어
- `PCA9685 CH0/CH1/CH2`로 3개 서보 제어
- `Motor_Update()` 루프로 연속 이동 처리

### Teensy

- Lepton 센서에서 frame 읽기
- SPI frame link로 ESP32에 frame 전달
- Lepton I2C/SPI 초기화 실패 시 재시도 및 오류 전파

## ESP32 Health Topic

`system/status`는 단순 로그 대체가 아니라, 현재 ESP32가 살아 있는지와 병목 위치가 어디인지 외부에서 판단하기 위한 운영용 토픽이다.

### 발행 방식

- 주기 발행: `60초`마다 `system/status`
- 즉시 응답: `system/request` 수신 시 즉시 `system/status`

예시:

```text
topic: system/request
payload: now
```

예상 응답:

```text
topic: system/status
payload: {"uptime_sec":123, ...}
```

### payload 형식

ESP32는 JSON 문자열을 발행한다.

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

### 필드 설명

기본 상태:

- `uptime_sec`: ESP32 부팅 후 경과 시간
- `wifi_connected`: AP 연결 여부
- `wifi_rssi`: 현재 Wi-Fi RSSI
- `mqtt_connected`: MQTT broker 연결 여부

메모리 / 큐:

- `free_heap`: 현재 free heap
- `min_heap`: 부팅 이후 최소 free heap
- `cmd_queue_depth`: STM32 UART 명령 큐 적재 개수

열화상 링크:

- `frame_packets`: Teensy -> ESP32 총 수신 패킷
- `frame_completed`: 완성된 frame 수
- `frame_timeouts`: SPI receive timeout 누적
- `frame_errors`: SPI driver 에러 누적
- `bad_magic`: frame header magic mismatch
- `bad_checksum`: checksum mismatch
- `bad_len`: payload length 오류
- `seq_errors`: sequence 불일치
- `queue_full_drops`: frame queue 가득 참으로 인한 drop
- `frame_ready`: 아직 소비되지 않은 ready frame 개수

## 모터 동작 의미

현재 코드 기준으로 `release`와 `stop`은 다르게 동작한다.

- `motor<N> release`
  - 현재 이동을 중단
  - 해당 채널 PWM output을 끔
  - 서보 holding torque를 해제

- `motor<N> stop`
  - 현재 이동을 중단
  - 현재 각도 PWM은 유지
  - 즉, 현재 위치를 계속 잡고 있음

- 연속 `press` 동작
  - `left press` / `right press`로 연속 이동 시작
  - `release`가 없으면 계속 이동 가능
  - 다만 현재 펌웨어는 `0도` 또는 `180도`에 도달하면 자동으로 `release` 처리해서 끝각도에서 계속 버티며 과열되는 상황을 줄임

## 빠른 점검

### Motor Path

정상이라면 다음 흐름이 보여야 한다.

- client가 `motor/control` publish
- ESP32 로그에서 UART queue 적재 확인
- STM32 응답이 `motor/response`로 publish

예시:

```text
motor/control -> motor1 left press
motor/response -> OK 91 90 90
```

### Health Path

정상이라면 broker subscriber에서 다음을 확인할 수 있어야 한다.

- `system/status`가 60초 주기로 도착
- `system/request` publish 직후 `system/status`가 즉시 도착

정상 상태 예시:

- `wifi_connected=true`
- `mqtt_connected=true`
- `free_heap`가 급격히 0 근처로 내려가지 않음
- `cmd_queue_depth=0` 또는 낮은 수준 유지

## 운영 메모

- STM32 UART는 현재 `USART1 (PA9/PA10)` 기준
- PCA9685 I2C는 현재 `PB8/PB9` 기준
- `motor/control`과 `lepton/frame/chunk`는 토픽 자체로는 충돌하지 않지만, 둘 다 같은 ESP32 리소스를 공유하므로 부하 영향은 있을 수 있음
- 고 FPS thermal streaming에서는 여전히 ESP32-C3 자원 한계가 존재할 수 있음
