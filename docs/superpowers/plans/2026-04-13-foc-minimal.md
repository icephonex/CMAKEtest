# FOC Minimal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为当前工程新增一套可复用的 FOC 基础数学库 `foc.c/.h`，并新增基于 `TIM8` 六路互补 PWM 的端口层 `foc_port.c/.h`。

**Architecture:** 算法层保持与 HAL 和板级资源解耦，只负责 `Clarke/Park/InvPark`、限幅和三相占空比计算；端口层只封装 `TIM8` 的三对互补输出、占空比写入和 PWM 启停。最后把 `foc.c` 与 `foc_port.c` 接入当前 STM32 固件构建，并用固件构建结果作为本轮唯一验证手段。

**Tech Stack:** C11, CMake, STM32 HAL TIM, TIM8 complementary PWM

---

## File Structure

- Create: `unilib/src/foc.h`
  作用：定义 FOC 基础数据结构、数学常量和算法层对外接口。
- Create: `unilib/src/foc.c`
  作用：实现基础坐标变换、限幅和三相占空比计算。
- Create: `Dev/port/foc_port.h`
  作用：声明当前板级 `TIM8` 六路互补 PWM 的封装接口。
- Create: `Dev/port/foc_port.c`
  作用：负责 `TIM8` 启停、`CCR1/CCR2/CCR3` 写入和按 `alpha-beta` 请求直接输出。
- Modify: `CMakeLists.txt`
  作用：把 `unilib/src/foc.c` 和 `Dev/port/foc_port.c` 加入固件构建。

### Task 1: 实现通用 FOC 数学核心

**Files:**
- Create: `unilib/src/foc.h`
- Create: `unilib/src/foc.c`

- [ ] **Step 1: 创建 `foc.h`，定义算法层接口和数据结构**

```c
#ifndef FOC_H
#define FOC_H

#ifdef __cplusplus
extern "C" {
#endif

/* 三相静止坐标系量，可用于电压或电流 */
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
```

- [ ] **Step 2: 创建 `foc.c`，实现最小可用数学算法**

```c
#include "foc.h"

#include <math.h>

#define FOC_HALF                    (0.5f)
#define FOC_ONE                     (1.0f)
#define FOC_MIN_SVPWM_MAGNITUDE     (FOC_INV_SQRT3)

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

    v_ab = FOC_LimitAlphaBeta(v_ab, FOC_MIN_SVPWM_MAGNITUDE);

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
```

### Task 2: 接入固件构建并新增 TIM8 六路互补 PWM 端口层

**Files:**
- Modify: `CMakeLists.txt`
- Create: `Dev/port/foc_port.h`
- Create: `Dev/port/foc_port.c`

- [ ] **Step 1: 把新源文件加入固件构建**

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/at_server_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/foc_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/lfs_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/at_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/foc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/src/spi_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/lwrb-develop/lwrb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/littlefs-master/lfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/littlefs-master/lfs_util.c
)
```

- [ ] **Step 2: 创建 `foc_port.h`，声明当前板级接口**

```c
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
```

- [ ] **Step 3: 创建 `foc_port.c`，绑定到当前 `TIM8` 六路互补 PWM**

```c
#include "foc_port.h"

extern TIM_HandleTypeDef htim8;

/* 记录 PWM 是否已经启动，避免重复启动导致不必要的 HAL 调用 */
static uint8_t s_foc_port_started;

/* 将归一化占空比裁剪到 0 到 1，避免越界写入比较寄存器 */
static float FOC_Port_ClampDuty(float duty)
{
    if (duty < 0.0f)
    {
        return 0.0f;
    }

    if (duty > 1.0f)
    {
        return 1.0f;
    }

    return duty;
}

/* 按当前自动重装载值把归一化占空比换算成 CCR 比较值 */
static uint32_t FOC_Port_DutyToCompare(float duty)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim8);
    float clamped = FOC_Port_ClampDuty(duty);

    return (uint32_t)(clamped * (float)arr);
}

void FOC_Port_Init(void)
{
    s_foc_port_started = 0U;

    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, 0U);
}

void FOC_Port_StartPwm(void)
{
    if (s_foc_port_started != 0U)
    {
        return;
    }

    if (HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_3) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_3) != HAL_OK)
    {
        Error_Handler();
    }

    s_foc_port_started = 1U;
}

void FOC_Port_StopPwm(void)
{
    (void)HAL_TIMEx_PWMN_Stop(&htim8, TIM_CHANNEL_1);
    (void)HAL_TIMEx_PWMN_Stop(&htim8, TIM_CHANNEL_2);
    (void)HAL_TIMEx_PWMN_Stop(&htim8, TIM_CHANNEL_3);
    (void)HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_1);
    (void)HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_2);
    (void)HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_3);

    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, 0U);

    s_foc_port_started = 0U;
}

void FOC_Port_SetDuty(const FOC_Duty *duty)
{
    if (duty == NULL)
    {
        return;
    }

    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, FOC_Port_DutyToCompare(duty->phase_u));
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, FOC_Port_DutyToCompare(duty->phase_v));
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, FOC_Port_DutyToCompare(duty->phase_w));
}

void FOC_Port_OutputAlphaBeta(FOC_AlphaBeta v_ab)
{
    FOC_Duty duty = FOC_AlphaBetaToDuty(v_ab);

    FOC_Port_SetDuty(&duty);
}
```

### Task 3: 构建与一致性检查

**Files:**
- Test: `build/Debug`

- [ ] **Step 1: 运行固件构建，确认新的算法层和端口层可正常编译**

Run: `cmake --build --preset Debug`
Expected: PASS，并成功生成当前 STM32 固件目标。

- [ ] **Step 2: 检查命名是否在整个仓库内保持一致**

Run: `rg -n "FOC_Port_|FOC_AlphaBeta|FOC_Duty|foc_port|foc.h" CMakeLists.txt Dev/port unilib/src`
Expected: 只出现 `foc`、`foc_port` 这一组命名，不出现其他混杂别名。

- [ ] **Step 3: 提交本轮实现**

```bash
git add CMakeLists.txt Dev/port/foc_port.h Dev/port/foc_port.c unilib/src/foc.h unilib/src/foc.c docs/superpowers/plans/2026-04-13-foc-minimal.md
git commit -m "feat: add foc math and port layers"
```
