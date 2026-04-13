#ifndef FOC_OPENLOOP_H
#define FOC_OPENLOOP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_base.h"

/**
 * @brief FOC 控制输出模式。
 */
typedef enum
{
    FOC_CONTROL_MODE_STOP = 0,
    FOC_CONTROL_MODE_OPENLOOP = 1,
    FOC_CONTROL_MODE_STATIC_VECTOR = 2
} FOC_ControlMode;

/**
 * @brief 初始化开环控制状态和默认参数。
 * @retval APP_STATUS_OK 表示初始化成功。
 */
APP_Status FOC_OpenLoop_Init(void);

/**
 * @brief FreeRTOS 开环控制任务入口，负责周期推进电角度并刷新 PWM。
 * @param argument 任务参数，当前未使用。
 */
void FOC_OpenLoop_Task(void const *argument);

/**
 * @brief 请求启动开环转动任务。
 * @retval APP_STATUS_OK 表示请求成功。
 */
APP_Status FOC_OpenLoop_RequestStart(void);

/**
 * @brief 请求停止开环转动任务，并关闭 PWM 输出。
 * @retval APP_STATUS_OK 表示请求成功。
 */
APP_Status FOC_OpenLoop_RequestStop(void);

/**
 * @brief 设置开环电频，单位 mHz。
 * @param frequency_millihz 目标电频。
 * @retval APP_STATUS_OK 表示设置成功。
 * @retval APP_STATUS_RANGE 表示超出允许范围。
 */
APP_Status FOC_OpenLoop_SetFrequencyMilliHz(uint32_t frequency_millihz);

/**
 * @brief 设置开环 dq 电压请求，单位千分比。
 * @param vd_permille d 轴电压千分比。
 * @param vq_permille q 轴电压千分比。
 * @retval APP_STATUS_OK 表示设置成功。
 * @retval APP_STATUS_RANGE 表示超出允许范围。
 */
APP_Status FOC_OpenLoop_SetVoltageDQPermille(int32_t vd_permille, int32_t vq_permille);

/**
 * @brief 设置静态 alpha-beta 电压矢量请求，单位千分比。
 * @param alpha_permille alpha 轴电压千分比。
 * @param beta_permille beta 轴电压千分比。
 * @retval APP_STATUS_OK 表示设置成功。
 * @retval APP_STATUS_RANGE 表示超出允许范围。
 */
APP_Status FOC_OpenLoop_SetStaticVectorPermille(int32_t alpha_permille, int32_t beta_permille);

/**
 * @brief 获取当前开环电频，单位 mHz。
 * @param frequency_millihz 输出的目标电频。
 * @retval APP_STATUS_OK 表示读取成功。
 * @retval APP_STATUS_INVALID_ARG 表示输出指针无效。
 */
APP_Status FOC_OpenLoop_GetFrequencyMilliHz(uint32_t *frequency_millihz);

/**
 * @brief 读取当前 dq 电压请求。
 * @param vd_permille 输出的 d 轴电压千分比。
 * @param vq_permille 输出的 q 轴电压千分比。
 * @retval APP_STATUS_OK 表示读取成功。
 * @retval APP_STATUS_INVALID_ARG 表示输出指针无效。
 */
APP_Status FOC_OpenLoop_GetVoltageDQPermille(int32_t *vd_permille, int32_t *vq_permille);

/**
 * @brief 获取当前控制模式。
 * @return 当前控制模式。
 */
FOC_ControlMode FOC_OpenLoop_GetMode(void);

#ifdef __cplusplus
}
#endif

#endif /* FOC_OPENLOOP_H */
