#include "at_server.h"

#include "app_text.h"
#include "lwrb/lwrb.h"

/* AT 协议引擎内部运行状态 */
typedef struct
{
    /* 运行配置，由端口层在初始化时传入 */
    AT_Server_Config config;
    /* 原始字节流环形缓冲区 */
    lwrb_t ring_buffer;
    /* 当前已拼接的行长度 */
    size_t line_length;
    /* 记录是否刚收到 '\r'，等待下一个字节判断是否为 "\r\n" */
    APP_Bool pending_cr;
    /* 行缓存溢出标记，等到行结束后统一按错误处理 */
    APP_Bool line_overflow;
    /* 初始化完成标记 */
    APP_Bool ready;
    /* 最近一次成功写入输入数据的时刻 */
    uint32_t last_input_tick;
} AT_Server_State;

static AT_Server_State s_at_server;

/* 清空当前正在拼接的命令行状态 */
static void AT_Server_ClearLine(void)
{
    s_at_server.line_length = 0U;
    s_at_server.pending_cr = APP_FALSE;
    s_at_server.line_overflow = APP_FALSE;
    if ((s_at_server.config.line_buffer != NULL) && (s_at_server.config.line_buffer_size > 0U))
    {
        s_at_server.config.line_buffer[0] = '\0';
    }
}

/* 当前是否存在尚未组成完整 "\r\n" 结尾的残留命令 */
static APP_Bool AT_Server_HasPartialLine(void)
{
    APP_Bool has_partial_line = APP_FALSE;

    if ((s_at_server.line_length > 0U) || (s_at_server.pending_cr != APP_FALSE) ||
        (s_at_server.line_overflow != APP_FALSE))
    {
        has_partial_line = APP_TRUE;
    }

    return has_partial_line;
}

/* 允许作为命令名的字符：字母、数字和下划线 */
static APP_Bool AT_Server_IsNameChar(char ch)
{
    APP_Bool is_name_char = APP_FALSE;

    if ((((ch >= 'A') && (ch <= 'Z')) || ((ch >= 'a') && (ch <= 'z')) ||
         ((ch >= '0') && (ch <= '9')) || (ch == '_')))
    {
        is_name_char = APP_TRUE;
    }

    return is_name_char;
}

/* 获取以 '\0' 结束字符串的长度 */
static size_t AT_Server_StringLength(const char *text)
{
    size_t length = 0U;

    if (text != NULL)
    {
        while (text[length] != '\0')
        {
            length++;
        }
    }

    return length;
}

/* 通过端口层发送固定字符串响应 */
static APP_Status AT_Server_WriteLiteral(const char *text)
{
    APP_Status status = APP_STATUS_OK;
    const size_t len = AT_Server_StringLength(text);

    if ((text == NULL) || (s_at_server.config.write == NULL) || (len == 0U))
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else
    {
        status = s_at_server.config.write((const uint8_t *)text, len, s_at_server.config.write_context);
    }

    return status;
}

/* 超时仍未收完整的 AT 命令直接丢弃，避免旧半包影响后续新命令 */
static void AT_Server_DropPartialIfTimedOut(void)
{
    uint32_t now;
    APP_Bool should_check_timeout = APP_FALSE;

    if ((s_at_server.ready != APP_FALSE) && (s_at_server.config.get_tick != NULL) &&
        (s_at_server.config.partial_timeout_ms > 0U) && (AT_Server_HasPartialLine() != APP_FALSE))
    {
        should_check_timeout = APP_TRUE;
    }

    if (should_check_timeout != APP_FALSE)
    {
        now = s_at_server.config.get_tick(s_at_server.config.tick_context);
        if ((uint32_t)(now - s_at_server.last_input_tick) >= s_at_server.config.partial_timeout_ms)
        {
            AT_Server_ClearLine();
        }
    }
}

/* 向当前行缓存追加一个字节，预留末尾 '\0' 空间 */
static void AT_Server_AppendByte(uint8_t ch)
{
    if (s_at_server.line_overflow == APP_FALSE)
    {
        if ((s_at_server.config.line_buffer == NULL) || (s_at_server.config.line_buffer_size < 2U) ||
            (s_at_server.line_length >= (s_at_server.config.line_buffer_size - 1U)))
        {
            s_at_server.line_overflow = APP_TRUE;
        }
        else
        {
            s_at_server.config.line_buffer[s_at_server.line_length++] = (char)ch;
            s_at_server.config.line_buffer[s_at_server.line_length] = '\0';
        }
    }
}

APP_Status AT_Server_WriteOk(void)
{
    return AT_Server_WriteLiteral("OK\r\n");
}

APP_Status AT_Server_WriteError(void)
{
    return AT_Server_WriteLiteral("ERROR\r\n");
}

APP_Status AT_Server_WriteHeadData(const char *head, const char *data)
{
    APP_Status status = APP_STATUS_OK;
    const char *local_head = head;
    const char *local_data = data;

    if (local_head == NULL)
    {
        local_head = "";
    }
    if (local_data == NULL)
    {
        local_data = "";
    }

    status = AT_Server_WriteLiteral("+");
    if (status == APP_STATUS_OK)
    {
        status = AT_Server_WriteLiteral(local_head);
        if (status == APP_STATUS_OK)
        {
            status = AT_Server_WriteLiteral(":");
            if (status == APP_STATUS_OK)
            {
                status = AT_Server_WriteLiteral(local_data);
                if (status == APP_STATUS_OK)
                {
                    status = AT_Server_WriteLiteral("\r\n");
                }
            }
        }
    }

    if (status != APP_STATUS_OK)
    {
        status = APP_STATUS_IO;
    }

    return status;
}

/* 一条命令以 "\r\n" 收完整后，在这里完成语法判断和业务分发 */
static void AT_Server_DispatchCompletedLine(void)
{
    AT_Server_Request request;
    char *line = NULL;
    char *cursor = NULL;
    AT_Server_HandlerResult result = AT_SERVER_HANDLER_RESULT_ERROR;
    APP_Bool matched = APP_FALSE;
    APP_Status status = APP_STATUS_OK;
    APP_Bool send_ok = APP_FALSE;
    APP_Bool send_error = APP_FALSE;
    APP_Bool dispatch_ready = APP_FALSE;

    request.name = NULL;
    request.name_len = 0U;
    request.args = NULL;
    request.args_len = 0U;
    request.type = AT_SERVER_COMMAND_TYPE_CMD;

    if (s_at_server.line_overflow != APP_FALSE)
    {
        /* 行过长视为非法命令 */
        send_error = APP_TRUE;
    }
    else if (s_at_server.line_length > 0U)
    {
        line = s_at_server.config.line_buffer;
        line[s_at_server.line_length] = '\0';

        status = APP_Text_EqualsLiteral(line, s_at_server.line_length, "AT", &matched);
        if ((status == APP_STATUS_OK) && (matched != APP_FALSE))
        {
            /* 裸 AT 作为链路探活命令，直接回复 OK */
            send_ok = APP_TRUE;
        }
        else if ((s_at_server.line_length < 3U) || (line[0] != 'A') || (line[1] != 'T') || (line[2] != '+'))
        {
            /* 非 "AT+" 前缀的完整行直接丢弃并回复错误 */
            send_error = APP_TRUE;
        }
        else if (s_at_server.config.handle == NULL)
        {
            send_error = APP_TRUE;
        }
        else
        {
            cursor = line + 3;
            request.name = cursor;

            /* 截取命令名，遇到 '?', '=' 或字符串结束即停止 */
            while (AT_Server_IsNameChar(*cursor) != APP_FALSE)
            {
                cursor++;
            }

            request.name_len = (size_t)(cursor - request.name);
            if (request.name_len == 0U)
            {
                send_error = APP_TRUE;
            }
            else if (*cursor == '\0')
            {
                request.type = AT_SERVER_COMMAND_TYPE_CMD;
                dispatch_ready = APP_TRUE;
            }
            else if ((*cursor == '?') && (cursor[1] == '\0'))
            {
                /* 查询命令必须以 '?' 结束 */
                request.type = AT_SERVER_COMMAND_TYPE_QUERY;
                *cursor = '\0';
                dispatch_ready = APP_TRUE;
            }
            else if (*cursor == '=')
            {
                /* 设置命令允许 '=' 后跟任意参数内容 */
                request.type = AT_SERVER_COMMAND_TYPE_SETUP;
                *cursor = '\0';
                request.args = cursor + 1;
                request.args_len = AT_Server_StringLength(request.args);
                dispatch_ready = APP_TRUE;
            }
            else
            {
                /* 其他尾部格式均视为非法 */
                send_error = APP_TRUE;
            }
        }
    }

    if (dispatch_ready != APP_FALSE)
    {
        /* 业务层只返回处理结果，响应格式由通用层统一封装 */
        result = s_at_server.config.handle(&request, s_at_server.config.handle_context);
        if (result == AT_SERVER_HANDLER_RESULT_OK)
        {
            send_ok = APP_TRUE;
        }
        else
        {
            send_error = APP_TRUE;
        }
    }

    if (send_ok != APP_FALSE)
    {
        (void)AT_Server_WriteOk();
    }
    else if (send_error != APP_FALSE)
    {
        (void)AT_Server_WriteError();
    }

    AT_Server_ClearLine();
}

/* 按字节驱动状态机，只认可以 "\r\n" 结尾的一条完整 AT 命令 */
static void AT_Server_ProcessByte(uint8_t ch)
{
    APP_Bool handled = APP_FALSE;

    if (s_at_server.pending_cr != APP_FALSE)
    {
        s_at_server.pending_cr = APP_FALSE;
        if (ch == (uint8_t)'\n')
        {
            /* 成功匹配到 "\r\n"，当前行收包完成 */
            AT_Server_DispatchCompletedLine();
            handled = APP_TRUE;
        }
        else
        {
            /* 收到单独 '\r' 但后面不是 '\n'，按规则丢弃旧半包，从新字节重新开始 */
            AT_Server_ClearLine();

            if (ch == (uint8_t)'\r')
            {
                /* 连续多个 '\r' 时，仅保留最后一个作为潜在行结束符 */
                s_at_server.pending_cr = APP_TRUE;
            }
            else if (ch != (uint8_t)'\n')
            {
                /* 非换行字符则把它作为新一条命令的起点重新拼接 */
                AT_Server_AppendByte(ch);
            }

            handled = APP_TRUE;
        }
    }

    if (handled == APP_FALSE)
    {
        if (ch == (uint8_t)'\n')
        {
            /* 单独 '\n' 不作为合法结束符，直接清空当前状态 */
            AT_Server_ClearLine();
        }
        else if (ch == (uint8_t)'\r')
        {
            /* 先记下 '\r'，等待下一个字节确认是否构成 "\r\n" */
            s_at_server.pending_cr = APP_TRUE;
        }
        else
        {
            /* 普通字节进入当前命令缓存 */
            AT_Server_AppendByte(ch);
        }
    }
}

APP_Status AT_Server_Init(const AT_Server_Config *config)
{
    APP_Status status = APP_STATUS_OK;

    if ((config == NULL) || (config->ring_buffer_storage == NULL) || (config->ring_buffer_size == 0U) ||
        (config->line_buffer == NULL) || (config->line_buffer_size < 2U) || (config->write == NULL))
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else
    {
        s_at_server.config = *config;
        s_at_server.line_length = 0U;
        s_at_server.pending_cr = APP_FALSE;
        s_at_server.line_overflow = APP_FALSE;
        s_at_server.ready = APP_FALSE;
        s_at_server.last_input_tick = 0U;

        if (lwrb_init(&s_at_server.ring_buffer, config->ring_buffer_storage, config->ring_buffer_size) == 0U)
        {
            status = APP_STATUS_IO;
        }
        else
        {
            /* 初始化后进入空闲状态，等待外部送入串口字节流 */
            s_at_server.ready = APP_TRUE;
            AT_Server_ClearLine();
        }
    }

    return status;
}

size_t AT_Server_InputBytes(const uint8_t *data, size_t len)
{
    size_t written = 0U;

    if ((s_at_server.ready != APP_FALSE) && (data != NULL) && (len > 0U))
    {
        AT_Server_DropPartialIfTimedOut();
        written = (size_t)lwrb_write(&s_at_server.ring_buffer, data, len);

        if (written > 0U)
        {
            /* 仅在确实写入新数据后刷新超时基准 */
            if (s_at_server.config.get_tick != NULL)
            {
                s_at_server.last_input_tick = s_at_server.config.get_tick(s_at_server.config.tick_context);
            }
        }

        if (written != len)
        {
            /* 环形缓冲区装不下时，整包丢弃并清掉当前解析状态，避免半包污染 */
            lwrb_reset(&s_at_server.ring_buffer);
            AT_Server_ClearLine();
        }
    }

    return written;
}

void AT_Server_Poll(void)
{
    uint8_t ch;

    if (s_at_server.ready != APP_FALSE)
    {
        /* 任务轮询时先做一次超时清理 */
        AT_Server_DropPartialIfTimedOut();

        /* 持续取出环形缓冲区中的字节，驱动协议状态机 */
        while (lwrb_read(&s_at_server.ring_buffer, &ch, 1U) == 1U)
        {
            AT_Server_ProcessByte(ch);
        }

        /* 读空后再检查一次，处理“长时间无新数据”的半包丢弃 */
        AT_Server_DropPartialIfTimedOut();
    }
}
