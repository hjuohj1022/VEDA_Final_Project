# Thermal Imaging and Remote Actuation Hardware

### 컴포넌트 명칭

FLIR Lepton 열화상 프레임 수집과 MQTT 기반 원격 구동 제어를 분리된 데이터 평면과 제어 평면으로 처리하는 3보드 협업 하드웨어 펌웨어 스택입니다.

**주요 환경 및 플랫폼:** Windows 10/11 x64 또는 Ubuntu 22.04 LTS, ESP-IDF 5.5.3, Zephyr 2.7.1, STM32CubeIDE 기반 arm-none-eabi-gcc, ESP32-C5 / STM32F401RE / Teensy 4.1, FLIR Lepton, PCA9685

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

이 섹션은 시스템의 설계 철학과 주요 모듈의 책임을 정의합니다.

###### 설계 아키텍처 패턴 (Architectural Pattern)

- **적용 패턴:** Split Control/Data Plane 기반 Bridge-Worker 아키텍처
- **설명:** ESP32-C5를 시스템 게이트웨이로 두고, 제어 트래픽은 `MQTT -> UART -> STM32` 경로로, 열화상 데이터는 `Teensy -> SPI -> ESP32 -> UDP/MQTT` 경로로 분리했습니다. 이 구조는 서보/레이저 제어처럼 지연에 민감한 명령과 대역폭이 큰 프레임 스트림이 서로 직접 간섭하지 않도록 완충하며, 보드별 책임을 명확히 분리해 확장과 디버깅을 쉽게 합니다.

###### 주요 구성 (Major Components)

| 구성 요소명 | 컴포넌트의 핵심 역할 | 핵심 소스/실행 파일 경로 |
| --- | --- | --- |
| ESP32-C5 Gateway | Wi-Fi 연결, MQTT/TLS 클라이언트, STM32 UART 브리지, 열화상 프레임 재조립 및 UDP/MQTT 송신 | `esp32/main/main.c`, `esp32/main/mqtt/`, `esp32/main/device/` |
| STM32F401RE Actuator Controller | UART 명령 파싱, PCA9685 기반 3축 서보 제어, 레이저 GPIO 제어 | `STM32/SMT_SPI_Slave/Core/Src/main.c`, `STM32/SMT_SPI_Slave/Core/Src/motor.c` |
| Teensy 4.1 Thermal Capture Node | FLIR Lepton 초기화, 프레임 캡처, SPI frame link 송신 | `teensy/TeensyC/src/main.c` |
| Host Tools / Reference | 시리얼 점검 스크립트와 명령 레퍼런스 제공 | `teensy/tools/`, `command.md` |

###### 모듈 상세 (Module Detail)

| 모듈명 | 상세 책임 정의 |
| --- | --- |
| `esp32/main/main.c` | NVS, 네트워크 스택, frame link, UART bridge, MQTT/health task 초기화 |
| `esp32/main/device/frame_link.c` | Teensy가 보내는 256바이트 SPI 패킷을 검증하고 `38400`바이트 프레임으로 재조립 |
| `esp32/main/device/cmd_uart.c` | MQTT에서 받은 제어 명령을 STM32 UART로 전달하고 응답을 `motor/response`로 재발행 |
| `esp32/main/mqtt/mqtt.c` | MQTT subscribe/publish, health JSON 생성, UDP/MQTT 프레임 전송 정책 처리 |
| `esp32/main/mqtt/wifi.c` | Wi-Fi STA 연결, IP 획득 후 UDP/MQTT bring-up |
| `STM32/SMT_SPI_Slave/Core/Src/main.c` | UART 명령 파싱, 부팅 진단 메시지, `READY` 응답, 레이저 제어 |
| `STM32/SMT_SPI_Slave/Core/Src/motor.c` | PCA9685 초기화, 절대/상대 각도 이동, press/release/stopall 상태 머신 |
| `STM32/SMT_SPI_Slave/Core/Inc/motor.h` | PCA9685 주소, PWM 주파수 330Hz, 서보별 보정값 정의 |
| `teensy/TeensyC/src/main.c` | Lepton CCI 설정, 세그먼트 단위 캡처, frame queue 관리, SPI 송신 스레드 실행 |
| `teensy/tools/*.ps1` | Teensy 시리얼 상태/FPS 점검용 PowerShell 클라이언트 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
[ Client / Server / Operator Tool ]
              |
              | MQTT control / status
              v
        [ MQTT Broker ]
              |
              v
          [ ESP32-C5 ]
          |         |
          | UART1   | UDP or MQTT frame stream
          v         v
[ STM32F401RE ]   [ Remote Thermal Consumer ]
          |
          | I2C1
          v
       [ PCA9685 ]
          |
          +--> Servo 1
          +--> Servo 2
          +--> Servo 3
          +--> Laser GPIO

[ FLIR Lepton ] <-> [ Teensy 4.1 ] -- SPI frame link --> [ ESP32-C5 ]
```

###### Features

- **기능 1**: FLIR Lepton 프레임을 Teensy 4.1에서 캡처하고 ESP32-C5를 통해 UDP 또는 MQTT로 송신
- **기능 2**: MQTT 명령을 STM32 UART 제어 채널로 브리지하여 3축 서보와 레이저를 원격 제어
- **기능 3**: `system/status` 헬스 체크와 `publish_status_now` 즉시 상태 요청으로 운영 상태를 원격 모니터링
- **기능 4**: 제어 채널과 열화상 채널을 분리해 프레임 부하 상황에서도 제어 응답 경로를 유지
- **기능 5**: SPI checksum, magic, sequence 검증과 캡처 타임아웃 리셋 로직으로 데이터 경로 이상을 감지

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **OS**: Windows 10/11 x64 권장, 또는 Ubuntu 22.04 LTS 이상
- **Compiler/Toolchain**:
  - ESP32-C5: ESP-IDF 5.5.3 (`idf.py`, RISC-V toolchain 포함)
  - Teensy 4.1: Zephyr 2.7.1 + `west`
  - STM32F401RE: STM32CubeIDE 내장 `arm-none-eabi-gcc`
- **Essential Libraries**:
  - ESP32: `esp_wifi`, `esp_event`, `esp_netif`, `mqtt`, `lwip`, `nvs_flash`, `esp_driver_gpio`, `esp_driver_spi`, `esp_driver_uart`
  - STM32: STM32 HAL Driver, CMSIS
  - Teensy: Zephyr kernel, GPIO/I2C/SPI/USB device stack
  - Hardware dependency: FLIR Lepton, PCA9685, 3채널 서보, Laser GPIO 출력

###### 환경 변수 및 경로 설정 (Path Configurations)

하드웨어 통합 시스템에서는 경로 일관성이 빌드 성공의 핵심입니다.

- **설정 파일명**:
  - `esp32/main/app_secrets.h`
  - `esp32/main/CMakeLists.txt`
  - `teensy/TeensyC/west.yml`
  - `STM32/SMT_SPI_Slave/STM_SPI_Slave.ioc`
- **필수 변수**:
  - ESP32 애플리케이션 설정: `APP_WIFI_SSID`, `APP_WIFI_PASS`, `APP_MQTT_BROKER_URI`, `APP_UDP_TARGET_IP`, `APP_UDP_TARGET_PORT`, `APP_FRAME_STREAM_MODE`, `APP_UDP_FRAME_8BIT`
  - ESP-IDF 환경: `IDF_PATH`
  - Zephyr 환경: `ZEPHYR_BASE`, `west` workspace 경로
  - 로컬 플래시/모니터링: `COMx` 또는 Linux 시리얼 포트 경로
- **인증서 경로 주의사항**:
  - ESP32 CMake는 `esp32/main/certs/rootCA.crt`, `esp32/main/certs/client-stm32.crt`, `esp32/main/certs/client-stm32.key`를 embed 대상으로 기대합니다.
  - 현재 저장소에는 `cert.h`, `cert.c`만 포함되어 있으므로 실제 PEM/CRT 파일은 로컬 환경에서 제공해야 합니다.

###### Dependency Setup

의존성 자동 설치 스크립트는 저장소에 포함되어 있지 않으며, 아래 순서로 수동 준비하는 것을 권장합니다.

**ESP32-C5**

```powershell
cd esp32
copy .\main\app_secrets.example.h .\main\app_secrets.h
idf.py set-target esp32c5
idf.py build
```

**Teensy 4.1**

```powershell
cd teensy\TeensyC
west init -l .
west update
west build -b teensy41 .
```

**STM32F401RE**

```text
STM32CubeIDE에서 STM32/SMT_SPI_Slave/STM_SPI_Slave.ioc 또는 프로젝트 폴더를 열고 Build / Flash 수행
```

##### 4. 설정 가이드 (Configuration)

운영 환경에 따라 수정이 필요한 설정 파일과 파라미터 정보입니다.

###### 설정 파일명 1: `esp32/main/app_secrets.h`

- `APP_WIFI_SSID`: 연결할 AP SSID
- `APP_WIFI_PASS`: AP 비밀번호
- `APP_MQTT_BROKER_URI`: 예 `mqtts://broker-host:8883`
- `APP_UDP_TARGET_IP`: 열화상 UDP 수신기 주소, 비어 있으면 UDP 스트림 비활성
- `APP_UDP_TARGET_PORT`: 기본값 `5005`
- `APP_FRAME_STREAM_MODE`:
  - `0`: MQTT only
  - `1`: UDP only
  - `2`: UDP + MQTT 동시 송신
  - `3`: UDP frame + MQTT control/status, 현재 기본값
- `APP_UDP_FRAME_8BIT`: `1`이면 16-bit 원시 열화상 데이터를 8-bit 정규화 청크로 UDP 송신

###### 설정 파일명 2: `STM32/SMT_SPI_Slave/Core/Inc/motor.h`

- `PCA9685_I2C_ADDR`: 기본 주소 `0x40`
- `PCA9685_PRESCALE_VAL`: PWM 330Hz 설정값
- `CAL_MIN_US`, `CAL_CENTER_US`, `CAL_MAX_US`: 모터별 펄스 보정값
- `MOTOR_ANGLE_MIN`, `MOTOR_ANGLE_MAX`: 허용 각도 범위 `0 ~ 180`

###### 설정 파일명 3: `teensy/TeensyC/prj.conf`

- `CONFIG_MAIN_STACK_SIZE`: 메인 스택 크기
- `CONFIG_HEAP_MEM_POOL_SIZE`: Zephyr heap 크기
- `CONFIG_USB_UART_CONSOLE`: USB 시리얼 콘솔 활성화

###### 보안 및 통신 설정: `esp32/main/CMakeLists.txt`

- 인증서 경로: `certs/rootCA.crt`, `certs/client-stm32.crt`, `certs/client-stm32.key`
- 보안 정책: ESP32 MQTT 클라이언트는 CA 인증서와 클라이언트 인증서를 사용하며, `skip_cert_common_name_check = true`가 설정되어 있습니다.
- 통신 포트:
  - ESP32 <-> STM32 UART: `115200 8N1`
  - ESP32 <-> Teensy SPI frame link: Teensy master `4 MHz`
  - STM32 <-> PCA9685 I2C1: `400 kHz`

###### 인터페이스 및 핀맵 요약

**ESP32-C5 <-> STM32F401RE UART**

| 신호 | ESP32-C5 | STM32F401RE |
| --- | --- | --- |
| TX | `GPIO11` | `PA10 (USART1_RX)` |
| RX | `GPIO12` | `PA9 (USART1_TX)` |
| GND | `GND` | `GND` |

**STM32F401RE <-> PCA9685**

| 신호 | STM32F401RE | PCA9685 |
| --- | --- | --- |
| SCL | `PB8` | `SCL` |
| SDA | `PB9` | `SDA` |
| VCC | `3V3/5V` | `VCC` |
| GND | `GND` | `GND` |

서보 채널 매핑:

- `CH0 -> motor1`
- `CH1 -> motor2`
- `CH2 -> motor3`
- `PA5 -> Laser enable GPIO`

**Teensy 4.1 <-> FLIR Lepton**

| 기능 | Teensy 4.1 핀 |
| --- | --- |
| Lepton I2C SCL | `19` |
| Lepton I2C SDA | `18` |
| Lepton SPI CS | `10` |
| Lepton SPI MOSI | `11` |
| Lepton SPI MISO | `12` |
| Lepton SPI SCK | `13` |

**Teensy 4.1 <-> ESP32-C5 SPI frame link**

| 신호 | Teensy 4.1 | ESP32-C5 |
| --- | --- | --- |
| CS | `0` | `GPIO27` |
| MOSI | `26` | `GPIO24` |
| MISO | `1` | `GPIO25` |
| SCK | `27` | `GPIO23` |
| GND | `GND` | `GND` |

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

컴포넌트별 빌드 및 산출물 생성 절차는 다음과 같습니다.

**ESP32-C5**

```powershell
cd esp32
idf.py set-target esp32c5
idf.py build
idf.py -p COMx flash monitor
```

**Teensy 4.1**

```powershell
cd teensy\TeensyC
west init -l .
west update
west build -b teensy41 . -d build\teensy41-debug --pristine=auto
west flash -d build\teensy41-debug
```

**STM32F401RE**

```text
STM32CubeIDE에서 Build Project
STM32CubeProgrammer 또는 IDE 내 Flash 기능으로 다운로드
```

###### Static Analysis (품질 검증)

코드의 안정성을 위해 로컬 환경과 CI 파이프라인에서 다음 분석을 수행하십시오.

- **Local IDE Analysis**:
  - STM32: STM32CubeIDE 정적 분석 또는 컴파일 경고 확인
  - ESP32: `idf.py build` 시 warning/error 확인
  - Teensy/Zephyr: `west build` 시 driver/device tree 경고 확인
- **CI/CD Pipeline Analysis**:
  - 현재 저장소에는 전용 CI 분석 스크립트가 포함되어 있지 않습니다.
  - 권장 항목: `clang-tidy`, `cppcheck`, MISRA 점검 도구를 컴포넌트별 빌드 파이프라인에 추가

```text
예시 권장 분석 흐름
1. ESP32: idf.py build
2. Teensy: west build -b teensy41 .
3. STM32: CubeIDE headless build + 정적 분석 리포트 수집
```

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

**펌웨어 기동**

```powershell
# ESP32-C5
cd esp32
idf.py -p COMx flash monitor

# Teensy 4.1
cd teensy\TeensyC
west flash -d build\teensy41-debug
```

**MQTT 제어 예시**

```powershell
mosquitto_pub -h <broker> -p 8883 -t motor/control -m "motor1 set 90"
mosquitto_pub -h <broker> -p 8883 -t laser/control -m "laser on"
mosquitto_pub -h <broker> -p 8883 -t system/control -m "publish_status_now"
```

**Teensy 시리얼 점검 도구**

```powershell
pwsh .\teensy\tools\teensy_spi_client.ps1 -Port COM8
pwsh .\teensy\tools\teensy_fps_client.ps1 -Port COM8
```

###### Test (검증 방법)

- **Unit Test**: 현재 저장소에는 독립적인 단위 테스트 타깃이 포함되어 있지 않습니다.
- **Smoke Test**:
  - STM32 부팅 후 UART에서 `BOOT USART1 PA9/PA10`, `I2C 0x40`, `READY` 출력 확인
  - ESP32 로그에서 `Frame link SPI slave init OK` 및 Wi-Fi/MQTT 연결 확인
  - `motor/control`에 `ping` 또는 `motor1 set 90` publish 후 `motor/response` 응답 확인
  - `system/control`에 `publish_status_now` publish 후 `system/status` JSON 확인
  - UDP 수신기에서 `APP_UDP_TARGET_PORT` 기본값 `5005`로 프레임 청크 유입 확인

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Start/Channel Select**:
  - Teensy는 부팅 후 Lepton 초기화가 완료되면 프레임 캡처를 시작합니다.
  - ESP32는 frame link, UART bridge, Wi-Fi, MQTT를 순서대로 초기화합니다.
  - STM32는 PCA9685 초기화 후 `READY`를 반환합니다.
  - 제어 채널의 대상 선택은 `motor1`, `motor2`, `motor3` 명령으로 이루어집니다.
- **Pause/Resume**:
  - 서보 축은 `release` 또는 `stop`으로 개별 축 이동을 멈출 수 있습니다.
  - 열화상 스트림에 대한 통합 `pause/resume` 명령은 현재 구현되어 있지 않습니다.
  - `APP_FRAME_STREAM_MODE`와 UDP 대상 설정으로 프레임 송신 정책을 제어합니다.
- **Stop**:
  - `stopall`은 모든 서보의 연속 이동 상태를 정지합니다.
  - Teensy는 캡처 타임아웃이 3회 연속 발생하면 Lepton을 리셋합니다.
  - ESP32는 Wi-Fi 단절 시 UDP 소켓을 리셋하고 재연결을 시도합니다.

###### Command Reference

제어 명령은 JSON이 아니라 MQTT payload 또는 UART 라인 기반의 ASCII 문자열입니다.

**MQTT 토픽 요약**

| 토픽 | 방향 | 설명 |
| --- | --- | --- |
| `motor/control` | Client -> ESP32 | 서보 제어 명령 |
| `motor/response` | STM32/ESP32 -> Client | STM32 응답 라인 |
| `laser/control` | Client -> ESP32 | 레이저 on/off 명령 |
| `system/status` | ESP32 -> Client | health/watchdog JSON |
| `system/control` | Client -> ESP32 | `publish_status_now` 요청 |
| `lepton/frame/chunk` | ESP32 -> Client | MQTT 프레임 chunk 전송 시 사용 |
| `lepton/status` | ESP32 -> Client | Lepton 상태 메시지 |

**MQTT 요청 예시**

```text
topic: motor/control
payload: motor1 left press
```

```text
topic: laser/control
payload: laser on
```

```text
topic: system/control
payload: publish_status_now
```

**STM32 응답 예시**

```text
READY
OK 90 90 90
ANGLES 90 90 90
PONG
ERR
ERR overflow
```

지원 명령:

- `motor<N> left press`
- `motor<N> right press`
- `motor<N> release`
- `motor<N> stop`
- `motor<N> set <0~180>`
- `motor<N> left <delta>`
- `motor<N> right <delta>`
- `read`
- `ping`
- `stopall`
- `LED ON`
- `LED OFF`
- `LASER ON`
- `LASER OFF`
- `help`

###### Stream/Data Format (데이터 규격)

바이너리 데이터 전송 시 엔디안과 패킹 규칙이 고정되어 있습니다.

| 데이터 구분 | ACK 메시지 | Header 구조 | Payload 데이터 |
| --- | --- | --- | --- |
| STM32 UART 제어 응답 | `READY`, `OK a b c`, `ANGLES a b c`, `PONG`, `ERR` | 없음, ASCII line protocol | CR/LF 종료 문자열 |
| Teensy -> ESP32 SPI frame packet | 별도 ACK 없음 | `magic[4]="TEST"`, `frame_id[2]`, `chunk_idx[2]`, `total_chunks[2]`, `payload_len[2]`, `checksum[2]` | 최대 `242`바이트, 전체 프레임 `38400`바이트 |
| ESP32 -> MQTT/UDP frame chunk | MQTT QoS 0 또는 UDP 무연결 | `frame_id[2]`, `chunk_idx[2]`, `total_chunks[2]`, `min[2]`, `max[2]` | 최대 `1200`바이트 chunk, UDP 8-bit 모드에서는 정규화된 픽셀 데이터 |
| ESP32 -> `system/status` | MQTT publish | 없음 | JSON 상태 스냅샷 |

- **Byte Order**:
  - SPI frame packet 헤더 필드: Big-endian 16-bit
  - 열화상 픽셀: Big-endian 16-bit raw 값
  - MQTT/UDP chunk 헤더 필드: Big-endian 16-bit
  - UART 제어 응답: ASCII 문자열이므로 엔디안 적용 대상 아님
- **Header Alignment**:
  - SPI frame packet 헤더: 14바이트 고정, padding 없음
  - MQTT/UDP chunk 헤더: 10바이트 고정, padding 없음

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting (자주 발생하는 문제)

| 현상 | 원인 | 해결책 |
| --- | --- | --- |
| ESP32 빌드 시 인증서 파일 없음 오류 | `esp32/main/certs/*.crt`, `*.key` 파일이 로컬에 없음 | 인증서 3종을 `esp32/main/certs/` 경로에 배치하고 다시 빌드 |
| ESP32가 Wi-Fi 연결 후 MQTT를 올리지 못함 | `APP_MQTT_BROKER_URI` 미설정 또는 TLS 인증서 불일치 | `app_secrets.h`와 인증서 체인을 재확인 |
| UDP 프레임이 오지 않음 | `APP_UDP_TARGET_IP`가 비어 있거나 대상 주소가 잘못됨 | `APP_UDP_TARGET_IP`, `APP_UDP_TARGET_PORT`를 설정하고 수신기 포트를 확인 |
| `motor/response`가 오지 않음 | ESP32 <-> STM32 UART 배선 오류 또는 STM32 미기동 | 실제 설정 기준 `GPIO11/12 <-> PA10/PA9`, `115200 8N1`, `READY` 출력 여부 확인 |
| ESP32 로그에 UART 핀이 `GPIO8/9`로 표시됨 | `main.c`의 디버그 문자열이 오래된 값일 수 있음 | 실제 동작은 `esp32/main/device/cmd_uart.h`의 `GPIO11/12` 정의를 기준으로 확인 |
| STM32 부팅 시 `I2C none` 또는 `ERR PCA9685` 발생 | PCA9685 미연결, 전원 불량, 주소 불일치 | `PB8/PB9`, 전원/GND, 주소 `0x40` 확인 |
| 프레임이 끊기거나 `bad_checksum`, `seq_errors`가 증가 | Teensy <-> ESP32 SPI 품질 저하 또는 처리량 초과 | 배선 길이 단축, 접지 확인, SPI clock/gap 조정 |
| Teensy가 반복적으로 capture timeout 발생 | Lepton 초기화 실패 또는 센서 응답 불안정 | Lepton 전원/배선 확인, timeout 3회 후 자동 reset 로그 확인 |

###### Operational Checklist (배포 전 최종 확인)

- 설정 파일의 Wi-Fi, MQTT Broker, UDP Target, IP/Port가 운영 환경에 맞게 수정되었는가?
- ESP32 인증서 파일 3종이 올바른 경로에 배치되었는가?
- ESP32 <-> STM32 UART, STM32 <-> PCA9685 I2C, Teensy <-> ESP32 SPI 배선이 최신 핀맵과 일치하는가?
- STM32 부팅 시 `READY`, ESP32 부팅 시 frame link 초기화 로그가 확인되는가?
- `system/status`에서 `wifi_connected`, `mqtt_connected`, `frame_errors`, `bad_checksum` 지표가 정상 범위인가?
- 서보 보정값(`CAL_MIN_US`, `CAL_CENTER_US`, `CAL_MAX_US`)이 실제 하드웨어 기준으로 검증되었는가?
- 열화상 스트림 소비자 측에서 UDP `5005` 또는 선택한 포트가 열려 있는가?
- 정적 분석 및 빌드 경고에서 치명적 오류가 남아 있지 않은가?

**작성자:** 황근하  
**마지막 업데이트:** 2026-03-18
