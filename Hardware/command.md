# Command Reference

이 문서는 현재 구현된 `server -> MQTT broker -> ESP32 -> UART -> STM32 -> PCA9685 -> servo motor` 명령 흐름을 기준으로 작성했다.

## 1. 전체 흐름

명령 경로:

`motor_control_gui.py`  
-> MQTT publish to `test/topic`  
-> ESP32 subscribe  
-> ESP32 UART1 bridge  
-> STM32 `USART1 (PA9/PA10)` line command parser  
-> PCA9685 servo control

응답 경로:

STM32 UART 응답  
-> ESP32 UART RX  
-> ESP32 MQTT publish to `stm32/resp`  
-> `motor_control_gui.py` subscribe / 표시

## 2. 통신 설정

### 2.1 MQTT

- Command topic: `test/topic`
- Response topic: `stm32/resp`

### 2.2 ESP32 <-> STM32 UART

- ESP32 TX: `GPIO21`
- ESP32 RX: `GPIO20`
- STM32 TX: `PA9` (`USART1_TX`)
- STM32 RX: `PA10` (`USART1_RX`)
- UART: `115200 8N1`
- 라인 종료: `\r`, `\n`, 또는 `\r\n`

배선:

- `ESP32 GPIO21 (TX)` -> `STM32 PA10 (RX)`
- `ESP32 GPIO20 (RX)` -> `STM32 PA9 (TX)`
- `GND` 공통

## 3. STM32 부팅 및 기본 응답

STM32 부팅 시 UART로 아래 문자열이 출력될 수 있다.

- `BOOT USART1 PA9/PA10`
- `I2C scan start`
- `I2C 0x40`
- `READY`

오류 시:

- `I2C none`
- `ERR PCA9685 <code>`

## 4. 지원 명령

모든 명령은 ASCII 문자열 한 줄이다.

기본 형식:

`motor<N> <action> <arg>`

예:

- `motor1 left press`
- `motor2 release`
- `motor3 set 120`
- `motor1 right 10`

## 5. 명령 목록

### 5.1 연속 이동 시작

버튼을 누르고 있는 동안 1도씩 주기적으로 이동시키는 명령이다.

지원 형식:

- `motor1 left press`
- `motor1 right press`
- `motor2 left press`
- `motor2 right press`
- `motor3 left press`
- `motor3 right press`

동작:

- `left press`: 해당 모터를 음의 방향으로 연속 이동
- `right press`: 해당 모터를 양의 방향으로 연속 이동

정상 응답:

- `OK <angle1> <angle2> <angle3>`

### 5.2 연속 이동 중지

지원 형식:

- `motor1 release`
- `motor2 release`
- `motor3 release`

또는:

- `motor1 stop`
- `motor2 stop`
- `motor3 stop`

정상 응답:

- `OK <angle1> <angle2> <angle3>`

### 5.3 절대 각도 이동

지원 형식:

- `motor1 set 90`
- `motor2 set 0`
- `motor3 set 180`

설명:

- 해당 모터를 지정한 절대 각도로 이동
- 내부적으로 0~180 범위로 제한됨

정상 응답:

- `OK <angle1> <angle2> <angle3>`

### 5.4 상대 각도 이동

지원 형식:

- `motor1 left 10`
- `motor1 right 10`
- `motor2 left 20`
- `motor3 right 5`

설명:

- 현재 각도 기준으로 상대 이동
- `left N`: `-N`도
- `right N`: `+N`도

정상 응답:

- `OK <angle1> <angle2> <angle3>`

### 5.5 각도 읽기

명령:

- `read`

응답:

- `ANGLES <angle1> <angle2> <angle3>`

예:

- `ANGLES 90 120 60`

### 5.6 연결 확인

명령:

- `ping`

응답:

- `PONG`

### 5.7 전체 정지

명령:

- `stopall`

응답:

- `OK <angle1> <angle2> <angle3>`

설명:

- 모든 모터의 연속 이동 상태를 즉시 정지
- 현재 각도는 유지

### 5.8 도움말

명령:

- `help`

응답:

- `CMD motor<N> left press`
- `CMD motor<N> right press`
- `CMD motor<N> release`
- `CMD motor<N> set <deg>`
- `CMD read | ping | stopall`

## 6. 에러 응답

아래와 같은 경우 STM32는 오류 응답을 보낸다.

- 잘못된 형식
- 지원하지 않는 motor 번호
- 숫자 파싱 실패
- 버퍼 오버플로우

응답:

- `ERR`
- `ERR overflow`

## 7. GUI 기준 실제 사용 명령

`motor_control_gui.py` 기준 버튼 동작은 아래와 같다.

- `Hold L`: `motor<N> left press`
- `Hold R`: `motor<N> right press`
- 버튼 release: `motor<N> release`
- `-10`: `motor<N> left 10`
- `+10`: `motor<N> right 10`
- `Stop`: `motor<N> release`
- `Center All`: `motor1 set 90`, `motor2 set 90`, `motor3 set 90`
- `Read Angles`: `read`
- `Stop All`: `stopall`

## 8. 응답 확인 방법

### 8.1 ESP32 로그

정상 명령 전달 시 예:

- `Topic: test/topic`
- `Data: motor1 left press`
- `Queued to STM32 UART: 'motor1 left press'`
- `STM32 UART TX: 'motor1 left press'`
- `STM32 UART RX: OK 91 90 90`

### 8.2 GUI 상태바

정상 연결:

- `MQTT connected`

명령 전송:

- `TX: motor1 left press rc=0`

응답 수신:

- `stm32/resp: OK 91 90 90`
- `stm32/resp: ANGLES 90 90 90`

## 9. 주의 사항

- STM32 제어용 UART는 현재 `USART1 (PA9/PA10)` 기준이다.
- 기존 `PA2/PA3` 기준 문서나 배선은 더 이상 사용하지 않는다.
- STM32가 부팅해도 ESP32 로그에 `STM32 UART RX: READY`가 보이지 않으면 UART 배선부터 확인해야 한다.
- `test/topic`과 `lepton/frame/chunk`는 토픽 자체로는 충돌하지 않지만, 둘 다 같은 ESP32 리소스를 사용하므로 프레임 전송 중 명령 응답이 늦어질 수 있다.
