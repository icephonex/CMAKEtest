#include "foc_port.h"

extern TIM_HandleTypeDef htim8;

/* 记录 PWM 是否已经启动，避免重复启动导致不必要的 HAL 调用 */
static uint8_t s_foc_port_started;

/* 将归一化占空比裁剪到 0 到 1，避免越界写入比较寄存器 */
static float FOC_Port_ClampDuty(float duty)
{
    if (duty < 0.0f)
    {
        return 0.0f;
    }

    if (duty > 1.0f)
    {
        return 1.0f;
    }

    return duty;
}

/* 按当前自动重装载值把归一化占空比换算成 CCR 比较值 */
static uint32_t FOC_Port_DutyToCompare(float duty)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim8);
    float clamped = FOC_Port_ClampDuty(duty);

    return (uint32_t)(clamped * (float)arr);
}

void FOC_Port_Init(void)
{
    s_foc_port_started = 0U;

    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, 0U);
}

void FOC_Port_StartPwm(void)
{
    if (s_foc_port_started != 0U)
    {
        return;
    }

    if (HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_3) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_3) != HAL_OK)
    {
        Error_Handler();
    }

    s_foc_port_started = 1U;
}

void FOC_Port_StopPwm(void)
{
    (void)HAL_TIMEx_PWMN_Stop(&htim8, TIM_CHANNEL_1);
    (void)HAL_TIMEx_PWMN_Stop(&htim8, TIM_CHANNEL_2);
    (void)HAL_TIMEx_PWMN_Stop(&htim8, TIM_CHANNEL_3);
    (void)HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_1);
    (void)HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_2);
    (void)HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_3);

    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, 0U);

    s_foc_port_started = 0U;
}

void FOC_Port_SetDuty(const FOC_Duty *duty)
{
    if (duty == NULL)
    {
        return;
    }

    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, FOC_Port_DutyToCompare(duty->phase_u));
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, FOC_Port_DutyToCompare(duty->phase_v));
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, FOC_Port_DutyToCompare(duty->phase_w));
}

void FOC_Port_OutputAlphaBeta(FOC_AlphaBeta v_ab)
{
    FOC_Duty duty = FOC_AlphaBetaToDuty(v_ab);

    FOC_Port_SetDuty(&duty);
}
