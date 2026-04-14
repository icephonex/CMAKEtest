#include "foc_openloop.h"

#include <math.h>

#include "cmsis_os.h"
#include "foc.h"
#include "foc_port.h"

#define FOC_OPENLOOP_TWO_PI             (6.2831853072f)
#define FOC_OPENLOOP_TASK_PERIOD_MS     (1U)
#define FOC_OPENLOOP_TASK_DT_S          (0.001f)
#define FOC_OPENLOOP_DEFAULT_FREQ_MHZ   (5000U)
#define FOC_OPENLOOP_DEFAULT_VD_PM      (0)
#define FOC_OPENLOOP_DEFAULT_VQ_PM      (100)
#define FOC_OPENLOOP_DEFAULT_ALPHA_PM   (0)
#define FOC_OPENLOOP_DEFAULT_BETA_PM    (0)
#define FOC_OPENLOOP_MAX_FREQ_MHZ       (200000U)
#define FOC_OPENLOOP_MAX_VOLTAGE_PM     (900)
#define FOC_OPENLOOP_MAX_VECTOR_PM      (1000)

typedef struct
{
    FOC_ControlMode mode;
    float theta;
    uint32_t frequency_millihz;
    int32_t vd_permille;
    int32_t vq_permille;
    int32_t alpha_permille;
    int32_t beta_permille;
    APP_Bool pwm_started;
} FOC_OpenLoop_State;

static volatile FOC_OpenLoop_State s_foc_openloop;

/**
 * @brief 检查整数参数是否落在指定区间内。
 * @param value 待检查的整数。
 * @param min_value 允许的最小值。
 * @param max_value 允许的最大值。
 * @retval APP_STATUS_OK 表示在允许范围内。
 * @retval APP_STATUS_RANGE 表示超出允许范围。
 */
static APP_Status FOC_OpenLoop_CheckRange(int32_t value, int32_t min_value, int32_t max_value)
{
    APP_Status status = APP_STATUS_OK;

    if ((value < min_value) || (value > max_value))
    {
        status = APP_STATUS_RANGE;
    }

    return status;
}

/**
 * @brief 将电角度回绕到 0 到 2*pi 区间。
 * @param theta 输入电角度。
 * @return 回绕后的电角度。
 */
static float FOC_OpenLoop_WrapTheta(float theta)
{
    while (theta >= FOC_OPENLOOP_TWO_PI)
    {
        theta -= FOC_OPENLOOP_TWO_PI;
    }
    while (theta < 0.0f)
    {
        theta += FOC_OPENLOOP_TWO_PI;
    }
    return theta;
}

/**
 * @brief 将千分比电压值换算为浮点量。
 * @param value_permille 输入的千分比值。
 * @return 对应的浮点值。
 */
static float FOC_OpenLoop_PermilleToFloat(int32_t value_permille)
{
    return ((float)value_permille / 1000.0f);
}

/**
 * @brief 将 mHz 频率换算为 Hz 浮点量。
 * @param frequency_millihz 输入的 mHz 频率。
 * @return 对应的 Hz 浮点量。
 */
static float FOC_OpenLoop_FrequencyToHz(uint32_t frequency_millihz)
{
    return ((float)frequency_millihz / 1000.0f);
}

/**
 * @brief 根据当前 theta 和 dq 请求生成旋转电压矢量并刷新 PWM。
 * @param None
 * @retval APP_STATUS_OK 表示更新成功。
 * @retval 其他状态码表示 PWM 输出失败。
 */
static APP_Status FOC_OpenLoop_UpdateOpenLoopOutput(void)
{
    FOC_DQ dq;
    FOC_AlphaBeta v_ab;
    const float theta = s_foc_openloop.theta;
    const float sin_theta = sinf(theta);
    const float cos_theta = cosf(theta);

    dq.d = FOC_OpenLoop_PermilleToFloat(s_foc_openloop.vd_permille);
    dq.q = FOC_OpenLoop_PermilleToFloat(s_foc_openloop.vq_permille);

    v_ab = FOC_InvPark(dq, sin_theta, cos_theta);
    return FOC_Port_OutputAlphaBeta(v_ab);
}

/**
 * @brief 根据当前静态 alpha-beta 请求刷新 PWM。
 * @param None
 * @retval APP_STATUS_OK 表示更新成功。
 * @retval 其他状态码表示 PWM 输出失败。
 */
static APP_Status FOC_OpenLoop_UpdateStaticVectorOutput(void)
{
    FOC_AlphaBeta v_ab;

    v_ab.alpha = FOC_OpenLoop_PermilleToFloat(s_foc_openloop.alpha_permille);
    v_ab.beta = FOC_OpenLoop_PermilleToFloat(s_foc_openloop.beta_permille);

    return FOC_Port_OutputAlphaBeta(v_ab);
}

/**
 * @brief 在需要输出时启动 PWM。
 * @param None
 * @retval APP_STATUS_OK 表示 PWM 已可用。
 * @retval 其他状态码表示启动失败。
 */
static APP_Status FOC_OpenLoop_StartPwmIfNeeded(void)
{
    APP_Status status = APP_STATUS_OK;

    if (s_foc_openloop.pwm_started == APP_FALSE)
    {
        status = FOC_Port_StartPwm();
        if (status == APP_STATUS_OK)
        {
            s_foc_openloop.pwm_started = APP_TRUE;
        }
    }

    return status;
}

/**
 * @brief 将当前输出切回安全停止状态。
 * @param None
 * @return None
 */
static void FOC_OpenLoop_EnterSafeStop(void)
{
    (void)FOC_Port_StopPwm();
    s_foc_openloop.mode = FOC_CONTROL_MODE_STOP;
    s_foc_openloop.theta = 0.0f;
    s_foc_openloop.pwm_started = APP_FALSE;
}

APP_Status FOC_OpenLoop_Init(void)
{
    s_foc_openloop.mode = FOC_CONTROL_MODE_STOP;
    s_foc_openloop.theta = 0.0f;
    s_foc_openloop.frequency_millihz = FOC_OPENLOOP_DEFAULT_FREQ_MHZ;
    s_foc_openloop.vd_permille = FOC_OPENLOOP_DEFAULT_VD_PM;
    s_foc_openloop.vq_permille = FOC_OPENLOOP_DEFAULT_VQ_PM;
    s_foc_openloop.alpha_permille = FOC_OPENLOOP_DEFAULT_ALPHA_PM;
    s_foc_openloop.beta_permille = FOC_OPENLOOP_DEFAULT_BETA_PM;
    s_foc_openloop.pwm_started = APP_FALSE;

    return APP_STATUS_OK;
}

void FOC_OpenLoop_Task(void const *argument)
{
    APP_Status status;

    (void)argument;

    for (;;)
    {
        if (s_foc_openloop.mode == FOC_CONTROL_MODE_OPENLOOP)
        {
            status = FOC_OpenLoop_StartPwmIfNeeded();
            if (status != APP_STATUS_OK)
            {
                FOC_OpenLoop_EnterSafeStop();
                osDelay(FOC_OPENLOOP_TASK_PERIOD_MS);
                continue;
            }

            s_foc_openloop.theta +=
                (FOC_OPENLOOP_TWO_PI * FOC_OpenLoop_FrequencyToHz(s_foc_openloop.frequency_millihz) *
                 FOC_OPENLOOP_TASK_DT_S);
            s_foc_openloop.theta = FOC_OpenLoop_WrapTheta(s_foc_openloop.theta);

            status = FOC_OpenLoop_UpdateOpenLoopOutput();
            if (status != APP_STATUS_OK)
            {
                FOC_OpenLoop_EnterSafeStop();
            }
        }
        else if (s_foc_openloop.mode == FOC_CONTROL_MODE_STATIC_VECTOR)
        {
            status = FOC_OpenLoop_StartPwmIfNeeded();
            if (status != APP_STATUS_OK)
            {
                FOC_OpenLoop_EnterSafeStop();
                osDelay(FOC_OPENLOOP_TASK_PERIOD_MS);
                continue;
            }

            status = FOC_OpenLoop_UpdateStaticVectorOutput();
            if (status != APP_STATUS_OK)
            {
                FOC_OpenLoop_EnterSafeStop();
            }
        }
        else if (s_foc_openloop.pwm_started != APP_FALSE)
        {
            FOC_OpenLoop_EnterSafeStop();
        }

        osDelay(FOC_OPENLOOP_TASK_PERIOD_MS);
    }
}

APP_Status FOC_OpenLoop_RequestStart(void)
{
    s_foc_openloop.mode = FOC_CONTROL_MODE_OPENLOOP;
    s_foc_openloop.theta = 0.0f;

    return APP_STATUS_OK;
}

APP_Status FOC_OpenLoop_RequestStop(void)
{
    s_foc_openloop.mode = FOC_CONTROL_MODE_STOP;

    return APP_STATUS_OK;
}

APP_Status FOC_OpenLoop_SetFrequencyMilliHz(uint32_t frequency_millihz)
{
    APP_Status status = APP_STATUS_OK;

    if (frequency_millihz > FOC_OPENLOOP_MAX_FREQ_MHZ)
    {
        status = APP_STATUS_RANGE;
    }
    else
    {
        s_foc_openloop.frequency_millihz = frequency_millihz;
    }

    return status;
}

APP_Status FOC_OpenLoop_SetVoltageDQPermille(int32_t vd_permille, int32_t vq_permille)
{
    APP_Status status;

    status = FOC_OpenLoop_CheckRange(vd_permille, -FOC_OPENLOOP_MAX_VOLTAGE_PM, FOC_OPENLOOP_MAX_VOLTAGE_PM);
    if (status == APP_STATUS_OK)
    {
        status = FOC_OpenLoop_CheckRange(vq_permille, -FOC_OPENLOOP_MAX_VOLTAGE_PM, FOC_OPENLOOP_MAX_VOLTAGE_PM);
        if (status == APP_STATUS_OK)
        {
            s_foc_openloop.vd_permille = vd_permille;
            s_foc_openloop.vq_permille = vq_permille;
        }
    }

    return status;
}

APP_Status FOC_OpenLoop_SetStaticVectorPermille(int32_t alpha_permille, int32_t beta_permille)
{
    APP_Status status;

    status = FOC_OpenLoop_CheckRange(alpha_permille, -FOC_OPENLOOP_MAX_VECTOR_PM, FOC_OPENLOOP_MAX_VECTOR_PM);
    if (status == APP_STATUS_OK)
    {
        status = FOC_OpenLoop_CheckRange(beta_permille, -FOC_OPENLOOP_MAX_VECTOR_PM, FOC_OPENLOOP_MAX_VECTOR_PM);
        if (status == APP_STATUS_OK)
        {
            s_foc_openloop.alpha_permille = alpha_permille;
            s_foc_openloop.beta_permille = beta_permille;
            s_foc_openloop.mode = FOC_CONTROL_MODE_STATIC_VECTOR;
        }
    }

    return status;
}

APP_Status FOC_OpenLoop_GetFrequencyMilliHz(uint32_t *frequency_millihz)
{
    APP_Status status = APP_STATUS_OK;

    if (frequency_millihz == NULL)
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else
    {
        *frequency_millihz = s_foc_openloop.frequency_millihz;
    }

    return status;
}

APP_Status FOC_OpenLoop_GetVoltageDQPermille(int32_t *vd_permille, int32_t *vq_permille)
{
    APP_Status status = APP_STATUS_OK;

    if ((vd_permille == NULL) || (vq_permille == NULL))
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else
    {
        *vd_permille = s_foc_openloop.vd_permille;
        *vq_permille = s_foc_openloop.vq_permille;
    }

    return status;
}

FOC_ControlMode FOC_OpenLoop_GetMode(void)
{
    return s_foc_openloop.mode;
}
