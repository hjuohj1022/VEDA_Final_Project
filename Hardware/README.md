# Thermal Imaging and Remote Actuation Hardware

이 디렉터리는 현재 동작 중인 하드웨어 펌웨어와 보드 간 인터페이스를 한 곳에 정리한 문서입니다.

- 열화상 경로: `FLIR Lepton -> Teensy 4.1 -> SPI frame link -> ESP32-C5 -> UDP`
- 제어 경로: `Client/Server -> MQTT/TLS -> ESP32-C5 -> UART -> STM32F401RE -> PCA9685 -> Servo / Laser`

현재 코드 기준 활성 구현 경로는 아래 3개입니다.

- `esp32/`: ESP-IDF 프로젝트, 실제 앱 소스는 `esp32/main/`
- `STM32/SMT_SPI_Slave/`: STM32CubeIDE 기반 STM32F401RE 펌웨어
- `teensy/TeensyC/`: Zephyr 기반 Teensy 4.1 펌웨어

## 시스템 개요

```text
[ Client / Server ]
        |
        | MQTT/TLS
        |  - motor/control
        |  - motor/response
        |  - laser/control
        |  - system/status
        |  - system/control
        v
[ MQTT Broker ] <----------------------> [ ESP32-C5 ]
                                              |
                                              | UART1 115200 8N1
                                              v
                                        [ STM32F401RE ]
                                              |
                                              | I2C1
                                              v
                                          [ PCA9685 ]
                                              |
                                              +--> Servo 1
                                              +--> Servo 2
                                              +--> Servo 3
                                              +--> Laser GPIO

[ FLIR Lepton ] <--> [ Teensy 4.1 ] -- SPI frame link --> [ ESP32-C5 ]
                                                        |
                                                        +--> UDP frame stream (default)
                                                        +--> MQTT frame chunk publish (optional)
```

## 보드별 역할

### ESP32-C5

- Wi-Fi STA 연결 및 MQTT/TLS 클라이언트
- STM32 명령 브리지: MQTT 명령을 UART로 전달하고 STM32 응답을 다시 MQTT로 publish
- Teensy에서 들어오는 SPI frame link 패킷 수신 및 프레임 재조립
- health/watchdog JSON publish
- 기본 설정상 열화상 프레임은 UDP 8-bit 스트리밍 모드로 전송

### STM32F401RE

- `USART1`로 ASCII 라인 명령 수신
- `I2C1`로 PCA9685 제어
- 서보 3축 제어 및 연속 이동 상태 관리
- 레이저 GPIO on/off 처리
- 부팅 시 I2C 스캔과 `READY` 응답 출력

### Teensy 4.1

- FLIR Lepton 초기화 및 프레임 캡처
- 프레임을 256바이트 패킷으로 분할해 ESP32로 SPI 전송
- free/ready queue 기반으로 캡처와 전송 경로 분리
- 캡처 timeout 누적 시 Lepton reset

## 디렉터리 맵

| 경로 | 설명 |
| --- | --- |
| `esp32/main/main.c` | ESP32 진입점 |
| `esp32/main/device/frame_link.c` | Teensy -> ESP32 SPI slave 프레임 링크 |
| `esp32/main/device/cmd_uart.c` | ESP32 <-> STM32 UART 브리지 |
| `esp32/main/mqtt/mqtt.c` | MQTT/UDP 전송, health publish |
| `STM32/SMT_SPI_Slave/Core/Src/main.c` | STM32 진입점, UART 명령 파서, 레이저 제어 |
| `STM32/SMT_SPI_Slave/Core/Src/motor.c` | PCA9685 기반 3축 서보 제어 |
| `teensy/TeensyC/src/main.c` | Lepton 캡처 및 SPI frame sender |

## 인터페이스와 핀맵

### ESP32-C5 <-> STM32F401RE UART

코드 기준 UART 설정:

- 포트: `UART1` <-> `USART1`
- 속도: `115200 8N1`
- 줄 종료: `\r`, `\n`, `\r\n`

| 신호 | ESP32-C5 | STM32F401RE |
| --- | --- | --- |
| TX | `GPIO11` | `PA10 (USART1_RX)` |
| RX | `GPIO12` | `PA9 (USART1_TX)` |
| GND | `GND` | `GND` |

### STM32F401RE <-> PCA9685

| 신호 | STM32F401RE | PCA9685 |
| --- | --- | --- |
| SCL | `PB8` | `SCL` |
| SDA | `PB9` | `SDA` |
| VCC | `3V3/5V` | `VCC` |
| GND | `GND` | `GND` |

서보 매핑:

- `CH0` -> `motor1`
- `CH1` -> `motor2`
- `CH2` -> `motor3`

레이저 출력:

- `PA5` -> Laser enable GPIO

### Teensy 4.1 <-> FLIR Lepton

| 기능 | Teensy 4.1 핀 |
| --- | --- |
| Lepton I2C SCL | `19` |
| Lepton I2C SDA | `18` |
| Lepton SPI CS | `10` |
| Lepton SPI MOSI | `11` |
| Lepton SPI MISO | `12` |
| Lepton SPI SCK | `13` |

### Teensy 4.1 <-> ESP32-C5 SPI frame link

| 신호 | Teensy 4.1 | ESP32-C5 |
| --- | --- | --- |
| CS | `0` | `GPIO27` |
| MOSI | `26` | `GPIO24` |
| MISO | `1` | `GPIO25` |
| SCK | `27` | `GPIO23` |
| GND | `GND` | `GND` |

패킷 형식:

- 총 길이: `256 bytes`
- 헤더: `14 bytes`
- payload: `242 bytes`
- 헤더 magic: `"TEST"`

## 통신 프로토콜

### MQTT 토픽

| 토픽 | 방향 | 설명 |
| --- | --- | --- |
| `motor/control` | Client -> ESP32 | STM32 서보 제어 명령 |
| `motor/response` | STM32/ESP32 -> Client | STM32 응답 라인 |
| `laser/control` | Client -> ESP32 | 레이저 on/off 명령 |
| `system/status` | ESP32 -> Client | health/watchdog JSON |
| `system/control` | Client -> ESP32 | `publish_status_now` 요청 |
| `lepton/frame/chunk` | ESP32 -> Client | MQTT 프레임 chunk 전송 시 사용 |
| `lepton/status` | ESP32 -> Client | Lepton 상태 메시지 |

### 현재 기본 프레임 전송 모드

`esp32/main/app_secrets.defaults.h` 기준 기본값:

- `APP_FRAME_STREAM_MODE = 3`
- `APP_UDP_FRAME_8BIT = 1`

즉 현재 기본 동작은 아래와 같습니다.

- 제어, 응답, health: MQTT 사용
- 열화상 프레임: UDP 사용
- UDP payload: 8-bit 정규화 프레임 chunk

MQTT 프레임 전송도 지원되지만 기본 설정에서는 비활성입니다. 필요하면 `APP_FRAME_STREAM_MODE`를 변경해야 합니다.

### UDP 스트리밍

- 대상 IP: `APP_UDP_TARGET_IP`
- 대상 포트: `APP_UDP_TARGET_PORT`
- 코드 기본 포트: `5005`
- 대상 주소가 비어 있으면 UDP 스트림 비활성

## STM32 명령 프로토콜

STM32는 ASCII 라인 명령을 받습니다.

지원 명령:

- `motor1 left press`
- `motor1 right press`
- `motor1 release`
- `motor1 stop`
- `motor1 set 90`
- `motor1 left 10`
- `motor1 right 10`
- `motor2 ...`
- `motor3 ...`
- `read`
- `ping`
- `stopall`
- `LED ON`
- `LED OFF`
- `LASER ON`
- `LASER OFF`
- `help`

대표 응답:

- `BOOT USART1 PA9/PA10`
- `I2C scan start`
- `I2C 0x40`
- `READY`
- `OK 90 90 90`
- `ANGLES 90 90 90`
- `PONG`
- `LED ON`
- `LED OFF`
- `ERR`
- `ERR overflow`

동작 규칙:

- `release`: PWM 출력을 끄고 holding torque도 해제
- `stop`: 현재 각도 PWM은 유지하고 이동만 중단
- `left press` / `right press`: 20ms 주기로 1도씩 연속 이동
- 연속 이동 중 `0` 또는 `180`에 도달하면 자동 `release`

GUI 기준 자주 쓰는 명령 매핑:

- `Hold L`: `motor<N> left press`
- `Hold R`: `motor<N> right press`
- 버튼 release: `motor<N> release`
- `-10`: `motor<N> left 10`
- `+10`: `motor<N> right 10`
- `Center All`: `motor1 set 90`, `motor2 set 90`, `motor3 set 90`
- `Read Angles`: `read`
- `Stop All`: `stopall`

## Health JSON

`system/status`는 주기적으로 60초마다 publish되며, `system/control`에 `publish_status_now`를 보내면 즉시 한 번 더 publish됩니다.

예시:

```text
topic: system/control
payload: publish_status_now
```

주요 필드:

- `uptime_sec`: 부팅 후 경과 시간
- `wifi_connected`: AP 연결 여부
- `wifi_rssi`: 현재 RSSI
- `mqtt_connected`: 브로커 연결 여부
- `free_heap`: 현재 free heap
- `min_heap`: 부팅 이후 최소 free heap
- `cmd_queue_depth`: STM32 UART 명령 큐 적재량
- `frame_packets`: Teensy -> ESP32 수신 packet 수
- `frame_completed`: 완성된 frame 수
- `frame_timeouts`: SPI receive timeout 누적
- `frame_errors`: SPI driver error 누적
- `bad_magic`: frame magic mismatch
- `bad_checksum`: checksum mismatch
- `bad_len`: payload length 오류
- `seq_errors`: chunk 순서 불일치
- `queue_full_drops`: frame buffer 부족 drop
- `stale_frame_drops`: 오래된 frame drop
- `frame_ready`: 아직 소비되지 않은 ready frame 수

## 빌드와 실행

### ESP32

로컬 설정 파일 생성:

1. `esp32/main/app_secrets.example.h`를 `esp32/main/app_secrets.h`로 복사
2. Wi-Fi, MQTT broker, UDP target 값을 채움
3. 필요하면 `esp32/main/mqtt/app_client_config.example.py`를 `esp32/main/mqtt/app_client_config.py`로 복사

빌드:

```powershell
cd esp32
idf.py set-target esp32c5
idf.py build
```

플래시 및 모니터:

```powershell
idf.py -p COMx flash monitor
```

비고:

- 인증서는 `esp32/main/certs/`에서 embed
- `sdkconfig`는 로컬 ESP-IDF 환경에서 갱신됨
- 현재 ESP-IDF 빌드는 `esp32/main/` 소스를 사용

### STM32

프로젝트 경로:

```text
STM32/SMT_SPI_Slave
```

권장 방법:

- STM32CubeIDE에서 `.ioc` 또는 프로젝트 폴더를 열어 빌드/플래시

### Teensy 4.1

Zephyr workspace 초기화:

```powershell
cd teensy/TeensyC
west init -l .
west update
```

빌드:

```powershell
west build -b teensy41 .
```

플래시:

```powershell
west flash
```

MCUXpresso for VS Code를 쓰는 경우:

- Repository type: `Zephyr`
- Repository path: `Hardware/teensy/TeensyC`
- Manifest file: `west.yml`
- Project type: `Repository application`

주요 Zephyr 진입 파일:

- `CMakeLists.txt`
- `prj.conf`
- `app.overlay`
- `west.yml`

## 빠른 점검 순서

1. Teensy가 Lepton 초기화 후 프레임을 보내는지 확인
2. ESP32 로그에서 `Frame link SPI slave init OK`와 Wi-Fi 연결 확인
3. STM32 부팅 후 `READY`가 UART를 통해 올라오는지 확인
4. MQTT에서 `motor/control` 명령 publish 후 `motor/response` 응답 확인
5. `system/control -> publish_status_now` 요청 후 `system/status` 즉시 응답 확인
