#ifndef FOC_OPENLOOP_H
#define FOC_OPENLOOP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 初始化开环控制状态和默认参数。
 */
void FOC_OpenLoop_Init(void);

/**
 * @brief FreeRTOS 开环控制任务入口，负责周期推进电角度并刷新 PWM。
 * @param argument 任务参数，当前未使用。
 */
void FOC_OpenLoop_Task(void const *argument);

/**
 * @brief 请求启动开环转动任务。
 */
void FOC_OpenLoop_RequestStart(void);

/**
 * @brief 请求停止开环转动任务，并关闭 PWM 输出。
 */
void FOC_OpenLoop_RequestStop(void);

/**
 * @brief 设置开环电频，单位 Hz。
 * @param frequency_hz 目标电频。
 */
void FOC_OpenLoop_SetFrequencyHz(float frequency_hz);

/**
 * @brief 设置开环 dq 电压请求。
 * @param vd d 轴电压。
 * @param vq q 轴电压。
 */
void FOC_OpenLoop_SetVoltageDQ(float vd, float vq);

/**
 * @brief 获取当前开环电频，单位 Hz。
 * @return 当前目标电频。
 */
float FOC_OpenLoop_GetFrequencyHz(void);

/**
 * @brief 读取当前 dq 电压请求。
 * @param vd 输出的 d 轴电压指针。
 * @param vq 输出的 q 轴电压指针。
 */
void FOC_OpenLoop_GetVoltageDQ(float *vd, float *vq);

/**
 * @brief 获取当前运行状态。
 * @return 1 表示运行中，0 表示停止。
 */
uint8_t FOC_OpenLoop_IsRunning(void);

#ifdef __cplusplus
}
#endif

#endif /* FOC_OPENLOOP_H */
