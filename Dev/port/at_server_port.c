#include "at_server_port.h"

#include <string.h>

#include "app_text.h"
#include "foc_openloop.h"

/* 当前用于 AT 通道的串口句柄 */
static UART_HandleTypeDef *s_at_uart = NULL;
/* DMA 直接写入的原始接收缓存 */
static uint8_t s_rx_dma_buffer[AT_SERVER_PORT_RX_DMA_BUFFER_SIZE];
/* 提供给 lwrb 的底层存储区 */
static uint8_t s_ring_buffer_storage[AT_SERVER_PORT_RING_BUFFER_SIZE];
/* AT 通用层用于拼接一条完整命令的缓存 */
static char s_line_buffer[AT_SERVER_PORT_LINE_BUFFER_SIZE];
/* 当前缓存的 alpha 轴静态调试电压千分比。 */
static int32_t s_foc_alpha_permille;
/* 当前缓存的 beta 轴静态调试电压千分比。 */
static int32_t s_foc_beta_permille;
/* 当前缓存的开环电频，单位毫赫兹。 */
static int32_t s_foc_freq_millihz;
/* 当前缓存的 d 轴电压千分比。 */
static int32_t s_foc_vd_permille;
/* 当前缓存的 q 轴电压千分比。 */
static int32_t s_foc_vq_permille;

/**
 * @brief 比较请求命令名是否与目标命令名一致。
 * @param request 当前解析出来的 AT 请求。
 * @param name 期望匹配的命令名。
 * @return 1 表示一致，0 表示不一致。
 */
static uint8_t AT_Server_Port_NameEquals(const AT_Server_Request *request, const char *name)
{
    uint8_t matched = APP_FALSE;

    if ((request == NULL) || (request->name == NULL) || (name == NULL))
    {
        return APP_FALSE;
    }

    if (APP_Text_EqualsLiteral(request->name, request->name_len, name, &matched) != APP_STATUS_OK)
    {
        return APP_FALSE;
    }

    return matched;
}

/**
 * @brief 查询当前缓存的 FOC 静态矢量请求并按 AT 头数据格式返回。
 * @param None
 * @return APP_STATUS_OK 表示发送成功，其他状态码表示发送失败。
 */
static APP_Status AT_Server_Port_WriteFocVector(void)
{
    char data[32];

    if (APP_Text_FormatInt32Pair(data, sizeof(data), s_foc_alpha_permille, s_foc_beta_permille) != APP_STATUS_OK)
    {
        return APP_STATUS_RANGE;
    }

    return AT_Server_WriteHeadData("FOCVAB", data);
}

/**
 * @brief 查询当前开环频率并按 AT 头数据格式返回。
 * @param None
 * @return APP_STATUS_OK 表示发送成功，其他状态码表示发送失败。
 */
static APP_Status AT_Server_Port_WriteFocFrequency(void)
{
    char data[24];
    uint32_t frequency_millihz = 0U;

    if (FOC_OpenLoop_GetFrequencyMilliHz(&frequency_millihz) != APP_STATUS_OK)
    {
        return APP_STATUS_IO;
    }

    s_foc_freq_millihz = (int32_t)frequency_millihz;
    if (APP_Text_FormatInt32(data, sizeof(data), s_foc_freq_millihz) != APP_STATUS_OK)
    {
        return APP_STATUS_RANGE;
    }

    return AT_Server_WriteHeadData("FOCFREQ", data);
}

/**
 * @brief 查询当前 dq 请求并按 AT 头数据格式返回。
 * @param None
 * @return APP_STATUS_OK 表示发送成功，其他状态码表示发送失败。
 */
static APP_Status AT_Server_Port_WriteFocDQ(void)
{
    char data[32];

    if (FOC_OpenLoop_GetVoltageDQPermille(&s_foc_vd_permille, &s_foc_vq_permille) != APP_STATUS_OK)
    {
        return APP_STATUS_IO;
    }

    if (APP_Text_FormatInt32Pair(data, sizeof(data), s_foc_vd_permille, s_foc_vq_permille) != APP_STATUS_OK)
    {
        return APP_STATUS_RANGE;
    }

    return AT_Server_WriteHeadData("FOCDQ", data);
}

/**
 * @brief 查询当前 FOC 控制模式并按 AT 头数据格式返回。
 * @param None
 * @return APP_STATUS_OK 表示发送成功，其他状态码表示发送失败。
 */
static APP_Status AT_Server_Port_WriteFocState(void)
{
    const FOC_ControlMode mode = FOC_OpenLoop_GetMode();

    if (mode == FOC_CONTROL_MODE_OPENLOOP)
    {
        return AT_Server_WriteHeadData("FOCSTATE", "RUN");
    }

    if (mode == FOC_CONTROL_MODE_STATIC_VECTOR)
    {
        return AT_Server_WriteHeadData("FOCSTATE", "STATIC");
    }

    return AT_Server_WriteHeadData("FOCSTATE", "STOP");
}

/* 通用层发送回调：统一从这里走串口发送 */
static APP_Status AT_Server_Port_Write(const uint8_t *data, size_t len, void *context)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)context;

    if ((huart == NULL) || (len == 0U) || (len > 0xFFFFU))
    {
        return APP_STATUS_INVALID_ARG;
    }

    if (HAL_UART_Transmit(huart, (uint8_t *)data, (uint16_t)len, 100U) != HAL_OK)
    {
        return APP_STATUS_HW_ERROR;
    }

    return APP_STATUS_OK;
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

APP_Status AT_Server_Port_Init(UART_HandleTypeDef *huart)
{
    APP_Status status;

    if ((huart == NULL) || (huart->hdmarx == NULL))
    {
        return APP_STATUS_INVALID_ARG;
    }

    /* 保存串口句柄，并把上下文透传给通用层回调 */
    s_at_uart = huart;
    s_at_server_config.write_context = huart;
    s_at_server_config.handle_context = huart;

    /* 上电或重新初始化时先清空各级缓存 */
    memset(s_rx_dma_buffer, 0, sizeof(s_rx_dma_buffer));
    memset(s_ring_buffer_storage, 0, sizeof(s_ring_buffer_storage));
    memset(s_line_buffer, 0, sizeof(s_line_buffer));
    s_foc_alpha_permille = 0;
    s_foc_beta_permille = 0;
    s_foc_freq_millihz = 5000;
    s_foc_vd_permille = 0;
    s_foc_vq_permille = 100;

    status = FOC_OpenLoop_SetFrequencyMilliHz((uint32_t)s_foc_freq_millihz);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = FOC_OpenLoop_SetVoltageDQPermille(s_foc_vd_permille, s_foc_vq_permille);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    /* 打开 UART 空闲中断，用于判定一批 DMA 数据接收结束 */
    __HAL_UART_CLEAR_IDLEFLAG(huart);
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);

    /* 启动 DMA 循环接收，数据先进入 DMA 缓冲区 */
    if (HAL_UART_Receive_DMA(huart, s_rx_dma_buffer, sizeof(s_rx_dma_buffer)) != HAL_OK)
    {
        return APP_STATUS_HW_ERROR;
    }

    /* 本方案只关心空闲中断切包，不使用半传输中断 */
    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
    return APP_STATUS_OK;
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
        return;
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
        return;
    }

    /* 继续保持关闭半传输中断 */
    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
}

AT_Server_HandlerResult AT_Server_Port_Handle(const AT_Server_Request *request, void *context)
{
    APP_Status status;
    int32_t parsed_value;
    int32_t first_value;
    int32_t second_value;

    (void)context;

    if (request == NULL)
    {
        return AT_SERVER_HANDLER_RESULT_ERROR;
    }

    if ((request->type == AT_SERVER_COMMAND_TYPE_CMD) &&
        (AT_Server_Port_NameEquals(request, "FOCSTART") != 0U))
    {
        return (FOC_OpenLoop_RequestStart() == APP_STATUS_OK) ? AT_SERVER_HANDLER_RESULT_OK
                                                              : AT_SERVER_HANDLER_RESULT_ERROR;
    }

    if ((request->type == AT_SERVER_COMMAND_TYPE_CMD) &&
        (AT_Server_Port_NameEquals(request, "FOCSTOP") != 0U))
    {
        return (FOC_OpenLoop_RequestStop() == APP_STATUS_OK) ? AT_SERVER_HANDLER_RESULT_OK
                                                             : AT_SERVER_HANDLER_RESULT_ERROR;
    }

    if (AT_Server_Port_NameEquals(request, "FOCFREQ") != 0U)
    {
        if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
        {
            status = APP_Text_ParseInt32(request->args, &parsed_value);
            if ((status != APP_STATUS_OK) || (parsed_value < 0))
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            status = FOC_OpenLoop_SetFrequencyMilliHz((uint32_t)parsed_value);
            if (status != APP_STATUS_OK)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            s_foc_freq_millihz = parsed_value;
            return AT_SERVER_HANDLER_RESULT_OK;
        }

        if (request->type == AT_SERVER_COMMAND_TYPE_QUERY)
        {
            return (AT_Server_Port_WriteFocFrequency() == APP_STATUS_OK) ? AT_SERVER_HANDLER_RESULT_OK
                                                                          : AT_SERVER_HANDLER_RESULT_ERROR;
        }
    }

    if (AT_Server_Port_NameEquals(request, "FOCDQ") != 0U)
    {
        if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
        {
            status = APP_Text_ParseInt32Pair(request->args, &first_value, &second_value);
            if (status != APP_STATUS_OK)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            status = FOC_OpenLoop_SetVoltageDQPermille(first_value, second_value);
            if (status != APP_STATUS_OK)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            s_foc_vd_permille = first_value;
            s_foc_vq_permille = second_value;
            return AT_SERVER_HANDLER_RESULT_OK;
        }

        if (request->type == AT_SERVER_COMMAND_TYPE_QUERY)
        {
            return (AT_Server_Port_WriteFocDQ() == APP_STATUS_OK) ? AT_SERVER_HANDLER_RESULT_OK
                                                                   : AT_SERVER_HANDLER_RESULT_ERROR;
        }
    }

    if (AT_Server_Port_NameEquals(request, "FOCVAB") != 0U)
    {
        if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
        {
            status = APP_Text_ParseInt32Pair(request->args, &first_value, &second_value);
            if (status != APP_STATUS_OK)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            status = FOC_OpenLoop_SetStaticVectorPermille(first_value, second_value);
            if (status != APP_STATUS_OK)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            s_foc_alpha_permille = first_value;
            s_foc_beta_permille = second_value;
            return AT_SERVER_HANDLER_RESULT_OK;
        }

        if (request->type == AT_SERVER_COMMAND_TYPE_QUERY)
        {
            return (AT_Server_Port_WriteFocVector() == APP_STATUS_OK) ? AT_SERVER_HANDLER_RESULT_OK
                                                                       : AT_SERVER_HANDLER_RESULT_ERROR;
        }
    }

    if ((request->type == AT_SERVER_COMMAND_TYPE_QUERY) &&
        (AT_Server_Port_NameEquals(request, "FOCSTATE") != 0U))
    {
        return (AT_Server_Port_WriteFocState() == APP_STATUS_OK) ? AT_SERVER_HANDLER_RESULT_OK
                                                                  : AT_SERVER_HANDLER_RESULT_ERROR;
    }

    return AT_SERVER_HANDLER_RESULT_UNSUPPORTED;
}
