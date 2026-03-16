# Command Reference

이 문서는 현재 구현된 `server -> MQTT broker -> ESP32 -> UART -> STM32 -> PCA9685 -> servo motor` 제어 흐름을 기준으로 작성되었다.

## 1. 전체 흐름

명령 경로:

```text
motor_control_gui.py
-> MQTT publish to motor/control
-> ESP32 subscribe
-> ESP32 UART bridge
-> STM32 USART1 line command parser
-> PCA9685 servo control
```

응답 경로:

```text
STM32 UART response
-> ESP32 UART RX
-> line assembly
-> MQTT publish to motor/response
-> GUI / server subscribe
```

## 2. 통신 설정

### 2.1 MQTT

- Command topic: `motor/control`
- Response topic: `motor/response`
- Health topic: `system/status`
- Health request topic: `system/request`

Health request flow:

- `system/request`에 아무 payload나 publish
- ESP32가 즉시 현재 health JSON을 `system/status`로 publish

### 2.2 ESP32 <-> STM32 UART

- ESP32 TX: `GPIO21`
- ESP32 RX: `GPIO20`
- STM32 TX: `PA9` (`USART1_TX`)
- STM32 RX: `PA10` (`USART1_RX`)
- UART: `115200 8N1`
- 라인 종료: `\r`, `\n`, `\r\n`

배선:

- `ESP32 GPIO21 (TX)` -> `STM32 PA10 (RX)`
- `ESP32 GPIO20 (RX)` <- `STM32 PA9 (TX)`
- `GND` 공통

## 3. STM32 부팅 시 기본 응답

STM32 부팅 직후 UART로 아래와 같은 메시지가 나올 수 있다.

- `BOOT USART1 PA9/PA10`
- `I2C scan start`
- `I2C 0x40`
- `READY`

오류 예시:

- `I2C none`
- `ERR PCA9685 <code>`

## 4. 명령 형식

모든 명령은 ASCII 라인 문자열이다.

기본 형식:

```text
motor<N> <action> <arg>
```

예시:

- `motor1 left press`
- `motor2 release`
- `motor3 set 120`
- `motor1 right 10`

## 5. 명령 목록

### 5.1 연속 이동 시작

명령:

- `motor1 left press`
- `motor1 right press`
- `motor2 left press`
- `motor2 right press`
- `motor3 left press`
- `motor3 right press`

동작:

- `left press`: 해당 모터를 왼쪽 방향으로 연속 이동
- `right press`: 해당 모터를 오른쪽 방향으로 연속 이동

정상 응답:

- `OK <angle1> <angle2> <angle3>`

### 5.2 연속 이동 중지

명령:

- `motor1 release`
- `motor2 release`
- `motor3 release`
- `motor1 stop`
- `motor2 stop`
- `motor3 stop`

정상 응답:

- `OK <angle1> <angle2> <angle3>`

현재 의미:

- `release`: PWM output을 끄고 holding torque를 해제
- `stop`: 현재 위치에서 이동만 멈추고 PWM은 유지
- 연속 `press`가 `0도` 또는 `180도`에 도달하면 자동 `release`

### 5.3 절대 각도 이동

명령:

- `motor1 set 90`
- `motor2 set 0`
- `motor3 set 180`

설명:

- 해당 모터를 지정한 절대 각도로 이동
- 허용 범위는 `0 ~ 180`
- 숫자 형식이 잘못되면 `ERR`

정상 응답:

- `OK <angle1> <angle2> <angle3>`

### 5.4 상대 각도 이동

명령:

- `motor1 left 10`
- `motor1 right 10`
- `motor2 left 20`
- `motor3 right 5`

설명:

- 현재 각도를 기준으로 상대 이동
- `left N`: `-N`
- `right N`: `+N`
- 허용 범위 밖이거나 숫자가 아니면 `ERR`

정상 응답:

- `OK <angle1> <angle2> <angle3>`

### 5.5 각도 읽기

명령:

- `read`

응답:

- `ANGLES <angle1> <angle2> <angle3>`

예시:

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

- 모든 모터의 연속 이동 상태를 중단
- 현재 출력 상태는 각 모터의 직전 상태에 따름

### 5.8 도움말

명령:

- `help`

응답 예시:

- `CMD motor<N> left press`
- `CMD motor<N> right press`
- `CMD motor<N> release`
- `CMD motor<N> set <deg>`
- `CMD read | ping | stopall`

## 6. 오류 응답

다음과 같은 경우 STM32가 오류 응답을 반환한다.

- 잘못된 명령 형식
- 지원하지 않는 motor 번호
- 숫자 파싱 실패
- UART 수신 버퍼 overflow

응답:

- `ERR`
- `ERR overflow`

## 7. GUI 기준 실제 사용 명령

`motor_control_gui.py` 기준으로 버튼 동작은 대략 다음과 같다.

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

정상이라면 다음 흐름이 보인다.

- `Topic: motor/control`
- `Data: motor1 left press`
- `Queued to STM32 UART: 'motor1 left press'`
- `STM32 UART TX: 'motor1 left press'`
- `STM32 UART RX: OK 91 90 90`

### 8.2 GUI / 서버

정상 연결:

- `MQTT connected`

명령 전송:

- `TX: motor1 left press rc=0`

응답 수신:

- `motor/response: OK 91 90 90`
- `motor/response: ANGLES 90 90 90`

## 9. Health Request 예시

요청:

```text
topic: system/request
payload: now
```

응답:

```text
topic: system/status
payload: {"uptime_sec":123, ...}
```

## 10. 주의 사항

- STM32 UART는 현재 `USART1 (PA9/PA10)` 기준이다.
- 예전 `PA2/PA3` 기준 문서나 배선은 더 이상 사용하지 않는다.
- STM32가 부팅해도 ESP32 로그에 `STM32 UART RX: READY`가 보이지 않으면 UART 배선을 먼저 확인해야 한다.
- `motor/control`과 `lepton/frame/chunk`는 같은 ESP32 자원을 사용하므로, 열화상 전송 부하가 높으면 명령 응답이 늦어질 수 있다.
