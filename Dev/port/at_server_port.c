#include "at_server_port.h"

#include <string.h>

/* 当前用于 AT 通道的串口句柄 */
static UART_HandleTypeDef *s_at_uart = NULL;
/* DMA 直接写入的原始接收缓存 */
static uint8_t s_rx_dma_buffer[AT_SERVER_PORT_RX_DMA_BUFFER_SIZE];
/* 提供给 lwrb 的底层存储区 */
static uint8_t s_ring_buffer_storage[AT_SERVER_PORT_RING_BUFFER_SIZE];
/* AT 通用层用于拼接一条完整命令的缓存 */
static char s_line_buffer[AT_SERVER_PORT_LINE_BUFFER_SIZE];

/* 通用层发送回调：统一从这里走串口发送 */
static int AT_Server_Port_Write(const uint8_t *data, size_t len, void *context)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)context;

    if ((huart == NULL) || (len == 0U) || (len > 0xFFFFU))
    {
        return -1;
    }

    if (HAL_UART_Transmit(huart, (uint8_t *)data, (uint16_t)len, 100U) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

/* 通用层获取时基回调：直接复用 HAL 毫秒节拍 */
static uint32_t AT_Server_Port_GetTick(void *context)
{
    (void)context;
    return HAL_GetTick();
}

/* 端口层给通用 AT 引擎提供的静态配置 */
static AT_Server_Config s_at_server_config = {
    .ring_buffer_storage = s_ring_buffer_storage,
    .ring_buffer_size = sizeof(s_ring_buffer_storage),
    .line_buffer = s_line_buffer,
    .line_buffer_size = sizeof(s_line_buffer),
    .write = AT_Server_Port_Write,
    .write_context = NULL,
    .handle = AT_Server_Port_Handle,
    .handle_context = NULL,
    .get_tick = AT_Server_Port_GetTick,
    .tick_context = NULL,
    .partial_timeout_ms = AT_SERVER_PORT_PARTIAL_TIMEOUT_MS,
};

void AT_Server_Port_Init(UART_HandleTypeDef *huart)
{
    /* 保存串口句柄，并把上下文透传给通用层回调 */
    s_at_uart = huart;
    s_at_server_config.write_context = huart;
    s_at_server_config.handle_context = huart;

    /* 上电或重新初始化时先清空各级缓存 */
    memset(s_rx_dma_buffer, 0, sizeof(s_rx_dma_buffer));
    memset(s_ring_buffer_storage, 0, sizeof(s_ring_buffer_storage));
    memset(s_line_buffer, 0, sizeof(s_line_buffer));

    /* 打开 UART 空闲中断，用于判定一批 DMA 数据接收结束 */
    __HAL_UART_CLEAR_IDLEFLAG(huart);
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);

    /* 启动 DMA 循环接收，数据先进入 DMA 缓冲区 */
    if (HAL_UART_Receive_DMA(huart, s_rx_dma_buffer, sizeof(s_rx_dma_buffer)) != HAL_OK)
    {
        Error_Handler();
    }

    /* 本方案只关心空闲中断切包，不使用半传输中断 */
    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
}

const AT_Server_Config *AT_Server_Port_GetConfig(void)
{
    return &s_at_server_config;
}

void AT_Server_Port_HandleIdleIrq(UART_HandleTypeDef *huart)
{
    uint16_t remaining;
    uint16_t received;

    if ((huart == NULL) || (huart != s_at_uart) || (huart->hdmarx == NULL))
    {
        return;
    }

    /* 先读取 DMA 剩余计数，计算本次空闲前一共收到多少字节 */
    remaining = (uint16_t)__HAL_DMA_GET_COUNTER(huart->hdmarx);

    /* 停止 DMA，锁定当前这批数据长度 */
    if (HAL_UART_DMAStop(huart) != HAL_OK)
    {
        Error_Handler();
    }

    received = (uint16_t)(sizeof(s_rx_dma_buffer) - remaining);
    if (received > 0U)
    {
        /* 把本批数据整体写入通用层的环形缓冲区，后续由任务轮询解析 */
        (void)AT_Server_InputBytes(s_rx_dma_buffer, received);
    }

    /* 清空 DMA 临时缓冲区并重新开启下一轮接收 */
    memset(s_rx_dma_buffer, 0, sizeof(s_rx_dma_buffer));
    if (HAL_UART_Receive_DMA(huart, s_rx_dma_buffer, sizeof(s_rx_dma_buffer)) != HAL_OK)
    {
        Error_Handler();
    }

    /* 继续保持关闭半传输中断 */
    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
}

AT_Server_HandlerResult AT_Server_Port_Handle(const AT_Server_Request *request, void *context)
{
    /* 这里是项目业务适配层，后续可根据命令名和类型补充具体处理逻辑 */
    (void)request;
    (void)context;
    return AT_SERVER_HANDLER_RESULT_UNSUPPORTED;
}
