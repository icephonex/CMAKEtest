#include "at_server_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "foc_openloop.h"
#include "foc_port.h"

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
/* 当前是否处于静态矢量调试输出模式。 */
static uint8_t s_foc_manual_output_enabled;

/**
 * @brief 比较请求命令名是否与目标命令名一致。
 * @param request 当前解析出来的 AT 请求。
 * @param name 期望匹配的命令名。
 * @return 1 表示一致，0 表示不一致。
 */
static uint8_t AT_Server_Port_NameEquals(const AT_Server_Request *request, const char *name)
{
    size_t name_len;

    if ((request == NULL) || (request->name == NULL) || (name == NULL))
    {
        return 0U;
    }

    name_len = strlen(name);
    if (request->name_len != name_len)
    {
        return 0U;
    }

    return (uint8_t)(memcmp(request->name, name, name_len) == 0 ? 1U : 0U);
}

/**
 * @brief 将字符串解析为有符号 32 位整数。
 * @param text 输入字符串起始地址。
 * @param value 输出的解析结果。
 * @return 0 表示解析成功，-1 表示解析失败。
 */
static int AT_Server_Port_ParseInt32(const char *text, int32_t *value)
{
    char *end_ptr;
    long parsed;

    if ((text == NULL) || (value == NULL) || (*text == '\0'))
    {
        return -1;
    }

    parsed = strtol(text, &end_ptr, 10);
    if ((*end_ptr != '\0') || (parsed < (long)INT32_MIN) || (parsed > (long)INT32_MAX))
    {
        return -1;
    }

    *value = (int32_t)parsed;
    return 0;
}

/**
 * @brief 解析 "x,y" 形式的两个有符号整数参数。
 * @param args 输入参数字符串。
 * @param first_value 输出的第一个整数。
 * @param second_value 输出的第二个整数。
 * @return 0 表示解析成功，-1 表示解析失败。
 */
static int AT_Server_Port_ParseInt32Pair(const char *args, int32_t *first_value, int32_t *second_value)
{
    const char *comma;
    size_t first_len;
    char first_buffer[16];
    char second_buffer[16];

    if ((args == NULL) || (first_value == NULL) || (second_value == NULL))
    {
        return -1;
    }

    comma = strchr(args, ',');
    if ((comma == NULL) || (comma == args) || (comma[1] == '\0'))
    {
        return -1;
    }

    first_len = (size_t)(comma - args);
    if ((first_len >= sizeof(first_buffer)) || (strlen(comma + 1U) >= sizeof(second_buffer)))
    {
        return -1;
    }

    memcpy(first_buffer, args, first_len);
    first_buffer[first_len] = '\0';
    strcpy(second_buffer, comma + 1U);

    if ((AT_Server_Port_ParseInt32(first_buffer, first_value) != 0) ||
        (AT_Server_Port_ParseInt32(second_buffer, second_value) != 0))
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 把当前缓存的千分比电压请求转换为 alpha-beta 浮点量。
 * @param None
 * @return 当前缓存的 alpha-beta 电压矢量。
 */
static FOC_AlphaBeta AT_Server_Port_GetFocVector(void)
{
    FOC_AlphaBeta v_ab;

    v_ab.alpha = (float)s_foc_alpha_permille / 1000.0f;
    v_ab.beta = (float)s_foc_beta_permille / 1000.0f;

    return v_ab;
}

/**
 * @brief 查询当前缓存的 FOC 电压请求并按 AT 头数据格式返回。
 * @param None
 * @return 0 表示发送成功，-1 表示发送失败。
 */
static int AT_Server_Port_WriteFocVector(void)
{
    char data[32];

    (void)snprintf(data, sizeof(data), "%ld,%ld", (long)s_foc_alpha_permille, (long)s_foc_beta_permille);
    return AT_Server_WriteHeadData("FOCVAB", data);
}

/**
 * @brief 查询当前 FOC PWM 运行状态并按 AT 头数据格式返回。
 * @param None
 * @return 0 表示发送成功，-1 表示发送失败。
 */
static int AT_Server_Port_WriteFocState(void)
{
    if (FOC_OpenLoop_IsRunning() != 0U)
    {
        return AT_Server_WriteHeadData("FOCSTATE", "RUN");
    }

    if (s_foc_manual_output_enabled != 0U)
    {
        return AT_Server_WriteHeadData("FOCSTATE", "STATIC");
    }

    return AT_Server_WriteHeadData("FOCSTATE", "STOP");
}

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
    s_foc_alpha_permille = 0;
    s_foc_beta_permille = 0;
    s_foc_freq_millihz = 5000;
    s_foc_vd_permille = 0;
    s_foc_vq_permille = 100;
    s_foc_manual_output_enabled = 0U;

    FOC_OpenLoop_SetFrequencyHz((float)s_foc_freq_millihz / 1000.0f);
    FOC_OpenLoop_SetVoltageDQ((float)s_foc_vd_permille / 1000.0f,
                              (float)s_foc_vq_permille / 1000.0f);

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
    (void)context;

    if (request == NULL)
    {
        return AT_SERVER_HANDLER_RESULT_ERROR;
    }

    if ((request->type == AT_SERVER_COMMAND_TYPE_CMD) &&
        (AT_Server_Port_NameEquals(request, "FOCSTART") != 0U))
    {
        if (s_foc_manual_output_enabled != 0U)
        {
            FOC_Port_StopPwm();
            s_foc_manual_output_enabled = 0U;
        }

        FOC_OpenLoop_RequestStart();
        return AT_SERVER_HANDLER_RESULT_OK;
    }

    if ((request->type == AT_SERVER_COMMAND_TYPE_CMD) &&
        (AT_Server_Port_NameEquals(request, "FOCSTOP") != 0U))
    {
        FOC_OpenLoop_RequestStop();

        if (s_foc_manual_output_enabled != 0U)
        {
            FOC_Port_StopPwm();
            s_foc_manual_output_enabled = 0U;
        }

        return AT_SERVER_HANDLER_RESULT_OK;
    }

    if (AT_Server_Port_NameEquals(request, "FOCFREQ") != 0U)
    {
        if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
        {
            if (AT_Server_Port_ParseInt32(request->args, &s_foc_freq_millihz) != 0)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            if (s_foc_freq_millihz < 0)
            {
                s_foc_freq_millihz = 0;
            }

            FOC_OpenLoop_SetFrequencyHz((float)s_foc_freq_millihz / 1000.0f);
            return AT_SERVER_HANDLER_RESULT_OK;
        }

        if (request->type == AT_SERVER_COMMAND_TYPE_QUERY)
        {
            char data[24];

            (void)snprintf(data, sizeof(data), "%ld", (long)s_foc_freq_millihz);
            return (AT_Server_WriteHeadData("FOCFREQ", data) == 0) ? AT_SERVER_HANDLER_RESULT_OK
                                                                   : AT_SERVER_HANDLER_RESULT_ERROR;
        }
    }

    if (AT_Server_Port_NameEquals(request, "FOCDQ") != 0U)
    {
        if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
        {
            if (AT_Server_Port_ParseInt32Pair(request->args, &s_foc_vd_permille, &s_foc_vq_permille) != 0)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            FOC_OpenLoop_SetVoltageDQ((float)s_foc_vd_permille / 1000.0f,
                                      (float)s_foc_vq_permille / 1000.0f);
            return AT_SERVER_HANDLER_RESULT_OK;
        }

        if (request->type == AT_SERVER_COMMAND_TYPE_QUERY)
        {
            char data[32];

            (void)snprintf(data, sizeof(data), "%ld,%ld",
                           (long)s_foc_vd_permille,
                           (long)s_foc_vq_permille);
            return (AT_Server_WriteHeadData("FOCDQ", data) == 0) ? AT_SERVER_HANDLER_RESULT_OK
                                                                 : AT_SERVER_HANDLER_RESULT_ERROR;
        }
    }

    if (AT_Server_Port_NameEquals(request, "FOCVAB") != 0U)
    {
        if (request->type == AT_SERVER_COMMAND_TYPE_SETUP)
        {
            if (AT_Server_Port_ParseInt32Pair(request->args, &s_foc_alpha_permille, &s_foc_beta_permille) != 0)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            if (FOC_OpenLoop_IsRunning() != 0U)
            {
                return AT_SERVER_HANDLER_RESULT_ERROR;
            }

            FOC_Port_OutputAlphaBeta(AT_Server_Port_GetFocVector());
            FOC_Port_StartPwm();
            s_foc_manual_output_enabled = 1U;

            return AT_SERVER_HANDLER_RESULT_OK;
        }

        if (request->type == AT_SERVER_COMMAND_TYPE_QUERY)
        {
            return (AT_Server_Port_WriteFocVector() == 0) ? AT_SERVER_HANDLER_RESULT_OK
                                                          : AT_SERVER_HANDLER_RESULT_ERROR;
        }
    }

    if ((request->type == AT_SERVER_COMMAND_TYPE_QUERY) &&
        (AT_Server_Port_NameEquals(request, "FOCSTATE") != 0U))
    {
        return (AT_Server_Port_WriteFocState() == 0) ? AT_SERVER_HANDLER_RESULT_OK
                                                     : AT_SERVER_HANDLER_RESULT_ERROR;
    }

    return AT_SERVER_HANDLER_RESULT_UNSUPPORTED;
}
