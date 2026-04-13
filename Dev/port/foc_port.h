#ifndef FOC_PORT_H
#define FOC_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "foc.h"

/**
 * @brief 初始化 FOC 端口层，将当前三相比较值清零。
 */
void FOC_Port_Init(void);

/**
 * @brief 启动 TIM8 的三路正向与三路互补 PWM 输出。
 */
void FOC_Port_StartPwm(void);

/**
 * @brief 停止 TIM8 的全部六路 PWM 输出，并清零比较值。
 */
void FOC_Port_StopPwm(void);

/**
 * @brief 按归一化占空比更新三相 PWM 比较值。
 * @param duty 输入的三相归一化占空比指针，范围应为 0.0f 到 1.0f。
 */
void FOC_Port_SetDuty(const FOC_Duty *duty);

/**
 * @brief 直接根据 alpha-beta 电压请求计算占空比并输出到 PWM。
 * @param v_ab 输入的 alpha-beta 电压矢量。
 */
void FOC_Port_OutputAlphaBeta(FOC_AlphaBeta v_ab);

#ifdef __cplusplus
}
#endif

#endif /* FOC_PORT_H */
