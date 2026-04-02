#include "at_server_port.h"

#include <string.h>

static UART_HandleTypeDef* s_at_uart = NULL;
static uint8_t s_rx_dma_buffer[AT_SERVER_PORT_RX_DMA_BUFFER_SIZE];
static uint8_t s_ring_buffer_storage[AT_SERVER_PORT_RING_BUFFER_SIZE];
static char s_line_buffer[AT_SERVER_PORT_LINE_BUFFER_SIZE];

static int AT_Server_Port_Write(const uint8_t* data, size_t len, void* context) {
    UART_HandleTypeDef* huart = (UART_HandleTypeDef*)context;

    if ((huart == NULL) || (len == 0U) || (len > 0xFFFFU)) {
        return -1;
    }

    if (HAL_UART_Transmit(huart, (uint8_t*)data, (uint16_t)len, 100U) != HAL_OK) {
        return -1;
    }

    return 0;
}

static uint32_t AT_Server_Port_GetTick(void* context) {
    (void)context;
    return HAL_GetTick();
}

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

void AT_Server_Port_Init(UART_HandleTypeDef* huart) {
    s_at_uart = huart;
    s_at_server_config.write_context = huart;
    s_at_server_config.handle_context = huart;

    memset(s_rx_dma_buffer, 0, sizeof(s_rx_dma_buffer));
    memset(s_ring_buffer_storage, 0, sizeof(s_ring_buffer_storage));
    memset(s_line_buffer, 0, sizeof(s_line_buffer));

    __HAL_UART_CLEAR_IDLEFLAG(huart);
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);

    if (HAL_UART_Receive_DMA(huart, s_rx_dma_buffer, sizeof(s_rx_dma_buffer)) != HAL_OK) {
        Error_Handler();
    }

    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
}

const AT_Server_Config* AT_Server_Port_GetConfig(void) {
    return &s_at_server_config;
}

void AT_Server_Port_HandleIdleIrq(UART_HandleTypeDef* huart) {
    uint16_t remaining;
    uint16_t received;

    if ((huart == NULL) || (huart != s_at_uart) || (huart->hdmarx == NULL)) {
        return;
    }

    remaining = (uint16_t)__HAL_DMA_GET_COUNTER(huart->hdmarx);

    if (HAL_UART_DMAStop(huart) != HAL_OK) {
        Error_Handler();
    }

    received = (uint16_t)(sizeof(s_rx_dma_buffer) - remaining);
    if (received > 0U) {
        (void)AT_Server_InputBytes(s_rx_dma_buffer, received);
    }

    memset(s_rx_dma_buffer, 0, sizeof(s_rx_dma_buffer));
    if (HAL_UART_Receive_DMA(huart, s_rx_dma_buffer, sizeof(s_rx_dma_buffer)) != HAL_OK) {
        Error_Handler();
    }

    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
}

AT_Server_HandlerResult AT_Server_Port_Handle(const AT_Server_Request* request, void* context) {
    (void)request;
    (void)context;
    return AT_SERVER_HANDLER_RESULT_UNSUPPORTED;
}
