#ifndef LFS_CONFIG_H
#define LFS_CONFIG_H

/*
 * These values describe the W25Qxx partition reserved for littlefs.
 * Adjust them to match the actual flash map used by the board.
 */
#ifndef LFS_PORT_FLASH_BASE_OFFSET
#define LFS_PORT_FLASH_BASE_OFFSET   (0u)
#endif

#ifndef LFS_PORT_FLASH_SIZE_BYTES
#define LFS_PORT_FLASH_SIZE_BYTES    (512u * 1024u)
#endif

#ifndef LFS_PORT_READ_SIZE
#define LFS_PORT_READ_SIZE           (16u)
#endif

#ifndef LFS_PORT_PROG_SIZE
#define LFS_PORT_PROG_SIZE           (256u)
#endif

#ifndef LFS_PORT_BLOCK_SIZE
#define LFS_PORT_BLOCK_SIZE          (4096u)
#endif

#ifndef LFS_PORT_BLOCK_SIZE_SHIFT
#define LFS_PORT_BLOCK_SIZE_SHIFT    (12u)
#endif

#if (LFS_PORT_BLOCK_SIZE != (1u << LFS_PORT_BLOCK_SIZE_SHIFT))
#error "LFS_PORT_BLOCK_SIZE must match LFS_PORT_BLOCK_SIZE_SHIFT."
#endif

#define LFS_PORT_BLOCK_COUNT         (LFS_PORT_FLASH_SIZE_BYTES >> LFS_PORT_BLOCK_SIZE_SHIFT)

#ifndef LFS_PORT_BLOCK_CYCLES
#define LFS_PORT_BLOCK_CYCLES        (500)
#endif

#ifndef LFS_PORT_CACHE_SIZE
#define LFS_PORT_CACHE_SIZE          (256u)
#endif

#ifndef LFS_PORT_LOOKAHEAD_SIZE
#define LFS_PORT_LOOKAHEAD_SIZE      (32u)
#endif

#if ((LFS_PORT_FLASH_SIZE_BYTES % LFS_PORT_BLOCK_SIZE) != 0u)
#error "LFS_PORT_FLASH_SIZE_BYTES must be a multiple of LFS_PORT_BLOCK_SIZE."
#endif

#if ((LFS_PORT_BLOCK_SIZE % LFS_PORT_READ_SIZE) != 0u)
#error "LFS_PORT_BLOCK_SIZE must be a multiple of LFS_PORT_READ_SIZE."
#endif

#if ((LFS_PORT_BLOCK_SIZE % LFS_PORT_PROG_SIZE) != 0u)
#error "LFS_PORT_BLOCK_SIZE must be a multiple of LFS_PORT_PROG_SIZE."
#endif

#if ((LFS_PORT_CACHE_SIZE % LFS_PORT_READ_SIZE) != 0u)
#error "LFS_PORT_CACHE_SIZE must be a multiple of LFS_PORT_READ_SIZE."
#endif

#if ((LFS_PORT_CACHE_SIZE % LFS_PORT_PROG_SIZE) != 0u)
#error "LFS_PORT_CACHE_SIZE must be a multiple of LFS_PORT_PROG_SIZE."
#endif

#endif /* LFS_CONFIG_H */
