# MISRA-2012 Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在当前工程自有代码范围内完成一次面向 MISRA-2012 的重构，把公开接口收敛为整数/定点风格，并统一控制状态、返回值和硬件访问边界。

**Architecture:** 本次重构分为五层推进：先建立公共基础模块，再把 `foc_port` 改成显式硬件绑定接口，然后把 `foc_openloop` 改成整数化控制层，接着收紧 `at_server` 与 `at_server_port` 的文本处理和控制入口，最后在 `main.c` 用户区完成初始化和绑定。实现过程中保持算法层浮点仅留在 `foc.c/.h` 与 `foc_openloop.c` 内部，AT 层与应用层不直接参与 PWM 连续刷新。

**Tech Stack:** C11, STM32 HAL, FreeRTOS CMSIS V1, TIM8 complementary PWM, CMake

---

## File Structure

- Create: `unilib/src/app_base.h`
  作用：定义项目级状态码、公共范围常量和最小通用约定。
- Create: `unilib/src/app_text.h`
  作用：声明受控文本比较、`int32` 解析、整数格式化和参数拆分接口。
- Create: `unilib/src/app_text.c`
  作用：实现受控文本与数值转换工具，替换散落的标准库文本处理。
- Modify: `Dev/port/foc_port.h`
  作用：新增 `FOC_Port_Config`，把 `extern htim8` 风格改为显式端口配置和状态码返回。
- Modify: `Dev/port/foc_port.c`
  作用：缓存显式配置，统一 PWM 启停、比较值更新和错误返回。
- Modify: `unilib/src/foc_openloop.h`
  作用：把开环控制层公开接口改成整数/定点风格，并引入控制模式枚举。
- Modify: `unilib/src/foc_openloop.c`
  作用：统一 `STOP/OPENLOOP/STATIC_VECTOR` 控制模式，收口 PWM 单写入口，内部完成整数到浮点转换。
- Modify: `unilib/src/foc.h`
  作用：补充算法层注释与边界说明，保持算法结构体为内部浮点接口。
- Modify: `unilib/src/foc.c`
  作用：补充显式限幅、常量与浮点转整数边界约束。
- Modify: `unilib/src/at_server.h`
  作用：收紧 AT 通用层返回值语义和回调接口说明。
- Modify: `unilib/src/at_server.c`
  作用：使用受控文本工具替换散落字符串处理，收紧状态机路径。
- Modify: `Dev/port/at_server_port.h`
  作用：把端口层初始化接口改为返回状态码，并声明新的受控边界。
- Modify: `Dev/port/at_server_port.c`
  作用：使用 `app_text` 解析命令，转发整数控制请求，不再直接写 PWM。
- Modify: `Core/Src/main.c`
  作用：在用户区构造 `FOC_Port_Config`、调用各模块初始化并统一处理错误返回。
- Modify: `CMakeLists.txt`
  作用：把 `app_text.c` 纳入构建。

> 按当前工程协作规则，本计划不展开专门测试用例与测试说明，实施阶段以代码重构与最小构建连通为主。

### Task 1: 建立公共基础模块

**Files:**
- Create: `unilib/src/app_base.h`
- Create: `unilib/src/app_text.h`
- Create: `unilib/src/app_text.c`
- Modify: `CMakeLists.txt`
- Test: 无

- [ ] **Step 1: 创建 `app_base.h`，统一项目级状态码和公共常量**

```c
#ifndef APP_BASE_H
#define APP_BASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    APP_STATUS_OK = 0,
    APP_STATUS_INVALID_ARG = 1,
    APP_STATUS_NOT_READY = 2,
    APP_STATUS_BUSY = 3,
    APP_STATUS_RANGE = 4,
    APP_STATUS_IO = 5,
    APP_STATUS_HW_ERROR = 6
} APP_Status;

#define APP_TRUE  (1U)
#define APP_FALSE (0U)

#ifdef __cplusplus
}
#endif

#endif /* APP_BASE_H */
```

- [ ] **Step 2: 创建 `app_text.h`，声明受控文本与数值接口**

```c
#ifndef APP_TEXT_H
#define APP_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_base.h"

APP_Status APP_Text_EqualsLiteral(const char *text, size_t text_len, const char *literal, uint8_t *matched);
APP_Status APP_Text_ParseInt32(const char *text, int32_t *value);
APP_Status APP_Text_ParseInt32Pair(const char *text, int32_t *first_value, int32_t *second_value);
APP_Status APP_Text_FormatInt32(char *buffer, size_t buffer_size, int32_t value);
APP_Status APP_Text_FormatInt32Pair(char *buffer, size_t buffer_size, int32_t first_value, int32_t second_value);

#ifdef __cplusplus
}
#endif

#endif /* APP_TEXT_H */
```

- [ ] **Step 3: 实现 `app_text.c`，集中处理比较、解析和格式化**

```c
#include "app_text.h"

#include <limits.h>
#include <stdio.h>

static size_t APP_Text_Length(const char *text)
{
    size_t length = 0U;

    if (text != NULL)
    {
        while (text[length] != '\0')
        {
            length++;
        }
    }

    return length;
}

APP_Status APP_Text_ParseInt32(const char *text, int32_t *value)
{
    int32_t sign = 1;
    int32_t result = 0;
    size_t index = 0U;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0'))
    {
        return APP_STATUS_INVALID_ARG;
    }

    if (text[index] == '-')
    {
        sign = -1;
        index++;
    }

    if (text[index] == '\0')
    {
        return APP_STATUS_INVALID_ARG;
    }

    while (text[index] != '\0')
    {
        const char ch = text[index];
        if ((ch < '0') || (ch > '9'))
        {
            return APP_STATUS_INVALID_ARG;
        }

        if (result > (INT32_MAX / 10))
        {
            return APP_STATUS_RANGE;
        }

        result = (int32_t)((result * 10) + (int32_t)(ch - '0'));
        index++;
    }

    *value = (int32_t)(result * sign);
    return APP_STATUS_OK;
}

APP_Status APP_Text_FormatInt32(char *buffer, size_t buffer_size, int32_t value)
{
    int written;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return APP_STATUS_INVALID_ARG;
    }

    written = snprintf(buffer, buffer_size, "%ld", (long)value);

    if ((written <= 0) || ((size_t)written >= buffer_size))
    {
        return APP_STATUS_RANGE;
    }

    return APP_STATUS_OK;
}
```

- [ ] **Step 4: 在 `CMakeLists.txt` 中加入 `app_text.c`**

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/at_server_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/foc_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/lfs_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/app_text.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/at_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/foc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/foc_openloop.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/spi_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/lwrb-develop/lwrb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/littlefs-master/lfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/littlefs-master/lfs_util.c
)
```

- [ ] **Step 5: 提交基础模块变更**

```bash
git add CMakeLists.txt unilib/src/app_base.h unilib/src/app_text.h unilib/src/app_text.c
git commit -m "refactor: add app base and text helpers"
```

### Task 2: 重构 FOC 端口层为显式配置接口

**Files:**
- Modify: `Dev/port/foc_port.h`
- Modify: `Dev/port/foc_port.c`
- Test: 无

- [ ] **Step 1: 在 `foc_port.h` 中定义端口配置和状态码风格接口**

```c
#ifndef FOC_PORT_H
#define FOC_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_base.h"
#include "main.h"
#include "foc.h"

typedef struct
{
    TIM_HandleTypeDef *timer;
    uint32_t channel_u;
    uint32_t channel_v;
    uint32_t channel_w;
} FOC_Port_Config;

APP_Status FOC_Port_Init(const FOC_Port_Config *config);
APP_Status FOC_Port_StartPwm(void);
APP_Status FOC_Port_StopPwm(void);
APP_Status FOC_Port_SetDuty(const FOC_Duty *duty);
APP_Status FOC_Port_OutputAlphaBeta(FOC_AlphaBeta v_ab);

#ifdef __cplusplus
}
#endif

#endif /* FOC_PORT_H */
```

- [ ] **Step 2: 在 `foc_port.c` 中缓存显式配置，不再依赖 `extern htim8`**

```c
#include "foc_port.h"

typedef struct
{
    FOC_Port_Config config;
    uint8_t ready;
    uint8_t started;
} FOC_Port_State;

static FOC_Port_State s_foc_port;

APP_Status FOC_Port_Init(const FOC_Port_Config *config)
{
    if ((config == NULL) || (config->timer == NULL))
    {
        return APP_STATUS_INVALID_ARG;
    }

    s_foc_port.config = *config;
    s_foc_port.ready = APP_TRUE;
    s_foc_port.started = APP_FALSE;

    __HAL_TIM_SET_COMPARE(s_foc_port.config.timer, s_foc_port.config.channel_u, 0U);
    __HAL_TIM_SET_COMPARE(s_foc_port.config.timer, s_foc_port.config.channel_v, 0U);
    __HAL_TIM_SET_COMPARE(s_foc_port.config.timer, s_foc_port.config.channel_w, 0U);

    return APP_STATUS_OK;
}
```

- [ ] **Step 3: 把 PWM 启停与比较值更新改成显式返回状态**

```c
APP_Status FOC_Port_StartPwm(void)
{
    if (s_foc_port.ready == APP_FALSE)
    {
        return APP_STATUS_NOT_READY;
    }

    if (s_foc_port.started != APP_FALSE)
    {
        return APP_STATUS_OK;
    }

    if (HAL_TIM_PWM_Start(s_foc_port.config.timer, s_foc_port.config.channel_u) != HAL_OK)
    {
        return APP_STATUS_HW_ERROR;
    }

    if (HAL_TIMEx_PWMN_Start(s_foc_port.config.timer, s_foc_port.config.channel_u) != HAL_OK)
    {
        return APP_STATUS_HW_ERROR;
    }

    s_foc_port.started = APP_TRUE;
    return APP_STATUS_OK;
}
```

- [ ] **Step 4: 提交 FOC 端口层重构**

```bash
git add Dev/port/foc_port.h Dev/port/foc_port.c
git commit -m "refactor: make foc port explicitly configured"
```

### Task 3: 把 FOC 控制层改为整数接口与单写状态机

**Files:**
- Modify: `unilib/src/foc_openloop.h`
- Modify: `unilib/src/foc_openloop.c`
- Test: 无

- [ ] **Step 1: 在 `foc_openloop.h` 中定义控制模式和整数接口**

```c
#ifndef FOC_OPENLOOP_H
#define FOC_OPENLOOP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_base.h"

typedef enum
{
    FOC_CONTROL_MODE_STOP = 0,
    FOC_CONTROL_MODE_OPENLOOP = 1,
    FOC_CONTROL_MODE_STATIC_VECTOR = 2
} FOC_ControlMode;

APP_Status FOC_OpenLoop_Init(void);
APP_Status FOC_OpenLoop_RequestStart(void);
APP_Status FOC_OpenLoop_RequestStop(void);
APP_Status FOC_OpenLoop_SetFrequencyMilliHz(uint32_t frequency_millihz);
APP_Status FOC_OpenLoop_SetVoltageDQPermille(int32_t vd_permille, int32_t vq_permille);
APP_Status FOC_OpenLoop_SetStaticVectorPermille(int32_t alpha_permille, int32_t beta_permille);
APP_Status FOC_OpenLoop_GetFrequencyMilliHz(uint32_t *frequency_millihz);
APP_Status FOC_OpenLoop_GetVoltageDQPermille(int32_t *vd_permille, int32_t *vq_permille);
FOC_ControlMode FOC_OpenLoop_GetMode(void);
void FOC_OpenLoop_Task(void const *argument);

#ifdef __cplusplus
}
#endif

#endif /* FOC_OPENLOOP_H */
```

- [ ] **Step 2: 在 `foc_openloop.c` 中改造内部状态，统一保存整数请求与运行模式**

```c
typedef struct
{
    FOC_ControlMode mode;
    float theta;
    uint32_t frequency_millihz;
    int32_t vd_permille;
    int32_t vq_permille;
    int32_t alpha_permille;
    int32_t beta_permille;
    uint8_t pwm_started;
} FOC_OpenLoop_State;

static FOC_OpenLoop_State s_foc_openloop;

static float FOC_OpenLoop_PermilleToFloat(int32_t value_permille)
{
    return ((float)value_permille / 1000.0f);
}

static float FOC_OpenLoop_FrequencyToHz(uint32_t frequency_millihz)
{
    return ((float)frequency_millihz / 1000.0f);
}
```

- [ ] **Step 3: 用控制模式统一开环与静态矢量输出路径**

```c
void FOC_OpenLoop_Task(void const *argument)
{
    (void)argument;

    for (;;)
    {
        if (s_foc_openloop.mode == FOC_CONTROL_MODE_OPENLOOP)
        {
            (void)FOC_Port_StartPwm();
            s_foc_openloop.pwm_started = APP_TRUE;
            s_foc_openloop.theta += (6.2831853072f *
                                     FOC_OpenLoop_FrequencyToHz(s_foc_openloop.frequency_millihz) *
                                     0.001f);
            FOC_OpenLoop_UpdateOpenLoopOutput();
        }
        else if (s_foc_openloop.mode == FOC_CONTROL_MODE_STATIC_VECTOR)
        {
            (void)FOC_Port_StartPwm();
            s_foc_openloop.pwm_started = APP_TRUE;
            FOC_OpenLoop_UpdateStaticVectorOutput();
        }
        else
        {
            if (s_foc_openloop.pwm_started != APP_FALSE)
            {
                (void)FOC_Port_StopPwm();
                s_foc_openloop.pwm_started = APP_FALSE;
                s_foc_openloop.theta = 0.0f;
            }
        }

        osDelay(1U);
    }
}
```

- [ ] **Step 4: 把设置接口改成整数/定点风格，并返回状态码**

```c
APP_Status FOC_OpenLoop_SetVoltageDQPermille(int32_t vd_permille, int32_t vq_permille)
{
    s_foc_openloop.vd_permille = vd_permille;
    s_foc_openloop.vq_permille = vq_permille;
    return APP_STATUS_OK;
}

APP_Status FOC_OpenLoop_SetStaticVectorPermille(int32_t alpha_permille, int32_t beta_permille)
{
    s_foc_openloop.alpha_permille = alpha_permille;
    s_foc_openloop.beta_permille = beta_permille;
    s_foc_openloop.mode = FOC_CONTROL_MODE_STATIC_VECTOR;
    return APP_STATUS_OK;
}

APP_Status FOC_OpenLoop_RequestStart(void)
{
    s_foc_openloop.mode = FOC_CONTROL_MODE_OPENLOOP;
    return APP_STATUS_OK;
}

APP_Status FOC_OpenLoop_RequestStop(void)
{
    s_foc_openloop.mode = FOC_CONTROL_MODE_STOP;
    return APP_STATUS_OK;
}

FOC_ControlMode FOC_OpenLoop_GetMode(void)
{
    return s_foc_openloop.mode;
}
```

- [ ] **Step 5: 提交控制层重构**

```bash
git add unilib/src/foc_openloop.h unilib/src/foc_openloop.c
git commit -m "refactor: convert foc control layer to fixed-point api"
```

### Task 4: 收紧 FOC 算法层边界与显式转换

**Files:**
- Modify: `unilib/src/foc.h`
- Modify: `unilib/src/foc.c`
- Test: 无

- [ ] **Step 1: 在 `foc.h` 中保留算法浮点结构，但补齐边界说明**

```c
typedef struct
{
    float alpha;
    float beta;
} FOC_AlphaBeta;

typedef struct
{
    float phase_u;
    float phase_v;
    float phase_w;
} FOC_Duty;

/**
 * @brief 将 alpha-beta 电压请求转换为三相归一化占空比。
 * @param v_ab 输入的 alpha-beta 电压矢量，仅在算法层和控制层内部流动。
 * @return 范围位于 0.0f 到 1.0f 的三相占空比。
 */
FOC_Duty FOC_AlphaBetaToDuty(FOC_AlphaBeta v_ab);
```

- [ ] **Step 2: 在 `foc.c` 中收紧常量、零值返回和显式限幅**

```c
#define FOC_ZERO                (0.0f)
#define FOC_HALF                (0.5f)
#define FOC_ONE                 (1.0f)
#define FOC_MAX_VECTOR_MAG      (FOC_INV_SQRT3)

FOC_AlphaBeta FOC_LimitAlphaBeta(FOC_AlphaBeta v_ab, float max_magnitude)
{
    FOC_AlphaBeta limited = v_ab;

    if (max_magnitude <= FOC_ZERO)
    {
        limited.alpha = FOC_ZERO;
        limited.beta = FOC_ZERO;
        return limited;
    }

    /* 其余流程保持当前算法结构不变，仅补充显式常量与边界控制。 */
    return limited;
}
```

- [ ] **Step 3: 提交算法层整理**

```bash
git add unilib/src/foc.h unilib/src/foc.c
git commit -m "refactor: tighten foc algorithm boundaries"
```

### Task 5: 收紧 AT 通用层与端口层

**Files:**
- Modify: `unilib/src/at_server.h`
- Modify: `unilib/src/at_server.c`
- Modify: `Dev/port/at_server_port.h`
- Modify: `Dev/port/at_server_port.c`
- Test: 无

- [ ] **Step 1: 在 `at_server.h` 中引入 `APP_Status`，统一初始化与发送接口语义**

```c
#include "app_base.h"

APP_Status AT_Server_Init(const AT_Server_Config *config);
size_t AT_Server_InputBytes(const uint8_t *data, size_t len);
void AT_Server_Poll(void);
APP_Status AT_Server_WriteOk(void);
APP_Status AT_Server_WriteError(void);
APP_Status AT_Server_WriteHeadData(const char *head, const char *data);
```

- [ ] **Step 2: 在 `at_server.c` 中用受控文本工具替代散落字符串处理**

```c
#include "app_text.h"

static APP_Status AT_Server_WriteLiteral(const char *text)
{
    size_t len = 0U;

    if ((text == NULL) || (s_at_server.config.write == NULL))
    {
        return APP_STATUS_INVALID_ARG;
    }

    while (text[len] != '\0')
    {
        len++;
    }

    if (len == 0U)
    {
        return APP_STATUS_INVALID_ARG;
    }

    if (s_at_server.config.write((const uint8_t *)text, len, s_at_server.config.write_context) != 0)
    {
        return APP_STATUS_IO;
    }

    return APP_STATUS_OK;
}
```

- [ ] **Step 3: 把 `at_server_port.h` 初始化接口改成返回状态码**

```c
APP_Status AT_Server_Port_Init(UART_HandleTypeDef *huart);
const AT_Server_Config *AT_Server_Port_GetConfig(void);
void AT_Server_Port_HandleIdleIrq(UART_HandleTypeDef *huart);
AT_Server_HandlerResult AT_Server_Port_Handle(const AT_Server_Request *request, void *context);
```

- [ ] **Step 4: 在 `at_server_port.c` 中使用 `app_text` 和整数控制接口**

```c
#include "app_text.h"

if (AT_Server_Port_NameEquals(request, "FOCFREQ") != 0U)
{
    if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
    {
        if (APP_Text_ParseInt32(request->args, &parsed_frequency) != APP_STATUS_OK)
        {
            return AT_SERVER_HANDLER_RESULT_ERROR;
        }

        if (FOC_OpenLoop_SetFrequencyMilliHz((uint32_t)parsed_frequency) != APP_STATUS_OK)
        {
            return AT_SERVER_HANDLER_RESULT_ERROR;
        }

        return AT_SERVER_HANDLER_RESULT_OK;
    }
}
```

- [ ] **Step 5: 从 `AT` 端口层移除直接 PWM 写入路径**

```c
if (AT_Server_Port_NameEquals(request, "FOCVAB") != 0U)
{
    if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
    {
        if (APP_Text_ParseInt32Pair(request->args, &s_foc_alpha_permille, &s_foc_beta_permille) != APP_STATUS_OK)
        {
            return AT_SERVER_HANDLER_RESULT_ERROR;
        }

        if (FOC_OpenLoop_SetStaticVectorPermille(s_foc_alpha_permille, s_foc_beta_permille) != APP_STATUS_OK)
        {
            return AT_SERVER_HANDLER_RESULT_ERROR;
        }

        return AT_SERVER_HANDLER_RESULT_OK;
    }
}
```

- [ ] **Step 6: 为查询命令补齐整数格式化与模式状态输出**

```c
if ((request->type == AT_SERVER_COMMAND_TYPE_QUERY) &&
    (AT_Server_Port_NameEquals(request, "FOCSTATE") != 0U))
{
    const FOC_ControlMode mode = FOC_OpenLoop_GetMode();
    const char *state_text = "STOP";

    if (mode == FOC_CONTROL_MODE_OPENLOOP)
    {
        state_text = "RUN";
    }
    else if (mode == FOC_CONTROL_MODE_STATIC_VECTOR)
    {
        state_text = "STATIC";
    }

    return (AT_Server_WriteHeadData("FOCSTATE", state_text) == APP_STATUS_OK) ?
        AT_SERVER_HANDLER_RESULT_OK :
        AT_SERVER_HANDLER_RESULT_ERROR;
}
```

- [ ] **Step 7: 提交 AT 层重构**

```bash
git add unilib/src/at_server.h unilib/src/at_server.c Dev/port/at_server_port.h Dev/port/at_server_port.c
git commit -m "refactor: route at commands through fixed-point control api"
```

### Task 6: 调整 `main.c` 用户区与初始化接线

**Files:**
- Modify: `Core/Src/main.c`
- Test: 无

- [ ] **Step 1: 在 `main.c` 用户区中构造 `FOC_Port_Config` 并按返回值初始化**

```c
static FOC_Port_Config g_foc_port_config = {
    .timer = &htim8,
    .channel_u = TIM_CHANNEL_1,
    .channel_v = TIM_CHANNEL_2,
    .channel_w = TIM_CHANNEL_3
};

if (FOC_Port_Init(&g_foc_port_config) != APP_STATUS_OK)
{
    Error_Handler();
}

if (FOC_OpenLoop_Init() != APP_STATUS_OK)
{
    Error_Handler();
}
```

- [ ] **Step 2: 把 `AT_Server_Port_Init()` 和 `AT_Server_Init()` 接口改成显式判断**

```c
if (AT_Server_Port_Init(&huart1) != APP_STATUS_OK)
{
    Error_Handler();
}

if (AT_Server_Init(AT_Server_Port_GetConfig()) != APP_STATUS_OK)
{
    Error_Handler();
}
```

- [ ] **Step 3: 保持任务创建逻辑，但只在用户区承担初始化与连接**

```c
osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

osThreadDef(focOpenLoopTask, FOC_OpenLoop_Task, osPriorityAboveNormal, 0, 256);
focOpenLoopTaskHandle = osThreadCreate(osThread(focOpenLoopTask), NULL);

if (focOpenLoopTaskHandle == NULL)
{
    Error_Handler();
}
```

- [ ] **Step 4: 提交入口层接线**

```bash
git add Core/Src/main.c
git commit -m "refactor: wire misra-style control initialization"
```

### Task 7: 统一命名与收尾整理

**Files:**
- Modify: `Dev/port/at_server_port.c`
- Modify: `Dev/port/foc_port.c`
- Modify: `unilib/src/foc_openloop.c`
- Modify: `unilib/src/at_server.c`
- Modify: `unilib/src/foc.c`
- Test: 无

- [ ] **Step 1: 清理遗留的旧接口调用和旧语义名称**

```c
/* 删除旧的 FOC_OpenLoop_SetFrequencyHz / FOC_OpenLoop_SetVoltageDQ 浮点接口调用， */
/* 所有调用点统一改为 MilliHz / Permille 风格。 */
```

- [ ] **Step 2: 统一中文注释、参数说明和返回值语义**

```c
/**
 * @brief 设置静态 alpha-beta 电压矢量请求。
 * @param alpha_permille alpha 轴电压千分比。
 * @param beta_permille beta 轴电压千分比。
 * @retval APP_STATUS_OK 表示设置成功，其余状态码表示参数或状态异常。
 */
APP_Status FOC_OpenLoop_SetStaticVectorPermille(int32_t alpha_permille, int32_t beta_permille);
```

- [ ] **Step 3: 提交最终整理**

```bash
git add Dev/port/at_server_port.c Dev/port/foc_port.c unilib/src/foc_openloop.c unilib/src/at_server.c unilib/src/foc.c
git commit -m "refactor: finalize misra-oriented interface cleanup"
```
