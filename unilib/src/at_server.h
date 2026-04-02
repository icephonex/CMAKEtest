#ifndef AT_SERVER_H
#define AT_SERVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AT_SERVER_COMMAND_TYPE_CMD = 0,
    AT_SERVER_COMMAND_TYPE_QUERY,
    AT_SERVER_COMMAND_TYPE_SETUP,
} AT_Server_CommandType;

typedef enum {
    AT_SERVER_HANDLER_RESULT_OK = 0,
    AT_SERVER_HANDLER_RESULT_ERROR = -1,
    AT_SERVER_HANDLER_RESULT_UNSUPPORTED = -2,
} AT_Server_HandlerResult;

typedef struct {
    const char* name;
    size_t name_len;
    const char* args;
    size_t args_len;
    AT_Server_CommandType type;
} AT_Server_Request;

typedef int (*AT_Server_WriteFn)(const uint8_t* data, size_t len, void* context);
typedef AT_Server_HandlerResult (*AT_Server_HandleFn)(const AT_Server_Request* request, void* context);
typedef uint32_t (*AT_Server_GetTickFn)(void* context);

typedef struct {
    uint8_t* ring_buffer_storage;
    size_t ring_buffer_size;
    char* line_buffer;
    size_t line_buffer_size;
    AT_Server_WriteFn write;
    void* write_context;
    AT_Server_HandleFn handle;
    void* handle_context;
    AT_Server_GetTickFn get_tick;
    void* tick_context;
    uint32_t partial_timeout_ms;
} AT_Server_Config;

int AT_Server_Init(const AT_Server_Config* config);
size_t AT_Server_InputBytes(const uint8_t* data, size_t len);
void AT_Server_Poll(void);
int AT_Server_WriteOk(void);
int AT_Server_WriteError(void);
int AT_Server_WriteHeadData(const char* head, const char* data);

#ifdef __cplusplus
}
#endif

#endif
