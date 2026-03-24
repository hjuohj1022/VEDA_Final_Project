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
static uint8_t s_speed_deg[MOTOR_NUM] = {
    MOTOR_SPEED_DEFAULT, MOTOR_SPEED_DEFAULT, MOTOR_SPEED_DEFAULT};
typedef struct
{
    uint8_t mask;
    int16_t angles[MOTOR_NUM];
} MotorSetCommand;

static MotorSetCommand s_set_queue[8];
static uint8_t s_set_queue_head = 0U;
static uint8_t s_set_queue_tail = 0U;
static uint8_t s_set_queue_count = 0U;
static bool s_set_queue_active = false;
static uint8_t s_set_active_mask = 0U;
static uint32_t s_last_tick[MOTOR_NUM] = {0, 0, 0};

#define MOTOR_STEP_INTERVAL_MS 20

static int16_t clamp_angle(int16_t angle)
{
    int16_t ret_angle = angle;

    if (ret_angle < (int16_t)MOTOR_ANGLE_MIN)
    {
        ret_angle = (int16_t)MOTOR_ANGLE_MIN;
    }
    if (ret_angle > (int16_t)MOTOR_ANGLE_MAX)
    {
        ret_angle = (int16_t)MOTOR_ANGLE_MAX;
    }

    return ret_angle;
}

static void motor_clear_set_queue(void)
{
    s_set_queue_head = 0U;
    s_set_queue_tail = 0U;
    s_set_queue_count = 0U;
    s_set_queue_active = false;
    s_set_active_mask = 0U;
}

static void motor_apply_target_angles(const int16_t *angles, uint8_t mask, uint32_t now)
{
    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++)
    {
        if ((mask & (uint8_t)(1U << i)) != 0U)
        {
            s_target_angle[i] = clamp_angle(angles[i]);
            s_moving[i] = 0;
            s_last_tick[i] = now;
        }
    }
}

static bool motor_active_set_complete(void)
{
    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++)
    {
        if (((s_set_active_mask & (uint8_t)(1U << i)) != 0U) &&
            ((s_moving[i] != 0) || (s_angle[i] != s_target_angle[i])))
        {
            return false;
        }
    }

    return true;
}

static void motor_start_next_queued_set(uint32_t now)
{
    const MotorSetCommand *next_cmd;

    if ((s_set_queue_count == 0U) || s_set_queue_active)
    {
        return;
    }

    next_cmd = &s_set_queue[s_set_queue_head];
    motor_apply_target_angles(next_cmd->angles, next_cmd->mask, now);

    s_set_queue_head++;
    if (s_set_queue_head >= (uint8_t)(sizeof(s_set_queue) / sizeof(s_set_queue[0])))
    {
        s_set_queue_head = 0U;
    }
    s_set_queue_count--;
    s_set_queue_active = true;
    s_set_active_mask = next_cmd->mask;
}

static bool motor_enqueue_target_angles(const int16_t *angles, uint8_t mask)
{
    if ((angles == NULL) || (s_hi2c == NULL))
    {
        return false;
    }

    if (s_set_queue_count >= (uint8_t)(sizeof(s_set_queue) / sizeof(s_set_queue[0])))
    {
        return false;
    }

    s_set_queue[s_set_queue_tail].mask = mask;
    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++)
    {
        s_set_queue[s_set_queue_tail].angles[i] = clamp_angle(angles[i]);
    }
    s_set_queue_tail++;
    if (s_set_queue_tail >= (uint8_t)(sizeof(s_set_queue) / sizeof(s_set_queue[0])))
    {
        s_set_queue_tail = 0U;
    }
    s_set_queue_count++;

    return true;
}

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

static bool parse_setall_command(const char *text, int16_t *angles)
{
    const char *ptr = skip_space(text);

    if ((ptr == NULL) || (angles == NULL))
    {
        return false;
    }

    if (strncmp(ptr, "setall", 6U) != 0)
    {
        return false;
    }

    ptr = skip_space(ptr + 6);
    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++)
    {
        char *end_ptr = NULL;
        long parsed = strtol(ptr, &end_ptr, 10);

        if ((end_ptr == ptr) ||
            (parsed < (long)MOTOR_ANGLE_MIN) ||
            (parsed > (long)MOTOR_ANGLE_MAX))
        {
            return false;
        }

        angles[i] = (int16_t)parsed;
        ptr = skip_space(end_ptr);
    }

    return (*ptr == '\0');
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
        s_speed_deg[i] = MOTOR_SPEED_DEFAULT;
        pca_set_pwm(i, PULSE_US_TO_TICK(s_cal_center[i]));
    }

    return 0;
}

void Motor_SetAngle(uint8_t motor_id, int16_t angle)
{
    if ((motor_id >= (uint8_t)MOTOR_NUM) || (s_hi2c == NULL))
    {
        return;
    }

    motor_clear_set_queue();
    s_target_angle[motor_id] = clamp_angle(angle);
    s_moving[motor_id] = 0;
    s_last_tick[motor_id] = HAL_GetTick();
}

void Motor_SetAllAngles(const int16_t *angles)
{
    if ((angles == NULL) || (s_hi2c == NULL))
    {
        return;
    }

    motor_clear_set_queue();
    motor_apply_target_angles(angles, (uint8_t)((1U << MOTOR_NUM) - 1U), HAL_GetTick());
}

void Motor_SetSpeed(uint8_t motor_id, uint8_t speed)
{
    if (motor_id >= (uint8_t)MOTOR_NUM)
    {
        return;
    }

    if (speed < (uint8_t)MOTOR_SPEED_MIN)
    {
        speed = (uint8_t)MOTOR_SPEED_MIN;
    }
    if (speed > (uint8_t)MOTOR_SPEED_MAX)
    {
        speed = (uint8_t)MOTOR_SPEED_MAX;
    }

    s_speed_deg[motor_id] = speed;
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

uint8_t Motor_GetSpeed(uint8_t motor_id)
{
    uint8_t ret_speed = 0U;

    if (motor_id < (uint8_t)MOTOR_NUM)
    {
        ret_speed = s_speed_deg[motor_id];
    }

    return ret_speed;
}

void Motor_StartMove(uint8_t motor_id, int8_t dir)
{
    if (motor_id < (uint8_t)MOTOR_NUM)
    {
        motor_clear_set_queue();
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
        motor_clear_set_queue();
        s_moving[motor_id] = 0;
        s_target_angle[motor_id] = s_angle[motor_id];
        pca_set_pwm(motor_id, angle_to_tick(motor_id, s_angle[motor_id]));
    }
}

static void Motor_Release(uint8_t motor_id)
{
    if (motor_id < (uint8_t)MOTOR_NUM)
    {
        motor_clear_set_queue();
        s_moving[motor_id] = 0;
        s_target_angle[motor_id] = s_angle[motor_id];
        pca_release_pwm(motor_id);
    }
}

void Motor_StopAll(void)
{
    motor_clear_set_queue();

    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++)
    {
        s_moving[i] = 0;
        s_target_angle[i] = s_angle[i];
    }
}

void Motor_Update(void)
{
    uint32_t now = HAL_GetTick();

    if (!s_set_queue_active)
    {
        motor_start_next_queued_set(now);
    }

    for (uint8_t i = 0U; i < (uint8_t)MOTOR_NUM; i++)
    {
        if ((s_moving[i] != 0) && ((now - s_last_tick[i]) >= (uint32_t)MOTOR_STEP_INTERVAL_MS))
        {
            const int16_t step_deg = (int16_t)s_speed_deg[i];
            int16_t next_angle = (int16_t)(s_angle[i] + ((int16_t)s_moving[i] * step_deg));

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
                 ((now - s_last_tick[i]) >= (uint32_t)MOTOR_STEP_INTERVAL_MS))
        {
            int16_t next_angle = s_angle[i];

            if (s_target_angle[i] > s_angle[i])
            {
                next_angle = (int16_t)(s_angle[i] + (int16_t)s_speed_deg[i]);
                if (next_angle > s_target_angle[i])
                {
                    next_angle = s_target_angle[i];
                }
            }
            else
            {
                next_angle = (int16_t)(s_angle[i] - (int16_t)s_speed_deg[i]);
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

    if (s_set_queue_active && motor_active_set_complete())
    {
        s_set_queue_active = false;
        s_set_active_mask = 0U;
        if (s_set_queue_count > 0U)
        {
            now = HAL_GetTick();
            motor_start_next_queued_set(now);
        }
    }
}

int32_t Motor_ParseAndRun(const char *data)
{
    int32_t ret = -1;

    if (data != NULL)
    {
        int16_t sync_angles[MOTOR_NUM] = {0};

        if (parse_setall_command(data, sync_angles))
        {
            ret = motor_enqueue_target_angles(sync_angles, (uint8_t)((1U << MOTOR_NUM) - 1U)) ? 0 : -1;
        }
        else
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
                        int16_t queued_angles[MOTOR_NUM] = {0};
                        queued_angles[motor_id] = value;
                        ret = motor_enqueue_target_angles(queued_angles, (uint8_t)(1U << motor_id)) ? 0 : -1;
                    }
                    else if ((strcmp(cmd1, "speed") == 0) &&
                             parse_strict_int16(cmd2,
                                                (int16_t)MOTOR_SPEED_MIN,
                                                (int16_t)MOTOR_SPEED_MAX,
                                                &value))
                    {
                        Motor_SetSpeed(motor_id, (uint8_t)value);
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
    }

    return ret;
}
