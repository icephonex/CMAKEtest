#include "spi_flash.h"

#include <string.h>

#include "app_base.h"
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
#define SPI_FLASH_PAGE_OFFSET_MASK         (SPI_FLASH_PAGE_SIZE - 1U)
#define SPI_FLASH_SECTOR_OFFSET_MASK       (SPI_FLASH_SECTOR_SIZE - 1U)

#if ((SPI_FLASH_PAGE_SIZE & SPI_FLASH_PAGE_OFFSET_MASK) != 0U)
#error "SPI_FLASH_PAGE_SIZE must be a power of two."
#endif

#if ((SPI_FLASH_SECTOR_SIZE & SPI_FLASH_SECTOR_OFFSET_MASK) != 0U)
#error "SPI_FLASH_SECTOR_SIZE must be a power of two."
#endif

static SPI_HandleTypeDef *s_spi_flash_hspi;
static SPI_Flash_JedecId s_spi_flash_jedec_id;
static APP_Bool s_spi_flash_ready;

static void spi_flash_cs_select(void);
static void spi_flash_cs_release(void);
static APP_Bool spi_flash_is_ready(void);
static APP_Bool spi_flash_is_range_valid(uint32_t address, uint32_t size);
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

static APP_Bool spi_flash_is_ready(void)
{
    APP_Bool is_ready = APP_TRUE;

    if ((s_spi_flash_ready == APP_FALSE) || (s_spi_flash_hspi == NULL))
    {
        is_ready = APP_FALSE;
    }

    return is_ready;
}

static APP_Bool spi_flash_is_range_valid(uint32_t address, uint32_t size)
{
    APP_Bool is_valid = APP_TRUE;

    if (address > SPI_FLASH_TOTAL_SIZE_BYTES)
    {
        is_valid = APP_FALSE;
    }
    else if (size > (SPI_FLASH_TOTAL_SIZE_BYTES - address))
    {
        is_valid = APP_FALSE;
    }

    return is_valid;
}

static int spi_flash_send_bytes(const uint8_t *data, uint16_t size)
{
    int status = 0;

    if ((s_spi_flash_hspi == NULL) || (data == NULL) || (size == 0U))
    {
        status = -1;
    }
    else if (HAL_SPI_Transmit(s_spi_flash_hspi, (uint8_t *)data, size, SPI_FLASH_IO_TIMEOUT_MS) != HAL_OK)
    {
        status = -1;
    }

    return status;
}

static int spi_flash_recv_bytes(uint8_t *data, uint32_t size)
{
    uint8_t dummy_tx[SPI_FLASH_RX_CHUNK_SIZE];
    int status = 0;

    if ((s_spi_flash_hspi == NULL) || (data == NULL))
    {
        status = -1;
    }
    else
    {
        memset(dummy_tx, 0xFF, sizeof(dummy_tx));

        while ((size > 0U) && (status == 0))
        {
            uint16_t chunk = (uint16_t)((size > SPI_FLASH_RX_CHUNK_SIZE) ? SPI_FLASH_RX_CHUNK_SIZE : size);
            if (HAL_SPI_TransmitReceive(s_spi_flash_hspi, dummy_tx, data, chunk, SPI_FLASH_IO_TIMEOUT_MS) != HAL_OK)
            {
                status = -1;
            }
            else
            {
                data += chunk;
                size -= chunk;
            }
        }
    }

    return status;
}

static int spi_flash_read_status1(uint8_t *status)
{
    uint8_t cmd;
    int result = -1;

    if (status != NULL)
    {
        cmd = SPI_FLASH_CMD_READ_STATUS1;
        spi_flash_cs_select();

        if ((spi_flash_send_bytes(&cmd, 1U) == 0) && (spi_flash_recv_bytes(status, 1U) == 0))
        {
            result = 0;
        }

        spi_flash_cs_release();
    }

    return result;
}

static int spi_flash_wait_while_busy(uint32_t timeout_ms)
{
    uint8_t status = 0U;
    uint32_t start_tick = HAL_GetTick();
    int result = -1;
    APP_Bool completed = APP_FALSE;

    while (completed == APP_FALSE)
    {
        if (spi_flash_read_status1(&status) != 0)
        {
            completed = APP_TRUE;
        }
        else if ((status & SPI_FLASH_STATUS1_BUSY_MASK) == 0U)
        {
            result = 0;
            completed = APP_TRUE;
        }
        else if ((uint32_t)(HAL_GetTick() - start_tick) >= timeout_ms)
        {
            completed = APP_TRUE;
        }
        else
        {
            HAL_Delay(1U);
        }
    }

    return result;
}

static int spi_flash_write_enable(void)
{
    uint8_t cmd;
    uint8_t status = 0U;
    int result = -1;

    cmd = SPI_FLASH_CMD_WRITE_ENABLE;
    spi_flash_cs_select();

    if (spi_flash_send_bytes(&cmd, 1U) == 0)
    {
        spi_flash_cs_release();
        if ((spi_flash_read_status1(&status) == 0) && ((status & SPI_FLASH_STATUS1_WEL_MASK) != 0U))
        {
            result = 0;
        }
    }
    else
    {
        spi_flash_cs_release();
    }

    return result;
}

static int spi_flash_read_jedec_id_raw(SPI_Flash_JedecId *jedec_id)
{
    uint8_t cmd;
    uint8_t raw_id[3];
    int result = -1;

    if (jedec_id != NULL)
    {
        cmd = SPI_FLASH_CMD_READ_JEDEC_ID;
        spi_flash_cs_select();

        if ((spi_flash_send_bytes(&cmd, 1U) == 0) && (spi_flash_recv_bytes(raw_id, sizeof(raw_id)) == 0))
        {
            jedec_id->manufacturer_id = raw_id[0];
            jedec_id->memory_type = raw_id[1];
            jedec_id->capacity_id = raw_id[2];
            result = 0;
        }

        spi_flash_cs_release();
    }

    return result;
}

static int spi_flash_page_program_once(uint32_t address, const uint8_t *data, uint16_t size)
{
    uint8_t cmd_and_addr[4];
    int result = -1;

    if ((data != NULL) && (size > 0U) && (size <= SPI_FLASH_PAGE_SIZE))
    {
        cmd_and_addr[0] = SPI_FLASH_CMD_PAGE_PROGRAM;
        cmd_and_addr[1] = (uint8_t)(address >> 16);
        cmd_and_addr[2] = (uint8_t)(address >> 8);
        cmd_and_addr[3] = (uint8_t)address;

        spi_flash_cs_select();

        if ((spi_flash_send_bytes(cmd_and_addr, sizeof(cmd_and_addr)) == 0) &&
            (spi_flash_send_bytes(data, size) == 0))
        {
            result = 0;
        }

        spi_flash_cs_release();
    }

    return result;
}

static int spi_flash_sector_erase_once(uint32_t address)
{
    uint8_t cmd_and_addr[4];
    int result = -1;

    cmd_and_addr[0] = SPI_FLASH_CMD_SECTOR_ERASE_4K;
    cmd_and_addr[1] = (uint8_t)(address >> 16);
    cmd_and_addr[2] = (uint8_t)(address >> 8);
    cmd_and_addr[3] = (uint8_t)address;

    spi_flash_cs_select();

    if (spi_flash_send_bytes(cmd_and_addr, sizeof(cmd_and_addr)) == 0)
    {
        result = 0;
    }

    spi_flash_cs_release();
    return result;
}

int spi_flash_init(SPI_HandleTypeDef *hspi)
{
    SPI_Flash_JedecId jedec_id;
    int result = -1;

    if (hspi != NULL)
    {
        s_spi_flash_hspi = hspi;
        s_spi_flash_ready = APP_FALSE;
        spi_flash_cs_release();

        if (spi_flash_wait_while_busy(SPI_FLASH_IO_TIMEOUT_MS) == 0)
        {
            if (spi_flash_read_jedec_id_raw(&jedec_id) == 0)
            {
                if ((jedec_id.manufacturer_id == SPI_FLASH_JEDEC_MANUFACTURER) &&
                    (jedec_id.capacity_id == SPI_FLASH_JEDEC_CAPACITY_W25Q64))
                {
                    s_spi_flash_jedec_id = jedec_id;
                    s_spi_flash_ready = APP_TRUE;
                    result = 0;
                }
            }
        }
    }

    return result;
}

int spi_flash_read_jedec_id(SPI_Flash_JedecId *jedec_id)
{
    int result = -1;

    if ((spi_flash_is_ready() != APP_FALSE) && (jedec_id != NULL))
    {
        *jedec_id = s_spi_flash_jedec_id;
        result = 0;
    }

    return result;
}

int spi_flash_read(uint32_t address, void *buffer, uint32_t size)
{
    uint8_t cmd_and_addr[4];
    int result = -1;

    if ((spi_flash_is_ready() != APP_FALSE) && (buffer != NULL) && (size > 0U) &&
        (spi_flash_is_range_valid(address, size) != APP_FALSE))
    {
        cmd_and_addr[0] = SPI_FLASH_CMD_READ_DATA;
        cmd_and_addr[1] = (uint8_t)(address >> 16);
        cmd_and_addr[2] = (uint8_t)(address >> 8);
        cmd_and_addr[3] = (uint8_t)address;

        spi_flash_cs_select();

        if ((spi_flash_send_bytes(cmd_and_addr, sizeof(cmd_and_addr)) == 0) &&
            (spi_flash_recv_bytes((uint8_t *)buffer, size) == 0))
        {
            result = 0;
        }

        spi_flash_cs_release();
    }

    return result;
}

int spi_flash_program(uint32_t address, const void *buffer, uint32_t size)
{
    const uint8_t *src;
    int result = -1;

    if ((spi_flash_is_ready() != APP_FALSE) && (buffer != NULL) && (size > 0U) &&
        (spi_flash_is_range_valid(address, size) != APP_FALSE))
    {
        src = (const uint8_t *)buffer;
        result = 0;

        while ((size > 0U) && (result == 0))
        {
            uint16_t page_offset;
            uint16_t chunk;

            page_offset = (uint16_t)(address & (uint32_t)SPI_FLASH_PAGE_OFFSET_MASK);
            chunk = (uint16_t)(SPI_FLASH_PAGE_SIZE - page_offset);
            if (chunk > size)
            {
                chunk = (uint16_t)size;
            }

            if (spi_flash_write_enable() != 0)
            {
                result = -1;
            }
            else if (spi_flash_page_program_once(address, src, chunk) != 0)
            {
                result = -1;
            }
            else if (spi_flash_wait_while_busy(SPI_FLASH_BUSY_TIMEOUT_MS) != 0)
            {
                result = -1;
            }
            else
            {
                address += chunk;
                src += chunk;
                size -= chunk;
            }
        }
    }

    return result;
}

int spi_flash_erase(uint32_t address, uint32_t size)
{
    int result = -1;

    if ((spi_flash_is_ready() != APP_FALSE) && (size > 0U) &&
        (spi_flash_is_range_valid(address, size) != APP_FALSE))
    {
        if (((address & (uint32_t)SPI_FLASH_SECTOR_OFFSET_MASK) == 0U) &&
            ((size & (uint32_t)SPI_FLASH_SECTOR_OFFSET_MASK) == 0U))
        {
            result = 0;
            while ((size > 0U) && (result == 0))
            {
                if (spi_flash_write_enable() != 0)
                {
                    result = -1;
                }
                else if (spi_flash_sector_erase_once(address) != 0)
                {
                    result = -1;
                }
                else if (spi_flash_wait_while_busy(SPI_FLASH_BUSY_TIMEOUT_MS) != 0)
                {
                    result = -1;
                }
                else
                {
                    address += SPI_FLASH_SECTOR_SIZE;
                    size -= SPI_FLASH_SECTOR_SIZE;
                }
            }
        }
    }

    return result;
}

int spi_flash_sync(void)
{
    int result = -1;

    if (spi_flash_is_ready() != APP_FALSE)
    {
        result = spi_flash_wait_while_busy(SPI_FLASH_BUSY_TIMEOUT_MS);
    }

    return result;
}
