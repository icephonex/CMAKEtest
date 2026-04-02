#ifndef AT_SERVER_PORT_H
#define AT_SERVER_PORT_H

#include "main.h"
#include "at_server.h"

#define AT_SERVER_PORT_RX_DMA_BUFFER_SIZE   64U
#define AT_SERVER_PORT_RING_BUFFER_SIZE     256U
#define AT_SERVER_PORT_LINE_BUFFER_SIZE     64U
#define AT_SERVER_PORT_PARTIAL_TIMEOUT_MS   100U

void AT_Server_Port_Init(UART_HandleTypeDef* huart);
const AT_Server_Config* AT_Server_Port_GetConfig(void);
void AT_Server_Port_HandleIdleIrq(UART_HandleTypeDef* huart);
AT_Server_HandlerResult AT_Server_Port_Handle(const AT_Server_Request* request, void* context);

#endif
