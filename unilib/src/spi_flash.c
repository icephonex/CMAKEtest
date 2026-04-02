#include "spi_flash.h"

#include <string.h>

#include "main.h"

#define SPI_FLASH_CS_GPIO_Port             GPIOB
#define SPI_FLASH_CS_Pin                   GPIO_PIN_12

#define SPI_FLASH_CMD_READ_JEDEC_ID        0x9FU
#define SPI_FLASH_CMD_WRITE_ENABLE         0x06U
#define SPI_FLASH_CMD_READ_STATUS1         0x05U
#define SPI_FLASH_CMD_READ_DATA            0x03U
#define SPI_FLASH_CMD_PAGE_PROGRAM         0x02U
#define SPI_FLASH_CMD_SECTOR_ERASE_4K      0x20U

#define SPI_FLASH_STATUS1_BUSY_MASK        0x01U
#define SPI_FLASH_STATUS1_WEL_MASK         0x02U

#define SPI_FLASH_JEDEC_MANUFACTURER       0xEFU
#define SPI_FLASH_JEDEC_CAPACITY_W25Q64    0x17U

#define SPI_FLASH_IO_TIMEOUT_MS            100U
#define SPI_FLASH_BUSY_TIMEOUT_MS          5000U
#define SPI_FLASH_RX_CHUNK_SIZE            32U

static SPI_HandleTypeDef *s_spi_flash_hspi;
static SPI_Flash_JedecId s_spi_flash_jedec_id;
static uint8_t s_spi_flash_ready;

static void spi_flash_cs_select(void);
static void spi_flash_cs_release(void);
static int spi_flash_is_ready(void);
static int spi_flash_is_range_valid(uint32_t address, uint32_t size);
static int spi_flash_send_bytes(const uint8_t *data, uint16_t size);
static int spi_flash_recv_bytes(uint8_t *data, uint32_t size);
static int spi_flash_read_status1(uint8_t *status);
static int spi_flash_wait_while_busy(uint32_t timeout_ms);
static int spi_flash_write_enable(void);
static int spi_flash_read_jedec_id_raw(SPI_Flash_JedecId *jedec_id);
static int spi_flash_page_program_once(uint32_t address, const uint8_t *data, uint16_t size);
static int spi_flash_sector_erase_once(uint32_t address);

static void spi_flash_cs_select(void)
{
    HAL_GPIO_WritePin(SPI_FLASH_CS_GPIO_Port, SPI_FLASH_CS_Pin, GPIO_PIN_RESET);
}

static void spi_flash_cs_release(void)
{
    HAL_GPIO_WritePin(SPI_FLASH_CS_GPIO_Port, SPI_FLASH_CS_Pin, GPIO_PIN_SET);
}

static int spi_flash_is_ready(void)
{
    if ((s_spi_flash_ready == 0U) || (s_spi_flash_hspi == NULL)) {
        return 0;
    }

    return 1;
}

static int spi_flash_is_range_valid(uint32_t address, uint32_t size)
{
    if (address > SPI_FLASH_TOTAL_SIZE_BYTES) {
        return 0;
    }

    if (size > (SPI_FLASH_TOTAL_SIZE_BYTES - address)) {
        return 0;
    }

    return 1;
}

static int spi_flash_send_bytes(const uint8_t *data, uint16_t size)
{
    if ((s_spi_flash_hspi == NULL) || (data == NULL) || (size == 0U)) {
        return -1;
    }

    if (HAL_SPI_Transmit(s_spi_flash_hspi, (uint8_t *)data, size, SPI_FLASH_IO_TIMEOUT_MS) != HAL_OK) {
        return -1;
    }

    return 0;
}

static int spi_flash_recv_bytes(uint8_t *data, uint32_t size)
{
    uint8_t dummy_tx[SPI_FLASH_RX_CHUNK_SIZE];

    if ((s_spi_flash_hspi == NULL) || (data == NULL)) {
        return -1;
    }

    memset(dummy_tx, 0xFF, sizeof(dummy_tx));

    while (size > 0U) {
        uint16_t chunk = (uint16_t)((size > SPI_FLASH_RX_CHUNK_SIZE) ? SPI_FLASH_RX_CHUNK_SIZE : size);
        if (HAL_SPI_TransmitReceive(s_spi_flash_hspi, dummy_tx, data, chunk, SPI_FLASH_IO_TIMEOUT_MS) != HAL_OK) {
            return -1;
        }

        data += chunk;
        size -= chunk;
    }

    return 0;
}

static int spi_flash_read_status1(uint8_t *status)
{
    uint8_t cmd;

    if (status == NULL) {
        return -1;
    }

    cmd = SPI_FLASH_CMD_READ_STATUS1;
    spi_flash_cs_select();

    if ((spi_flash_send_bytes(&cmd, 1U) != 0) || (spi_flash_recv_bytes(status, 1U) != 0)) {
        spi_flash_cs_release();
        return -1;
    }

    spi_flash_cs_release();
    return 0;
}

static int spi_flash_wait_while_busy(uint32_t timeout_ms)
{
    uint8_t status;
    uint32_t start_tick;

    start_tick = HAL_GetTick();
    for (;;) {
        if (spi_flash_read_status1(&status) != 0) {
            return -1;
        }

        if ((status & SPI_FLASH_STATUS1_BUSY_MASK) == 0U) {
            return 0;
        }

        if ((uint32_t)(HAL_GetTick() - start_tick) >= timeout_ms) {
            return -1;
        }

        HAL_Delay(1);
    }
}

static int spi_flash_write_enable(void)
{
    uint8_t cmd;
    uint8_t status;

    cmd = SPI_FLASH_CMD_WRITE_ENABLE;
    spi_flash_cs_select();

    if (spi_flash_send_bytes(&cmd, 1U) != 0) {
        spi_flash_cs_release();
        return -1;
    }

    spi_flash_cs_release();

    if (spi_flash_read_status1(&status) != 0) {
        return -1;
    }

    if ((status & SPI_FLASH_STATUS1_WEL_MASK) == 0U) {
        return -1;
    }

    return 0;
}

static int spi_flash_read_jedec_id_raw(SPI_Flash_JedecId *jedec_id)
{
    uint8_t cmd;
    uint8_t raw_id[3];

    if (jedec_id == NULL) {
        return -1;
    }

    cmd = SPI_FLASH_CMD_READ_JEDEC_ID;
    spi_flash_cs_select();

    if ((spi_flash_send_bytes(&cmd, 1U) != 0) || (spi_flash_recv_bytes(raw_id, sizeof(raw_id)) != 0)) {
        spi_flash_cs_release();
        return -1;
    }

    spi_flash_cs_release();

    jedec_id->manufacturer_id = raw_id[0];
    jedec_id->memory_type = raw_id[1];
    jedec_id->capacity_id = raw_id[2];
    return 0;
}

static int spi_flash_page_program_once(uint32_t address, const uint8_t *data, uint16_t size)
{
    uint8_t cmd_and_addr[4];

    if ((data == NULL) || (size == 0U) || (size > SPI_FLASH_PAGE_SIZE)) {
        return -1;
    }

    cmd_and_addr[0] = SPI_FLASH_CMD_PAGE_PROGRAM;
    cmd_and_addr[1] = (uint8_t)(address >> 16);
    cmd_and_addr[2] = (uint8_t)(address >> 8);
    cmd_and_addr[3] = (uint8_t)address;

    spi_flash_cs_select();

    if ((spi_flash_send_bytes(cmd_and_addr, sizeof(cmd_and_addr)) != 0) ||
        (spi_flash_send_bytes(data, size) != 0)) {
        spi_flash_cs_release();
        return -1;
    }

    spi_flash_cs_release();
    return 0;
}

static int spi_flash_sector_erase_once(uint32_t address)
{
    uint8_t cmd_and_addr[4];

    cmd_and_addr[0] = SPI_FLASH_CMD_SECTOR_ERASE_4K;
    cmd_and_addr[1] = (uint8_t)(address >> 16);
    cmd_and_addr[2] = (uint8_t)(address >> 8);
    cmd_and_addr[3] = (uint8_t)address;

    spi_flash_cs_select();

    if (spi_flash_send_bytes(cmd_and_addr, sizeof(cmd_and_addr)) != 0) {
        spi_flash_cs_release();
        return -1;
    }

    spi_flash_cs_release();
    return 0;
}

int spi_flash_init(SPI_HandleTypeDef *hspi)
{
    SPI_Flash_JedecId jedec_id;

    if (hspi == NULL) {
        return -1;
    }

    s_spi_flash_hspi = hspi;
    s_spi_flash_ready = 0U;
    spi_flash_cs_release();

    if (spi_flash_wait_while_busy(SPI_FLASH_IO_TIMEOUT_MS) != 0) {
        return -1;
    }

    if (spi_flash_read_jedec_id_raw(&jedec_id) != 0) {
        return -1;
    }

    if ((jedec_id.manufacturer_id != SPI_FLASH_JEDEC_MANUFACTURER) ||
        (jedec_id.capacity_id != SPI_FLASH_JEDEC_CAPACITY_W25Q64)) {
        return -1;
    }

    s_spi_flash_jedec_id = jedec_id;
    s_spi_flash_ready = 1U;
    return 0;
}

int spi_flash_read_jedec_id(SPI_Flash_JedecId *jedec_id)
{
    if ((spi_flash_is_ready() == 0) || (jedec_id == NULL)) {
        return -1;
    }

    *jedec_id = s_spi_flash_jedec_id;
    return 0;
}

int spi_flash_read(uint32_t address, void *buffer, uint32_t size)
{
    uint8_t cmd_and_addr[4];

    if ((spi_flash_is_ready() == 0) || (buffer == NULL) || (size == 0U) ||
        (spi_flash_is_range_valid(address, size) == 0)) {
        return -1;
    }

    cmd_and_addr[0] = SPI_FLASH_CMD_READ_DATA;
    cmd_and_addr[1] = (uint8_t)(address >> 16);
    cmd_and_addr[2] = (uint8_t)(address >> 8);
    cmd_and_addr[3] = (uint8_t)address;

    spi_flash_cs_select();

    if ((spi_flash_send_bytes(cmd_and_addr, sizeof(cmd_and_addr)) != 0) ||
        (spi_flash_recv_bytes((uint8_t *)buffer, size) != 0)) {
        spi_flash_cs_release();
        return -1;
    }

    spi_flash_cs_release();
    return 0;
}

int spi_flash_program(uint32_t address, const void *buffer, uint32_t size)
{
    const uint8_t *src;

    if ((spi_flash_is_ready() == 0) || (buffer == NULL) || (size == 0U) ||
        (spi_flash_is_range_valid(address, size) == 0)) {
        return -1;
    }

    src = (const uint8_t *)buffer;
    while (size > 0U) {
        uint16_t page_offset;
        uint16_t chunk;

        page_offset = (uint16_t)(address % SPI_FLASH_PAGE_SIZE);
        chunk = (uint16_t)(SPI_FLASH_PAGE_SIZE - page_offset);
        if (chunk > size) {
            chunk = (uint16_t)size;
        }

        if (spi_flash_write_enable() != 0) {
            return -1;
        }

        if (spi_flash_page_program_once(address, src, chunk) != 0) {
            return -1;
        }

        if (spi_flash_wait_while_busy(SPI_FLASH_BUSY_TIMEOUT_MS) != 0) {
            return -1;
        }

        address += chunk;
        src += chunk;
        size -= chunk;
    }

    return 0;
}

int spi_flash_erase(uint32_t address, uint32_t size)
{
    if ((spi_flash_is_ready() == 0) || (size == 0U) ||
        (spi_flash_is_range_valid(address, size) == 0)) {
        return -1;
    }

    if (((address % SPI_FLASH_SECTOR_SIZE) != 0U) || ((size % SPI_FLASH_SECTOR_SIZE) != 0U)) {
        return -1;
    }

    while (size > 0U) {
        if (spi_flash_write_enable() != 0) {
            return -1;
        }

        if (spi_flash_sector_erase_once(address) != 0) {
            return -1;
        }

        if (spi_flash_wait_while_busy(SPI_FLASH_BUSY_TIMEOUT_MS) != 0) {
            return -1;
        }

        address += SPI_FLASH_SECTOR_SIZE;
        size -= SPI_FLASH_SECTOR_SIZE;
    }

    return 0;
}

int spi_flash_sync(void)
{
    if (spi_flash_is_ready() == 0) {
        return -1;
    }

    return spi_flash_wait_while_busy(SPI_FLASH_BUSY_TIMEOUT_MS);
}
