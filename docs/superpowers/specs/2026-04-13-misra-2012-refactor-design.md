# MISRA-2012 重构设计

**编写日期：** 2026-04-13
**适用工程：** `CMAKEtest`

## 目标

在不改动第三方库、HAL 驱动主体和 CubeMX 自动生成主体逻辑的前提下，对当前工程自有代码执行一次面向 `MISRA C:2012` 的重构。

本次重构目标不是宣称“全工程零偏差”，而是实现以下结果：

- 自有代码的接口、状态、返回值和数值表达方式更加受控
- 算法层、协议层、端口层、应用层的边界更加清晰
- 浮点运算尽量收口到算法内部，对外公开接口优先使用整数或定点表达
- 为后续接入静态分析工具提供更稳定的代码基础

## 范围边界

本次重构仅包含以下自有代码：

- `Core/Src/main.c` 的用户代码区
- `Dev/port/at_server_port.h`
- `Dev/port/at_server_port.c`
- `Dev/port/foc_port.h`
- `Dev/port/foc_port.c`
- `unilib/src/at_server.h`
- `unilib/src/at_server.c`
- `unilib/src/foc.h`
- `unilib/src/foc.c`
- `unilib/src/foc_openloop.h`
- `unilib/src/foc_openloop.c`

本次重构计划新增以下基础模块：

- `unilib/src/app_base.h`
- `unilib/src/app_text.h`
- `unilib/src/app_text.c`

本次重构不包含以下内容：

- `Drivers/` 下的 HAL、CMSIS 代码
- `Middlewares/` 下的 FreeRTOS 及其他第三方代码
- `unilib/thirdparty/` 下的第三方组件
- CubeMX 自动生成区的整体风格重写
- 新增闭环控制、采样中断、电流环、速度环

## 设计原则

### 1. 自有接口优先整数化

面向业务层、协议层和端口层公开的接口不再优先暴露浮点参数。

本次统一采用以下表达：

- 频率使用 `mHz`
- 电压矢量和 `dq` 量使用 `permille`
- 状态使用具名枚举
- 成功失败使用项目级状态码

浮点仅保留在以下场景：

- `foc.c` 中的数学变换
- `foc_openloop.c` 内部的角度推进和内部量换算

### 2. 单写入口原则

同一类运行状态和同一份硬件资源只允许一个模块承担最终写入责任。

本次明确：

- `AT` 层只提交控制请求
- `FOC_OpenLoop_Task` 是唯一允许执行 PWM 启停和占空比刷新的任务
- `main.c` 只负责初始化和绑定，不直接改运行态

### 3. 受控依赖原则

必须依赖的 `HAL`、`CMSIS`、`FreeRTOS` 调用不在算法层或协议层散落出现，而是通过端口层、配置结构或受控包装接口集中暴露。

### 4. 显式约束原则

所有对外可见的函数都应显式说明：

- 参数含义
- 返回值语义
- 允许范围
- 失败条件

避免依赖隐式约定、默认真值或裸常量语义。

## 总体架构

本次重构后，工程中的自有代码按以下层次协作：

### 1. 基础公共层

由 `app_base.h` 与 `app_text.h/.c` 组成。

职责：

- 提供统一状态码
- 提供统一布尔与数值常量风格
- 提供受控的字符串比较、整数解析、定点格式化和参数拆分

该层不依赖外设，不依赖任务调度，不直接访问 HAL。

### 2. 算法层

由 `foc.h/.c` 组成。

职责：

- Clarke / Park / 反 Park 变换
- `alpha-beta` 到三相占空比的计算
- 必要的限幅与显式转换

限制：

- 不直接访问定时器或串口
- 不依赖全局硬件句柄
- 不承担协议解析职责

### 3. 控制层

由 `foc_openloop.h/.c` 组成。

职责：

- 保存当前控制请求与运行状态
- 维护开环任务状态机
- 将整数化请求转换为内部浮点量
- 周期推进电角度并驱动算法层
- 统一决定 PWM 的启停与刷新时机

限制：

- 对外公开接口以整数和状态码为主
- 不向外暴露内部浮点状态结构

### 4. 端口层

由 `foc_port.h/.c` 与 `at_server_port.h/.c` 组成。

职责：

- `foc_port`：绑定 TIM8 资源并执行 PWM 输出
- `at_server_port`：把 AT 请求转换为控制层请求

限制：

- `foc_port` 不直接持有 `extern TIM_HandleTypeDef htim8`
- `at_server_port` 不直接参与连续 PWM 刷新
- `at_server_port` 不直接做浮点运算

### 5. 应用入口层

由 `main.c` 用户区组成。

职责：

- 初始化硬件和端口配置
- 绑定控制层与端口层
- 创建默认任务和开环任务

限制：

- 不直接操作控制层内部状态
- 不直接承担协议处理逻辑

## 公共基础模块设计

### `app_base.h`

计划提供以下公共定义：

- 项目级状态码枚举，例如：
  - `APP_STATUS_OK`
  - `APP_STATUS_INVALID_ARG`
  - `APP_STATUS_BUSY`
  - `APP_STATUS_NOT_READY`
  - `APP_STATUS_HW_ERROR`
- 公共范围常量
- 公共辅助宏或内联函数的最小集合

约束：

- 不定义复杂宏逻辑
- 不引入带副作用的函数式宏
- 不重复实现标准类型

### `app_text.h/.c`

计划统一提供以下受控接口：

- 固定长度命令名比较
- 十进制 `int32` 解析
- 单个整数格式化为字符串
- 两个整数拼接为 `x,y`
- 逗号分隔参数拆分

设计目的：

- 收口当前 `AT` 相关代码中分散的 `strlen`、`strtol`、`strcpy`、`snprintf`、`strchr`
- 把参数长度检查、结束符检查、越界检查集中在一个模块中
- 让协议层更容易满足 MISRA 风格的显式边界处理

## FOC 算法层重构设计

### 保留内容

继续保留：

- `FOC_Clarke`
- `FOC_Park`
- `FOC_InvPark`
- `FOC_LimitAlphaBeta`
- `FOC_AlphaBetaToDuty`

### 调整内容

- 明确常量定义与后缀
- 显式处理浮点限幅
- 对浮点转寄存器比较值的路径进行边界约束
- 尽量避免直接在公开头文件中泄露不必要的实现细节

### 接口边界

`foc.h` 继续保留浮点结构体作为算法层输入输出，但这些结构体只在算法层和控制层之间流动，不直接作为 `AT` 或应用层公开配置格式。

## FOC 控制层重构设计

### 控制模式

新增受控模式枚举，至少包含：

- `FOC_CONTROL_MODE_STOP`
- `FOC_CONTROL_MODE_OPENLOOP`
- `FOC_CONTROL_MODE_STATIC_VECTOR`

设计目的：

- 收口当前“静态矢量输出”和“开环连续旋转”两条路径
- 避免 `AT` 层与开环任务并发写同一 PWM 资源

### 对外接口

控制层对外公开接口改为整数/定点风格，例如：

```c
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
```

以上为设计目标，实际命名允许在实施时做等价收敛，但必须保留以下原则：

- 公开接口不直接接收浮点参数
- 所有设置接口都返回状态码
- 查询接口通过输出指针返回数据并检查空指针

### 内部状态

控制层内部维护单一状态结构，包含：

- 当前控制模式
- 当前运行状态
- 开环角度
- 目标频率 `mHz`
- 目标 `dq` 千分比
- 目标静态矢量千分比
- PWM 已启动标记

内部结构仅允许 `foc_openloop.c` 本文件访问。

### 数据转换

控制层在任务上下文中完成以下转换：

- `mHz -> Hz`
- `permille -> float`
- 整数模式请求 -> 算法层浮点输入

这样可以确保：

- 公开接口保持整数化
- 浮点只出现在内部运行路径
- 转换逻辑可集中限幅

### 单写资源约束

本次重构后：

- `AT_Server_Port_Handle()` 不直接调用 `FOC_Port_StartPwm()`
- `AT_Server_Port_Handle()` 不直接调用 `FOC_Port_StopPwm()`
- `AT_Server_Port_Handle()` 不直接调用 `FOC_Port_OutputAlphaBeta()`

上述三个动作仅由 `FOC_OpenLoop_Task()` 根据控制模式统一执行。

## FOC 端口层重构设计

### 显式硬件配置

`foc_port` 重构为显式配置式初始化，避免使用外部 `extern` 句柄。

计划新增配置结构，例如：

```c
typedef struct
{
    TIM_HandleTypeDef *timer;
    uint32_t channel_u;
    uint32_t channel_v;
    uint32_t channel_w;
} FOC_Port_Config;
```

可根据实现需要增加互补输出或其他字段，但核心要求不变：

- 端口层通过配置结构获得硬件资源
- 初始化后缓存受控配置
- 后续操作前检查配置是否有效

### 接口职责

`foc_port` 负责：

- 端口初始化
- PWM 启动
- PWM 停止
- 三相占空比更新
- `alpha-beta` 输出下发

`foc_port` 不负责：

- 电角度推进
- AT 命令解析
- 控制模式切换

### 输出安全性

对于比较值下发路径，必须保证：

- 占空比先限幅到合法区间
- 浮点转整数时采用显式转换
- 停止输出时比较值清零

## AT 通用层重构设计

### 目标

在保持现有协议行为基本不变的前提下，收紧状态机、返回值和字符串处理方式。

### 重点调整

- 统一对外返回值语义
- 用受控文本工具替代散落的字符串处理
- 保持状态机仍然只接受以 `\r\n` 结束的一条完整命令
- 保持半包超时丢弃机制

### 保持的协议行为

继续支持：

- `AT`
- `AT+FOCSTART`
- `AT+FOCSTOP`
- `AT+FOCFREQ=<mHz>`
- `AT+FOCFREQ?`
- `AT+FOCDQ=<vd_permille>,<vq_permille>`
- `AT+FOCDQ?`
- `AT+FOCVAB=<alpha_permille>,<beta_permille>`
- `AT+FOCVAB?`
- `AT+FOCSTATE?`

### 响应语义

框架仍统一输出：

- `OK`
- `ERROR`
- `+HEAD:DATA`

但业务层和框架层之间的内部处理结果需使用具名枚举区分。

## AT 端口层重构设计

### 目标

把 AT 请求转换成控制层的整数请求，不直接执行硬件动作。

### 命令处理原则

- `FOCSTART`：只提交进入开环模式的请求
- `FOCSTOP`：只提交停止请求
- `FOCFREQ`：只更新目标频率
- `FOCDQ`：只更新目标 `dq` 请求
- `FOCVAB`：只更新静态矢量请求，并切换到静态模式
- `FOCSTATE?`：只查询控制层状态

### 缓存状态

`at_server_port.c` 中保留的静态缓存仅用于：

- 保存最近一次查询要返回的整数参数
- 保存 DMA 接收缓冲区
- 保存环形缓冲区与行缓存

不再让该模块承担“直接写 PWM”的职责。

## `main.c` 用户区重构设计

### 目标

把应用入口层限制为“配置与连接”，不承担业务逻辑。

### 初始化流程

建议用户区按以下顺序组织：

1. 初始化 HAL 与系统时钟
2. 初始化 GPIO、DMA、USART、TIM 等外设
3. 构造 `FOC_Port_Config`
4. 初始化 `FOC_Port`
5. 初始化 `FOC_OpenLoop`
6. 初始化 `AT_Server_Port`
7. 初始化 `AT_Server`
8. 创建任务

### 错误处理边界

初始化阶段如果端口绑定或协议初始化失败，可由 `main.c` 决定进入 `Error_Handler()`。

运行阶段的普通控制失败应优先返回错误状态，不建议在业务路径中直接进入不可恢复死循环。

## MISRA 风格落地规则

本次实施时应遵循以下约束：

- 使用明确宽度整数类型
- 禁止在业务语义中依赖裸字面值真值
- 所有对外接口参数都进行空指针与范围检查
- 返回值必须被显式处理
- 减少隐式类型提升与隐式符号变化
- 控制流应保持显式、可追踪
- 中文注释至少说明函数功能、参数含义和返回值语义

## 预期收益

完成本次重构后，工程将获得以下收益：

- 自有模块接口风格统一
- `AT`、控制、算法、硬件访问边界更加清晰
- PWM 资源写入路径唯一化
- 公开接口整数化，降低协议层与端口层的浮点使用范围
- 更适合后续继续补充静态分析配置与偏差记录

## 结论

本次推荐采用方案 B，即“在自有代码范围内执行更严格的 MISRA 风格重构，并将公开接口尽量改为整数/定点语义”。

该方案虽然改动面大于最小修补方案，但更适合当前工程后续继续演进：

- 能解决当前 AT 层直接写 PWM 的边界混乱问题
- 能减少公开接口中的浮点暴露
- 能为后续接入静态分析和继续扩展 FOC 功能打下更稳定的结构基础
