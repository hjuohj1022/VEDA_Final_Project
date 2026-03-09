/* motor.c */
/**
 ******************************************************************************
 * @file    motor.c
 * @brief   PCA9685 I2C PWM 드라이버 + 3채널 서보 제어 + SPI 명령 파서
 *
 * 명령 형식 (SPI DATA 필드, 최대 30바이트):
 *   "motor1 left press"    → motor1을 왼쪽(CCW)으로 1도씩 계속 이동
 *   "motor2 right press"   → motor2를 오른쪽(CW)으로 1도씩 계속 이동
 *   "motor1 release"       → motor1 이동 중지
 *   "motor3 set 120"       → motor3를 절대 120°로 이동
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
static int8_t  s_moving[MOTOR_NUM] = {0, 0, 0};      /* -1: Left, 1: Right, 0: Stop */
static uint32_t s_last_tick[MOTOR_NUM] = {0, 0, 0};

#define SMOOTH_STEP_DEG   1   // 버튼 누를 때 1도씩 이동
#define MOVING_INTERVAL_MS 20 // 20ms 마다 1도씩 이동 (초당 50도)

/* ── PCA9685 저수준 함수 ───────────────────── */

static HAL_StatusTypeDef pca_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = val;
    return HAL_I2C_Master_Transmit(s_hi2c, (uint16_t)PCA9685_I2C_ADDR,
                                   buf, 2U, (uint32_t)PCA9685_I2C_TIMEOUT);
}

/**
 * @brief PCA9685 특정 채널 PWM 설정
 *        ON=0, OFF=tick (위상 고정 방식)
 */
static void pca_set_pwm(uint8_t ch, uint16_t tick)
{
    const uint8_t reg = (uint8_t)PCA9685_LED0_ON_L + (ch * 4U);
    uint8_t buf[5];
    buf[0] = reg;
    buf[1] = 0x00U;          /* ON_L  */
    buf[2] = 0x00U;          /* ON_H  */
    buf[3] = (uint8_t)(tick & 0xFFU);   /* OFF_L */
    buf[4] = (uint8_t)((tick >> 8U) & 0x1FU); /* OFF_H (12비트) */
    (void)HAL_I2C_Master_Transmit(s_hi2c, (uint16_t)PCA9685_I2C_ADDR,
                            buf, 5U, (uint32_t)PCA9685_I2C_TIMEOUT);
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
    uint16_t ret_tick;

    if (angle <= 0)   
    {
        ret_tick = PULSE_US_TO_TICK(s_cal_min[motor_id]);
    }
    else if (angle >= 180) 
    {
        ret_tick = PULSE_US_TO_TICK(s_cal_max[motor_id]);
    }
    else if (angle == 90)  
    {
        ret_tick = PULSE_US_TO_TICK(s_cal_center[motor_id]);
    }
    else
    {
        uint16_t us;
        if (angle < 90) 
        {
            us = (uint16_t)(s_cal_min[motor_id] +
                 (uint16_t)((uint32_t)(s_cal_center[motor_id] - s_cal_min[motor_id])
                 * (uint32_t)angle / 90U));
        } 
        else 
        {
            us = (uint16_t)(s_cal_center[motor_id] +
                 (uint16_t)((uint32_t)(s_cal_max[motor_id] - s_cal_center[motor_id])
                 * (uint32_t)(angle - 90) / 90U));
        }
        ret_tick = PULSE_US_TO_TICK(us);
    }
    return ret_tick;
}

static const char *skip_space(const char *p)
{
    const char *ptr = p;
    while ((*ptr != '\0') && (isspace((unsigned char)*ptr) != 0)) 
    {
        ptr++;
    }
    return ptr;
}

/* ── 공개 API ──────────────────────────────── */

int32_t Motor_Init(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;

    /* 소프트웨어 리셋 */
    if (pca_write(PCA9685_MODE1, 0x80U) != HAL_OK) 
    {
        return -1;
    }
    HAL_Delay(10U);

    /* sleep 모드 진입 후 prescale 설정 */
    if (pca_write(PCA9685_MODE1, 0x10U) != HAL_OK) 
    {
        return -1;
    }
    if (pca_write(PCA9685_PRESCALE, (uint8_t)PCA9685_PRESCALE_VAL) != HAL_OK) 
    {
        return -1;
    }

    /* 정상 동작 모드, Auto-Increment ON */
    if (pca_write(PCA9685_MODE1, 0x20U) != HAL_OK) 
    {
        return -1;
    }
    HAL_Delay(1U);

    /* 전체 서보 90° 중립 */
    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++) 
    {
        s_angle[i] = 90;
        s_moving[i] = 0;
        pca_set_pwm(i, PULSE_US_TO_TICK(s_cal_center[i]));
    }

    return 0;
}

void Motor_SetAngle(uint8_t motor_id, int16_t angle)
{
    int16_t target_angle = angle;
    if ((motor_id >= (uint8_t)MOTOR_NUM) || (NULL == s_hi2c)) 
    {
        return;
    }

    if (target_angle < (int16_t)MOTOR_ANGLE_MIN) 
    {
        target_angle = (int16_t)MOTOR_ANGLE_MIN;
    }
    if (target_angle > (int16_t)MOTOR_ANGLE_MAX) 
    {
        target_angle = (int16_t)MOTOR_ANGLE_MAX;
    }

    s_angle[motor_id] = target_angle;
    pca_set_pwm(motor_id, angle_to_tick(motor_id, s_angle[motor_id]));
}

void Motor_MoveRelative(uint8_t motor_id, int16_t delta)
{
    if (motor_id < (uint8_t)MOTOR_NUM) 
    {
        Motor_SetAngle(motor_id, (int16_t)(s_angle[motor_id] + delta));
    }
}

int16_t Motor_GetAngle(uint8_t motor_id)
{
    int16_t ret_angle = 0;
    if (motor_id < (uint8_t)MOTOR_NUM) 
    {
        ret_angle = s_angle[motor_id];
    }
    return ret_angle;
}

void Motor_StartMove(uint8_t motor_id, int8_t dir)
{
    if (motor_id < (uint8_t)MOTOR_NUM) 
    {
        s_moving[motor_id] = dir;
        s_last_tick[motor_id] = HAL_GetTick();
    }
}

void Motor_Stop(uint8_t motor_id)
{
    if (motor_id < (uint8_t)MOTOR_NUM) 
    {
        s_moving[motor_id] = 0;
    }
}

void Motor_Update(void)
{
    const uint32_t now = HAL_GetTick();
    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++) 
    {
        if (s_moving[i] != 0) 
        {
            if ((now - s_last_tick[i]) >= (uint32_t)MOVING_INTERVAL_MS) 
            {
                int16_t next_angle = (int16_t)(s_angle[i] + ((int16_t)s_moving[i] * (int16_t)SMOOTH_STEP_DEG));
                
                if (next_angle < (int16_t)MOTOR_ANGLE_MIN) 
                {
                    next_angle = (int16_t)MOTOR_ANGLE_MIN;
                }
                if (next_angle > (int16_t)MOTOR_ANGLE_MAX) 
                {
                    next_angle = (int16_t)MOTOR_ANGLE_MAX;
                }
                
                if (next_angle != s_angle[i]) 
                {
                    s_angle[i] = next_angle;
                    pca_set_pwm(i, angle_to_tick(i, s_angle[i]));
                }
                s_last_tick[i] = now;
            }
        }
    }
}

int32_t Motor_ParseAndRun(const char *data)
{
    int32_t ret = -1;
    if (NULL != data) 
    {
        int32_t motor_num = 0;
        char cmd1[16] = {0};
        char cmd2[16] = {0};

        /* "motor1 left press" -> n=3, "motor1 release" -> n=2 */
        const int32_t n = (int32_t)sscanf(data, " motor%d %15s %15s", &motor_num, cmd1, cmd2);

        if ((n >= 2) && (motor_num >= 1) && (motor_num <= (int32_t)MOTOR_NUM)) 
        {
            const uint8_t motor_id = (uint8_t)(motor_num - 1);

            /* 소문자로 변환하여 비교 */
            for (int32_t j = 0; cmd1[j] != '\0'; j++) 
            {
                cmd1[j] = (char)tolower((unsigned char)cmd1[j]);
            }
            for (int32_t j = 0; cmd2[j] != '\0'; j++) 
            {
                cmd2[j] = (char)tolower((unsigned char)cmd2[j]);
            }

            if (n == 2) 
            {
                /* "motor1 release" 또는 "motor1 stop" */
                if ((strcmp(cmd1, "release") == 0) || (strcmp(cmd1, "stop") == 0)) 
                {
                    Motor_Stop(motor_id);
                    ret = 0;
                }
            } 
            else if (n == 3) 
            {
                /* "motor1 left press" / "motor1 right press" */
                if (strcmp(cmd2, "press") == 0) 
                {
                    if (strcmp(cmd1, "left") == 0) 
                    {
                        Motor_StartMove(motor_id, -1);
                        ret = 0;
                    } 
                    else if (strcmp(cmd1, "right") == 0) 
                    {
                        Motor_StartMove(motor_id, 1);
                        ret = 0;
                    }
                    else
                    {
                        /* Unknown direction */
                    }
                }
                /* "motor1 left release" / "motor1 right release" */
                else if (strcmp(cmd2, "release") == 0) 
                {
                    Motor_Stop(motor_id);
                    ret = 0;
                }
                /* "motor1 set 120" (절대 각도) */
                else if (strcmp(cmd1, "set") == 0) 
                {
                    Motor_SetAngle(motor_id, (int16_t)atoi(cmd2));
                    ret = 0;
                }
                /* "motor1 left 10" (상대 각도 이동) */
                else if (strcmp(cmd1, "left") == 0) 
                {
                    Motor_MoveRelative(motor_id, (int16_t)(-(int16_t)atoi(cmd2)));
                    ret = 0;
                }
                else if (strcmp(cmd1, "right") == 0) 
                {
                    Motor_MoveRelative(motor_id, (int16_t)atoi(cmd2));
                    ret = 0;
                }
                else
                {
                    /* Unknown command */
                }
            }
            else
            {
                /* Should not reach here */
            }
        }
    }

    return ret;
}
