# Thermal Imaging and Remote Actuation Hardware

### 컴포넌트 명칭

FLIR Lepton 열화상 프레임 수집과 원격 서보/레이저 제어를 분리된 데이터 평면과 제어 평면으로 처리하는 3보드 협업형 하드웨어 펌웨어 스택

**주요 환경 및 플랫폼:** Windows 10/11 x64 또는 Ubuntu 22.04 LTS 이상, ESP-IDF 5.5.3, Zephyr 2.7.1, STM32CubeIDE 기반 `arm-none-eabi-gcc`, ESP32-C5 / STM32F401RE / Teensy 4.1, FLIR Lepton, PCA9685

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

이 섹션은 시스템의 설계 철학과 주요 모듈의 책임을 정의합니다.

###### 설계 아키텍처 패턴 (Architectural Pattern)

* **적용 패턴:** Split Control/Data Plane 기반 Bridge-Worker + Pub-Sub 아키텍처
* **설명:** 열화상 프레임은 대역폭 사용량이 크고 지속 전송이 필요하며, 서보/레이저 제어는 작은 패킷이지만 지연과 누락에 민감합니다. 본 시스템은 이를 분리하기 위해 `Teensy -> ESP32 -> UDP` 경로를 데이터 평면으로, `Client -> MQTT/TLS -> ESP32 -> UART -> STM32` 경로를 제어 평면으로 구성합니다. 이 구조는 네트워크 부하가 큰 프레임 스트리밍이 제어 응답성을 직접 저하시킬 가능성을 줄이고, 보드별 책임을 명확히 하여 디버깅과 확장을 용이하게 합니다.

###### 주요 구성 (Major Components)

| 구성 요소명 | 컴포넌트의 핵심 역할 | 핵심 소스/실행 파일 경로 |
| --- | --- | --- |
| ESP32-C5 Gateway | Wi-Fi 연결, MQTT/TLS 클라이언트, UDP 프레임 송신, Teensy SPI 프레임 재조립, STM32 UART 브리지 | `esp32/main/main.c`, `esp32/main/mqtt/`, `esp32/main/device/` |
| STM32F401RE Actuator Controller | UART 명령 파싱, PCA9685 기반 3축 서보 제어, 레이저 GPIO 제어 | `STM32/SMT_SPI_Slave/Core/Src/main.c`, `STM32/SMT_SPI_Slave/Core/Src/motor.c` |
| Teensy 4.1 Thermal Capture Node | FLIR Lepton 초기화, 프레임 캡처, SPI frame link 송신 | `teensy/TeensyC/src/main.c` |
| Host Tools / Diagnostics | Teensy 시리얼 상태 및 FPS 점검용 PowerShell 도구 제공 | `teensy/tools/` |

###### 모듈 상세 (Module Detail)

| 파일명/모듈명 | 구체적인 비즈니스 로직 및 기능 설명 |
| --- | --- |
| `esp32/main/main.c` | ESP32 전체 초기화 진입점. NVS, 네트워크 스택, frame link, UART bridge, frame task, health task를 초기화하고 Wi-Fi 연결을 시작합니다. |
| `esp32/main/mqtt/mqtt.c` | MQTT subscribe/publish 처리, health JSON 생성, 열화상 프레임을 UDP 또는 MQTT chunk로 송신하는 정책을 담당합니다. |
| `esp32/main/mqtt/udp_stream.c` | UDP 소켓 초기화, 송신 재시도, 혼잡 상태 판단, Wi-Fi reconnect 이후 재초기화를 담당합니다. |
| `esp32/main/mqtt/wifi.c` | Wi-Fi STA 연결, IP 획득 후 MQTT client 및 UDP stream 초기화 타이밍 제어를 담당합니다. |
| `esp32/main/device/frame_link.c` | Teensy가 전송한 256바이트 SPI 패킷의 magic/checksum/sequence를 검증하고 38400바이트 프레임으로 재조립합니다. |
| `esp32/main/device/cmd_uart.c` | MQTT 제어 명령을 STM32 UART로 전달하고, STM32의 line 기반 응답을 `motor/response`로 재발행합니다. |
| `STM32/SMT_SPI_Slave/Core/Src/main.c` | STM32 메인 루프. UART 명령 수신, boot diagnostics, `READY` 응답, 레이저 제어를 수행합니다. |
| `STM32/SMT_SPI_Slave/Core/Src/motor.c` | PCA9685 초기화, 절대/상대 각도 이동, 연속 이동, `release`/`stop`/`stopall` 상태 전이를 담당합니다. |
| `STM32/SMT_SPI_Slave/Core/Inc/motor.h` | PCA9685 주소, PWM 주파수 330Hz, 서보 보정값, 허용 각도 범위 등의 상수를 정의합니다. |
| `teensy/TeensyC/src/main.c` | Lepton CCI 설정, 세그먼트 단위 캡처, frame queue 관리, ESP32로의 SPI 송신 스레드를 실행합니다. |
| `teensy/tools/teensy_spi_client.ps1` | Teensy 시리얼 상태 확인에 사용되는 PowerShell 도구입니다. |
| `teensy/tools/teensy_fps_client.ps1` | Teensy 프레임 전송률 확인에 사용되는 PowerShell 도구입니다. |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

아키텍처 구조도는 다음 텍스트 구성으로 해석할 수 있습니다.

```text
[ Client / Server / Operator Tool ]
              |
              | MQTT/TLS
              |  - motor/control
              |  - motor/response
              |  - laser/control
              |  - system/status
              |  - system/control
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

* **기능 1** : FLIR Lepton의 160x120 16-bit 열화상 프레임을 Teensy 4.1에서 캡처하고 ESP32-C5를 통해 UDP 또는 MQTT chunk로 송신
* **기능 2** : MQTT/TLS 기반 제어 채널을 통해 3축 서보와 레이저를 원격 제어
* **기능 3** : `system/status` 주기 publish 및 `publish_status_now` 요청 기반의 운영 상태 모니터링
* **기능 4** : 제어 평면과 데이터 평면을 분리하여 프레임 부하 상황에서도 제어 응답 경로를 유지
* **기능 5** : SPI magic/checksum/sequence 검증, Lepton capture timeout reset, UDP 재연결 지연 초기화 등 장애 대응 로직 포함

###### 최근 코드 변경 반영 메모 (2026-03-23 이후)

아래 설명 중 일부는 README 마지막 대규모 정리 시점(2026-03-19) 기준 용어를 유지하고 있으므로, 현재 코드는 다음 기준으로 읽어야 합니다.

* **ESP32 프레임 전송 경로 단순화** : `esp32/main/mqtt/udp_stream.c`는 `lwip` 소켓 기반 plain UDP 송신기로 동작합니다.
* **설정 키 변경** : `esp32/main/app_secrets.example.h` 기준 열화상 수신 설정은 `APP_UDP_TARGET_HOST`, `APP_UDP_TARGET_PORT`만 사용합니다.
* **프레임 송신 혼잡 제어 보강** : `esp32/main/mqtt/mqtt.c`와 `esp32/main/mqtt/udp_stream.c`에서 UDP 혼잡 상태를 별도로 감지해 프레임 단위 지연, 재시도, 재연결 backoff를 더 보수적으로 적용합니다.
* **레이저 출력 극성 변경** : STM32는 `LASER_ACTIVE_STATE`, `LASER_INACTIVE_STATE`를 도입해 NPN 구동 기준 active-high 출력으로 정리되었습니다. 따라서 `laser on`은 GPIOA `PA5`를 `SET`하는 동작으로 해석하면 됩니다.
* **STM32 주변장치 정리** : 현재 STM32 펌웨어는 UART + I2C + GPIO에 집중하며, 사용하지 않는 SPI1/DMA/TIM2 초기화와 인터럽트 경로는 제거되었습니다.
* **저장소 정리** : `esp32/main/mqtt/` 아래의 임시 Python 수신기/뷰어 스크립트와 `esp32/.vscode/` 예제 설정은 저장소에서 제거되었습니다. 현재 문서 기준 진단 도구는 `teensy/tools/` PowerShell 스크립트를 우선 사용합니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

* **OS** : Windows 10/11 x64 또는 Ubuntu 22.04 LTS 이상
* **Compiler/Toolchain** :
  * ESP32-C5: ESP-IDF 5.5.3
  * Teensy 4.1: Zephyr 2.7.1 + `west`
  * STM32F401RE: STM32CubeIDE 내장 `arm-none-eabi-gcc`
* **Essential Libraries** :
  * ESP32: `esp_wifi`, `esp_event`, `esp_netif`, `mqtt`, `lwip`, `nvs_flash`, `mbedtls`, `esp_driver_gpio`, `esp_driver_spi`, `esp_driver_uart`
  * STM32: STM32 HAL Driver, CMSIS
  * Teensy: Zephyr kernel, GPIO/I2C/SPI/USB stack

###### 환경 변수 및 경로 설정 (Path Configurations)

하드웨어 통합 시스템에서는 경로 일관성이 빌드 성공의 핵심입니다.

* **설정 파일명** :
  * `esp32/main/app_secrets.h`
  * `esp32/main/CMakeLists.txt`
  * `teensy/TeensyC/prj.conf`
  * `teensy/TeensyC/app.overlay`
  * `STM32/SMT_SPI_Slave/STM_SPI_Slave.ioc`
* **필수 변수** :
  * ESP32 애플리케이션 설정: `APP_WIFI_SSID`, `APP_WIFI_PASS`, `APP_MQTT_BROKER_URI`, `APP_UDP_TARGET_HOST`, `APP_UDP_TARGET_PORT`, `APP_FRAME_STREAM_MODE`, `APP_UDP_FRAME_8BIT`
  * ESP-IDF 환경: `IDF_PATH`
  * Zephyr 환경: `ZEPHYR_BASE`, `west` workspace 경로
  * 로컬 플래시/모니터링: `COMx` 또는 Linux serial port 경로

###### Dependency Setup

의존성 자동 설치 스크립트는 저장소에 포함되어 있지 않으므로 아래 절차를 수동 수행합니다.

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
west build -b teensy41 . -d build\teensy41-debug --pristine=auto
```

**STM32F401RE**

```text
STM32CubeIDE에서 STM32/SMT_SPI_Slave 프로젝트 또는 .ioc를 열고 Build 수행
```

##### 4. 설정 가이드 (Configuration)

운영 환경에 따라 수정이 필요한 설정 파일과 파라미터 정보입니다.

###### 설정 파일명 1: `esp32/main/app_secrets.h`

현재 예제 파일(`esp32/main/app_secrets.example.h`) 기준 설정은 plain UDP 송신을 사용합니다.

* `APP_WIFI_SSID`: 접속할 AP SSID
* `APP_WIFI_PASS`: AP 비밀번호
* `APP_MQTT_BROKER_URI`: 예 `mqtts://broker-host:8883`
* `APP_UDP_TARGET_HOST`: 열화상 UDP 수신기 호스트 또는 IP
* `APP_UDP_TARGET_PORT`: UDP 수신 포트, 기본값 `5005`
* `APP_FRAME_STREAM_MODE`:
  * `0`: MQTT only
  * `1`: UDP only
  * `2`: UDP + MQTT 동시 송신
  * `3`: UDP frame + MQTT control/status
* `APP_UDP_FRAME_8BIT`:
  * `0`: raw 16-bit frame chunk 전송
  * `1`: 8-bit normalized frame chunk 전송
* IP/PORT:
  * MQTT는 `APP_MQTT_BROKER_URI`
  * UDP는 `APP_UDP_TARGET_HOST` + `APP_UDP_TARGET_PORT`

###### 보안 및 통신 설정: `esp32/main/CMakeLists.txt` 및 `esp32/main/certs/`

추가 메모: 열화상 데이터 평면은 plain UDP입니다. MQTT/TLS 제어 채널과는 분리해서 해석해야 하며, 열화상 수신기는 별도의 UDP receiver로 준비해야 합니다.

* 인증서 경로:
  * `esp32/main/certs/rootCA.crt`
  * `esp32/main/certs/client-stm32.crt`
  * `esp32/main/certs/client-stm32.key`
* 보안 정책:
  * MQTT는 CA + client cert/key 기반 TLS를 사용
  * `mqtt.h`는 `cert.h`를 통해 embed된 인증서 심볼을 참조

###### 설정 파일명 2: `STM32/SMT_SPI_Slave/Core/Inc/motor.h`

* `PCA9685_I2C_ADDR`: 기본 주소 `0x40`
* `PCA9685_PRESCALE_VAL`: 330Hz PWM 설정값 `17`
* `MOTOR_ANGLE_MIN`, `MOTOR_ANGLE_MAX`: 허용 각도 범위 `0 ~ 180`
* `CAL_MIN_US`, `CAL_CENTER_US`, `CAL_MAX_US`: 모터별 calibration 값

###### 설정 파일명 3: `teensy/TeensyC/prj.conf`

* `CONFIG_MAIN_STACK_SIZE`: 메인 스택 크기
* `CONFIG_HEAP_MEM_POOL_SIZE`: Zephyr heap 크기
* `CONFIG_USB_UART_CONSOLE`: USB serial console 활성화 여부

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

컴파일 및 산출물 생성을 위한 단계별 명령어입니다.

**ESP32-C5**

```powershell
cd esp32
copy .\main\app_secrets.example.h .\main\app_secrets.h
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
STM32CubeIDE에서 STM32/SMT_SPI_Slave 프로젝트 또는 .ioc를 열고 Build / Flash 수행
```

###### Static Analysis (품질 검증)

코드의 안정성을 위해 로컬 환경과 CI 파이프라인에서 다음 분석을 수행하십시오.

* **Local IDE Analysis** :
  * ESP32: `idf.py build` 시 warning/error 점검
  * Teensy: `west build` 시 Zephyr device tree 및 driver warning 점검
  * STM32: STM32CubeIDE compiler warning 및 정적 분석 기능 점검
* **CI/CD Pipeline Analysis** :
  * 현재 저장소에는 전용 CI 정적 분석 스크립트가 포함되어 있지 않습니다.
  * 권장 항목: `clang-tidy`, `cppcheck`, MISRA 점검 도구를 컴포넌트별 파이프라인에 추가

정적 분석 실행 예시는 저장소에 자동화되어 있지 않으므로 프로젝트별 빌드 로그 검토를 기본 절차로 삼습니다.

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

**ESP32-C5**

```powershell
cd esp32
idf.py -p COMx flash monitor
```

**Teensy 4.1**

```powershell
cd teensy\TeensyC
west flash -d build\teensy41-debug
```

**MQTT 제어 예시**

```powershell
mosquitto_pub -h <broker> -p 8883 -t motor/control -m "ping"
mosquitto_pub -h <broker> -p 8883 -t motor/control -m "motor1 set 90"
mosquitto_pub -h <broker> -p 8883 -t laser/control -m "laser on"
mosquitto_pub -h <broker> -p 8883 -t system/control -m "publish_status_now"
```

**진단 도구 실행**

```powershell
pwsh .\teensy\tools\teensy_spi_client.ps1 -Port COM8
pwsh .\teensy\tools\teensy_fps_client.ps1 -Port COM8
```

###### Test (검증 방법)

* **Unit Test** :
  * 현재 저장소에는 독립적인 unit test target이 포함되어 있지 않습니다.
* **Smoke Test** :
  * STM32 UART에서 `BOOT USART1 PA9/PA10`, `I2C 0x40`, `READY` 출력 확인
  * ESP32 로그에서 `Frame link SPI slave init OK`, Wi-Fi 연결, MQTT 연결 확인
  * `motor/control -> ping` publish 후 `motor/response -> PONG` 확인
  * `system/control -> publish_status_now` publish 후 `system/status` JSON 확인
  * UDP receiver에서 `APP_UDP_TARGET_PORT` 기본값 `5005`로 frame chunk 유입 확인

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

컴포넌트 수명 주기 제어에 따른 상태 변화 정의입니다.

* **Start/Channel Select** :
  * Teensy는 Lepton 초기화 후 frame capture를 시작합니다.
  * ESP32는 frame link, UART bridge, frame task, health task, Wi-Fi를 순차적으로 초기화합니다.
  * Wi-Fi 연결 이후 MQTT client와 UDP stream 초기화 타이밍이 결정됩니다.
  * STM32는 PCA9685 초기화 후 `READY`를 반환합니다.
* **Pause/Resume** :
  * 전체 스트림에 대한 통합 pause/resume 명령은 구현되어 있지 않습니다.
  * 서보 제어는 `release`, `stop`, `stopall`로 세부 상태 제어가 가능합니다.
  * 열화상 전송 정책은 `APP_FRAME_STREAM_MODE`, `APP_UDP_FRAME_8BIT` 조합으로 결정됩니다.
* **Stop** :
  * `stopall`은 모든 서보의 연속 이동 상태를 중단합니다.
  * Teensy는 capture timeout 3회 연속 발생 시 Lepton reset을 수행합니다.
  * ESP32는 Wi-Fi disconnect 시 UDP stream reset request를 설정하고 재연결 후 지연 초기화를 수행합니다.

###### Command Reference

제어 명령은 JSON이 아니라 ASCII 문자열 기반 request/response 형식입니다.

**요청 예시**

```text
motor1 left press
```

```text
motor2 set 135
```

```text
laser on
```

```text
publish_status_now
```

**응답 예시**

```text
READY
OK 90 90 90
ANGLES 90 90 90
PONG
ERR
ERR overflow
```

**지원 명령 목록**

| 명령 | 설명 |
| --- | --- |
| `motor<N> left press` | 특정 서보 연속 왼쪽 이동 시작 |
| `motor<N> right press` | 특정 서보 연속 오른쪽 이동 시작 |
| `motor<N> release` | PWM 해제, 토크 유지 없이 해방 |
| `motor<N> stop` | 현재 각도 유지 |
| `motor<N> set <deg>` | 목표 각도 지정 |
| `motor<N> left <delta>` | 상대 각도 왼쪽 이동 |
| `motor<N> right <delta>` | 상대 각도 오른쪽 이동 |
| `read` | 현재 각도 조회 |
| `ping` | 통신 확인 |
| `stopall` | 전체 연속 이동 중지 |
| `LED ON`, `LED OFF` | 레이저 GPIO와 동일 동작 |
| `LASER ON`, `LASER OFF` | 레이저 GPIO 제어 |
| `help` | 지원 명령 요약 출력 |

###### Stream/Data Format (데이터 규격)

바이너리 데이터 전송 시 엔디안(Endianness) 및 정렬(Alignment) 규칙이 엄격히 준수되어야 합니다.

| 데이터 구분 | ACK 메시지 | Header 구조 | Payload 데이터 |
| --- | --- | --- | --- |
| STM32 UART 제어 응답 | `READY`, `OK a b c`, `ANGLES a b c`, `PONG`, `ERR` | Header 없음, line 기반 ASCII protocol | `\r\n` 종료 문자열 |
| Teensy -> ESP32 SPI frame packet | 별도 ACK 없음 | `magic[4]`, `frame_id[2]`, `chunk_idx[2]`, `total_chunks[2]`, `payload_len[2]`, `checksum[2]` | 최대 242바이트 payload, 전체 raw frame 38400바이트 |
| ESP32 -> UDP 또는 MQTT frame chunk | UDP는 무연결, MQTT는 QoS 0 | `frame_id[2]`, `chunk_idx[2]`, `total_chunks[2]`, `min[2]`, `max[2]` | UDP 시 최대 1024바이트, MQTT-only 시 최대 1200바이트 |
| ESP32 -> `system/status` | MQTT publish | Header 없음 | JSON 상태 스냅샷 |

* **Byte Order** :
  * SPI frame packet header: Big-endian 16-bit
  * 열화상 raw pixel: Big-endian 16-bit
  * ESP32 frame chunk header: Big-endian 16-bit
  * UART 응답: ASCII 문자열이므로 엔디안 적용 대상 아님
* **Header Alignment** :
  * Teensy -> ESP32 SPI frame header: 14-byte 고정, padding 없음
  * ESP32 -> remote frame chunk header: 10-byte 고정, padding 없음

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting (자주 발생하는 문제)

| 현상 | 원인 | 해결책 |
| --- | --- | --- |
| UDP frame이 수신되지 않음 | `APP_UDP_TARGET_HOST`, `APP_UDP_TARGET_PORT`, 수신 포트 설정 오류 | `app_secrets.h`의 UDP 설정과 receiver 포트 상태 확인 |
| MQTT 연결 실패 | broker URI 미설정 또는 인증서 불일치 | `APP_MQTT_BROKER_URI`, `rootCA.crt`, client cert/key 재검증 |
| `motor/response` 미수신 | ESP32 <-> STM32 UART 배선 또는 baud 설정 오류 | `GPIO11/12 <-> PA10/PA9`, `115200 8N1`, STM32 `READY` 출력 확인 |
| STM32 부팅 시 `ERR PCA9685` 또는 `I2C none` | PCA9685 미연결, 전원 문제, 주소 불일치 | `PB8/PB9`, GND, 주소 `0x40` 점검 |
| `bad_checksum` 또는 `seq_errors` 증가 | Teensy <-> ESP32 SPI 신호 품질 저하 | 배선 길이 단축, 접지 점검, 클록 무결성 확인 |
| Teensy capture timeout 반복 | Lepton 응답 불안정 또는 초기화 실패 | Lepton 전원/배선 점검, timeout 3회 후 reset 로그 확인 |
| Wi-Fi reconnect 후 프레임 복구 지연 | UDP stream이 reconnect 직후 즉시 올라오지 않고 deferred init 수행 | ESP32 로그에서 `UDP stream init deferred` 및 재초기화 성공 여부 확인 |

###### Operational Checklist (배포 전 최종 확인)

* 설정 파일의 Wi-Fi, MQTT broker, UDP target, IP/Port가 운영 환경에 맞게 수정되었는가?
* ESP32 인증서 파일 3종이 `esp32/main/certs/` 경로에 배치되었는가?
* ESP32 <-> STM32 UART, STM32 <-> PCA9685 I2C, Teensy <-> ESP32 SPI 배선이 최신 핀맵과 일치하는가?
* STM32 부팅 시 `READY`, ESP32 부팅 시 frame link 초기화 로그가 확인되는가?
* `system/status`에서 `wifi_connected`, `mqtt_connected`, `frame_errors`, `bad_checksum` 지표가 정상 범위인가?
* 서보 calibration 값이 실제 하드웨어 기준으로 검증되었는가?
* 열화상 소비자 측에서 UDP 포트 `5005` 또는 선택한 포트가 열려 있는가?
* 빌드 warning 및 수동 정적 분석 결과에서 치명적 오류가 남아 있지 않은가?

**작성자:** 황근하
**마지막 업데이트:** 2026-03-23
