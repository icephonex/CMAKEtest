/**
 * \file            lwrb.h
 * \brief           LwRB - 轻量级环形缓冲区
 */

/*
 * Copyright (c) 2024 Tilen MAJERLE
 *
 * 现免费授予任何获得本软件及相关文档文件（以下简称 "软件"）副本的任何人
 * 不受限制地处理本软件的权利，包括但不限于使用、复制、修改、合并、
 * 发布、分发、再许可和/或销售本软件副本的权利，并允许向其提供本软件的人员
 * 在遵守以下条件的前提下这样做：
 *
 * 上述版权声明和本许可声明应包含在本软件的所有副本
 * 或重要部分中。
 *
 * 本软件按 "原样" 提供，不附带任何形式的明示或暗示担保，
 * 包括但不限于适销性、特定用途适用性和非侵权性的担保。
 * 在任何情况下，作者或版权持有人均不对任何索赔、损害或其他责任负责，
 * 无论该责任因合同、侵权或其他行为产生，
 * 也无论其是否源于本软件、使用本软件或与本软件相关的其他处理。
 *
 * 本文件属于 LwRB - 轻量级环形缓冲区库。
 *
 * 作者:            Tilen MAJERLE <tilen@majerle.eu>
 * 版本:            v3.2.0
 */
#ifndef LWRB_HDR_H
#define LWRB_HDR_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * \defgroup        LWRB 轻量级环形缓冲区管理器
 * \brief           轻量级环形缓冲区管理器
 * \{
 */

#if !defined(LWRB_DISABLE_ATOMIC) || __DOXYGEN__
#include <stdatomic.h>

/**
 * \brief           用于大小变量的原子类型。
 *                  默认值设置为 `unsigned 32-bits` 类型
 */
typedef atomic_ulong lwrb_sz_atomic_t;

/**
 * \brief           库中所有操作使用的大小类型。
 *                  默认值设置为 `unsigned 32-bits` 类型
 */
typedef unsigned long lwrb_sz_t;
#else
typedef unsigned long lwrb_sz_atomic_t;
typedef unsigned long lwrb_sz_t;
#endif

/**
 * \brief           缓冲区操作的事件类型
 */
typedef enum {
    LWRB_EVT_READ,  /*!< 读取事件 */
    LWRB_EVT_WRITE, /*!< 写入事件 */
    LWRB_EVT_RESET, /*!< 重置事件 */
} lwrb_evt_type_t;

/**
 * \brief           缓冲区结构体前向声明
 */
struct lwrb;

/**
 * \brief           事件回调函数类型
 * \param[in]       buff: 事件对应的缓冲区句柄
 * \param[in]       evt: 事件类型
 * \param[in]       bp: 写入或读取的字节数（使用时），具体取决于事件类型
 */
typedef void (*lwrb_evt_fn)(struct lwrb* buff, lwrb_evt_type_t evt, lwrb_sz_t bp);

/* 标志列表 */
#define LWRB_FLAG_READ_ALL  ((uint16_t)0x0001)
#define LWRB_FLAG_WRITE_ALL ((uint16_t)0x0001)

/**
 * \brief           缓冲区结构体
 */
typedef struct lwrb {
    uint8_t* buff;  /*!< 指向缓冲区数据的指针。当 `buff != NULL` 且 `size > 0` 时，缓冲区视为已初始化 */
    lwrb_sz_t size; /*!< 缓冲区数据大小。实际缓冲区容量比该值少 `1` 字节 */
    lwrb_sz_atomic_t r_ptr; /*!< 下一个读取位置指针。
                                当 `r == w` 时缓冲区为空，当 `w == r - 1` 时缓冲区为满 */
    lwrb_sz_atomic_t w_ptr; /*!< 下一个写入位置指针。
                                当 `r == w` 时缓冲区为空，当 `w == r - 1` 时缓冲区为满 */
    lwrb_evt_fn evt_fn;     /*!< 事件回调函数指针 */
    void* arg;              /*!< 事件的用户自定义参数 */
} lwrb_t;

uint8_t lwrb_init(lwrb_t* buff, void* buffdata, lwrb_sz_t size);
uint8_t lwrb_is_ready(lwrb_t* buff);
void lwrb_free(lwrb_t* buff);
void lwrb_reset(lwrb_t* buff);
void lwrb_set_evt_fn(lwrb_t* buff, lwrb_evt_fn fn);
void lwrb_set_arg(lwrb_t* buff, void* arg);
void* lwrb_get_arg(lwrb_t* buff);

/* 读写函数 */
lwrb_sz_t lwrb_write(lwrb_t* buff, const void* data, lwrb_sz_t btw);
lwrb_sz_t lwrb_read(lwrb_t* buff, void* data, lwrb_sz_t btr);
lwrb_sz_t lwrb_peek(const lwrb_t* buff, lwrb_sz_t skip_count, void* data, lwrb_sz_t btp);

/* 扩展读写函数 */
uint8_t lwrb_write_ex(lwrb_t* buff, const void* data, lwrb_sz_t btw, lwrb_sz_t* bwritten, uint16_t flags);
uint8_t lwrb_read_ex(lwrb_t* buff, void* data, lwrb_sz_t btr, lwrb_sz_t* bread, uint16_t flags);

/* 缓冲区容量信息 */
lwrb_sz_t lwrb_get_free(const lwrb_t* buff);
lwrb_sz_t lwrb_get_full(const lwrb_t* buff);

/* 读取数据块管理 */
void* lwrb_get_linear_block_read_address(const lwrb_t* buff);
lwrb_sz_t lwrb_get_linear_block_read_length(const lwrb_t* buff);
lwrb_sz_t lwrb_skip(lwrb_t* buff, lwrb_sz_t len);

/* 写入数据块管理 */
void* lwrb_get_linear_block_write_address(const lwrb_t* buff);
lwrb_sz_t lwrb_get_linear_block_write_length(const lwrb_t* buff);
lwrb_sz_t lwrb_advance(lwrb_t* buff, lwrb_sz_t len);

/* 在缓冲区中搜索 */
uint8_t lwrb_find(const lwrb_t* buff, const void* bts, lwrb_sz_t len, lwrb_sz_t start_offset, lwrb_sz_t* found_idx);
lwrb_sz_t lwrb_overwrite(lwrb_t* buff, const void* data, lwrb_sz_t btw);
lwrb_sz_t lwrb_move(lwrb_t* dest, lwrb_t* src);

/**
 * \}
 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LWRB_HDR_H */
