# 当前 AT 架构说明

**同步日期：** 2026-04-02

## 概述

当前工程已经接入一条完整的 AT 数据链路：

- `USART1` 使用 DMA 接收上位机字节流
- 串口空闲中断触发后，将 DMA 缓冲区中的本批数据转交给 AT 通用层
- AT 通用层使用 `lwrb` 环形缓冲区缓存原始字节流
- FreeRTOS 任务 `task_loop` 周期性调用 `AT_Server_Poll()`
- 轮询任务逐字节完成 AT 组帧、语法识别、命令分发和响应发送

当前实现已经具备“接收 -> 缓冲 -> 解析 -> 应答”的最小闭环，并加入了“不完整命令超时丢弃”的保护策略。

## 对应代码

- `unilib/src/at_server.h`
- `unilib/src/at_server.c`
- `Dev/port/at_server_port.h`
- `Dev/port/at_server_port.c`
- `Core/Src/main.c`
- `Core/Src/stm32f1xx_it.c`
- `unilib/thirdparty/lwrb-develop/lwrb.c`

## 分层架构

当前实现分为五层：

1. 串口硬件初始化层  
   `main.c` 中完成 `USART1` 初始化，`stm32f1xx_hal_msp.c` 中完成 GPIO、DMA、USART 中断等底层资源配置。

2. DMA 接收与空闲中断搬运层  
   `USART1` 的 DMA 先把数据写入端口层的 DMA 临时缓冲区；当出现空闲中断时，中断函数只负责计算本批收到的数据长度，并把这批数据交给 AT 通用层。

3. 通用 AT 协议引擎层  
   `at_server` 负责环形缓冲、按字节组帧、`\r\n` 结束判定、命令形态识别、完整请求分发，以及统一回复 `OK` / `ERROR`。

4. 项目适配层  
   `at_server_port` 负责把通用 AT 引擎接入当前工程，包括提供发送出口、系统时基、缓冲区配置，以及项目命令处理入口。

5. FreeRTOS 轮询任务层  
   `task_loop` 在任务上下文中持续调用 `AT_Server_Poll()`，解析逻辑和响应发送都在任务上下文完成，不在中断中做协议处理。

## 文件职责

### `unilib/src/at_server.h`

定义通用 AT 引擎对外接口和共享数据结构，包括：

- `AT_Server_Config`
- `AT_Server_Request`
- `AT_Server_CommandType`
- `AT_Server_HandlerResult`
- `AT_Server_Init`
- `AT_Server_InputBytes`
- `AT_Server_Poll`
- `AT_Server_WriteOk`
- `AT_Server_WriteError`
- `AT_Server_WriteHeadData`

该头文件只定义协议引擎的使用方式，不包含项目业务逻辑。

### `unilib/src/at_server.c`

实现通用 AT 协议引擎，负责：

- 使用 `lwrb` 缓存原始字节流
- 维护当前命令行状态
- 只认可 `\r\n` 作为命令结束符
- 识别裸命令 `AT`
- 识别 `AT+NAME`、`AT+NAME?`、`AT+NAME=...`
- 将 `AT+...` 命令转交给端口层处理
- 根据处理结果统一发送 `OK\r\n` 或 `ERROR\r\n`
- 在不完整命令超时时主动丢弃残留半包

### `Dev/port/at_server_port.h`

声明项目适配层接口和当前配置宏，包括：

- DMA 临时接收缓冲区大小
- 通用层环形缓冲区大小
- 单行命令缓存大小
- 不完整命令超时时间
- 端口层初始化入口
- 空闲中断处理入口
- 项目业务命令处理入口

### `Dev/port/at_server_port.c`

实现项目适配层，当前职责包括：

- 保存当前使用的串口句柄
- 提供 `HAL_UART_Transmit` 发送回调
- 提供 `HAL_GetTick` 时基回调
- 提供 `AT_Server_Config`
- 启动 `USART1` DMA 接收并打开空闲中断
- 在空闲中断时停止 DMA、计算长度、搬运数据、重启 DMA

当前 `AT_Server_Port_Handle(...)` 仍是占位实现，统一返回 `AT_SERVER_HANDLER_RESULT_UNSUPPORTED`。

### `Core/Src/main.c`

负责系统接线：

- 初始化 `USART1`
- 调用 `AT_Server_Port_Init(&huart1)`
- 调用 `AT_Server_Init(AT_Server_Port_GetConfig())`
- 创建 FreeRTOS 任务 `task_loop`
- 在 `task_loop` 中循环调用 `AT_Server_Poll()`

### `Core/Src/stm32f1xx_it.c`

负责中断侧数据搬运：

- 检测 `USART1` 空闲中断
- 清除空闲中断标志
- 调用 `AT_Server_Port_HandleIdleIrq(&huart1)`
- 继续执行 `HAL_UART_IRQHandler(&huart1)`

## 初始化链路

当前初始化顺序如下：

1. `main()` 调用 `MX_USART1_UART_Init()` 初始化 `USART1`
2. `main()` 调用 `AT_Server_Port_Init(&huart1)`
3. 端口层启动 `HAL_UART_Receive_DMA(...)`
4. 端口层打开 `UART_IT_IDLE`
5. `main()` 调用 `AT_Server_Init(AT_Server_Port_GetConfig())`
6. 创建并启动 `task_loop`
7. `task_loop` 持续调用 `AT_Server_Poll()`

## 接收数据流

当前接收数据流如下：

1. 上位机通过 `USART1` 发送 AT 字节流。
2. DMA 将字节写入 `s_rx_dma_buffer`。
3. 串口出现空闲间隙后，触发 `USART1_IRQHandler()`。
4. 中断函数检测到 `UART_FLAG_IDLE` 后，调用 `AT_Server_Port_HandleIdleIrq(&huart1)`。
5. 端口层读取 DMA 剩余计数，计算本批收到的有效字节数。
6. 端口层调用 `HAL_UART_DMAStop()` 锁定当前这批数据。
7. 若本批有效字节数大于 0，则调用 `AT_Server_InputBytes(s_rx_dma_buffer, received)`。
8. 通用 AT 层将数据写入 `lwrb` 环形缓冲区。
9. 端口层清空 DMA 临时缓冲区，并重新调用 `HAL_UART_Receive_DMA(...)` 开启下一轮接收。
10. `task_loop` 在任务上下文中调用 `AT_Server_Poll()`。
11. `AT_Server_Poll()` 从 `lwrb` 中逐字节读取数据，并交给内部状态机处理。
12. 当状态机识别到一条完整命令时，执行语法解析、业务分发和响应发送。

## 发送数据流

当前发送路径如下：

1. `AT_Server_DispatchCompletedLine()` 判断一条完整命令已经收齐。
2. 若是裸命令 `AT`，直接调用 `AT_Server_WriteOk()`。
3. 若是 `AT+...` 命令，则先调用端口层 `AT_Server_Port_Handle(...)`。
4. 通用层根据端口层返回值统一决定发 `OK\r\n` 还是 `ERROR\r\n`。
5. 实际发送由端口层回调 `AT_Server_Port_Write(...)` 完成。
6. `AT_Server_Port_Write(...)` 当前使用阻塞式 `HAL_UART_Transmit(huart, data, len, 100U)`。

说明：

- 当前 AT 回复发送发生在任务上下文，不在中断上下文中执行。
- 当前 AT 回复没有使用 TX DMA，虽然底层 `USART1_TX DMA` 已初始化。

## 当前解析规则

当前代码实现的解析规则如下：

- 一条命令只有以 `\r\n` 结尾时才算完整。
- 裸命令 `AT\r\n` 返回 `OK\r\n`。
- 其他完整但不符合 AT 语法的命令返回 `ERROR\r\n`。
- `AT+NAME\r\n` 会被识别为 `CMD` 类型。
- `AT+NAME?\r\n` 会被识别为 `QUERY` 类型。
- `AT+NAME=VALUE\r\n` 会被识别为 `SETUP` 类型。
- 命令名只允许字母、数字和下划线。
- `AT+` 后面如果没有合法命令名，返回 `ERROR\r\n`。
- `AT+...` 语法合法但端口层返回失败或不支持时，返回 `ERROR\r\n`。

当前端口层的业务处理函数始终返回“不支持”，因此当前真正能成功返回 `OK\r\n` 的只有裸命令 `AT\r\n`。

## 重同步与丢弃规则

当前状态机除了支持拆包，也实现了明确的丢弃规则：

- 如果先收到 `AT`，后续再收到 `\r\n`，仍会被识别为同一条有效命令。
- 如果收到单独的 `\n`，当前半包立即丢弃，不产生响应。
- 如果收到 `\r` 后，下一个字节不是 `\n`，则旧半包立即丢弃。
- 旧半包被丢弃后，如果当前新字节不是换行符，则把这个新字节当作新命令起点继续尝试重同步。
- 如果连续收到多个 `\r`，状态机会只保留最后一个 `\r` 作为潜在结束符。

这意味着以下场景能按当前实现工作：

- `AT` + 后续 `\r\n`：正确识别为一条命令
- `AT\rAT\r\n`：前一个无效半包被丢弃，从第二个 `AT` 重新同步
- `AT\n\rAT\nAT\rAT`：不会误返回 `OK\r\n`

## 不完整命令超时策略

当前实现已加入不完整命令超时丢弃机制。

配置项：

- `AT_SERVER_PORT_PARTIAL_TIMEOUT_MS = 100U`

行为如下：

- 当已经收到一部分命令，但尚未组成完整 `\r\n` 时，AT 引擎会记录最近一次收到新数据的时间。
- `AT_Server_InputBytes()` 写入新数据前会检查是否已有超时残包。
- `AT_Server_Poll()` 在读环形缓冲区前后也会检查超时。
- 如果当前存在半包，且在设定超时时间内没有新的输入数据到来，则当前半包会被直接清空。

该策略用于防止旧的不完整命令长期残留，干扰后续新的命令同步。

## 缓冲区与配置

当前端口层配置如下：

- DMA 临时接收缓冲区：`AT_SERVER_PORT_RX_DMA_BUFFER_SIZE = 64`
- 通用层环形缓冲区：`AT_SERVER_PORT_RING_BUFFER_SIZE = 256`
- 单行命令缓存：`AT_SERVER_PORT_LINE_BUFFER_SIZE = 64`
- 半包超时丢弃时间：`AT_SERVER_PORT_PARTIAL_TIMEOUT_MS = 100 ms`

当前串口配置：

- 串口：`USART1`
- 波特率：`115200`
- 数据位：`8`
- 停止位：`1`
- 校验：`None`

当前 DMA 相关策略：

- RX 使用 DMA 接收
- 空闲中断作为一批数据结束标志
- DMA 半传输中断被显式关闭

## 异常处理策略

### 环形缓冲区写满

如果 `AT_Server_InputBytes()` 不能把本批数据完整写入 `lwrb`：

- 当前实现会直接 `lwrb_reset(...)`
- 同时清空当前命令行状态

也就是说，当前策略不是“保留一部分数据”，而是“当前待解析数据整体失效，重新开始同步”。

### 单行命令过长

如果当前命令长度超过单行缓存上限：

- 状态机会先打上溢出标记
- 后续继续等待这一行闭合
- 当这条过长命令最终以 `\r\n` 闭合时，统一回复 `ERROR\r\n`
- 随后清空当前行状态

### 空闲中断收到 0 字节

如果空闲中断触发时计算得到本批接收长度为 0：

- 不向 AT 通用层送数据
- 直接重新启动 DMA 接收

## `AT_Server_WriteHeadData()` 的当前语义

当前代码中提供了 `AT_Server_WriteHeadData(head, data)` 辅助函数，其行为是：

- 发送 `+`
- 发送 `head`
- 发送 `:`
- 发送 `data`
- 最后发送 `\r\n`

它当前只负责输出一条 `+HEAD:DATA\r\n` 风格的数据行，本身不会自动追加 `OK\r\n`。

如果后续某个业务命令要输出：

```text
+HEAD:DATA\r\n
OK\r\n
```

则通常应由业务处理函数先调用 `AT_Server_WriteHeadData(...)`，再返回成功，让通用层继续补发 `OK\r\n`。

## 当前限制

当前实现仍有以下限制：

- 仅接入 `USART1`
- 仅有一个全局静态 AT 引擎实例
- 当前只有裸命令 `AT` 能成功返回 `OK`
- `AT+...` 虽然已经具备语法解析和分发能力，但业务处理仍未实现
- 当前采用轮询任务，不是事件驱动唤醒
- 当前发送路径使用阻塞式 `HAL_UART_Transmit`

## 当前结论

截至当前代码状态，AT 架构已经形成如下闭环：

- `USART1 DMA` 接收
- 空闲中断切批
- `lwrb` 环形缓冲
- FreeRTOS 任务轮询
- 通用 AT 状态机解析
- 端口层统一接入
- 阻塞式串口回包

这是一个“结构已搭好、最小链路已跑通、业务命令待补充”的当前版本架构。
