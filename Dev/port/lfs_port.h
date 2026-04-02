#ifndef LFS_PORT_H
#define LFS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "lfs.h"
#include "lfs_config.h"

int lfs_port_init(void);
int lfs_port_mount(void);
int lfs_port_format(void);
int lfs_port_mount_or_format(void);
int lfs_port_unmount(void);

lfs_t *lfs_port_fs(void);
const struct lfs_config *lfs_port_cfg(void);

/*
 * littlefs 端口层统一通过 spi_flash 访问 W25Q64。
 * 这些接口保留给 lfs_port.c 内部实现与 littlefs 桥接使用。
 */
int lfs_port_flash_init(void);
int lfs_port_flash_read(uint32_t address, void *buffer, uint32_t size);
int lfs_port_flash_prog(uint32_t address, const void *buffer, uint32_t size);
int lfs_port_flash_erase(uint32_t address, uint32_t size);
int lfs_port_flash_sync(void);

#ifdef __cplusplus
}
#endif

#endif /* LFS_PORT_H */
