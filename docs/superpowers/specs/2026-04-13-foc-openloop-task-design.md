# FOC 开环转动任务设计

**编写日期：** 2026-04-13
**适用工程：** `CMAKEtest`

## 目标

在当前工程中新增一个由 `AT` 命令控制启停的开环转动任务，使系统具备“持续推进电角度并刷新三相 PWM 输出”的最小开环旋转能力。

本次设计建立在当前已有能力之上：

- `TIM8` 六路互补 PWM 已接入
- `foc.c/.h` 已提供基础坐标变换和 `alpha-beta -> duty` 算法
- `foc_port.c/.h` 已提供 PWM 启停与占空比下发接口
- `AT` 命令通道已可控制 `FOCSTART` / `FOCSTOP` / `FOCVAB`

## 范围边界

本次实现包含：

- 新增一个独立的 FreeRTOS 开环任务
- 任务固定周期推进电角度
- 使用 `InvPark` 从 `dq` 电压请求生成旋转 `alpha-beta` 电压矢量
- 调用 `FOC_Port_OutputAlphaBeta()` 持续刷新 PWM
- `AT+FOCSTART` / `AT+FOCSTOP` 控制开环任务的运行请求
- 增加最小参数接口，用于设置开环频率和 `q` 轴幅值

本次实现不包含：

- 启动对齐
- 加速斜坡
- 电流环
- 速度环
- 位置环
- 霍尔、编码器或观测器反馈
- 中断驱动的高精度调度
- 故障保护状态机

## 推荐架构

本次采用“AT 参数层 + 开环任务层 + PWM 端口层”的三层结构。

### 1. AT 参数层

由 `at_server_port.c` 负责：

- 解析 `FOCSTART` / `FOCSTOP`
- 解析开环任务参数命令
- 更新一组共享控制量

AT 层不直接做电角度推进，不直接执行连续刷新逻辑，只负责改状态和参数。

### 2. 开环任务层

建议新增独立模块，例如：

- `unilib/src/foc_openloop.h`
- `unilib/src/foc_openloop.c`

这一层负责：

- 保存运行请求和运行状态
- 保存当前电角度
- 保存目标电频和目标 `dq` 电压
- 在固定周期内推进 `theta`
- 调用 `FOC_InvPark()` 生成旋转 `alpha-beta`
- 调用 `FOC_Port_OutputAlphaBeta()` 输出到 PWM

### 3. PWM 端口层

继续复用当前已有的：

- `Dev/port/foc_port.h`
- `Dev/port/foc_port.c`

这一层仍然只负责：

- 启动/停止六路 PWM
- 下发三相占空比

## 调度方案

本次使用独立 FreeRTOS 任务，而不是软件定时器或硬件中断。

原因：

- 对当前工程侵入最小
- 与现有 `defaultTask` 风格一致
- 后续加状态机、启动阶段、加速逻辑时更容易扩展

建议首版任务周期：

- `1 ms`

每次任务循环中：

1. 检查是否存在“运行请求”
2. 若未运行，则保持低频轮询，不刷新旋转矢量
3. 若已运行，则按 `theta += omega * dt` 推进电角度
4. 用当前 `dq` 请求执行反 `Park`
5. 将结果下发到 `FOC_Port_OutputAlphaBeta()`
6. `osDelay(1)`

## 控制模型

本次开环任务采用最小匀速旋转模型。

### 控制输入

- `vd`：默认先固定为 `0`
- `vq`：由上层设置，作为驱动力幅值
- `felec_hz`：目标电频，单位 `Hz`

### 电角度推进

每个任务周期按以下公式推进：

```c
theta += 2.0f * PI * felec_hz * dt_s;
```

并在超出 `2*pi` 后回绕到 `0 ~ 2*pi` 区间。

### 磁场生成

每次循环中使用：

- `d = vd`
- `q = vq`

然后通过反 `Park` 得到一个随时间旋转的 `alpha-beta` 矢量。

这意味着当前输出的是“连续旋转电压矢量”，而不是静态矢量。

## 默认参数建议

首版建议默认值：

- `vd = 0.0f`
- `vq = 0.10f`
- `felec_hz = 5.0f`

选择理由：

- 先从较低电频开始，避免一开始就失步
- `q` 轴只给较小幅值，降低调试风险
- `d` 轴保持为零，符合最小 FOC 开环思路

## 任务接口建议

建议新增以下最小接口：

```c
void FOC_OpenLoop_Init(void);
void FOC_OpenLoop_Task(void const *argument);
void FOC_OpenLoop_RequestStart(void);
void FOC_OpenLoop_RequestStop(void);
void FOC_OpenLoop_SetFrequencyHz(float frequency_hz);
void FOC_OpenLoop_SetVoltageDQ(float vd, float vq);
uint8_t FOC_OpenLoop_IsRunning(void);
```

## AT 接口建议

建议保留现有 `FOCSTART` / `FOCSTOP`，并补以下两个命令：

- `AT+FOCFREQ=<hz_milli>`
  说明：使用毫赫兹整数或毫单位定点，避免在 AT 命令里直接解析浮点。
- `AT+FOCDQ=<vd_permille>,<vq_permille>`
  说明：使用千分比定点传参，与当前 `FOCVAB` 风格保持一致。

建议查询命令：

- `AT+FOCFREQ?`
- `AT+FOCDQ?`
- `AT+FOCSTATE?`

其中：

- `FOCSTART` 只置位运行请求并启动 PWM
- `FOCSTOP` 清运行请求并关闭 PWM
- `FOCFREQ` 和 `FOCDQ` 只更新目标参数

## 与当前 `FOCVAB` 的关系

当前已有的 `FOCVAB` 更适合“静态矢量输出调试”。

加入开环任务后，建议解释为：

- 未运行开环任务时，可继续手动通过 `FOCVAB` 输出静态矢量
- 运行开环任务时，以 `FOCDQ` + `FOCFREQ` 生成动态旋转矢量

这样能避免两种模式互相覆盖。

## 状态定义

首版只定义两个最小状态：

- `STOPPED`
- `RUNNING`

不引入 `ALIGN`、`RAMP`、`FAULT` 等更复杂状态。

## 安全策略

首版保持最小安全约束：

- 上电默认 `STOPPED`
- 只有收到 `FOCSTART` 才启动 PWM
- 收到 `FOCSTOP` 立即停 PWM
- 频率与 `dq` 幅值都做限幅
- 任务停止时输出清零

## 构建接入

需要新增并接入：

- `unilib/src/foc_openloop.h`
- `unilib/src/foc_openloop.c`

并在系统启动阶段完成：

- `FOC_OpenLoop_Init()`
- 创建新的 FreeRTOS 任务

## 后续扩展方向

本设计为后续升级预留空间，但本轮不实现：

- 启动对齐
- 频率斜坡
- 转速可调节闭环
- 电流采样闭环
- 中断调度

## 结论

本次推荐采用“独立 `FOC_OpenLoop_Task` + AT 命令控制启停”的方案。

这样可以：

- 保持 `AT` 层只做控制入口
- 保持 `foc_port` 层只做 PWM 输出
- 把连续转动逻辑集中在一个独立任务里

这是当前工程里最稳妥、最容易继续扩展的开环控制落地方式。
