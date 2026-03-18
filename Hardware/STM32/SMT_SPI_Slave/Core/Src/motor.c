/* motor.c */
#include "motor.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static I2C_HandleTypeDef *s_hi2c = NULL;
static int16_t s_angle[MOTOR_NUM] = {90, 90, 90};
static int16_t s_target_angle[MOTOR_NUM] = {90, 90, 90};
static int8_t s_moving[MOTOR_NUM] = {0, 0, 0};
static uint32_t s_last_tick[MOTOR_NUM] = {0, 0, 0};

#define SMOOTH_STEP_DEG    1
#define MOVING_INTERVAL_MS 20
#define SET_STEP_DEG       1
#define SET_INTERVAL_MS    35

static HAL_StatusTypeDef pca_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2];

    buf[0] = reg;
    buf[1] = val;
    return HAL_I2C_Master_Transmit(s_hi2c,
                                   (uint16_t)PCA9685_I2C_ADDR,
                                   buf,
                                   2U,
                                   (uint32_t)PCA9685_I2C_TIMEOUT);
}

static void pca_set_pwm(uint8_t ch, uint16_t tick)
{
    const uint8_t reg = (uint8_t)PCA9685_LED0_ON_L + (ch * 4U);
    uint8_t buf[5];

    buf[0] = reg;
    buf[1] = 0x00U;
    buf[2] = 0x00U;
    buf[3] = (uint8_t)(tick & 0xFFU);
    buf[4] = (uint8_t)((tick >> 8U) & 0x1FU);
    (void)HAL_I2C_Master_Transmit(s_hi2c,
                                  (uint16_t)PCA9685_I2C_ADDR,
                                  buf,
                                  5U,
                                  (uint32_t)PCA9685_I2C_TIMEOUT);
}

static void pca_release_pwm(uint8_t ch)
{
    const uint8_t reg = (uint8_t)PCA9685_LED0_ON_L + (ch * 4U);
    uint8_t buf[5];

    buf[0] = reg;
    buf[1] = 0x00U;
    buf[2] = 0x00U;
    buf[3] = 0x00U;
    buf[4] = 0x10U;
    (void)HAL_I2C_Master_Transmit(s_hi2c,
                                  (uint16_t)PCA9685_I2C_ADDR,
                                  buf,
                                  5U,
                                  (uint32_t)PCA9685_I2C_TIMEOUT);
}

static const uint16_t s_cal_min[MOTOR_NUM] = CAL_MIN_US;
static const uint16_t s_cal_center[MOTOR_NUM] = CAL_CENTER_US;
static const uint16_t s_cal_max[MOTOR_NUM] = CAL_MAX_US;

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
                            (uint16_t)(((uint32_t)(s_cal_center[motor_id] - s_cal_min[motor_id]) *
                                        (uint32_t)angle) / 90U));
        }
        else
        {
            us = (uint16_t)(s_cal_center[motor_id] +
                            (uint16_t)(((uint32_t)(s_cal_max[motor_id] - s_cal_center[motor_id]) *
                                        (uint32_t)(angle - 90)) / 90U));
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

static bool parse_strict_int16(const char *text, int16_t min_val, int16_t max_val, int16_t *out_value)
{
    char *end_ptr = NULL;
    long parsed = 0;

    if ((text == NULL) || (out_value == NULL))
    {
        return false;
    }

    text = skip_space(text);
    if (*text == '\0')
    {
        return false;
    }

    parsed = strtol(text, &end_ptr, 10);
    if ((end_ptr == text) || (*skip_space(end_ptr) != '\0'))
    {
        return false;
    }

    if ((parsed < (long)min_val) || (parsed > (long)max_val) ||
        (parsed < INT16_MIN) || (parsed > INT16_MAX))
    {
        return false;
    }

    *out_value = (int16_t)parsed;
    return true;
}

int32_t Motor_Init(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;

    if (pca_write(PCA9685_MODE1, 0x80U) != HAL_OK)
    {
        return -1;
    }
    HAL_Delay(10U);

    if (pca_write(PCA9685_MODE1, 0x10U) != HAL_OK)
    {
        return -2;
    }
    if (pca_write(PCA9685_PRESCALE, (uint8_t)PCA9685_PRESCALE_VAL) != HAL_OK)
    {
        return -3;
    }

    if (pca_write(PCA9685_MODE1, 0x20U) != HAL_OK)
    {
        return -4;
    }
    HAL_Delay(1U);

    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++)
    {
        s_angle[i] = 90;
        s_target_angle[i] = 90;
        s_moving[i] = 0;
        pca_set_pwm(i, PULSE_US_TO_TICK(s_cal_center[i]));
    }

    return 0;
}

void Motor_SetAngle(uint8_t motor_id, int16_t angle)
{
    int16_t target_angle = angle;

    if ((motor_id >= (uint8_t)MOTOR_NUM) || (s_hi2c == NULL))
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

    s_target_angle[motor_id] = target_angle;
    s_moving[motor_id] = 0;
    s_last_tick[motor_id] = HAL_GetTick();
}

void Motor_MoveRelative(uint8_t motor_id, int16_t delta)
{
    if (motor_id < (uint8_t)MOTOR_NUM)
    {
        Motor_SetAngle(motor_id, (int16_t)(s_target_angle[motor_id] + delta));
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
        pca_set_pwm(motor_id, angle_to_tick(motor_id, s_angle[motor_id]));
        s_target_angle[motor_id] = s_angle[motor_id];
        s_moving[motor_id] = dir;
        s_last_tick[motor_id] = HAL_GetTick();
    }
}

void Motor_Stop(uint8_t motor_id)
{
    if (motor_id < (uint8_t)MOTOR_NUM)
    {
        s_moving[motor_id] = 0;
        s_target_angle[motor_id] = s_angle[motor_id];
        pca_set_pwm(motor_id, angle_to_tick(motor_id, s_angle[motor_id]));
    }
}

static void Motor_Release(uint8_t motor_id)
{
    if (motor_id < (uint8_t)MOTOR_NUM)
    {
        s_moving[motor_id] = 0;
        s_target_angle[motor_id] = s_angle[motor_id];
        pca_release_pwm(motor_id);
    }
}

void Motor_StopAll(void)
{
    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++)
    {
        s_moving[i] = 0;
        s_target_angle[i] = s_angle[i];
    }
}

void Motor_Update(void)
{
    const uint32_t now = HAL_GetTick();

    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++)
    {
        if ((s_moving[i] != 0) && ((now - s_last_tick[i]) >= (uint32_t)MOVING_INTERVAL_MS))
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
                s_target_angle[i] = next_angle;
                pca_set_pwm(i, angle_to_tick(i, s_angle[i]));
            }
            s_last_tick[i] = now;

            if (((s_moving[i] < 0) && (s_angle[i] <= (int16_t)MOTOR_ANGLE_MIN)) ||
                ((s_moving[i] > 0) && (s_angle[i] >= (int16_t)MOTOR_ANGLE_MAX)))
            {
                Motor_Release(i);
            }
        }
        else if ((s_moving[i] == 0) &&
                 (s_angle[i] != s_target_angle[i]) &&
                 ((now - s_last_tick[i]) >= (uint32_t)SET_INTERVAL_MS))
        {
            int16_t next_angle = s_angle[i];

            if (s_target_angle[i] > s_angle[i])
            {
                next_angle = (int16_t)(s_angle[i] + (int16_t)SET_STEP_DEG);
                if (next_angle > s_target_angle[i])
                {
                    next_angle = s_target_angle[i];
                }
            }
            else
            {
                next_angle = (int16_t)(s_angle[i] - (int16_t)SET_STEP_DEG);
                if (next_angle < s_target_angle[i])
                {
                    next_angle = s_target_angle[i];
                }
            }

            s_angle[i] = next_angle;
            pca_set_pwm(i, angle_to_tick(i, s_angle[i]));
            s_last_tick[i] = now;
        }
    }
}

int32_t Motor_ParseAndRun(const char *data)
{
    int32_t ret = -1;

    if (data != NULL)
    {
        int32_t motor_num = 0;
        int16_t value = 0;
        char cmd1[16] = {0};
        char cmd2[16] = {0};
        const int32_t n = (int32_t)sscanf(data, " motor%ld %15s %15s", &motor_num, cmd1, cmd2);

        if ((n >= 2) && (motor_num >= 1) && (motor_num <= (int32_t)MOTOR_NUM))
        {
            const uint8_t motor_id = (uint8_t)(motor_num - 1);

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
                if ((strcmp(cmd1, "release") == 0) || (strcmp(cmd1, "stop") == 0))
                {
                    if (strcmp(cmd1, "release") == 0)
                    {
                        Motor_Release(motor_id);
                    }
                    else
                    {
                        Motor_Stop(motor_id);
                    }
                    ret = 0;
                }
            }
            else if (n == 3)
            {
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
                }
                else if (strcmp(cmd2, "release") == 0)
                {
                    Motor_Release(motor_id);
                    ret = 0;
                }
                else if ((strcmp(cmd1, "set") == 0) &&
                         parse_strict_int16(cmd2,
                                            (int16_t)MOTOR_ANGLE_MIN,
                                            (int16_t)MOTOR_ANGLE_MAX,
                                            &value))
                {
                    Motor_SetAngle(motor_id, value);
                    ret = 0;
                }
                else if ((strcmp(cmd1, "left") == 0) &&
                         parse_strict_int16(cmd2, 0, (int16_t)MOTOR_ANGLE_MAX, &value))
                {
                    Motor_MoveRelative(motor_id, (int16_t)(-value));
                    ret = 0;
                }
                else if ((strcmp(cmd1, "right") == 0) &&
                         parse_strict_int16(cmd2, 0, (int16_t)MOTOR_ANGLE_MAX, &value))
                {
                    Motor_MoveRelative(motor_id, value);
                    ret = 0;
                }
            }
        }
    }

    return ret;
}
