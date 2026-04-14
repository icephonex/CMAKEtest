#include "app_text.h"

#include <limits.h>
#include <stdio.h>

#define APP_TEXT_DECIMAL_BASE              (10LL)
#define APP_TEXT_NEGATIVE_SIGN             ('-')
#define APP_TEXT_POSITIVE_SIGN             ('+')
#define APP_TEXT_COMMA                     (',')
#define APP_TEXT_NUMERIC_STRING_MAX_LEN    (12U)

/**
 * @brief 计算以 '\0' 结束的字符串长度。
 * @param text 输入字符串。
 * @return 字符串长度。
 */
static size_t APP_Text_Length(const char *text)
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

/**
 * @brief 将一段非空字符区间复制为以 '\0' 结束的字符串。
 * @param source 输入字符区间起始地址。
 * @param source_len 输入字符区间长度。
 * @param destination 输出缓冲区。
 * @param destination_size 输出缓冲区大小。
 * @retval APP_STATUS_OK 表示复制成功。
 * @retval APP_STATUS_INVALID_ARG 表示参数无效。
 * @retval APP_STATUS_RANGE 表示目标缓冲区空间不足。
 */
static APP_Status APP_Text_CopySpan(const char *source, size_t source_len, char *destination, size_t destination_size)
{
    APP_Status status = APP_STATUS_OK;
    size_t index;

    if ((source == NULL) || (destination == NULL) || (destination_size == 0U))
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else if (source_len >= destination_size)
    {
        status = APP_STATUS_RANGE;
    }
    else
    {
        for (index = 0U; index < source_len; index++)
        {
            destination[index] = source[index];
        }

        destination[source_len] = '\0';
    }

    return status;
}

APP_Status APP_Text_EqualsLiteral(const char *text, size_t text_len, const char *literal, APP_Bool *matched)
{
    APP_Status status = APP_STATUS_OK;
    size_t literal_len;
    size_t index;
    APP_Bool is_equal = APP_TRUE;

    if ((text == NULL) || (literal == NULL) || (matched == NULL))
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else
    {
        *matched = APP_FALSE;
        literal_len = APP_Text_Length(literal);

        if (literal_len == text_len)
        {
            for (index = 0U; index < text_len; index++)
            {
                if (text[index] != literal[index])
                {
                    is_equal = APP_FALSE;
                    break;
                }
            }

            if (is_equal != APP_FALSE)
            {
                *matched = APP_TRUE;
            }
        }
    }

    return status;
}

APP_Status APP_Text_ParseInt32(const char *text, int32_t *value)
{
    APP_Status status = APP_STATUS_OK;
    int64_t magnitude = 0LL;
    int64_t limit = (int64_t)INT32_MAX;
    int32_t sign = 1;
    size_t index = 0U;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0'))
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else
    {
        if (text[index] == APP_TEXT_NEGATIVE_SIGN)
        {
            sign = -1;
            limit = (int64_t)INT32_MAX + 1LL;
            index++;
        }
        else if (text[index] == APP_TEXT_POSITIVE_SIGN)
        {
            index++;
        }

        if (text[index] == '\0')
        {
            status = APP_STATUS_INVALID_ARG;
        }

        while ((text[index] != '\0') && (status == APP_STATUS_OK))
        {
            const char ch = text[index];
            const int64_t digit = (int64_t)(ch - '0');

            if ((ch < '0') || (ch > '9'))
            {
                status = APP_STATUS_INVALID_ARG;
            }
            else if ((magnitude > (limit / APP_TEXT_DECIMAL_BASE)) ||
                     ((magnitude == (limit / APP_TEXT_DECIMAL_BASE)) &&
                      (digit > (limit % APP_TEXT_DECIMAL_BASE))))
            {
                status = APP_STATUS_RANGE;
            }
            else
            {
                magnitude = (magnitude * APP_TEXT_DECIMAL_BASE) + digit;
                index++;
            }
        }

        if (status == APP_STATUS_OK)
        {
            if (sign < 0)
            {
                *value = (magnitude == ((int64_t)INT32_MAX + 1LL)) ? INT32_MIN : (int32_t)(-magnitude);
            }
            else
            {
                *value = (int32_t)magnitude;
            }
        }
    }

    return status;
}

APP_Status APP_Text_ParseInt32Pair(const char *text, int32_t *first_value, int32_t *second_value)
{
    APP_Status status = APP_STATUS_OK;
    size_t comma_index = 0U;
    size_t first_length;
    size_t second_length;
    char first_buffer[APP_TEXT_NUMERIC_STRING_MAX_LEN];
    char second_buffer[APP_TEXT_NUMERIC_STRING_MAX_LEN];

    if ((text == NULL) || (first_value == NULL) || (second_value == NULL))
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else
    {
        while ((text[comma_index] != '\0') && (text[comma_index] != APP_TEXT_COMMA))
        {
            comma_index++;
        }

        if ((text[comma_index] != APP_TEXT_COMMA) || (comma_index == 0U) || (text[comma_index + 1U] == '\0'))
        {
            status = APP_STATUS_INVALID_ARG;
        }
        else
        {
            first_length = comma_index;
            second_length = APP_Text_Length(&text[comma_index + 1U]);

            status = APP_Text_CopySpan(text, first_length, first_buffer, sizeof(first_buffer));
            if (status == APP_STATUS_OK)
            {
                status = APP_Text_CopySpan(&text[comma_index + 1U], second_length, second_buffer, sizeof(second_buffer));
                if (status == APP_STATUS_OK)
                {
                    status = APP_Text_ParseInt32(first_buffer, first_value);
                    if (status == APP_STATUS_OK)
                    {
                        status = APP_Text_ParseInt32(second_buffer, second_value);
                    }
                }
            }
        }
    }

    return status;
}

APP_Status APP_Text_FormatInt32(char *buffer, size_t buffer_size, int32_t value)
{
    APP_Status status = APP_STATUS_OK;
    int written;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else
    {
        written = snprintf(buffer, buffer_size, "%ld", (long)value);
        if ((written <= 0) || ((size_t)written >= buffer_size))
        {
            status = APP_STATUS_RANGE;
        }
    }

    return status;
}

APP_Status APP_Text_FormatInt32Pair(char *buffer, size_t buffer_size, int32_t first_value, int32_t second_value)
{
    APP_Status status = APP_STATUS_OK;
    int written;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        status = APP_STATUS_INVALID_ARG;
    }
    else
    {
        written = snprintf(buffer, buffer_size, "%ld,%ld", (long)first_value, (long)second_value);
        if ((written <= 0) || ((size_t)written >= buffer_size))
        {
            status = APP_STATUS_RANGE;
        }
    }

    return status;
}
