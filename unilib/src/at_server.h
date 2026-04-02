#ifndef AT_SERVER_H
#define AT_SERVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* 标准执行命令，例如 AT+RST */
    AT_SERVER_COMMAND_TYPE_CMD = 0,
    /* 查询命令，例如 AT+VER? */
    AT_SERVER_COMMAND_TYPE_QUERY,
    /* 设置命令，例如 AT+MODE=1 */
    AT_SERVER_COMMAND_TYPE_SETUP,
} AT_Server_CommandType;

typedef enum {
    /* 业务层处理成功，框架会统一回复 OK */
    AT_SERVER_HANDLER_RESULT_OK = 0,
    /* 业务层处理失败，框架会统一回复 ERROR */
    AT_SERVER_HANDLER_RESULT_ERROR = -1,
    /* 命令未支持，当前同样按 ERROR 返回 */
    AT_SERVER_HANDLER_RESULT_UNSUPPORTED = -2,
} AT_Server_HandlerResult;

typedef struct {
    /* 命令名起始地址，不包含前缀 "AT+" */
    const char* name;
    /* 命令名长度 */
    size_t name_len;
    /* SETUP 命令参数起始地址 */
    const char* args;
    /* SETUP 命令参数长度 */
    size_t args_len;
    /* 解析后的命令类型 */
    AT_Server_CommandType type;
} AT_Server_Request;

/* 底层发送回调，由端口层提供具体串口输出实现 */
typedef int (*AT_Server_WriteFn)(const uint8_t* data, size_t len, void* context);
/* 业务处理回调，由端口层实现具体命令逻辑 */
typedef AT_Server_HandlerResult (*AT_Server_HandleFn)(const AT_Server_Request* request, void* context);
/* 获取系统时基的回调，用于超时丢弃不完整命令 */
typedef uint32_t (*AT_Server_GetTickFn)(void* context);

typedef struct {
    /* 环形缓冲区底层存储区，接收的原始字节先进入这里 */
    uint8_t* ring_buffer_storage;
    /* 环形缓冲区容量 */
    size_t ring_buffer_size;
    /* 单行 AT 命令缓存区，用于拼接一条完整命令 */
    char* line_buffer;
    /* 单行缓存区容量，需包含字符串结束符空间 */
    size_t line_buffer_size;
    /* 统一发送响应的回调 */
    AT_Server_WriteFn write;
    void* write_context;
    /* 命令处理回调 */
    AT_Server_HandleFn handle;
    void* handle_context;
    /* 获取时基回调 */
    AT_Server_GetTickFn get_tick;
    void* tick_context;
    /* 不完整命令超时时间，单位 ms，0 表示不启用 */
    uint32_t partial_timeout_ms;
} AT_Server_Config;

/* 初始化通用 AT 协议引擎 */
int AT_Server_Init(const AT_Server_Config* config);
/* 向环形缓冲区写入一批新收到的字节 */
size_t AT_Server_InputBytes(const uint8_t* data, size_t len);
/* 在循环任务中轮询调用，负责取字节、拼包、解析与分发 */
void AT_Server_Poll(void);
/* 统一发送 OK 响应 */
int AT_Server_WriteOk(void);
/* 统一发送 ERROR 响应 */
int AT_Server_WriteError(void);
/* 统一发送 +HEAD:DATA\r\n 形式的数据响应 */
int AT_Server_WriteHeadData(const char* head, const char* data);

#ifdef __cplusplus
}
#endif

#endif
