#include "lfs_port.h"

#include <string.h>

#include "app_base.h"
#include "main.h"
#include "spi_flash.h"

static int lfs_port_bd_read(const struct lfs_config *c, lfs_block_t block,
                            lfs_off_t off, void *buffer, lfs_size_t size);
static int lfs_port_bd_prog(const struct lfs_config *c, lfs_block_t block,
                            lfs_off_t off, const void *buffer, lfs_size_t size);
static int lfs_port_bd_erase(const struct lfs_config *c, lfs_block_t block);
static int lfs_port_bd_sync(const struct lfs_config *c);

static int lfs_port_ensure_ready(void);
static void lfs_port_reset_fs(void);
static APP_Bool lfs_port_is_range_valid(lfs_block_t block, lfs_off_t off,
                                        lfs_size_t size);
static uint32_t lfs_port_block_address(lfs_block_t block, lfs_off_t off);

static uint8_t s_lfs_read_buffer[LFS_PORT_CACHE_SIZE];
static uint8_t s_lfs_prog_buffer[LFS_PORT_CACHE_SIZE];
static uint8_t s_lfs_lookahead_buffer[LFS_PORT_LOOKAHEAD_SIZE];

static lfs_t s_lfs;
static APP_Bool s_lfs_flash_ready;
static APP_Bool s_lfs_mounted;

extern SPI_HandleTypeDef hspi2;

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

int lfs_port_flash_init(void)
{
    return spi_flash_init(&hspi2);
}

int lfs_port_flash_read(uint32_t address, void *buffer, uint32_t size)
{
    return spi_flash_read(address, buffer, size);
}

int lfs_port_flash_prog(uint32_t address, const void *buffer, uint32_t size)
{
    return spi_flash_program(address, buffer, size);
}

int lfs_port_flash_erase(uint32_t address, uint32_t size)
{
    return spi_flash_erase(address, size);
}

int lfs_port_flash_sync(void)
{
    return spi_flash_sync();
}

int lfs_port_init(void)
{
    int status = 0;

    if (s_lfs_flash_ready == APP_FALSE)
    {
        if (lfs_port_flash_init() != 0)
        {
            status = LFS_ERR_IO;
        }
        else
        {
            s_lfs_flash_ready = APP_TRUE;
        }
    }

    return status;
}

int lfs_port_mount(void)
{
    int err = lfs_port_ensure_ready();

    if ((err == 0) && (s_lfs_mounted == APP_FALSE))
    {
        lfs_port_reset_fs();
        err = lfs_mount(&s_lfs, &s_lfs_cfg);
        if (err == 0)
        {
            s_lfs_mounted = APP_TRUE;
        }
    }

    return err;
}

int lfs_port_format(void)
{
    int err = lfs_port_ensure_ready();

    if ((err == 0) && (s_lfs_mounted != APP_FALSE))
    {
        err = lfs_unmount(&s_lfs);
        if (err == 0)
        {
            s_lfs_mounted = APP_FALSE;
        }
    }

    if (err == 0)
    {
        lfs_port_reset_fs();
        err = lfs_format(&s_lfs, &s_lfs_cfg);
    }

    return err;
}

int lfs_port_mount_or_format(void)
{
    int err = lfs_port_mount();
    
    if (err == LFS_ERR_CORRUPT)
    {
        err = lfs_port_format();
        if (err == 0)
        {
            err = lfs_port_mount();
        }
    }

    return err;
}

int lfs_port_unmount(void)
{
    int err = 0;

    if (s_lfs_mounted != APP_FALSE)
    {
        err = lfs_unmount(&s_lfs);
        if (err == 0)
        {
            s_lfs_mounted = APP_FALSE;
            lfs_port_reset_fs();
        }
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
    int err = 0;

    (void)c;

    if (lfs_port_is_range_valid(block, off, size) == APP_FALSE)
    {
        err = LFS_ERR_INVAL;
    }
    else
    {
        address = lfs_port_block_address(block, off);
        if (lfs_port_flash_read(address, buffer, size) != 0)
        {
            err = LFS_ERR_IO;
        }
    }

    return err;
}

static int lfs_port_bd_prog(const struct lfs_config *c, lfs_block_t block,
                            lfs_off_t off, const void *buffer, lfs_size_t size)
{
    uint32_t address;
    int err = 0;

    (void)c;

    if (lfs_port_is_range_valid(block, off, size) == APP_FALSE)
    {
        err = LFS_ERR_INVAL;
    }
    else
    {
        address = lfs_port_block_address(block, off);
        if (lfs_port_flash_prog(address, buffer, size) != 0)
        {
            err = LFS_ERR_IO;
        }
    }

    return err;
}

static int lfs_port_bd_erase(const struct lfs_config *c, lfs_block_t block)
{
    uint32_t address;
    int err = 0;

    (void)c;

    if (block >= (lfs_block_t)LFS_PORT_BLOCK_COUNT)
    {
        err = LFS_ERR_INVAL;
    }
    else
    {
        address = lfs_port_block_address(block, 0U);
        if (lfs_port_flash_erase(address, LFS_PORT_BLOCK_SIZE) != 0)
        {
            err = LFS_ERR_IO;
        }
    }

    return err;
}

static int lfs_port_bd_sync(const struct lfs_config *c)
{
    int err = 0;

    (void)c;

    if (lfs_port_flash_sync() != 0)
    {
        err = LFS_ERR_IO;
    }

    return err;
}

static int lfs_port_ensure_ready(void)
{
    int err = 0;

    if (s_lfs_flash_ready == APP_FALSE)
    {
        err = lfs_port_init();
    }

    return err;
}

static void lfs_port_reset_fs(void)
{
    (void)memset(&s_lfs, 0, sizeof(s_lfs));
}

static APP_Bool lfs_port_is_range_valid(lfs_block_t block, lfs_off_t off,
                                        lfs_size_t size)
{
    APP_Bool is_valid = APP_TRUE;

    if (block >= (lfs_block_t)LFS_PORT_BLOCK_COUNT)
    {
        is_valid = APP_FALSE;
    }
    else if (off > (lfs_off_t)LFS_PORT_BLOCK_SIZE)
    {
        is_valid = APP_FALSE;
    }
    else if (size > (lfs_size_t)(LFS_PORT_BLOCK_SIZE - off))
    {
        is_valid = APP_FALSE;
    }

    return is_valid;
}

static uint32_t lfs_port_block_address(lfs_block_t block, lfs_off_t off)
{
    return (uint32_t)LFS_PORT_FLASH_BASE_OFFSET + ((uint32_t)block * (uint32_t)LFS_PORT_BLOCK_SIZE) + (uint32_t)off;
}
