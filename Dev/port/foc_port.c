#include "foc_port.h"

/**
 * @brief FOC 端口层运行状态。
 * @param config 当前绑定的 PWM 端口配置。
 * @param ready 端口层是否已完成初始化。
 * @param started PWM 是否已经启动。
 */
typedef struct
{
    FOC_Port_Config config;
    uint8_t ready;
    uint8_t started;
} FOC_Port_State;

static FOC_Port_State s_foc_port;

/**
 * @brief 将归一化占空比裁剪到 0 到 1。
 * @param duty 输入的归一化占空比。
 * @return 裁剪后的占空比。
 */
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

/**
 * @brief 清零三相比较寄存器。
 * @param None
 * @return None
 */
static void FOC_Port_ClearCompare(void)
{
    __HAL_TIM_SET_COMPARE(s_foc_port.config.timer, s_foc_port.config.channel_u, 0U);
    __HAL_TIM_SET_COMPARE(s_foc_port.config.timer, s_foc_port.config.channel_v, 0U);
    __HAL_TIM_SET_COMPARE(s_foc_port.config.timer, s_foc_port.config.channel_w, 0U);
}

/**
 * @brief 按当前自动重装载值把归一化占空比换算成比较值。
 * @param duty 输入的归一化占空比。
 * @return 计算得到的比较值。
 */
static uint32_t FOC_Port_DutyToCompare(float duty)
{
    const uint32_t arr = __HAL_TIM_GET_AUTORELOAD(s_foc_port.config.timer);
    const float clamped = FOC_Port_ClampDuty(duty);

    return (uint32_t)(clamped * (float)arr);
}

/**
 * @brief 启动一组正向与互补 PWM 通道。
 * @param channel PWM 通道号。
 * @retval APP_STATUS_OK 表示启动成功。
 * @retval APP_STATUS_HW_ERROR 表示底层启动失败。
 */
static APP_Status FOC_Port_StartChannelPair(uint32_t channel)
{
    if (HAL_TIM_PWM_Start(s_foc_port.config.timer, channel) != HAL_OK)
    {
        return APP_STATUS_HW_ERROR;
    }

    if (HAL_TIMEx_PWMN_Start(s_foc_port.config.timer, channel) != HAL_OK)
    {
        (void)HAL_TIM_PWM_Stop(s_foc_port.config.timer, channel);
        return APP_STATUS_HW_ERROR;
    }

    return APP_STATUS_OK;
}

/**
 * @brief 停止一组正向与互补 PWM 通道。
 * @param channel PWM 通道号。
 * @retval APP_STATUS_OK 表示停止成功。
 * @retval APP_STATUS_HW_ERROR 表示底层停止失败。
 */
static APP_Status FOC_Port_StopChannelPair(uint32_t channel)
{
    APP_Status status = APP_STATUS_OK;

    if (HAL_TIMEx_PWMN_Stop(s_foc_port.config.timer, channel) != HAL_OK)
    {
        status = APP_STATUS_HW_ERROR;
    }

    if (HAL_TIM_PWM_Stop(s_foc_port.config.timer, channel) != HAL_OK)
    {
        status = APP_STATUS_HW_ERROR;
    }

    return status;
}

APP_Status FOC_Port_Init(const FOC_Port_Config *config)
{
    if ((config == NULL) || (config->timer == NULL))
    {
        return APP_STATUS_INVALID_ARG;
    }

    s_foc_port.config = *config;
    s_foc_port.ready = APP_TRUE;
    s_foc_port.started = APP_FALSE;
    FOC_Port_ClearCompare();

    return APP_STATUS_OK;
}

APP_Status FOC_Port_StartPwm(void)
{
    APP_Status status;

    if (s_foc_port.ready == APP_FALSE)
    {
        return APP_STATUS_NOT_READY;
    }

    if (s_foc_port.started != APP_FALSE)
    {
        return APP_STATUS_OK;
    }

    status = FOC_Port_StartChannelPair(s_foc_port.config.channel_u);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = FOC_Port_StartChannelPair(s_foc_port.config.channel_v);
    if (status != APP_STATUS_OK)
    {
        (void)FOC_Port_StopChannelPair(s_foc_port.config.channel_u);
        return status;
    }

    status = FOC_Port_StartChannelPair(s_foc_port.config.channel_w);
    if (status != APP_STATUS_OK)
    {
        (void)FOC_Port_StopChannelPair(s_foc_port.config.channel_u);
        (void)FOC_Port_StopChannelPair(s_foc_port.config.channel_v);
        return status;
    }

    s_foc_port.started = APP_TRUE;
    return APP_STATUS_OK;
}

APP_Status FOC_Port_StopPwm(void)
{
    APP_Status status = APP_STATUS_OK;

    if (s_foc_port.ready == APP_FALSE)
    {
        return APP_STATUS_NOT_READY;
    }

    if (FOC_Port_StopChannelPair(s_foc_port.config.channel_u) != APP_STATUS_OK)
    {
        status = APP_STATUS_HW_ERROR;
    }
    if (FOC_Port_StopChannelPair(s_foc_port.config.channel_v) != APP_STATUS_OK)
    {
        status = APP_STATUS_HW_ERROR;
    }
    if (FOC_Port_StopChannelPair(s_foc_port.config.channel_w) != APP_STATUS_OK)
    {
        status = APP_STATUS_HW_ERROR;
    }

    FOC_Port_ClearCompare();
    s_foc_port.started = APP_FALSE;

    return status;
}

APP_Status FOC_Port_SetDuty(const FOC_Duty *duty)
{
    if (s_foc_port.ready == APP_FALSE)
    {
        return APP_STATUS_NOT_READY;
    }

    if (duty == NULL)
    {
        return APP_STATUS_INVALID_ARG;
    }

    __HAL_TIM_SET_COMPARE(s_foc_port.config.timer, s_foc_port.config.channel_u, FOC_Port_DutyToCompare(duty->phase_u));
    __HAL_TIM_SET_COMPARE(s_foc_port.config.timer, s_foc_port.config.channel_v, FOC_Port_DutyToCompare(duty->phase_v));
    __HAL_TIM_SET_COMPARE(s_foc_port.config.timer, s_foc_port.config.channel_w, FOC_Port_DutyToCompare(duty->phase_w));

    return APP_STATUS_OK;
}

APP_Status FOC_Port_OutputAlphaBeta(FOC_AlphaBeta v_ab)
{
    const FOC_Duty duty = FOC_AlphaBetaToDuty(v_ab);

    return FOC_Port_SetDuty(&duty);
}
