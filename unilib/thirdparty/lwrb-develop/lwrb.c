/**
 * \file            lwrb.c
 * \brief           轻量级环形缓冲区
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
#include "lwrb/lwrb.h"

/* 内存置零与拷贝函数 */
#define BUF_MEMSET      memset
#define BUF_MEMCPY      memcpy

#define BUF_IS_VALID(b) ((b) != NULL && (b)->buff != NULL && (b)->size > 0)
#define BUF_MIN(x, y)   ((x) < (y) ? (x) : (y))
#define BUF_MAX(x, y)   ((x) > (y) ? (x) : (y))
#define BUF_SEND_EVT(b, type, bp)                                                                                      \
    do {                                                                                                               \
        if ((b)->evt_fn != NULL) {                                                                                     \
            (b)->evt_fn((void*)(b), (type), (bp));                                                                     \
        }                                                                                                              \
    } while (0)

/* 可选原子操作 */
#ifdef LWRB_DISABLE_ATOMIC
#define LWRB_INIT(var, val)        (var) = (val)
#define LWRB_LOAD(var, type)       (var)
#define LWRB_STORE(var, val, type) (var) = (val)
#else
#define LWRB_INIT(var, val)        atomic_init(&(var), (val))
#define LWRB_LOAD(var, type)       atomic_load_explicit(&(var), (type))
#define LWRB_STORE(var, val, type) atomic_store_explicit(&(var), (val), (type))
#endif

/**
 * \brief           使用给定大小和缓冲区数据数组，将缓冲区句柄初始化为默认值
 * \param[in]       buff: 环形缓冲区实例
 * \param[in]       buffdata: 作为缓冲区数据区使用的内存指针
 * \param[in]       size: `buffdata` 的大小，单位为字节
 *                      缓冲区实际可容纳的最大字节数为 `size - 1`
 * \return          成功返回 `1`，否则返回 `0`
 */
uint8_t
lwrb_init(lwrb_t* buff, void* buffdata, lwrb_sz_t size) {
    if (buff == NULL || buffdata == NULL || size == 0) {
        return 0;
    }

    buff->evt_fn = NULL;
    buff->size = size;
    buff->buff = buffdata;
    LWRB_INIT(buff->w_ptr, 0);
    LWRB_INIT(buff->r_ptr, 0);
    return 1;
}

/**
 * \brief           检查 buff 是否已初始化并可使用
 * \param[in]       buff: 环形缓冲区实例
 * \return          可用返回 `1`，否则返回 `0`
 */
uint8_t
lwrb_is_ready(lwrb_t* buff) {
    return BUF_IS_VALID(buff);
}

/**
 * \brief           释放缓冲区内存
 * \note            由于实现未使用动态内存分配，
 *                  这里只会将缓冲区数据指针设为 `NULL`
 * \param[in]       buff: 环形缓冲区实例
 */
void
lwrb_free(lwrb_t* buff) {
    if (BUF_IS_VALID(buff)) {
        buff->buff = NULL;
    }
}

/**
 * \brief           为不同缓冲区操作设置事件回调函数
 * \param[in]       buff: 环形缓冲区实例
 * \param[in]       evt_fn: 回调函数
 */
void
lwrb_set_evt_fn(lwrb_t* buff, lwrb_evt_fn evt_fn) {
    if (BUF_IS_VALID(buff)) {
        buff->evt_fn = evt_fn;
    }
}

/**
 * \brief           设置自定义缓冲区参数，可在事件回调函数中取回
 * \param[in]       buff: 环形缓冲区实例
 * \param[in]       arg: 用户自定义参数
 */
void
lwrb_set_arg(lwrb_t* buff, void* arg) {
    if (BUF_IS_VALID(buff)) {
        buff->arg = arg;
    }
}

/**
 * \brief           获取之前通过 \ref lwrb_set_arg 设置的自定义缓冲区参数
 * \param[in]       buff: 环形缓冲区实例
 * \return          之前通过 \ref lwrb_set_arg 设置的用户参数
 */
void*
lwrb_get_arg(lwrb_t* buff) {
    return buff != NULL ? buff->arg : NULL;
}

/**
 * \brief           向缓冲区写入数据。
 *                  将 `data` 数组中的数据拷贝到缓冲区，最多推进写指针 `btw` 个字节。
 *
 *                  如果缓冲区中可用空间不足，实际拷贝的字节数会更少。
 *                  用户必须检查函数返回值，并与请求写入长度进行比较，
 *                  以确认是否所有数据都已写入。
 *
 * \note            需要更高级的用法时，请使用 \ref lwrb_write_ex
 *
 * \param[in]       buff: 环形缓冲区实例
 * \param[in]       data: 指向待写入数据的指针
 * \param[in]       btw: 要写入的字节数
 * \return          写入到缓冲区的字节数。
 *                  当返回值小于 `btw` 时，说明可用空间不足，
 *                  无法拷贝完整的数据数组。
 */
lwrb_sz_t
lwrb_write(lwrb_t* buff, const void* data, lwrb_sz_t btw) {
    lwrb_sz_t written = 0;

    if (lwrb_write_ex(buff, data, btw, &written, 0)) {
        return written;
    }
    return 0;
}

/**
 * \brief           扩展写入功能
 *
 * \param           buff: 环形缓冲区实例
 * \param           data: 指向待写入数据的指针
 * \param           btw: 要写入的字节数
 * \param           bwritten: 输出指针，用于返回实际写入缓冲区的字节数
 * \param           flags: 可选标志。
 *                      \ref LWRB_FLAG_WRITE_ALL: 要求写入全部数据（最多 btw 字节）。
 *                          如果没有可用内存，将提前返回
 * \return          写入操作成功返回 `1`，否则返回 `0`
 */
uint8_t
lwrb_write_ex(lwrb_t* buff, const void* data, lwrb_sz_t btw, lwrb_sz_t* bwritten, uint16_t flags) {
    lwrb_sz_t tocopy = 0, free = 0, w_ptr = 0;
    const uint8_t* d_ptr = data;

    if (!BUF_IS_VALID(buff) || data == NULL || btw == 0) {
        return 0;
    }

    /* 计算当前最多可写入的字节数 */
    free = lwrb_get_free(buff);
    /* 如果没有空闲空间，或要求全部写入但空间不足，则提前返回 */
    if (free == 0 || (free < btw && (flags & LWRB_FLAG_WRITE_ALL))) {
        return 0;
    }
    btw = BUF_MIN(free, btw);
    w_ptr = LWRB_LOAD(buff->w_ptr, memory_order_acquire);

    /* 步骤 1：写入缓冲区的线性区域 */
    tocopy = BUF_MIN(buff->size - w_ptr, btw);
    BUF_MEMCPY(&buff->buff[w_ptr], d_ptr, tocopy);
    d_ptr += tocopy;
    w_ptr += tocopy;
    btw -= tocopy;

    /* 步骤 2：写入缓冲区起始位置（回卷部分） */
    if (btw > 0) {
        BUF_MEMCPY(buff->buff, d_ptr, btw);
        w_ptr = btw;
    }

    /* 步骤 3：检查是否到达缓冲区末尾 */
    if (w_ptr >= buff->size) {
        w_ptr = 0;
    }

    /*
     * 将最终值写回实际运行变量。
     * 这样可以确保读取操作不会访问到中间状态的数据
     */
    LWRB_STORE(buff->w_ptr, w_ptr, memory_order_release);

    BUF_SEND_EVT(buff, LWRB_EVT_WRITE, tocopy + btw);
    if (bwritten != NULL) {
        *bwritten = tocopy + btw;
    }
    return 1;
}

/**
 * \brief           从缓冲区读取数据。
 *                  将缓冲区中的数据拷贝到 `data` 输出数组，最多推进读指针 `btr` 个字节。
 *
 *                  如果缓冲区中的可读数据不足，实际拷贝的字节数会更少。
 *
 * \note            需要更高级的用法时，请使用 \ref lwrb_read_ex
 *
 * \param[in]       buff: 环形缓冲区实例
 * \param[out]      data: 指向用于接收缓冲区数据的输出内存
 * \param[in]       btr: 要读取的字节数
 * \return          从缓冲区读取并拷贝到数据数组中的字节数
 */
lwrb_sz_t
lwrb_read(lwrb_t* buff, void* data, lwrb_sz_t btr) {
    lwrb_sz_t read = 0;

    if (lwrb_read_ex(buff, data, btr, &read, 0)) {
        return read;
    }
    return 0;
}

/**
 * \brief           扩展读取功能
 *
 * \param           buff: 环形缓冲区实例
 * \param           data: 用于接收从缓冲区读取数据的内存指针
 * \param           btr: 要读取的字节数
 * \param           bread: 输出指针，用于返回从缓冲区读取并写入输出 `data` 变量的字节数
 * \param           flags: 可选标志
 *                      \ref LWRB_FLAG_READ_ALL: 要求读取全部数据（最多 btr 字节）。
 *                          如果缓冲区中没有足够字节，将提前返回
 * \return          读取操作成功返回 `1`，否则返回 `0`
 */
uint8_t
lwrb_read_ex(lwrb_t* buff, void* data, lwrb_sz_t btr, lwrb_sz_t* bread, uint16_t flags) {
    lwrb_sz_t tocopy = 0, full = 0, r_ptr = 0;
    uint8_t* d_ptr = data;

    if (!BUF_IS_VALID(buff) || data == NULL || btr == 0) {
        return 0;
    }

    /* 计算当前最多可读取的字节数 */
    full = lwrb_get_full(buff);
    if (full == 0 || (full < btr && (flags & LWRB_FLAG_READ_ALL))) {
        return 0;
    }
    btr = BUF_MIN(full, btr);
    r_ptr = LWRB_LOAD(buff->r_ptr, memory_order_acquire);

    /* 步骤 1：读取缓冲区的线性区域 */
    tocopy = BUF_MIN(buff->size - r_ptr, btr);
    BUF_MEMCPY(d_ptr, &buff->buff[r_ptr], tocopy);
    d_ptr += tocopy;
    r_ptr += tocopy;
    btr -= tocopy;

    /* 步骤 2：从缓冲区起始位置读取（回卷部分） */
    if (btr > 0) {
        BUF_MEMCPY(d_ptr, buff->buff, btr);
        r_ptr = btr;
    }

    /* 步骤 3：检查是否到达缓冲区末尾 */
    if (r_ptr >= buff->size) {
        r_ptr = 0;
    }

    /*
     * 将最终值写回实际运行变量。
     * 这样可以确保写入操作不会访问到中间状态的数据
     */
    LWRB_STORE(buff->r_ptr, r_ptr, memory_order_release);

    BUF_SEND_EVT(buff, LWRB_EVT_READ, tocopy + btr);
    if (bread != NULL) {
        *bread = tocopy + btr;
    }
    return 1;
}

/**
 * \brief           在不改变读指针的情况下从缓冲区读取数据（仅窥视）
 * \param[in]       buff: 环形缓冲区实例
 * \param[in]       skip_count: 读取前要跳过的字节数
 * \param[out]      data: 指向用于接收缓冲区数据的输出内存
 * \param[in]       btp: 要窥视的字节数
 * \return          窥视并写入输出数组的字节数
 */
lwrb_sz_t
lwrb_peek(const lwrb_t* buff, lwrb_sz_t skip_count, void* data, lwrb_sz_t btp) {
    lwrb_sz_t full = 0, tocopy = 0, r_ptr = 0;
    uint8_t* d_ptr = data;

    if (!BUF_IS_VALID(buff) || data == NULL || btp == 0) {
        return 0;
    }

    /*
     * 计算当前最多可读取的字节数，
     * 并检查请求是否落在可读取范围内。
     *
     * skip_count 大于等于缓冲区中的可读数据量属于无效输入，
     * 因此可以安全地直接返回
     */
    full = lwrb_get_full(buff);
    if (skip_count >= full) {
        return 0;
    }
    r_ptr = LWRB_LOAD(buff->r_ptr, memory_order_relaxed);
    r_ptr += skip_count;
    full -= skip_count;
    if (r_ptr >= buff->size) {
        r_ptr -= buff->size;
    }
    btp = BUF_MIN(full, btp);

    /* 步骤 1：读取缓冲区的线性区域 */
    tocopy = BUF_MIN(buff->size - r_ptr, btp);
    BUF_MEMCPY(d_ptr, &buff->buff[r_ptr], tocopy);
    d_ptr += tocopy;
    btp -= tocopy;

    /* 步骤 2：从缓冲区起始位置读取（回卷部分） */
    if (btp > 0) {
        BUF_MEMCPY(d_ptr, buff->buff, btp);
    }
    return tocopy + btp;
}

/**
 * \brief           获取缓冲区中可用于写操作的空间大小
 * \param[in]       buff: 环形缓冲区实例
 * \return          空闲内存中的字节数
 */
lwrb_sz_t
lwrb_get_free(const lwrb_t* buff) {
    lwrb_sz_t size = 0, w_ptr = 0, r_ptr = 0;

    if (!BUF_IS_VALID(buff)) {
        return 0;
    }

    /*
     * 通过原子访问将缓冲区指针复制到局部变量中。
     *
     * 为保证线程安全（仅适用于单入口、单出口的 FIFO 使用场景），
     * 必须先把缓冲区的 r 和 w 值读取到局部变量中。
     *
     * 这样可确保下面的 if 判断始终使用同一组值，
     * 即使 buff->w 或 buff->r 在中断处理期间发生变化也是如此。
     *
     * 它们在加载过程中可能变化，关键在于
     * 后续 if-else 判断期间不要再变化。
     *
     * lwrb_get_free 仅用于写场景，因此在 FIFO 模式下：
     * - 当前处于写模式，buff->w 指针不会被其他过程或中断修改
     * - buff->r 指针可能被其他过程修改。如果它在加载到局部变量之后发生变化，
     *   缓冲区看到的“空闲大小”会比实际值更小。这不是问题，应用程序
     *   仍可以在下一次继续把数据写入这部分刚刚释放出来的剩余空间
     */
    w_ptr = LWRB_LOAD(buff->w_ptr, memory_order_relaxed);
    r_ptr = LWRB_LOAD(buff->r_ptr, memory_order_relaxed);

    if (w_ptr >= r_ptr) {
        size = buff->size - (w_ptr - r_ptr);
    } else {
        size = r_ptr - w_ptr;
    }

    /* 缓冲区空闲大小始终比实际数组大小少 1 */
    return size - 1;
}

/**
 * \brief           获取当前缓冲区中可用的数据字节数
 * \param[in]       buff: 环形缓冲区实例
 * \return          可供读取的字节数
 */
lwrb_sz_t
lwrb_get_full(const lwrb_t* buff) {
    lwrb_sz_t size = 0, w_ptr = 0, r_ptr = 0;

    if (!BUF_IS_VALID(buff)) {
        return 0;
    }

    /*
     * 将缓冲区指针复制到局部变量中。
     *
     * 为保证线程安全（仅适用于单入口、单出口的 FIFO 使用场景），
     * 必须先把缓冲区的 r 和 w 值读取到局部变量中。
     *
     * 这样可确保下面的 if 判断始终使用同一组值，
     * 即使 buff->w 或 buff->r 在中断处理期间发生变化也是如此。
     *
     * 它们在加载过程中可能变化，关键在于
     * 后续 if-else 判断期间不要再变化。
     *
     * lwrb_get_full 仅用于读场景，因此在 FIFO 模式下：
     * - 当前处于读模式，buff->r 指针不会被其他过程或中断修改
     * - buff->w 指针可能被其他过程修改。如果它在加载到局部变量之后发生变化，
     *   缓冲区看到的“已用大小”会比实际值更小。这不是问题，应用程序
     *   仍可以在下一次继续读取这部分刚刚写入的剩余数据
     */
    w_ptr = LWRB_LOAD(buff->w_ptr, memory_order_relaxed);
    r_ptr = LWRB_LOAD(buff->r_ptr, memory_order_relaxed);

    if (w_ptr >= r_ptr) {
        size = w_ptr - r_ptr;
    } else {
        size = buff->size - (r_ptr - w_ptr);
    }
    return size;
}

/**
 * \brief           将缓冲区重置为默认值，但不修改缓冲区大小
 * \note            此函数不是线程安全的。
 *                  使用时，应用程序必须确保当前没有进行中的读写操作
 * \param[in]       buff: 环形缓冲区实例
 */
void
lwrb_reset(lwrb_t* buff) {
    if (BUF_IS_VALID(buff)) {
        LWRB_STORE(buff->w_ptr, 0, memory_order_release);
        LWRB_STORE(buff->r_ptr, 0, memory_order_release);
        BUF_SEND_EVT(buff, LWRB_EVT_RESET, 0);
    }
}

/**
 * \brief           获取用于快速读取的缓冲区线性地址
 * \param[in]       buff: 环形缓冲区实例
 * \return          线性缓冲区的起始地址
 */
void*
lwrb_get_linear_block_read_address(const lwrb_t* buff) {
    lwrb_sz_t ptr = 0;

    if (!BUF_IS_VALID(buff)) {
        return NULL;
    }
    ptr = LWRB_LOAD(buff->r_ptr, memory_order_relaxed);
    return &buff->buff[ptr];
}

/**
 * \brief           获取读操作在线性地址上、发生回卷前的连续长度
 * \param[in]       buff: 环形缓冲区实例
 * \return          读操作可使用的线性缓冲区长度，单位为字节
 */
lwrb_sz_t
lwrb_get_linear_block_read_length(const lwrb_t* buff) {
    lwrb_sz_t len = 0, w_ptr = 0, r_ptr = 0;

    if (!BUF_IS_VALID(buff)) {
        return 0;
    }

    /*
     * 使用临时值，以防它们在操作期间发生变化。
     * 为什么这样做是安全的，请参见 lwrb_get_free 或 lwrb_get_full 函数。
     */
    w_ptr = LWRB_LOAD(buff->w_ptr, memory_order_relaxed);
    r_ptr = LWRB_LOAD(buff->r_ptr, memory_order_relaxed);

    if (w_ptr > r_ptr) {
        len = w_ptr - r_ptr;
    } else if (r_ptr > w_ptr) {
        len = buff->size - r_ptr;
    } else {
        len = 0;
    }
    return len;
}

/**
 * \brief           跳过（忽略；推进读指针）缓冲区中的数据
 *                  将缓冲区中的数据标记为已读，并最多为 `len` 个字节释放空间
 *
 * \note            适用于 DMA 等流式传输结束时的场景
 * \param[in]       buff: 环形缓冲区实例
 * \param[in]       len: 要跳过并标记为已读的字节数
 * \return          实际跳过的字节数
 */
lwrb_sz_t
lwrb_skip(lwrb_t* buff, lwrb_sz_t len) {
    lwrb_sz_t full = 0, r_ptr = 0;

    if (!BUF_IS_VALID(buff) || len == 0) {
        return 0;
    }

    full = lwrb_get_full(buff);
    len = BUF_MIN(len, full);
    r_ptr = LWRB_LOAD(buff->r_ptr, memory_order_acquire);
    r_ptr += len;
    if (r_ptr >= buff->size) {
        r_ptr -= buff->size;
    }
    LWRB_STORE(buff->r_ptr, r_ptr, memory_order_release);
    BUF_SEND_EVT(buff, LWRB_EVT_READ, len);
    return len;
}

/**
 * \brief           获取用于快速写入的缓冲区线性地址
 * \param[in]       buff: 环形缓冲区实例
 * \return          线性缓冲区的起始地址
 */
void*
lwrb_get_linear_block_write_address(const lwrb_t* buff) {
    lwrb_sz_t ptr = 0;

    if (!BUF_IS_VALID(buff)) {
        return NULL;
    }
    ptr = LWRB_LOAD(buff->w_ptr, memory_order_relaxed);
    return &buff->buff[ptr];
}

/**
 * \brief           获取写操作在线性地址上、发生回卷前的连续长度
 * \param[in]       buff: 环形缓冲区实例
 * \return          写操作可使用的线性缓冲区长度，单位为字节
 */
lwrb_sz_t
lwrb_get_linear_block_write_length(const lwrb_t* buff) {
    lwrb_sz_t len = 0, w_ptr = 0, r_ptr = 0;

    if (!BUF_IS_VALID(buff)) {
        return 0;
    }

    /*
     * 使用临时值，以防它们在操作期间发生变化。
     * 为什么这样做是安全的，请参见 lwrb_get_free 或 lwrb_get_full 函数。
     */
    w_ptr = LWRB_LOAD(buff->w_ptr, memory_order_relaxed);
    r_ptr = LWRB_LOAD(buff->r_ptr, memory_order_relaxed);

    if (w_ptr >= r_ptr) {
        len = buff->size - w_ptr;
        /*
         * 当读指针为 0 时，
         * 最大长度必须减 1；否则如果写入过多字节，
         * 缓冲区会再次被判定为空（r == w）
         */
        if (r_ptr == 0) {
            /*
             * 这里不会下溢：
             * - 如果 r 不为 0，就不会进入这个分支
             * - buff->size 不可能为 0，且当 r 为 0 时，len 必然大于 0
             */
            --len;
        }
    } else {
        len = r_ptr - w_ptr - 1;
    }
    return len;
}

/**
 * \brief           推进缓冲区中的写指针。
 *                  与 skip 函数类似，但修改的是写指针而不是读指针
 *
 * \note            适用于硬件正在向缓冲区写入，而应用程序需要同步增加
 *                  硬件已写入字节数的场景
 * \param[in]       buff: 环形缓冲区实例
 * \param[in]       len: 要推进的字节数
 * \return          实际推进的写入字节数
 */
lwrb_sz_t
lwrb_advance(lwrb_t* buff, lwrb_sz_t len) {
    lwrb_sz_t free = 0, w_ptr = 0;

    if (!BUF_IS_VALID(buff) || len == 0) {
        return 0;
    }

    /* 写回主结构体之前先使用局部变量 */
    free = lwrb_get_free(buff);
    len = BUF_MIN(len, free);
    w_ptr = LWRB_LOAD(buff->w_ptr, memory_order_acquire);
    w_ptr += len;
    if (w_ptr >= buff->size) {
        w_ptr -= buff->size;
    }
    LWRB_STORE(buff->w_ptr, w_ptr, memory_order_release);
    BUF_SEND_EVT(buff, LWRB_EVT_WRITE, len);
    return len;
}

/**
 * \brief           从给定偏移开始，在缓冲区中搜索一个 *needle* 序列。
 *
 * \note            此函数不是线程安全的。
 *
 * \param           buff: 要在其中搜索 needle 的环形缓冲区
 * \param           bts: 要在缓冲区中搜索的常量字节序列
 * \param           len: \arg bts 数组的长度
 * \param           start_offset: 在缓冲区中的起始偏移
 * \param           found_idx: 输出指针，用于写入 bts 被找到时在缓冲区中的索引
 *                      不得为 `NULL`
 * \return          找到 \arg bts 时返回 `1`，否则返回 `0`
 */
uint8_t
lwrb_find(const lwrb_t* buff, const void* bts, lwrb_sz_t len, lwrb_sz_t start_offset, lwrb_sz_t* found_idx) {
    lwrb_sz_t full = 0, r_ptr = 0, buff_r_ptr = 0, max_x = 0;
    uint8_t found = 0;
    const uint8_t* needle = bts;

    if (!BUF_IS_VALID(buff) || needle == NULL || len == 0 || found_idx == NULL) {
        return 0;
    }
    *found_idx = 0;

    full = lwrb_get_full(buff);
    /* 验证初始条件 */
    if (full < (len + start_offset)) {
        return 0;
    }

    /* 获取本次搜索实际使用的缓冲区读指针 */
    buff_r_ptr = LWRB_LOAD(buff->r_ptr, memory_order_relaxed);

    /* 最大循环次数为缓冲区满长度减去输入长度与起始偏移 */
    max_x = full - len;
    for (lwrb_sz_t skip_x = start_offset; !found && skip_x <= max_x; ++skip_x) {
        found = 1; /* 默认先假定已找到 */

        /* 准备读取起点 */
        r_ptr = buff_r_ptr + skip_x;
        if (r_ptr >= buff->size) {
            r_ptr -= buff->size;
        }

        /* 在缓冲区中搜索 */
        for (lwrb_sz_t idx = 0; idx < len; ++idx) {
            if (buff->buff[r_ptr] != needle[idx]) {
                found = 0;
                break;
            }
            if (++r_ptr >= buff->size) {
                r_ptr = 0;
            }
        }
        if (found) {
            *found_idx = skip_x;
        }
    }
    return found;
}
