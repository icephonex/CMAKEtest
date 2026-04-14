#ifndef APP_BASE_H
#define APP_BASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef uint8_t APP_Bool;

typedef enum
{
    APP_STATUS_OK = 0,
    APP_STATUS_INVALID_ARG = 1,
    APP_STATUS_NOT_READY = 2,
    APP_STATUS_BUSY = 3,
    APP_STATUS_RANGE = 4,
    APP_STATUS_IO = 5,
    APP_STATUS_HW_ERROR = 6
} APP_Status;

#define APP_TRUE    ((APP_Bool)1U)
#define APP_FALSE   ((APP_Bool)0U)

#ifdef __cplusplus
}
#endif

#endif /* APP_BASE_H */
