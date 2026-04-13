#include "at_server.h"

#include <string.h>

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
    uint8_t pending_cr;
    /* 行缓存溢出标记，等到行结束后统一按错误处理 */
    uint8_t line_overflow;
    /* 初始化完成标记 */
    uint8_t ready;
    /* 最近一次成功写入输入数据的时刻 */
    uint32_t last_input_tick;
} AT_Server_State;

static AT_Server_State s_at_server;

/* 清空当前正在拼接的命令行状态 */
static void AT_Server_ClearLine(void)
{
    s_at_server.line_length = 0U;
    s_at_server.pending_cr = 0U;
    s_at_server.line_overflow = 0U;
    if ((s_at_server.config.line_buffer != NULL) && (s_at_server.config.line_buffer_size > 0U))
    {
        s_at_server.config.line_buffer[0] = '\0';
    }
}

/* 当前是否存在尚未组成完整 "\r\n" 结尾的残留命令 */
static uint8_t AT_Server_HasPartialLine(void)
{
    return (uint8_t)((s_at_server.line_length > 0U) || (s_at_server.pending_cr != 0U) ||
                     (s_at_server.line_overflow != 0U));
}

/* 允许作为命令名的字符：字母、数字和下划线 */
static uint8_t AT_Server_IsNameChar(char ch)
{
    return (uint8_t)((((ch >= 'A') && (ch <= 'Z')) || ((ch >= 'a') && (ch <= 'z')) ||
                      ((ch >= '0') && (ch <= '9')) || (ch == '_'))
                         ? 1
                         : 0);
}

/* 通过端口层发送固定字符串响应 */
static int AT_Server_WriteLiteral(const char *text)
{
    size_t len;

    if ((text == NULL) || (s_at_server.config.write == NULL))
    {
        return -1;
    }

    len = strlen(text);
    if ((len == 0U) ||
        (s_at_server.config.write((const uint8_t *)text, len, s_at_server.config.write_context) != 0))
    {
        return -1;
    }

    return 0;
}

/* 超时仍未收完整的 AT 命令直接丢弃，避免旧半包影响后续新命令 */
static void AT_Server_DropPartialIfTimedOut(void)
{
    uint32_t now;

    if ((s_at_server.ready == 0U) || (s_at_server.config.get_tick == NULL) ||
        (s_at_server.config.partial_timeout_ms == 0U) || (AT_Server_HasPartialLine() == 0U))
    {
        return;
    }

    now = s_at_server.config.get_tick(s_at_server.config.tick_context);
    if ((uint32_t)(now - s_at_server.last_input_tick) >= s_at_server.config.partial_timeout_ms)
    {
        AT_Server_ClearLine();
    }
}

/* 向当前行缓存追加一个字节，预留末尾 '\0' 空间 */
static void AT_Server_AppendByte(uint8_t ch)
{
    if (s_at_server.line_overflow != 0U)
    {
        return;
    }

    if ((s_at_server.config.line_buffer == NULL) || (s_at_server.config.line_buffer_size < 2U) ||
        (s_at_server.line_length >= (s_at_server.config.line_buffer_size - 1U)))
    {
        s_at_server.line_overflow = 1U;
        return;
    }

    s_at_server.config.line_buffer[s_at_server.line_length++] = (char)ch;
    s_at_server.config.line_buffer[s_at_server.line_length] = '\0';
}

int AT_Server_WriteOk(void)
{
    return AT_Server_WriteLiteral("OK\r\n");
}

int AT_Server_WriteError(void)
{
    return AT_Server_WriteLiteral("ERROR\r\n");
}

int AT_Server_WriteHeadData(const char *head, const char *data)
{
    if (head == NULL)
    {
        head = "";
    }
    if (data == NULL)
    {
        data = "";
    }
    if (AT_Server_WriteLiteral("+") != 0)
    {
        return -1;
    }
    if (AT_Server_WriteLiteral(head) != 0)
    {
        return -1;
    }
    if (AT_Server_WriteLiteral(":") != 0)
    {
        return -1;
    }
    if (AT_Server_WriteLiteral(data) != 0)
    {
        return -1;
    }
    return AT_Server_WriteLiteral("\r\n");
}

/* 一条命令以 "\r\n" 收完整后，在这里完成语法判断和业务分发 */
static void AT_Server_DispatchCompletedLine(void)
{
    AT_Server_Request request;
    char *line;
    char *cursor;
    AT_Server_HandlerResult result;

    if (s_at_server.line_overflow != 0U)
    {
        /* 行过长视为非法命令 */
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    if (s_at_server.line_length == 0U)
    {
        AT_Server_ClearLine();
        return;
    }

    line = s_at_server.config.line_buffer;
    line[s_at_server.line_length] = '\0';

    if (strcmp(line, "AT") == 0)
    {
        /* 裸 AT 作为链路探活命令，直接回复 OK */
        (void)AT_Server_WriteOk();
        AT_Server_ClearLine();
        return;
    }

    if (strncmp(line, "AT+", 3U) != 0)
    {
        /* 非 "AT+" 前缀的完整行直接丢弃并回复错误 */
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    if (s_at_server.config.handle == NULL)
    {
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    memset(&request, 0, sizeof(request));
    cursor = line + 3;
    request.name = cursor;

    /* 截取命令名，遇到 '?', '=' 或字符串结束即停止 */
    while (AT_Server_IsNameChar(*cursor) != 0U)
    {
        cursor++;
    }

    request.name_len = (size_t)(cursor - request.name);
    if (request.name_len == 0U)
    {
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    if (*cursor == '\0')
    {
        request.type = AT_SERVER_COMMAND_TYPE_CMD;
    }
    else if ((*cursor == '?') && (cursor[1] == '\0'))
    {
        /* 查询命令必须以 '?' 结束 */
        request.type = AT_SERVER_COMMAND_TYPE_QUERY;
        *cursor = '\0';
    }
    else if (*cursor == '=')
    {
        /* 设置命令允许 '=' 后跟任意参数内容 */
        request.type = AT_SERVER_COMMAND_TYPE_SETUP;
        *cursor = '\0';
        request.args = cursor + 1;
        request.args_len = strlen(request.args);
    }
    else
    {
        /* 其他尾部格式均视为非法 */
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    /* 业务层只返回处理结果，响应格式由通用层统一封装 */
    result = s_at_server.config.handle(&request, s_at_server.config.handle_context);
    if (result == AT_SERVER_HANDLER_RESULT_OK)
    {
        (void)AT_Server_WriteOk();
    }
    else
    {
        (void)AT_Server_WriteError();
    }

    AT_Server_ClearLine();
}

/* 按字节驱动状态机，只认可以 "\r\n" 结尾的一条完整 AT 命令 */
static void AT_Server_ProcessByte(uint8_t ch)
{
    for (;;)
    {
        if (s_at_server.pending_cr != 0U)
        {
            s_at_server.pending_cr = 0U;
            if (ch == (uint8_t)'\n')
            {
                /* 成功匹配到 "\r\n"，当前行收包完成 */
                AT_Server_DispatchCompletedLine();
                return;
            }

            /* 收到单独 '\r' 但后面不是 '\n'，按规则丢弃旧半包，从新字节重新开始 */
            AT_Server_ClearLine();

            if (ch == (uint8_t)'\n')
            {
                return;
            }
            if (ch == (uint8_t)'\r')
            {
                /* 连续多个 '\r' 时，仅保留最后一个作为潜在行结束符 */
                s_at_server.pending_cr = 1U;
                return;
            }

            /* 非换行字符则把它作为新一条命令的起点重新拼接 */
            AT_Server_AppendByte(ch);
            return;
        }

        if (ch == (uint8_t)'\n')
        {
            /* 单独 '\n' 不作为合法结束符，直接清空当前状态 */
            AT_Server_ClearLine();
            return;
        }

        if (ch == (uint8_t)'\r')
        {
            /* 先记下 '\r'，等待下一个字节确认是否构成 "\r\n" */
            s_at_server.pending_cr = 1U;
            return;
        }

        /* 普通字节进入当前命令缓存 */
        AT_Server_AppendByte(ch);
        return;
    }
}

int AT_Server_Init(const AT_Server_Config *config)
{
    if ((config == NULL) || (config->ring_buffer_storage == NULL) || (config->ring_buffer_size == 0U) ||
        (config->line_buffer == NULL) || (config->line_buffer_size < 2U) || (config->write == NULL))
    {
        return -1;
    }

    memset(&s_at_server, 0, sizeof(s_at_server));
    s_at_server.config = *config;
    if (lwrb_init(&s_at_server.ring_buffer, config->ring_buffer_storage, config->ring_buffer_size) == 0U)
    {
        return -1;
    }

    /* 初始化后进入空闲状态，等待外部送入串口字节流 */
    s_at_server.ready = 1U;
    AT_Server_ClearLine();
    return 0;
}

size_t AT_Server_InputBytes(const uint8_t *data, size_t len)
{
    size_t written;

    if ((s_at_server.ready == 0U) || (data == NULL) || (len == 0U))
    {
        return 0U;
    }

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

    return written;
}

void AT_Server_Poll(void)
{
    uint8_t ch;

    if (s_at_server.ready == 0U)
    {
        return;
    }

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
