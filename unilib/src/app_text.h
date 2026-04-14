#ifndef APP_TEXT_H
#define APP_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_base.h"

/**
 * @brief 比较指定长度文本是否与目标字面量完全一致。
 * @param text 待比较的文本起始地址。
 * @param text_len 待比较文本的长度。
 * @param literal 目标字面量字符串。
 * @param matched 输出比较结果，APP_TRUE 表示一致，APP_FALSE 表示不一致。
 * @retval APP_STATUS_OK 表示执行完成。
 * @retval APP_STATUS_INVALID_ARG 表示输入参数无效。
 */
APP_Status APP_Text_EqualsLiteral(const char *text, size_t text_len, const char *literal, APP_Bool *matched);

/**
 * @brief 将十进制字符串解析为有符号 32 位整数。
 * @param text 输入字符串。
 * @param value 输出的解析结果。
 * @retval APP_STATUS_OK 表示解析成功。
 * @retval APP_STATUS_INVALID_ARG 表示参数为空或格式非法。
 * @retval APP_STATUS_RANGE 表示数值超出 int32 范围。
 */
APP_Status APP_Text_ParseInt32(const char *text, int32_t *value);

/**
 * @brief 将 "x,y" 形式的字符串解析为两个有符号 32 位整数。
 * @param text 输入字符串。
 * @param first_value 输出的第一个整数。
 * @param second_value 输出的第二个整数。
 * @retval APP_STATUS_OK 表示解析成功。
 * @retval APP_STATUS_INVALID_ARG 表示参数为空或格式非法。
 * @retval APP_STATUS_RANGE 表示数值超出 int32 范围。
 */
APP_Status APP_Text_ParseInt32Pair(const char *text, int32_t *first_value, int32_t *second_value);

/**
 * @brief 将有符号 32 位整数格式化为十进制字符串。
 * @param buffer 输出缓冲区。
 * @param buffer_size 输出缓冲区大小。
 * @param value 待格式化的整数。
 * @retval APP_STATUS_OK 表示格式化成功。
 * @retval APP_STATUS_INVALID_ARG 表示参数无效。
 * @retval APP_STATUS_RANGE 表示缓冲区空间不足。
 */
APP_Status APP_Text_FormatInt32(char *buffer, size_t buffer_size, int32_t value);

/**
 * @brief 将两个有符号 32 位整数格式化为 "x,y" 字符串。
 * @param buffer 输出缓冲区。
 * @param buffer_size 输出缓冲区大小。
 * @param first_value 第一个整数。
 * @param second_value 第二个整数。
 * @retval APP_STATUS_OK 表示格式化成功。
 * @retval APP_STATUS_INVALID_ARG 表示参数无效。
 * @retval APP_STATUS_RANGE 表示缓冲区空间不足。
 */
APP_Status APP_Text_FormatInt32Pair(char *buffer, size_t buffer_size, int32_t first_value, int32_t second_value);

#ifdef __cplusplus
}
#endif

#endif /* APP_TEXT_H */
