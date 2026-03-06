# Thermal Imaging & Remote Servo Control System

이 프로젝트는 FLIR Lepton 3.0 열화상 카메라 데이터를 수집하여 원격으로 전송하고, MQTT를 통해 서보 모터를 정밀하게 제어하는 임베디드 시스템입니다. Teensy 4.1, ESP32-C3, STM32F401RE 세 가지 마이크로컨트롤러가 각각 데이터 수집, 네트워크 브리지, 하드웨어 제어 역할을 수행합니다.

## 🏗 시스템 아키텍처

```text
[ FLIR Lepton 3.0 ] --(SPI/I2C)-- [ Teensy 4.1 ]
                                       |
                                     (UART 921,600bps)
                                       |
[ Remote MQTT Broker ] <--(WiFi/TLS)--> [ ESP32-C3 ]
                                       |
                                     (SPI Master/Slave)
                                       |
[ Servo Motors (x3) ] <--(PWM)-- [ PCA9685 ] <--(I2C)-- [ STM32F401RE ]
```

---

## 📱 주요 구성 요소 및 기능

### 1. Teensy 4.1 (열화상 데이터 획득)
열화상 센서로부터 데이터를 읽어 프레임을 구성하고 전송하는 역할을 합니다.
- **센서 인터페이스**: FLIR Lepton 3.0과 VoSPI(SPI) 및 CCI(I2C)로 통신.
- **프레임 동기화**: Lepton의 4개 세그먼트를 조합하여 160x120 해상도의 14비트 RAW 데이터를 생성.
- **고속 전송**: 획득한 프레임 데이터를 `FSTART` 헤더와 함께 UART(921,600 bps)로 ESP32에 전송.

### 2. ESP32-C3 (네트워크 및 통신 브리지)
중앙 허브 역할을 하며 외부 네트워크와 내부 하드웨어를 연결합니다.
- **WiFi & MQTT**: AWS IoT 등 MQTT 브로커에 TLS/SSL 보안 연결을 유지.
- **열화상 데이터 중계**: Teensy로부터 받은 대용량 프레임 데이터를 분할(Chunking)하여 MQTT 토픽으로 게시(Publish).
- **명령 수신 및 전달**: MQTT로 수신된 모터 제어 명령을 해석하여 SPI 통신을 통해 STM32로 전달.

### 3. STM32F401RE (하드웨어 제어)
전달받은 명령에 따라 실제 물리적인 동작을 수행합니다.
- **SPI 슬레이브**: ESP32(Master)로부터 32바이트 고정 길이 패킷으로 명령 수신.
- **PCA9685 제어**: I2C를 통해 PWM 확장 드라이버인 PCA9685를 제어하여 최대 3개의 서보 모터 구동.
- **정밀 제어**: 절대 각도(`set`), 상대 각도(`left`/`right`) 이동 및 부드러운 움직임(Smoothing) 지원.

---

## 🛠 통신 프로토콜

### MQTT 제어 명령 (Topic: `test/topic`)
STM32의 모터를 제어하기 위해 다음 형식의 문자열 메시지를 보냅니다.
- `motor[1-3] set [0-180]`: 해당 모터를 특정 각도로 이동. (예: `motor1 set 90`)
- `motor[1-3] left [deg]`: 현재 위치에서 왼쪽으로 특정 도만큼 이동.
- `motor[1-3] right [deg]`: 현재 위치에서 오른쪽으로 특정 도만큼 이동.

### SPI 패킷 구조 (ESP32 ↔ STM32)
32바이트 고정 크기 패킷을 사용합니다.
- **Byte 0**: Command (0x01: Write, 0x02: Read)
- **Byte 1**: Data Length
- **Byte 2~31**: Payload (명령 문자열 또는 데이터)

---

## 🚀 시작하기

### 하드웨어 연결
1. **Teensy ↔ ESP32**: UART 연결 (TX/RX 교차 연결).
2. **ESP32 ↔ STM32**: SPI 연결 (MOSI, MISO, SCK, CS).
3. **STM32 ↔ PCA9685**: I2C 연결 (SCL, SDA).

### 소프트웨어 설정
1. **Teensy**: `teensy/testflir/testflir.ino`를 Teensy 4.1에 업로드.
2. **ESP32**: `esp32` 폴더의 프로젝트를 ESP-IDF 또는 PlatformIO로 빌드 후 업로드 (WiFi SSID/PW 및 MQTT 인증서 설정 필요).
3. **STM32**: `STM32/SMT_SPI_Slave` 프로젝트를 STM32CubeIDE에서 빌드 후 업로드.
4. **Monitoring**: PC에서 `teensy/test.py`를 실행하여 실시간 열화상 영상을 확인 가능.

---

## 📁 폴더 구조
- `esp32/`: ESP32-C3 소스 코드 (FreeRTOS 기반 MQTT/SPI/UART).
- `STM32/`: STM32F401RE 소스 코드 (HAL 드라이버 기반 모터 제어).
- `teensy/`: Teensy 4.1 소스 코드 및 Python 모니터링 스크립트.
