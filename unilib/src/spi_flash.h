#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#include "stm32f1xx_hal.h"

/* W25Q64 基本几何参数 */
#define SPI_FLASH_TOTAL_SIZE_BYTES (8u * 1024u * 1024u)
#define SPI_FLASH_PAGE_SIZE 256u
#define SPI_FLASH_SECTOR_SIZE 4096u

    typedef struct
    {
        uint8_t manufacturer_id;
        uint8_t memory_type;
        uint8_t capacity_id;
    } SPI_Flash_JedecId;

    /* 绑定 SPI 句柄并校验 W25Q64 JEDEC ID */
    int spi_flash_init(SPI_HandleTypeDef *hspi);

    /* 读取 JEDEC ID */
    int spi_flash_read_jedec_id(SPI_Flash_JedecId *jedec_id);

    /* 从 flash 读取任意长度数据 */
    int spi_flash_read(uint32_t address, void *buffer, uint32_t size);

    /* 按页编程，内部自动拆分页边界 */
    int spi_flash_program(uint32_t address, const void *buffer, uint32_t size);

    /* 按 4KB 扇区擦除，address/size 需按扇区对齐 */
    int spi_flash_erase(uint32_t address, uint32_t size);

    /* 等待器件内部操作完成 */
    int spi_flash_sync(void);

#ifdef __cplusplus
}
#endif

#endif /* SPI_FLASH_H */
