# FOC 基础算法与端口层设计

**编写日期：** 2026-04-13
**适用工程：** `CMAKEtest`

## 目标

在当前工程中新增一组最小可用的 FOC 基础模块，分为：

- `unilib/src/foc.h`
- `unilib/src/foc.c`
- `Dev/port/foc_port.h`
- `Dev/port/foc_port.c`

本轮只实现基础算法层和硬件端口层，不实现完整闭环控制器，不接入电流采样、角度估算、速度环或状态机。

## 范围边界

本次实现包含：

- `Clarke` 变换
- `Park` 变换
- 反 `Park` 变换
- 基于 `alpha-beta` 电压矢量的三相占空比计算
- 占空比限幅与归一化
- 将算法输出写入当前板级 `TIM8` 六路互补 PWM
- PWM 启停与占空比下发接口

本次实现不包含：

- 电流环 PI 控制器
- 速度环
- 位置环
- 观测器
- 编码器或霍尔接口
- ADC 采样触发与采样同步
- 六步换相状态机

## 当前工程前提

当前工程已经具备以下基础：

- `HAL_TIM` 已打开
- `TIM8` 已配置为三对互补 PWM
- 正向通道为 `PC6/PC7/PC8`
- 互补通道为 `PA7/PB0/PB1`

因此本次新增模块只需要消费现有 `TIM8` 资源，不需要重新定义另一套 PWM 外设。

## 分层设计

### 1. 通用算法层

`unilib/src/foc.h` 与 `unilib/src/foc.c` 负责纯算法逻辑，不直接依赖 `TIM_HandleTypeDef`、GPIO 或 HAL 外设函数。

这一层负责：

- 定义三相、两相静止坐标系、两相旋转坐标系数据结构
- 提供 `Clarke`、`Park`、反 `Park` 变换函数
- 根据 `alpha-beta` 电压请求计算三相归一化占空比
- 对输入电压矢量和输出占空比进行限幅

这一层不负责：

- 启动定时器
- 写寄存器
- 使能主输出
- 配置死区

### 2. 端口层

`Dev/port/foc_port.h` 与 `Dev/port/foc_port.c` 负责把通用算法结果映射到当前板级硬件。

这一层负责：

- 绑定当前使用的 `TIM8`
- 启动 `CH1/CH2/CH3` 与 `CH1N/CH2N/CH3N`
- 关闭六路 PWM 输出
- 提供设置三相占空比的统一接口
- 负责把 `0.0f ~ 1.0f` 的归一化占空比转换为 `CCR`

这一层不负责：

- 坐标变换计算
- 电机控制状态机
- 调速策略
- 闭环控制

## 文件职责

### `unilib/src/foc.h`

声明通用算法层的数据结构、常量和对外函数，计划包含：

- 三相电流或电压结构体
- `alpha-beta` 坐标结构体
- `dq` 坐标结构体
- 三相占空比结构体
- 基础算法函数声明

### `unilib/src/foc.c`

实现通用算法，计划包含：

- `Clarke` 变换
- `Park` 变换
- 反 `Park` 变换
- 电压矢量限幅
- 三相占空比计算
- 占空比归一化与边界裁剪

### `Dev/port/foc_port.h`

声明端口层接口，计划包含：

- 端口初始化接口
- PWM 启动接口
- PWM 停止接口
- 三相占空比写入接口
- 可选的“按 `alpha-beta` 请求直接下发”辅助接口

### `Dev/port/foc_port.c`

实现 `TIM8` 相关硬件接口，计划包含：

- `extern TIM_HandleTypeDef htim8`
- 启动正向与互补输出
- 写入 `CCR1/CCR2/CCR3`
- 关闭全部 PWM 输出
- 必要的参数检查与占空比裁剪

## 推荐接口

### 通用算法层接口

```c
typedef struct
{
    float a;
    float b;
    float c;
} FOC_ABC;

typedef struct
{
    float alpha;
    float beta;
} FOC_AlphaBeta;

typedef struct
{
    float d;
    float q;
} FOC_DQ;

typedef struct
{
    float phase_u;
    float phase_v;
    float phase_w;
} FOC_Duty;

FOC_AlphaBeta FOC_Clarke(FOC_ABC abc);
FOC_DQ FOC_Park(FOC_AlphaBeta alpha_beta, float sin_theta, float cos_theta);
FOC_AlphaBeta FOC_InvPark(FOC_DQ dq, float sin_theta, float cos_theta);
FOC_AlphaBeta FOC_LimitAlphaBeta(FOC_AlphaBeta v_ab, float max_magnitude);
FOC_Duty FOC_AlphaBetaToDuty(FOC_AlphaBeta v_ab);
```

### 端口层接口

```c
void FOC_Port_Init(void);
void FOC_Port_StartPwm(void);
void FOC_Port_StopPwm(void);
void FOC_Port_SetDuty(const FOC_Duty *duty);
void FOC_Port_OutputAlphaBeta(FOC_AlphaBeta v_ab);
```

## 数据流

第一版数据流固定为：

1. 上层先给出一个 `alpha-beta` 或 `dq` 电压请求
2. 若输入为 `dq`，先在算法层做反 `Park`
3. 算法层把 `alpha-beta` 电压请求转换为三相归一化占空比
4. 端口层将归一化占空比转换为 `TIM8->CCR1/CCR2/CCR3`
5. `TIM8` 的正向和互补通道输出六路 PWM

## 约束与默认假设

- 算法层默认使用 `float`
- `sin(theta)` 与 `cos(theta)` 由上层提供，本轮不在算法层内计算三角函数
- 占空比输出统一限定在 `0.0f ~ 1.0f`
- 端口层只管理当前已有的 `TIM8`
- 本轮不引入额外中断和任务

## 错误处理策略

- 算法层尽量使用“输入限幅 + 输出裁剪”的方式，避免异常值直接传到 PWM
- 对空指针一类端口层输入做保护
- 端口层不处理复杂故障状态，只保证“能安全停 PWM”

## 构建接入

需要把以下文件加入构建：

- `unilib/src/foc.c`
- `Dev/port/foc_port.c`

并确保以下头文件目录继续保留：

- `unilib/src`
- `Dev/port`

## 后续扩展方向

当前设计为后续扩展预留接口，但本轮不实现：

- 在算法层上继续叠加 PI 电流环
- 引入 ADC 电流采样
- 加入电角度来源
- 接入调度层形成完整 FOC 控制器

## 结论

本次采用“纯算法核心 + 独立端口层”的最小结构：

- `foc` 只负责数学与调制
- `foc_port` 只负责当前板级 `TIM8` 六路互补 PWM 输出

这样既能尽快把基础 FOC 算法落到工程里，也不会过早把控制策略和硬件细节耦合在一起。
