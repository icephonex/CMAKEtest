#include "foc.h"

#include <math.h>

#define FOC_HALF                (0.5f)
#define FOC_ONE                 (1.0f)
#define FOC_MAX_VECTOR_MAG      (FOC_INV_SQRT3)

/* 将浮点值限制到指定区间，避免异常值直接进入 PWM 输出 */
static float FOC_Clamp(float value, float min_value, float max_value)
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

/* 求三个浮点量中的最大值，用于后续公共模式电压注入 */
static float FOC_Max3(float x, float y, float z)
{
    float max_value = x;

    if (y > max_value)
    {
        max_value = y;
    }
    if (z > max_value)
    {
        max_value = z;
    }

    return max_value;
}

/* 求三个浮点量中的最小值，用于后续公共模式电压注入 */
static float FOC_Min3(float x, float y, float z)
{
    float min_value = x;

    if (y < min_value)
    {
        min_value = y;
    }
    if (z < min_value)
    {
        min_value = z;
    }

    return min_value;
}

FOC_AlphaBeta FOC_Clarke(FOC_ABC abc)
{
    FOC_AlphaBeta alpha_beta;

    /* 这里默认三相满足 a+b+c=0 的平衡条件，因此 beta 只需由 a、b 推出。 */
    alpha_beta.alpha = abc.a;
    alpha_beta.beta = (abc.a + (2.0f * abc.b)) * FOC_INV_SQRT3;

    return alpha_beta;
}

FOC_DQ FOC_Park(FOC_AlphaBeta alpha_beta, float sin_theta, float cos_theta)
{
    FOC_DQ dq;

    dq.d = (alpha_beta.alpha * cos_theta) + (alpha_beta.beta * sin_theta);
    dq.q = (-alpha_beta.alpha * sin_theta) + (alpha_beta.beta * cos_theta);

    return dq;
}

FOC_AlphaBeta FOC_InvPark(FOC_DQ dq, float sin_theta, float cos_theta)
{
    FOC_AlphaBeta alpha_beta;

    alpha_beta.alpha = (dq.d * cos_theta) - (dq.q * sin_theta);
    alpha_beta.beta = (dq.d * sin_theta) + (dq.q * cos_theta);

    return alpha_beta;
}

FOC_AlphaBeta FOC_LimitAlphaBeta(FOC_AlphaBeta v_ab, float max_magnitude)
{
    float magnitude;
    float scale;

    if (max_magnitude <= 0.0f)
    {
        return (FOC_AlphaBeta){0.0f, 0.0f};
    }

    magnitude = sqrtf((v_ab.alpha * v_ab.alpha) + (v_ab.beta * v_ab.beta));
    if (magnitude <= max_magnitude)
    {
        return v_ab;
    }

    scale = max_magnitude / magnitude;
    v_ab.alpha *= scale;
    v_ab.beta *= scale;

    return v_ab;
}

FOC_Duty FOC_AlphaBetaToDuty(FOC_AlphaBeta v_ab)
{
    float phase_u;
    float phase_v;
    float phase_w;
    float common_mode;
    FOC_Duty duty;

    /* 用零序注入方式把 alpha-beta 电压请求映射到三相占空比。 */
    v_ab = FOC_LimitAlphaBeta(v_ab, FOC_MAX_VECTOR_MAG);

    phase_u = v_ab.alpha;
    phase_v = (-FOC_HALF * v_ab.alpha) + (FOC_SQRT3_BY_2 * v_ab.beta);
    phase_w = (-FOC_HALF * v_ab.alpha) - (FOC_SQRT3_BY_2 * v_ab.beta);

    common_mode = FOC_HALF * (FOC_Max3(phase_u, phase_v, phase_w) +
                              FOC_Min3(phase_u, phase_v, phase_w));

    duty.phase_u = FOC_Clamp((phase_u - common_mode) + FOC_HALF, 0.0f, FOC_ONE);
    duty.phase_v = FOC_Clamp((phase_v - common_mode) + FOC_HALF, 0.0f, FOC_ONE);
    duty.phase_w = FOC_Clamp((phase_w - common_mode) + FOC_HALF, 0.0f, FOC_ONE);

    return duty;
}
