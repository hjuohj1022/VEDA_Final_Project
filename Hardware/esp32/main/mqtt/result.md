# 열화상 전송 구조 변경 및 성능 개선 결과

## 1. 최종 목표

이번 작업의 목표는 다음 두 가지였다.

- 열화상 프레임은 MQTT 대신 UDP로 전송해서 프레임 확보량을 높일 것
- 모터 명령, 상태 보고, 제어 메시지는 기존처럼 MQTT/TLS를 유지할 것

최종적으로 현재 구조는 아래와 같이 정리되었다.

- 열화상 프레임: UDP
- 모터 명령: MQTT/TLS
- 상태 보고: MQTT/TLS
- 제어 메시지: MQTT/TLS

## 2. 현재 확정된 전송 구조

ESP32 기본 설정은 다음과 같다.

- `APP_FRAME_STREAM_MODE 3`
- `APP_UDP_FRAME_8BIT 1`
- UDP 대상 IP/포트는 `APP_UDP_TARGET_IP`, `APP_UDP_TARGET_PORT`로 설정

이 의미는 다음과 같다.

- 열화상 프레임 데이터는 UDP로만 전송
- MQTT 클라이언트는 계속 연결 유지
- `motor/control`, `system/status`, `system/control`은 MQTT로 처리

즉 프레임과 제어 채널이 분리되었다.

## 3. ESP32 쪽 주요 변경 사항

### 프레임 경로 분리

- 프레임 전송 모드에 `FRAME_STREAM_MODE_UDP_FRAME_MQTT_CONTROL`를 추가
- 이 모드에서는 프레임은 UDP만 사용
- MQTT는 명령/상태/제어 채널 용도로만 유지

### UDP 프레임 전송 최적화

- 8비트 fast UDP 전송 경로 유지
- 프레임 1장을 여러 UDP 청크로 분할 전송
- Wi-Fi 연결 상태를 확인한 뒤에만 UDP 전송
- UDP socket 오류 시 reset/re-init 처리 추가
- 짧은 양보 지점 추가로 task watchdog 문제 완화

### 수신 안정화 관련 변경

- frame buffer 수 조정
- stale frame 자동 폐기 정책을 UDP fast 경로에 맞게 완화
- health/status 로그에 frame 관련 통계 유지

### 화면 표시 관련 보정

- 8비트 UDP 수신 화면에서 추가 stretch를 적용하여 단색 화면 문제 완화
- raw 16비트 모드와 8비트 모드를 모두 확인 가능하게 유지

## 4. Teensy 쪽 주요 변경 사항

이번 성능 개선에서 가장 큰 병목은 Teensy 쪽 구조였다.

기존 구조:

- Lepton 캡처
- 프레임 전송
- 다시 캡처

이 구조에서는 전송 중 Lepton 연속 스트림을 놓치기 쉬워 프레임 확보량이 낮았다.

현재 구조:

- Lepton 캡처와 ESP32 전송을 분리
- 다중 frame buffer 사용
- ready/free queue 기반으로 캡처와 전송 경로 분리
- sender thread가 완성된 frame만 ESP32로 전송

추가로 다음 변경도 반영했다.

- 전송 후 과도한 고정 sleep 제거 또는 축소
- frame 단위 과도한 로그 출력 축소
- capture timeout 시 즉시 reset하지 않고 연속 실패 기준으로 reset

## 5. 실제 확인된 결과

초기 단계에서 확인된 상태:

- 약 `2 ~ 2.5 fps` 수준
- 프레임은 보이지만 전송량과 입력 구조 한계가 큼
- Teensy와 ESP32 사이 구조적 병목 존재

구조 변경 후 확인된 상태:

- 대략 `5 ~ 6 fps` 수준까지 향상
- 모터 명령은 계속 정상 수신
- 열화상 UDP 수신도 정상 동작
- MQTT/TLS와 UDP 병행 동작 확인

즉, 프레임 전송 채널을 UDP로 분리하고 Teensy 구조를 바꾼 것이 실제 성능 향상으로 이어졌다.

## 6. 현재 남아 있는 관찰 포인트

현재는 기본 동작은 성공했지만, 아래 항목은 계속 관찰할 필요가 있다.

- ESP32 로그의 `seq`
- ESP32 로그의 `bad_magic`
- ESP32 로그의 `bad_checksum`
- Teensy 쪽 `dropped_ready`
- 간헐적인 Lepton capture timeout

이 값들이 크게 증가하지 않으면 현재 구조는 실사용 가능한 수준으로 본다.

## 7. 서버 측 요구 사항

서버는 UDP 수신 준비가 되어 있어야 한다.

- ESP32가 설정된 IP와 포트로 UDP 전송
- 서버는 해당 IP/포트에서 bind 필요
- 방화벽에서 UDP 포트 허용 필요

동시에 MQTT/TLS 브로커도 살아 있어야 한다.

- 모터 명령
- 상태 보고
- 제어 메시지

관련 상세 내용은 `Server.md`에 정리하였다.

## 8. 결론

최종 결과는 다음과 같다.

- 열화상 프레임 전송은 UDP로 분리 완료
- 모터 명령과 상태/제어는 MQTT/TLS 유지 완료
- Teensy 구조 개선을 통해 실제 프레임 확보량이 약 `5 ~ 6 fps`까지 상승
- 현재 시스템은 원래 목표였던 "프레임은 UDP, 제어는 MQTT/TLS" 구조를 만족함

향후 추가 개선이 필요하다면 우선순위는 다음과 같다.

- Teensy SPI 전송 타이밍 추가 조정
- Lepton capture timeout 추가 안정화
- ESP32 `seq` 감소를 위한 세부 튜닝
