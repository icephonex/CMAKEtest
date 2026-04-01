#include "lfs_port.h"

#include <stdbool.h>
#include <string.h>

#if defined(__GNUC__)
#define LFS_PORT_WEAK __attribute__((weak))
#else
#define LFS_PORT_WEAK
#endif

static int lfs_port_bd_read(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size);
static int lfs_port_bd_prog(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size);
static int lfs_port_bd_erase(const struct lfs_config *c, lfs_block_t block);
static int lfs_port_bd_sync(const struct lfs_config *c);

static int lfs_port_ensure_ready(void);
static void lfs_port_reset_fs(void);
static bool lfs_port_is_range_valid(lfs_block_t block, lfs_off_t off,
        lfs_size_t size);
static uint32_t lfs_port_block_address(lfs_block_t block, lfs_off_t off);

static uint8_t s_lfs_read_buffer[LFS_PORT_CACHE_SIZE];
static uint8_t s_lfs_prog_buffer[LFS_PORT_CACHE_SIZE];
static uint8_t s_lfs_lookahead_buffer[LFS_PORT_LOOKAHEAD_SIZE];

static lfs_t s_lfs;
static bool s_lfs_flash_ready;
static bool s_lfs_mounted;

static struct lfs_config s_lfs_cfg = {
    .context = NULL,
    .read = lfs_port_bd_read,
    .prog = lfs_port_bd_prog,
    .erase = lfs_port_bd_erase,
    .sync = lfs_port_bd_sync,
    .read_size = LFS_PORT_READ_SIZE,
    .prog_size = LFS_PORT_PROG_SIZE,
    .block_size = LFS_PORT_BLOCK_SIZE,
    .block_count = LFS_PORT_BLOCK_COUNT,
    .block_cycles = LFS_PORT_BLOCK_CYCLES,
    .cache_size = LFS_PORT_CACHE_SIZE,
    .lookahead_size = LFS_PORT_LOOKAHEAD_SIZE,
    .compact_thresh = 0,
    .read_buffer = s_lfs_read_buffer,
    .prog_buffer = s_lfs_prog_buffer,
    .lookahead_buffer = s_lfs_lookahead_buffer,
    .name_max = 0,
    .file_max = 0,
    .attr_max = 0,
    .metadata_max = 0,
    .inline_max = 0,
};

LFS_PORT_WEAK int lfs_port_flash_init(void)
{
    return -1;
}

LFS_PORT_WEAK int lfs_port_flash_read(uint32_t address, void *buffer, uint32_t size)
{
    (void)address;
    (void)buffer;
    (void)size;
    return -1;
}

LFS_PORT_WEAK int lfs_port_flash_prog(uint32_t address, const void *buffer, uint32_t size)
{
    (void)address;
    (void)buffer;
    (void)size;
    return -1;
}

LFS_PORT_WEAK int lfs_port_flash_erase(uint32_t address, uint32_t size)
{
    (void)address;
    (void)size;
    return -1;
}

LFS_PORT_WEAK int lfs_port_flash_sync(void)
{
    return 0;
}

int lfs_port_init(void)
{
    if (s_lfs_flash_ready) {
        return 0;
    }

    if (lfs_port_flash_init() != 0) {
        return LFS_ERR_IO;
    }

    s_lfs_flash_ready = true;
    return 0;
}

int lfs_port_mount(void)
{
    int err = lfs_port_ensure_ready();
    if (err != 0) {
        return err;
    }

    if (s_lfs_mounted) {
        return 0;
    }

    lfs_port_reset_fs();
    err = lfs_mount(&s_lfs, &s_lfs_cfg);
    if (err == 0) {
        s_lfs_mounted = true;
    }

    return err;
}

int lfs_port_format(void)
{
    int err = lfs_port_ensure_ready();
    if (err != 0) {
        return err;
    }

    if (s_lfs_mounted) {
        err = lfs_unmount(&s_lfs);
        if (err != 0) {
            return err;
        }
        s_lfs_mounted = false;
    }

    lfs_port_reset_fs();
    return lfs_format(&s_lfs, &s_lfs_cfg);
}

int lfs_port_mount_or_format(void)
{
    int err = lfs_port_mount();
    if (err == 0) {
        return 0;
    }

    if (err != LFS_ERR_CORRUPT) {
        return err;
    }

    err = lfs_port_format();
    if (err != 0) {
        return err;
    }

    return lfs_port_mount();
}

int lfs_port_unmount(void)
{
    int err;

    if (!s_lfs_mounted) {
        return 0;
    }

    err = lfs_unmount(&s_lfs);
    if (err == 0) {
        s_lfs_mounted = false;
        lfs_port_reset_fs();
    }

    return err;
}

lfs_t *lfs_port_fs(void)
{
    return &s_lfs;
}

const struct lfs_config *lfs_port_cfg(void)
{
    return &s_lfs_cfg;
}

static int lfs_port_bd_read(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size)
{
    uint32_t address;

    (void)c;

    if (!lfs_port_is_range_valid(block, off, size)) {
        return LFS_ERR_INVAL;
    }

    address = lfs_port_block_address(block, off);
    if (lfs_port_flash_read(address, buffer, size) != 0) {
        return LFS_ERR_IO;
    }

    return 0;
}

static int lfs_port_bd_prog(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size)
{
    uint32_t address;

    (void)c;

    if (!lfs_port_is_range_valid(block, off, size)) {
        return LFS_ERR_INVAL;
    }

    address = lfs_port_block_address(block, off);
    if (lfs_port_flash_prog(address, buffer, size) != 0) {
        return LFS_ERR_IO;
    }

    return 0;
}

static int lfs_port_bd_erase(const struct lfs_config *c, lfs_block_t block)
{
    uint32_t address;

    (void)c;

    if (block >= (lfs_block_t)LFS_PORT_BLOCK_COUNT) {
        return LFS_ERR_INVAL;
    }

    address = lfs_port_block_address(block, 0u);
    if (lfs_port_flash_erase(address, LFS_PORT_BLOCK_SIZE) != 0) {
        return LFS_ERR_IO;
    }

    return 0;
}

static int lfs_port_bd_sync(const struct lfs_config *c)
{
    (void)c;

    if (lfs_port_flash_sync() != 0) {
        return LFS_ERR_IO;
    }

    return 0;
}

static int lfs_port_ensure_ready(void)
{
    if (s_lfs_flash_ready) {
        return 0;
    }

    return lfs_port_init();
}

static void lfs_port_reset_fs(void)
{
    (void)memset(&s_lfs, 0, sizeof(s_lfs));
}

static bool lfs_port_is_range_valid(lfs_block_t block, lfs_off_t off,
        lfs_size_t size)
{
    if (block >= (lfs_block_t)LFS_PORT_BLOCK_COUNT) {
        return false;
    }

    if (off > (lfs_off_t)LFS_PORT_BLOCK_SIZE) {
        return false;
    }

    if (size > (lfs_size_t)(LFS_PORT_BLOCK_SIZE - off)) {
        return false;
    }

    return true;
}

static uint32_t lfs_port_block_address(lfs_block_t block, lfs_off_t off)
{
    return (uint32_t)LFS_PORT_FLASH_BASE_OFFSET
        + ((uint32_t)block * (uint32_t)LFS_PORT_BLOCK_SIZE)
        + (uint32_t)off;
}
