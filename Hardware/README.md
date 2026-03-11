# Thermal Imaging & Remote Servo Control System

이 프로젝트는 FLIR Lepton 3.0 열화상 카메라 데이터를 수집하여 원격으로 전송하고, MQTT를 통해 서보 모터를 정밀하게 제어하는 임베디드 시스템입니다. Teensy 4.1(Zephyr), ESP32-C3(ESP-IDF), STM32F401RE(HAL) 세 가지 마이크로컨트롤러가 협력하여 전체 시스템을 구성합니다.

## 시스템 아키텍처

```text
[ FLIR Lepton 3.0 ] --(SPI/I2C)-- [ Teensy 4.1 (Zephyr RTOS) ]
                                       |
                                     (UART 921,600bps)
                                       |
[ Remote MQTT Broker ] <--(MQTTS/TLS)--> [ ESP32-C3 (ESP-IDF) ]
      (PC Clients)                     |
           ^                         (SPI Master/Slave)
           |                           |
[ Servo Motors (x3) ] <--(PWM)-- [ PCA9685 ] <--(I2C)-- [ STM32F401RE (HAL) ]
```

---

## 주요 구성 요소 및 기능

### 1. Teensy 4.1 (열화상 데이터 획득) - `teensy/TeensyC`
**Zephyr RTOS**를 기반으로 동작하며, 열화상 센서로부터 고속으로 데이터를 읽어 프레임을 구성합니다.
- **센서 인터페이스**: FLIR Lepton 3.0과 VoSPI(SPI) 및 CCI(I2C)로 통신.
- **프레임 동기화**: Lepton의 4개 세그먼트를 조합하여 160x120 해상도의 14비트 RAW 데이터를 생성.
- **고속 전송**: `FSTART` 헤더와 함께 UART(921,600 bps)로 프레임 데이터를 ESP32에 전송.

### 2. ESP32-C3 (네트워크 및 통신 브리지) - `esp32`
**ESP-IDF (FreeRTOS)** 환경에서 동작하며, 외부 네트워크와 내부 하드웨어를 연결하는 보안 게이트웨이 역할을 합니다.
- **MQTTS (Secure MQTT)**: AWS IoT 등 MQTT 브로커에 TLS/SSL 보안 인증서(`certs/`)를 사용하여 연결.
- **데이터 중계**: 
  - (Upstream) Teensy로부터 받은 열화상 데이터를 청크(Chunk) 단위로 분할하여 MQTT로 Publish.
  - (Downstream) MQTT로 수신된 모터 제어 명령을 SPI(Master)를 통해 STM32로 전달.

### 3. STM32F401RE (하드웨어 제어) - `STM32/SMT_SPI_Slave`
**STM32Cube HAL**을 기반으로 동작하며, 전달받은 명령에 따라 실제 물리적인 동작을 수행합니다.
- **SPI 슬레이브**: ESP32로부터 32바이트 고정 길이 패킷으로 명령 수신 및 상태 응답.
- **PCA9685 제어**: I2C를 통해 PWM 확장 드라이버를 제어하여 3개의 서보 모터 구동.
- **명령 해석**: 절대 각도(`set`), 상대 각도(`left`/`right`) 및 `press/release` 동작 지원.

---

## 통신 프로토콜 및 데이터 구조

### MQTT 제어 (Topic: `test/topic`)
- **형식**: `motor[1-3] [set/left/right/press/release] [value]`
- **예시**: `motor1 set 90`, `motor2 left press`

### SPI 패킷 (ESP32 ↔ STM32)
32바이트 고정 크기 패킷을 사용합니다.
- **Byte 0**: Command (0x01: Write, 0x02: Read)
- **Byte 1**: Payload Length
- **Byte 2~31**: Payload (명령 문자열 또는 상태 데이터)

### 열화상 데이터 (Topic: `lepton/frame/chunk`)
- 8바이트 헤더(`index`, `total`, `min`, `max`) + 프레임 청크 데이터로 구성되어 전송됩니다.

---

## PC 클라이언트 도구 (`esp32/src/mqtt/`)

시스템을 제어하고 데이터를 시각화하기 위한 Python 도구를 제공합니다.
- **`receive.py`**: MQTT로 수신된 청크 데이터를 재조립하여 160x120 열화상 영상을 실시간으로 디스플레이 (OpenCV 사용).
- **`motor_control_gui.py`**: GUI(Tkinter)를 통해 3개 모터의 방향과 동작을 원격 제어.

---

## 시작하기

### 하드웨어 설정
1. **Teensy 4.1**: Lepton 3.0 센서 연결 (SPI/I2C).
2. **연결**: Teensy(UART) ↔ ESP32 ↔ STM32(SPI).
3. **모터**: STM32(I2C) ↔ PCA9685 ↔ 서보 모터.

### 소프트웨어 빌드
1. **Teensy**: `west build`를 사용하여 Zephyr 프로젝트 빌드 및 업로드.
2. **ESP32**: `idf.py build`를 통해 빌드 (NVS에 WiFi/MQTT 설정 필요).
3. **STM32**: STM32CubeIDE를 사용하여 프로젝트 빌드 및 펌웨어 쓰기.
4. **PC**: `pip install paho-mqtt numpy opencv-python` 후 파이썬 스크립트 실행.

---

## 폴더 구조
- `esp32/`: ESP32-C3용 ESP-IDF 프로젝트 및 Python 도구.
- `STM32/`: STM32F401RE용 STM32CubeIDE 프로젝트.
- `teensy/TeensyC/`: Teensy 4.1용 Zephyr RTOS 프로젝트.
- `teensy/TeensyIno/`: (참고용) Teensy Arduino 스케치.
