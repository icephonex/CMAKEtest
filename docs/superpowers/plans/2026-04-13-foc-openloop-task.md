# FOC Open-Loop Task Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在当前工程中新增一个由 AT 命令控制启停的 FreeRTOS 开环转动任务，使系统能够持续推进电角度并输出旋转三相 PWM。

**Architecture:** 保持现有分层不变：`foc.c/.h` 继续只负责数学算法，`foc_port.c/.h` 继续只负责 PWM 输出，新增 `foc_openloop.c/.h` 作为开环控制层，集中管理 `theta`、`vd/vq`、电频和运行状态。AT 层只改共享参数与启停请求，实际的旋转磁场刷新由独立任务周期执行。

**Tech Stack:** C11, STM32 HAL, FreeRTOS CMSIS V1, TIM8 complementary PWM

---

## File Structure

- Create: `unilib/src/foc_openloop.h`
  作用：声明开环控制层状态、参数接口和 FreeRTOS 任务入口。
- Create: `unilib/src/foc_openloop.c`
  作用：实现 `theta` 推进、反 `Park`、PWM 刷新、启停控制和参数限幅。
- Modify: `Dev/port/at_server_port.c`
  作用：把 `FOCSTART` / `FOCSTOP` 从直接控 PWM 改为调用开环控制层，并补 `FOCFREQ`、`FOCDQ` 的设置与查询。
- Modify: `Core/Src/main.c`
  作用：在系统初始化阶段调用 `FOC_OpenLoop_Init()`，并创建新的开环任务。
- Modify: `CMakeLists.txt`
  作用：把 `unilib/src/foc_openloop.c` 加入固件构建。

### Task 1: 新增开环控制层接口头文件

**Files:**
- Create: `unilib/src/foc_openloop.h`

- [ ] **Step 1: 创建开环控制层头文件**

```c
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
```

### Task 2: 实现开环控制层核心逻辑

**Files:**
- Create: `unilib/src/foc_openloop.c`

- [ ] **Step 1: 创建开环控制层源文件**

```c
#include "foc_openloop.h"

#include <math.h>

#include "cmsis_os.h"
#include "foc.h"
#include "foc_port.h"

#define FOC_OPENLOOP_PI                 (3.1415926536f)
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

/* 将浮点量限制在指定范围，避免异常参数直接进入开环任务。 */
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

/* 将电角度回绕到 0 ~ 2*pi，避免长时间运行后浮点量持续增大。 */
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

/* 运行态下计算旋转电压矢量并刷新到三相 PWM。 */
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
            if (s_foc_openloop.pwm_started == 0U)
            {
                FOC_Port_StartPwm();
                s_foc_openloop.pwm_started = 1U;
            }

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
```

### Task 3: 将开环控制层接入系统启动和任务创建

**Files:**
- Modify: `Core/Src/main.c`

- [ ] **Step 1: 在 `main.c` 中引入开环控制层头文件**

```c
#include "foc_openloop.h"
```

- [ ] **Step 2: 在系统初始化阶段调用开环控制层初始化**

```c
  /* 初始化 FOC 端口层与开环控制层，上电默认不自动出波形。 */
  FOC_Port_Init();
  FOC_OpenLoop_Init();
```

- [ ] **Step 3: 在 `main.c` 中新增任务句柄与函数声明**

```c
osThreadId focOpenLoopTaskHandle;
void FOC_OpenLoop_Task(void const * argument);
```

- [ ] **Step 4: 在任务创建区新增开环任务**

```c
  osThreadDef(focOpenLoopTask, FOC_OpenLoop_Task, osPriorityAboveNormal, 0, 256);
  focOpenLoopTaskHandle = osThreadCreate(osThread(focOpenLoopTask), NULL);
  if (focOpenLoopTaskHandle == NULL)
  {
    Error_Handler();
  }
```

### Task 4: 扩展 AT 命令入口，改为控制开环任务

**Files:**
- Modify: `Dev/port/at_server_port.c`

- [ ] **Step 1: 引入开环控制层头文件并新增 dq / 频率状态缓存**

```c
#include "foc_openloop.h"
```

```c
static int32_t s_foc_freq_millihz;
static int32_t s_foc_vd_permille;
static int32_t s_foc_vq_permille;
```

- [ ] **Step 2: 在初始化函数中设置默认参数并同步到开环控制层**

```c
    s_foc_freq_millihz = 5000;
    s_foc_vd_permille = 0;
    s_foc_vq_permille = 100;

    FOC_OpenLoop_SetFrequencyHz((float)s_foc_freq_millihz / 1000.0f);
    FOC_OpenLoop_SetVoltageDQ((float)s_foc_vd_permille / 1000.0f,
                              (float)s_foc_vq_permille / 1000.0f);
```

- [ ] **Step 3: 把 `FOCSTART` / `FOCSTOP` 改为控制开环任务请求**

```c
    if ((request->type == AT_SERVER_COMMAND_TYPE_CMD) &&
        (AT_Server_Port_NameEquals(request, "FOCSTART") != 0U))
    {
        FOC_OpenLoop_RequestStart();
        return AT_SERVER_HANDLER_RESULT_OK;
    }

    if ((request->type == AT_SERVER_COMMAND_TYPE_CMD) &&
        (AT_Server_Port_NameEquals(request, "FOCSTOP") != 0U))
    {
        FOC_OpenLoop_RequestStop();
        return AT_SERVER_HANDLER_RESULT_OK;
    }
```

- [ ] **Step 4: 新增 `FOCFREQ` 命令**

```c
    if (AT_Server_Port_NameEquals(request, "FOCFREQ") != 0U)
    {
        if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
        {
            if (AT_Server_Port_ParseInt32(request->args, &s_foc_freq_millihz) != 0)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            if (s_foc_freq_millihz < 0)
            {
                s_foc_freq_millihz = 0;
            }

            FOC_OpenLoop_SetFrequencyHz((float)s_foc_freq_millihz / 1000.0f);
            return AT_SERVER_HANDLER_RESULT_OK;
        }

        if (request->type == AT_SERVER_COMMAND_TYPE_QUERY)
        {
            char data[24];
            (void)snprintf(data, sizeof(data), "%ld", (long)s_foc_freq_millihz);
            return (AT_Server_WriteHeadData("FOCFREQ", data) == 0) ? AT_SERVER_HANDLER_RESULT_OK
                                                                   : AT_SERVER_HANDLER_RESULT_ERROR;
        }
    }
```

- [ ] **Step 5: 新增 `FOCDQ` 命令**

```c
    if (AT_Server_Port_NameEquals(request, "FOCDQ") != 0U)
    {
        if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
        {
            if (AT_Server_Port_ParseFocVector(request->args, &s_foc_vd_permille, &s_foc_vq_permille) != 0)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            FOC_OpenLoop_SetVoltageDQ((float)s_foc_vd_permille / 1000.0f,
                                      (float)s_foc_vq_permille / 1000.0f);
            return AT_SERVER_HANDLER_RESULT_OK;
        }

        if (request->type == AT_SERVER_COMMAND_TYPE_QUERY)
        {
            char data[32];
            (void)snprintf(data, sizeof(data), "%ld,%ld",
                           (long)s_foc_vd_permille,
                           (long)s_foc_vq_permille);
            return (AT_Server_WriteHeadData("FOCDQ", data) == 0) ? AT_SERVER_HANDLER_RESULT_OK
                                                                 : AT_SERVER_HANDLER_RESULT_ERROR;
        }
    }
```

- [ ] **Step 6: 将 `FOCSTATE` 查询改为返回开环任务状态**

```c
static int AT_Server_Port_WriteFocState(void)
{
    return AT_Server_WriteHeadData("FOCSTATE", (FOC_OpenLoop_IsRunning() != 0U) ? "RUN" : "STOP");
}
```

- [ ] **Step 7: 保留 `FOCVAB` 作为静态矢量调试命令，仅在开环任务停止时生效**

```c
            if (FOC_OpenLoop_IsRunning() != 0U)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            FOC_Port_OutputAlphaBeta(AT_Server_Port_GetFocVector());
            return AT_SERVER_HANDLER_RESULT_OK;
```

### Task 5: 接入构建并完成固件验证

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 把开环控制层源文件加入构建**

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/at_server_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/foc_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/lfs_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/at_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/foc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/foc_openloop.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/spi_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/lwrb-develop/lwrb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/littlefs-master/lfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/littlefs-master/lfs_util.c
)
```

- [ ] **Step 2: 运行固件构建验证**

Run: `cmake --build --preset Debug`
Expected: PASS，并成功生成当前 STM32 固件目标。

- [ ] **Step 3: 检查命名和新接口是否在仓库中保持一致**

Run: `rg -n "FOC_OpenLoop_|FOCFREQ|FOCDQ|FOCSTART|FOCSTOP|FOCSTATE" Core Dev unilib CMakeLists.txt`
Expected: 命名统一落在 `foc_openloop`、`FOCFREQ`、`FOCDQ` 这一组接口上，不出现第二套同义接口。
