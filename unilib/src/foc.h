#ifndef FOC_H
#define FOC_H

#ifdef __cplusplus
extern "C" {
#endif

/* 三相静止坐标系量，可用于表示三相电压或电流 */
typedef struct
{
    float a;
    float b;
    float c;
} FOC_ABC;

/* 两相静止坐标系量 */
typedef struct
{
    float alpha;
    float beta;
} FOC_AlphaBeta;

/* 两相旋转坐标系量 */
typedef struct
{
    float d;
    float q;
} FOC_DQ;

/* 三相 PWM 归一化占空比 */
typedef struct
{
    float phase_u;
    float phase_v;
    float phase_w;
} FOC_Duty;

#define FOC_SQRT3           (1.7320508076f)
#define FOC_INV_SQRT3       (0.5773502692f)
#define FOC_SQRT3_BY_2      (0.8660254038f)

/**
 * @brief 执行 Clarke 变换，将三相量映射到 alpha-beta 静止坐标系。
 * @param abc 输入的三相量。
 * @return 变换后的 alpha-beta 结果。
 */
FOC_AlphaBeta FOC_Clarke(FOC_ABC abc);

/**
 * @brief 执行 Park 变换，将 alpha-beta 静止坐标系映射到 d-q 旋转坐标系。
 * @param alpha_beta 输入的 alpha-beta 量。
 * @param sin_theta 当前电角度正弦值。
 * @param cos_theta 当前电角度余弦值。
 * @return 变换后的 d-q 结果。
 */
FOC_DQ FOC_Park(FOC_AlphaBeta alpha_beta, float sin_theta, float cos_theta);

/**
 * @brief 执行反 Park 变换，将 d-q 旋转坐标系映射回 alpha-beta 静止坐标系。
 * @param dq 输入的 d-q 量。
 * @param sin_theta 当前电角度正弦值。
 * @param cos_theta 当前电角度余弦值。
 * @return 变换后的 alpha-beta 结果。
 */
FOC_AlphaBeta FOC_InvPark(FOC_DQ dq, float sin_theta, float cos_theta);

/**
 * @brief 对 alpha-beta 电压矢量做幅值限幅。
 * @param v_ab 输入的 alpha-beta 电压矢量。
 * @param max_magnitude 允许的最大矢量幅值。
 * @return 限幅后的 alpha-beta 矢量。
 */
FOC_AlphaBeta FOC_LimitAlphaBeta(FOC_AlphaBeta v_ab, float max_magnitude);

/**
 * @brief 将 alpha-beta 电压请求转换为三相归一化占空比。
 * @param v_ab 输入的 alpha-beta 电压矢量。
 * @return 范围位于 0.0f 到 1.0f 的三相占空比。
 */
FOC_Duty FOC_AlphaBetaToDuty(FOC_AlphaBeta v_ab);

#ifdef __cplusplus
}
#endif

#endif /* FOC_H */
