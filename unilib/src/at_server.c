#include "at_server.h"

#include <string.h>

#include "lwrb/lwrb.h"

typedef struct {
    AT_Server_Config config;
    lwrb_t ring_buffer;
    size_t line_length;
    uint8_t pending_cr;
    uint8_t line_overflow;
    uint8_t ready;
    uint32_t last_input_tick;
} AT_Server_State;

static AT_Server_State s_at_server;

static void AT_Server_ClearLine(void) {
    s_at_server.line_length = 0U;
    s_at_server.pending_cr = 0U;
    s_at_server.line_overflow = 0U;
    if ((s_at_server.config.line_buffer != NULL) && (s_at_server.config.line_buffer_size > 0U)) {
        s_at_server.config.line_buffer[0] = '\0';
    }
}

static uint8_t AT_Server_HasPartialLine(void) {
    return (uint8_t)((s_at_server.line_length > 0U) || (s_at_server.pending_cr != 0U) ||
                     (s_at_server.line_overflow != 0U));
}

static uint8_t AT_Server_IsNameChar(char ch) {
    return (uint8_t)((((ch >= 'A') && (ch <= 'Z')) || ((ch >= 'a') && (ch <= 'z')) ||
                      ((ch >= '0') && (ch <= '9')) || (ch == '_')) ?
                         1 :
                         0);
}

static int AT_Server_WriteLiteral(const char* text) {
    size_t len;

    if ((text == NULL) || (s_at_server.config.write == NULL)) {
        return -1;
    }

    len = strlen(text);
    if ((len == 0U) ||
        (s_at_server.config.write((const uint8_t*)text, len, s_at_server.config.write_context) != 0)) {
        return -1;
    }

    return 0;
}

static void AT_Server_DropPartialIfTimedOut(void) {
    uint32_t now;

    if ((s_at_server.ready == 0U) || (s_at_server.config.get_tick == NULL) ||
        (s_at_server.config.partial_timeout_ms == 0U) || (AT_Server_HasPartialLine() == 0U)) {
        return;
    }

    now = s_at_server.config.get_tick(s_at_server.config.tick_context);
    if ((uint32_t)(now - s_at_server.last_input_tick) >= s_at_server.config.partial_timeout_ms) {
        AT_Server_ClearLine();
    }
}

static void AT_Server_AppendByte(uint8_t ch) {
    if (s_at_server.line_overflow != 0U) {
        return;
    }

    if ((s_at_server.config.line_buffer == NULL) || (s_at_server.config.line_buffer_size < 2U) ||
        (s_at_server.line_length >= (s_at_server.config.line_buffer_size - 1U))) {
        s_at_server.line_overflow = 1U;
        return;
    }

    s_at_server.config.line_buffer[s_at_server.line_length++] = (char)ch;
    s_at_server.config.line_buffer[s_at_server.line_length] = '\0';
}

int AT_Server_WriteOk(void) {
    return AT_Server_WriteLiteral("OK\r\n");
}

int AT_Server_WriteError(void) {
    return AT_Server_WriteLiteral("ERROR\r\n");
}

int AT_Server_WriteHeadData(const char* head, const char* data) {
    if (head == NULL) {
        head = "";
    }
    if (data == NULL) {
        data = "";
    }
    if (AT_Server_WriteLiteral("+") != 0) {
        return -1;
    }
    if (AT_Server_WriteLiteral(head) != 0) {
        return -1;
    }
    if (AT_Server_WriteLiteral(":") != 0) {
        return -1;
    }
    if (AT_Server_WriteLiteral(data) != 0) {
        return -1;
    }
    return AT_Server_WriteLiteral("\r\n");
}

static void AT_Server_DispatchCompletedLine(void) {
    AT_Server_Request request;
    char* line;
    char* cursor;
    AT_Server_HandlerResult result;

    if (s_at_server.line_overflow != 0U) {
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    if (s_at_server.line_length == 0U) {
        AT_Server_ClearLine();
        return;
    }

    line = s_at_server.config.line_buffer;
    line[s_at_server.line_length] = '\0';

    if (strcmp(line, "AT") == 0) {
        (void)AT_Server_WriteOk();
        AT_Server_ClearLine();
        return;
    }

    if (strncmp(line, "AT+", 3U) != 0) {
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    if (s_at_server.config.handle == NULL) {
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    memset(&request, 0, sizeof(request));
    cursor = line + 3;
    request.name = cursor;

    while (AT_Server_IsNameChar(*cursor) != 0U) {
        cursor++;
    }

    request.name_len = (size_t)(cursor - request.name);
    if (request.name_len == 0U) {
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    if (*cursor == '\0') {
        request.type = AT_SERVER_COMMAND_TYPE_CMD;
    } else if ((*cursor == '?') && (cursor[1] == '\0')) {
        request.type = AT_SERVER_COMMAND_TYPE_QUERY;
        *cursor = '\0';
    } else if (*cursor == '=') {
        request.type = AT_SERVER_COMMAND_TYPE_SETUP;
        *cursor = '\0';
        request.args = cursor + 1;
        request.args_len = strlen(request.args);
    } else {
        (void)AT_Server_WriteError();
        AT_Server_ClearLine();
        return;
    }

    result = s_at_server.config.handle(&request, s_at_server.config.handle_context);
    if (result == AT_SERVER_HANDLER_RESULT_OK) {
        (void)AT_Server_WriteOk();
    } else {
        (void)AT_Server_WriteError();
    }

    AT_Server_ClearLine();
}

static void AT_Server_ProcessByte(uint8_t ch) {
    for (;;) {
        if (s_at_server.pending_cr != 0U) {
            s_at_server.pending_cr = 0U;
            if (ch == (uint8_t)'\n') {
                AT_Server_DispatchCompletedLine();
                return;
            }

            AT_Server_ClearLine();

            if (ch == (uint8_t)'\n') {
                return;
            }
            if (ch == (uint8_t)'\r') {
                s_at_server.pending_cr = 1U;
                return;
            }

            AT_Server_AppendByte(ch);
            return;
        }

        if (ch == (uint8_t)'\n') {
            AT_Server_ClearLine();
            return;
        }

        if (ch == (uint8_t)'\r') {
            s_at_server.pending_cr = 1U;
            return;
        }

        AT_Server_AppendByte(ch);
        return;
    }
}

int AT_Server_Init(const AT_Server_Config* config) {
    if ((config == NULL) || (config->ring_buffer_storage == NULL) || (config->ring_buffer_size == 0U) ||
        (config->line_buffer == NULL) || (config->line_buffer_size < 2U) || (config->write == NULL)) {
        return -1;
    }

    memset(&s_at_server, 0, sizeof(s_at_server));
    s_at_server.config = *config;
    if (lwrb_init(&s_at_server.ring_buffer, config->ring_buffer_storage, config->ring_buffer_size) == 0U) {
        return -1;
    }

    s_at_server.ready = 1U;
    AT_Server_ClearLine();
    return 0;
}

size_t AT_Server_InputBytes(const uint8_t* data, size_t len) {
    size_t written;

    if ((s_at_server.ready == 0U) || (data == NULL) || (len == 0U)) {
        return 0U;
    }

    AT_Server_DropPartialIfTimedOut();
    written = (size_t)lwrb_write(&s_at_server.ring_buffer, data, len);

    if (written > 0U) {
        if (s_at_server.config.get_tick != NULL) {
            s_at_server.last_input_tick = s_at_server.config.get_tick(s_at_server.config.tick_context);
        }
    }

    if (written != len) {
        lwrb_reset(&s_at_server.ring_buffer);
        AT_Server_ClearLine();
    }

    return written;
}

void AT_Server_Poll(void) {
    uint8_t ch;

    if (s_at_server.ready == 0U) {
        return;
    }

    AT_Server_DropPartialIfTimedOut();

    while (lwrb_read(&s_at_server.ring_buffer, &ch, 1U) == 1U) {
        AT_Server_ProcessByte(ch);
    }

    AT_Server_DropPartialIfTimedOut();
}
