#ifndef AT_SERVER_PORT_H
#define AT_SERVER_PORT_H

#include "app_base.h"
#include "main.h"
#include "at_server.h"

/* USART DMA 临时接收缓冲区大小，空闲中断触发后整体搬运 */
#define AT_SERVER_PORT_RX_DMA_BUFFER_SIZE 64U
/* AT 通用层使用的环形缓冲区大小 */
#define AT_SERVER_PORT_RING_BUFFER_SIZE 256U
/* 单条命令行缓存大小 */
#define AT_SERVER_PORT_LINE_BUFFER_SIZE 64U
/* 不完整命令超时丢弃时间 */
#define AT_SERVER_PORT_PARTIAL_TIMEOUT_MS 100U

/* 绑定串口、启动 DMA 接收并打开空闲中断 */
APP_Status AT_Server_Port_Init(UART_HandleTypeDef *huart);
/* 返回通用 AT 引擎初始化所需配置 */
const AT_Server_Config *AT_Server_Port_GetConfig(void);
/* 在串口空闲中断中调用，把 DMA 缓冲区数据转存到 AT 通用层 */
void AT_Server_Port_HandleIdleIrq(UART_HandleTypeDef *huart);
/* 项目业务处理入口，具体命令功能在这里实现 */
AT_Server_HandlerResult AT_Server_Port_Handle(const AT_Server_Request *request, void *context);

#endif
