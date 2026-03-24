/* motor.h */
#ifndef MOTOR_H
#define MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* ──────────────────────────────────────────────
 * PCA9685 설정
 *   I2C 주소  : 0x40 (A0~A5 모두 GND 기본값)
 *   PWM 주파수: 330 Hz (CLS6336HV 디지털 서보)
 *   PCA9685 내부 클럭: 25 MHz
 *   prescale  = round(25MHz / (4096 * 330Hz)) - 1 = 17
 *
 *   채널 할당:
 *     CH0 → motor1
 *     CH1 → motor2
 *     CH2 → motor3
 *
 *   펄스 범위 (4096 tick, 330Hz 기준):
 *     0°   : 500µs  → tick ≈  676
 *     90°  : 1520µs → tick ≈ 2054  (CLS6336HV center)
 *     180° : 2500µs → tick ≈ 3379
 * ─────────────────────────────────────────────*/

/* I2C */
#define PCA9685_I2C_ADDR      (0x40 << 1)  /* HAL은 7비트 주소를 1비트 시프트 */
#define PCA9685_I2C_TIMEOUT    10

/* PCA9685 레지스터 */
#define PCA9685_MODE1          0x00
#define PCA9685_PRESCALE       0xFE
#define PCA9685_LED0_ON_L      0x06        /* CH0 기준, CHn = 0x06 + n*4 */

/* 330Hz prescale 값: round(25MHz / (4096 * 330)) - 1 = 17 */
#define PCA9685_PRESCALE_VAL   17

/* µs → PCA9685 4096단계 tick 변환 */
#define PULSE_US_TO_TICK(us)   ((uint16_t)((uint32_t)(us) * 330 * 4096 / 1000000))

/* 서보 펄스 범위 (기본값) */
#define MOTOR_ANGLE_MIN        0
#define MOTOR_ANGLE_MAX        180
#define MOTOR_SPEED_MIN        1
#define MOTOR_SPEED_MAX        10
#define MOTOR_SPEED_DEFAULT    1

/* ── 모터별 캘리브레이션 값 (µs) ──────────────
 *   motor1: min=540  center=1530  max=2600
 *   motor2: min=600  center=1500  max=2500
 *   motor3: min=500  center=1490  max=2500
 * ─────────────────────────────────────────────*/
#define CAL_MIN_US    {540u, 600u, 500u}
#define CAL_CENTER_US {1530u, 1500u, 1490u}
#define CAL_MAX_US    {2600u, 2500u, 2500u}

/* 모터 ID (PCA9685 채널과 1:1 매핑) */
#define MOTOR_1    0
#define MOTOR_2    1
#define MOTOR_3    2
#define MOTOR_NUM  3

/* ──────────────────────────────────────────────
 * API
 * ─────────────────────────────────────────────*/

/**
 * @brief  PCA9685 초기화, 전체 서보 90° 중립
 * @param  hi2c  CubeMX가 생성한 I2C 핸들 포인터
 * @return 0=성공, -1=I2C 오류
 */
int32_t Motor_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief  특정 모터 절대 각도 이동
 * @param  motor_id  MOTOR_1 / MOTOR_2 / MOTOR_3
 * @param  angle     0 ~ 180 (°)
 */
void    Motor_SetAngle(uint8_t motor_id, int16_t angle);
void    Motor_SetAllAngles(const int16_t *angles);
void    Motor_SetSpeed(uint8_t motor_id, uint8_t speed);

/**
 * @brief  특정 모터 상대 이동
 * @param  motor_id  MOTOR_1 / MOTOR_2 / MOTOR_3
 * @param  delta     음수=왼쪽(CCW), 양수=오른쪽(CW)
 */
void    Motor_MoveRelative(uint8_t motor_id, int16_t delta);

/**
 * @brief  특정 모터 현재 각도 반환
 */
int16_t Motor_GetAngle(uint8_t motor_id);
uint8_t Motor_GetSpeed(uint8_t motor_id);

/**
 * @brief  모터 상태 업데이트 (메인 루프에서 주기적 호출)
 *         연속 이동 상태일 때 각도를 점진적으로 변경
 */
void    Motor_Update(void);

/**
 * @brief  모터 연속 이동 시작
 * @param  motor_id  MOTOR_1 / MOTOR_2 / MOTOR_3
 * @param  dir       -1: Left, 1: Right, 0: Stop
 */
void    Motor_StartMove(uint8_t motor_id, int8_t dir);

/**
 * @brief  모터 정지
 */
void    Motor_Stop(uint8_t motor_id);
void    Motor_StopAll(void);

/**
 * @brief  UART command 문자열 파싱 → 서보 제어
 *
 *  형식: "motor<N> left press"    → 왼쪽으로 계속 이동 시작
 *        "motor<N> right press"   → 오른쪽으로 계속 이동 시작
 *        "motor<N> release"       → 이동 정지
 *        "motor<N> set <deg>"     → 특정 각도로 이동
 *        "motor<N> speed <val>"   → 이동 속도 설정
 *        "setall <deg1> <deg2> <deg3>" → 3개 모터 목표각 동시 반영
 *
 * @return 0=성공, -1=파싱 실패
 */
int32_t Motor_ParseAndRun(const char *data);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_H */
