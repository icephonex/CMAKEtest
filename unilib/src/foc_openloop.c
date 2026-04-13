#include "foc_openloop.h"

#include <math.h>

#include "cmsis_os.h"
#include "foc.h"
#include "foc_port.h"

#define FOC_OPENLOOP_TWO_PI             (6.2831853072f)
#define FOC_OPENLOOP_TASK_PERIOD_MS     (1U)
#define FOC_OPENLOOP_TASK_DT_S          (0.001f)
#define FOC_OPENLOOP_DEFAULT_FREQ_HZ    (5.0f)
#define FOC_OPENLOOP_DEFAULT_VD         (0.0f)
#define FOC_OPENLOOP_DEFAULT_VQ         (0.10f)
#define FOC_OPENLOOP_MAX_FREQ_HZ        (200.0f)
#define FOC_OPENLOOP_MAX_VOLTAGE        (0.90f)

typedef struct
{
    float theta;
    float frequency_hz;
    float vd;
    float vq;
    uint8_t running;
    uint8_t pwm_started;
} FOC_OpenLoop_State;

static FOC_OpenLoop_State s_foc_openloop;

/**
 * @brief 将浮点量限制在指定范围，避免异常参数直接进入开环任务。
 * @param value 输入值。
 * @param min_value 最小允许值。
 * @param max_value 最大允许值。
 * @return 限幅后的结果。
 */
static float FOC_OpenLoop_Clamp(float value, float min_value, float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

/**
 * @brief 将电角度回绕到 0 到 2*pi，避免长时间运行后持续增大。
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
 * @brief 根据当前 theta 和 dq 请求生成旋转电压矢量并刷新 PWM。
 * @param None
 * @return None
 */
static void FOC_OpenLoop_UpdateOutput(void)
{
    FOC_DQ dq;
    FOC_AlphaBeta v_ab;
    float sin_theta;
    float cos_theta;

    sin_theta = sinf(s_foc_openloop.theta);
    cos_theta = cosf(s_foc_openloop.theta);

    dq.d = s_foc_openloop.vd;
    dq.q = s_foc_openloop.vq;

    v_ab = FOC_InvPark(dq, sin_theta, cos_theta);
    FOC_Port_OutputAlphaBeta(v_ab);
}

/**
 * @brief 在首次进入运行态时先下发初值，再启动 PWM。
 * @param None
 * @return None
 */
static void FOC_OpenLoop_StartPwmIfNeeded(void)
{
    if (s_foc_openloop.pwm_started != 0U)
    {
        return;
    }

    FOC_OpenLoop_UpdateOutput();
    FOC_Port_StartPwm();
    s_foc_openloop.pwm_started = 1U;
}

void FOC_OpenLoop_Init(void)
{
    s_foc_openloop.theta = 0.0f;
    s_foc_openloop.frequency_hz = FOC_OPENLOOP_DEFAULT_FREQ_HZ;
    s_foc_openloop.vd = FOC_OPENLOOP_DEFAULT_VD;
    s_foc_openloop.vq = FOC_OPENLOOP_DEFAULT_VQ;
    s_foc_openloop.running = 0U;
    s_foc_openloop.pwm_started = 0U;
}

void FOC_OpenLoop_Task(void const *argument)
{
    (void)argument;

    for (;;)
    {
        if (s_foc_openloop.running != 0U)
        {
            FOC_OpenLoop_StartPwmIfNeeded();

            s_foc_openloop.theta +=
                (FOC_OPENLOOP_TWO_PI * s_foc_openloop.frequency_hz * FOC_OPENLOOP_TASK_DT_S);
            s_foc_openloop.theta = FOC_OpenLoop_WrapTheta(s_foc_openloop.theta);
            FOC_OpenLoop_UpdateOutput();
        }
        else if (s_foc_openloop.pwm_started != 0U)
        {
            FOC_Port_StopPwm();
            s_foc_openloop.pwm_started = 0U;
            s_foc_openloop.theta = 0.0f;
        }

        osDelay(FOC_OPENLOOP_TASK_PERIOD_MS);
    }
}

void FOC_OpenLoop_RequestStart(void)
{
    s_foc_openloop.running = 1U;
}

void FOC_OpenLoop_RequestStop(void)
{
    s_foc_openloop.running = 0U;
}

void FOC_OpenLoop_SetFrequencyHz(float frequency_hz)
{
    s_foc_openloop.frequency_hz = FOC_OpenLoop_Clamp(frequency_hz, 0.0f, FOC_OPENLOOP_MAX_FREQ_HZ);
}

void FOC_OpenLoop_SetVoltageDQ(float vd, float vq)
{
    s_foc_openloop.vd = FOC_OpenLoop_Clamp(vd, -FOC_OPENLOOP_MAX_VOLTAGE, FOC_OPENLOOP_MAX_VOLTAGE);
    s_foc_openloop.vq = FOC_OpenLoop_Clamp(vq, -FOC_OPENLOOP_MAX_VOLTAGE, FOC_OPENLOOP_MAX_VOLTAGE);
}

float FOC_OpenLoop_GetFrequencyHz(void)
{
    return s_foc_openloop.frequency_hz;
}

void FOC_OpenLoop_GetVoltageDQ(float *vd, float *vq)
{
    if (vd != NULL)
    {
        *vd = s_foc_openloop.vd;
    }
    if (vq != NULL)
    {
        *vq = s_foc_openloop.vq;
    }
}

uint8_t FOC_OpenLoop_IsRunning(void)
{
    return s_foc_openloop.running;
}
