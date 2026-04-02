/**
 * \file            lwrb_ex.c
 * \brief           轻量级环形缓冲区 - 扩展函数
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

#if defined(LWRB_DEV)

/* 未启用开发模式时不参与编译 */

#define BUF_IS_VALID(b) ((b) != NULL && (b)->buff != NULL && (b)->size > 0)
#define BUF_MIN(x, y)   ((x) < (y) ? (x) : (y))

/**
 * \brief           以覆盖方式向缓冲区写入数据，当剩余空间不足以容纳完整输入数据时使用。
 * \note            与 \ref lwrb_write 类似，但会覆盖旧数据
 * \param[in]       buff: 缓冲区句柄
 * \param[in]       data: 要写入环形缓冲区的数据
 * \param[in]       btw: 要写入的字节数
 * \return          实际写入缓冲区的字节数，始终返回 btw
 * \note            该功能主要分为两部分：先写入一段线性区域，如果仍有数据，
 *                  再写入回卷区域。若 w 指针追上 r 指针，则会推进 r 指针。
 *                  该操作同时包含读和写两类行为；若需线程安全，可按文档说明使用互斥锁。
 */
lwrb_sz_t
lwrb_overwrite(lwrb_t* buff, const void* data, lwrb_sz_t btw) {
    lwrb_sz_t orig_btw = btw, max_cap;
    const uint8_t* d = data;

    if (!BUF_IS_VALID(buff) || data == NULL || btw == 0) {
        return 0;
    }

    /* 处理完整输入数组 */
    max_cap = buff->size - 1; /* 缓冲区可容纳的最大容量 */
    if (btw > max_cap) {
        /*
         * 当待写入数据大于缓冲区最大容量时，
         * 可以重置缓冲区，并只写入输入缓冲区最后一部分数据。
         *
         * 这里通过计算需要保留的末尾长度，
         * 再将指针推进到输入缓冲区尾部附近来实现。
         */
        d += btw - max_cap; /* 推进数据指针 */
        btw = max_cap;      /* 限制写入长度 */
        lwrb_reset(buff);   /* 重置缓冲区 */
    } else {
        /*
         * 待写入字节数小于容量时，
         * 最多只需要执行一次 skip 操作，
         * 而且只有当空闲空间小于
         * btw 时才需要；否则无需跳过，
         * 直接写入数据即可。
         */
        lwrb_sz_t f = lwrb_get_free(buff);
        if (f < btw) {
            lwrb_skip(buff, btw - f);
        }
    }
    lwrb_write(buff, d, btw);
    return orig_btw;
}

/**
 * \brief           将一个环形缓冲区中的数据移动到另一个环形缓冲区，移动量最多受限于源缓冲区中的数据量
 *                  或目标缓冲区中的空闲空间。
 * \param[in]       dest: 复制后的数据将写入该缓冲区
 * \param[in]       src:  复制数据的来源缓冲区。
 *                  执行该操作时，源缓冲区中的数据会被实际读取。
 * \return          写入目标缓冲区的字节数
 * \note            此操作会对源缓冲区执行读操作，成功时会更新 r 索引；
 *                  也会对目标缓冲区执行写操作，并可能更新 w 索引。
 *                  若需线程安全，可按文档说明使用互斥锁。
 */
lwrb_sz_t
lwrb_move(lwrb_t* dest, lwrb_t* src) {
    lwrb_sz_t len_to_copy, len_to_copy_orig, src_full, dest_free;

    if (!BUF_IS_VALID(dest) || !BUF_IS_VALID(src)) {
        return 0;
    }
    src_full = lwrb_get_full(src);
    dest_free = lwrb_get_free(dest);
    len_to_copy = BUF_MIN(src_full, dest_free);
    len_to_copy_orig = len_to_copy;

    /* 可复制长度已在上方计算完成。
       这里默认循环内部的各项操作都能正常完成。 */
    while (len_to_copy > 0) {
        lwrb_sz_t max_seq_read, max_seq_write, op_len;
        const uint8_t* d_src;
        uint8_t* d_dst;

        /* 计算本轮数据长度 */
        max_seq_read = lwrb_get_linear_block_read_length(src);
        max_seq_write = lwrb_get_linear_block_write_length(dest);
        op_len = BUF_MIN(max_seq_read, max_seq_write);
        op_len = BUF_MIN(len_to_copy, op_len);

        /* 获取地址 */
        d_src = lwrb_get_linear_block_read_address(src);
        d_dst = lwrb_get_linear_block_write_address(dest);

        /* 逐字节拷贝 */
        for (lwrb_sz_t i = 0; i < op_len; ++i) {
            *d_dst++ = *d_src++;
        }

        lwrb_advance(dest, op_len);
        lwrb_skip(src, op_len);
        len_to_copy -= op_len;
        if (op_len == 0) {
            /* 严重错误... */
            return 0;
        }
    }
    return len_to_copy_orig;
}

#endif /* defined(LWRB_DEV) */
