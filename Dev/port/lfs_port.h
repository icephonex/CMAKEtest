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
 * These hooks are expected to be provided by the board's W25Qxx driver.
 * Weak defaults live in lfs_port.c so the project can link before the
 * concrete flash driver is added.
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
