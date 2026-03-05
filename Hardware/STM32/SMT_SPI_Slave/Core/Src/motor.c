/* motor.c */
/**
 ******************************************************************************
 * @file    motor.c
 * @brief   PCA9685 I2C PWM 드라이버 + 3채널 서보 제어 + SPI 명령 파서
 *
 * 명령 형식 (SPI DATA 필드, 최대 30바이트):
 *   "motor1 left  90"   → motor1을 현재 각도에서 왼쪽(CCW) 90° 이동
 *   "motor2 right 45"   → motor2를 현재 각도에서 오른쪽(CW) 45° 이동
 *   "motor3 set   120"  → motor3를 절대 120°로 이동
 *
 * 하드웨어:
 *   PCA9685 ← I2C1 (PB6=SCL, PB7=SDA)
 *   CH0 → motor1 서보 신호선
 *   CH1 → motor2 서보 신호선
 *   CH2 → motor3 서보 신호선
 ******************************************************************************
 */

#include "motor.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ── 내부 상태 ─────────────────────────────── */
static I2C_HandleTypeDef *s_hi2c = NULL;
static int16_t s_angle[MOTOR_NUM] = {90, 90, 90};

#define SMOOTH_STEP_DEG   2   // 작을수록 부드러움 (권장: 1~5)
#define SMOOTH_STEP_MS    10  // 클수록 느림 (권장: 5~20)
/* ── PCA9685 저수준 함수 ───────────────────── */

static HAL_StatusTypeDef pca_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return HAL_I2C_Master_Transmit(s_hi2c, PCA9685_I2C_ADDR,
                                   buf, 2, PCA9685_I2C_TIMEOUT);
}

/**
 * @brief PCA9685 특정 채널 PWM 설정
 *        ON=0, OFF=tick (위상 고정 방식)
 */
static void pca_set_pwm(uint8_t ch, uint16_t tick)
{
    uint8_t reg = PCA9685_LED0_ON_L + ch * 4;
    uint8_t buf[5];
    buf[0] = reg;
    buf[1] = 0x00;          /* ON_L  */
    buf[2] = 0x00;          /* ON_H  */
    buf[3] = tick & 0xFF;   /* OFF_L */
    buf[4] = (tick >> 8) & 0x1F; /* OFF_H (12비트) */
    HAL_I2C_Master_Transmit(s_hi2c, PCA9685_I2C_ADDR,
                            buf, 5, PCA9685_I2C_TIMEOUT);
}

/* ── 캘리브레이션 테이블 ────────────────────── */
static const uint16_t s_cal_min[MOTOR_NUM]    = CAL_MIN_US;
static const uint16_t s_cal_center[MOTOR_NUM] = CAL_CENTER_US;
static const uint16_t s_cal_max[MOTOR_NUM]    = CAL_MAX_US;

/* ── 내부 유틸 ─────────────────────────────── */

/**
 * 캘리브레이션 기반 각도 → tick 변환
 *   0° ~ 90°  : min  → center 선형 보간
 *   90° ~ 180°: center → max 선형 보간
 */
static uint16_t angle_to_tick(uint8_t motor_id, int16_t angle)
{
    if (angle <= 0)   return PULSE_US_TO_TICK(s_cal_min[motor_id]);
    if (angle >= 180) return PULSE_US_TO_TICK(s_cal_max[motor_id]);
    if (angle == 90)  return PULSE_US_TO_TICK(s_cal_center[motor_id]);

    uint16_t us;
    if (angle < 90) {
        us = (uint16_t)(s_cal_min[motor_id] +
             (uint32_t)(s_cal_center[motor_id] - s_cal_min[motor_id])
             * (uint32_t)angle / 90);
    } else {
        us = (uint16_t)(s_cal_center[motor_id] +
             (uint32_t)(s_cal_max[motor_id] - s_cal_center[motor_id])
             * (uint32_t)(angle - 90) / 90);
    }
    return PULSE_US_TO_TICK(us);
}

static const char *skip_space(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* ── 공개 API ──────────────────────────────── */

int Motor_Init(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;

    /* 소프트웨어 리셋 */
    if (pca_write(PCA9685_MODE1, 0x80) != HAL_OK) return -1;
    HAL_Delay(10);

    /* sleep 모드 진입 후 prescale 설정 */
    if (pca_write(PCA9685_MODE1, 0x10) != HAL_OK) return -1;  /* SLEEP=1 */
    if (pca_write(PCA9685_PRESCALE, PCA9685_PRESCALE_VAL) != HAL_OK) return -1;

    /* 정상 동작 모드, Auto-Increment ON */
    if (pca_write(PCA9685_MODE1, 0x20) != HAL_OK) return -1;  /* AI=1 */
    HAL_Delay(1);

    /* 전체 서보 90° 중립 */
    for (uint8_t i = 0; i < MOTOR_NUM; i++) {
        s_angle[i] = 90;
        pca_set_pwm(i, PULSE_US_TO_TICK(s_cal_center[i]));
    }

    return 0;
}

void Motor_SetAngle(uint8_t motor_id, int16_t angle)
{
    if (motor_id >= MOTOR_NUM || !s_hi2c) return;

    if (angle < MOTOR_ANGLE_MIN) angle = MOTOR_ANGLE_MIN;
    if (angle > MOTOR_ANGLE_MAX) angle = MOTOR_ANGLE_MAX;

    int16_t current = s_angle[motor_id];

    /* 이미 목표 위치면 그냥 리턴 */
    if (current == angle) return;

    /* 현재 → 목표까지 스텝 단위로 이동 */
    while (current != angle) {
        if (current < angle) {
            current += SMOOTH_STEP_DEG;
            if (current > angle) current = angle;
        } else {
            current -= SMOOTH_STEP_DEG;
            if (current < angle) current = angle;
        }
        pca_set_pwm(motor_id, angle_to_tick(motor_id, current));
        HAL_Delay(SMOOTH_STEP_MS);
    }

    s_angle[motor_id] = angle;
}

void Motor_MoveRelative(uint8_t motor_id, int16_t delta)
{
    if (motor_id >= MOTOR_NUM) return;
    Motor_SetAngle(motor_id, s_angle[motor_id] + delta);
}

int16_t Motor_GetAngle(uint8_t motor_id)
{
    if (motor_id >= MOTOR_NUM) return 0;
    return s_angle[motor_id];
}

int Motor_ParseAndRun(const char *data)
{
    if (!data) return -1;

    const char *p = skip_space(data);

    /* "motor" 키워드 확인 */
    if (strncasecmp(p, "motor", 5) != 0) return -1;
    p += 5;

    /* 모터 번호 파싱: motor1 / motor2 / motor3 */
    if (*p < '1' || *p > '3') return -1;
    uint8_t motor_id = (uint8_t)(*p - '1');  /* 0,1,2 */
    p++;

    p = skip_space(p);

    /* 방향/모드 파싱 */
    char dir[8] = {0};
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < (int)(sizeof(dir) - 1))
        dir[i++] = (char)tolower((unsigned char)*p++);
    dir[i] = '\0';

    p = skip_space(p);

    /* 각도 파싱 */
    if (*p == '\0') return -1;
    char *endptr = NULL;
    long deg = strtol(p, &endptr, 10);
    if (endptr == p) return -1;

    /* 명령 실행 */
    if (strcmp(dir, "left") == 0) {
        Motor_MoveRelative(motor_id, -(int16_t)deg);
    } else if (strcmp(dir, "right") == 0) {
        Motor_MoveRelative(motor_id, (int16_t)deg);
    } else if (strcmp(dir, "set") == 0) {
        Motor_SetAngle(motor_id, (int16_t)deg);
    } else {
        return -1;
    }

    return 0;
}
