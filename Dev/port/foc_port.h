#ifndef FOC_PORT_H
#define FOC_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_base.h"
#include "main.h"
#include "foc.h"

/**
 * @brief FOC PWM 端口层的硬件配置。
 * @param timer 用于输出三相 PWM 的高级定时器句柄。
 * @param channel_u U 相通道。
 * @param channel_v V 相通道。
 * @param channel_w W 相通道。
 */
typedef struct
{
    TIM_HandleTypeDef *timer;
    uint32_t channel_u;
    uint32_t channel_v;
    uint32_t channel_w;
} FOC_Port_Config;

/**
 * @brief 初始化 FOC 端口层，并清零三相比较值。
 * @param config PWM 输出端口配置。
 * @retval APP_STATUS_OK 表示初始化成功。
 * @retval APP_STATUS_INVALID_ARG 表示输入配置无效。
 */
APP_Status FOC_Port_Init(const FOC_Port_Config *config);

/**
 * @brief 启动三路正向与三路互补 PWM 输出。
 * @retval APP_STATUS_OK 表示启动成功或已经处于启动状态。
 * @retval APP_STATUS_NOT_READY 表示端口层尚未初始化。
 * @retval APP_STATUS_HW_ERROR 表示底层硬件启动失败。
 */
APP_Status FOC_Port_StartPwm(void);

/**
 * @brief 停止全部六路 PWM 输出，并清零比较值。
 * @retval APP_STATUS_OK 表示停止成功。
 * @retval APP_STATUS_NOT_READY 表示端口层尚未初始化。
 * @retval APP_STATUS_HW_ERROR 表示底层硬件停止失败。
 */
APP_Status FOC_Port_StopPwm(void);

/**
 * @brief 按归一化占空比更新三相 PWM 比较值。
 * @param duty 输入的三相归一化占空比指针，范围应为 0.0f 到 1.0f。
 * @retval APP_STATUS_OK 表示更新成功。
 * @retval APP_STATUS_INVALID_ARG 表示输入参数无效。
 * @retval APP_STATUS_NOT_READY 表示端口层尚未初始化。
 */
APP_Status FOC_Port_SetDuty(const FOC_Duty *duty);

/**
 * @brief 直接根据 alpha-beta 电压请求计算占空比并输出到 PWM。
 * @param v_ab 输入的 alpha-beta 电压矢量。
 * @retval APP_STATUS_OK 表示输出成功。
 * @retval APP_STATUS_NOT_READY 表示端口层尚未初始化。
 */
APP_Status FOC_Port_OutputAlphaBeta(FOC_AlphaBeta v_ab);

#ifdef __cplusplus
}
#endif

#endif /* FOC_PORT_H */
