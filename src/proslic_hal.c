/**
 * proslic_hal.c
 * 
 * Hardware Abstraction Layer (HAL) for Skyworks ProSLIC API
 * on ESP32-S3 using ESP-IDF SPI master driver.
 * 
 * The ProSLIC API requires four platform functions to be implemented:
 *   ctrl_ResetWrapper()
 *   ctrl_SPI_Init()
 *   ctrl_ReadRegWrapper()
 *   ctrl_WriteRegWrapper()
 *   ctrl_ReadRAMWrapper()
 *   ctrl_WriteRAMWrapper()
 * 
 * Wire these to the Si32177 over SPI_SLIC bus (VSPI).
 * 
 * Reference: Skyworks ProSLIC API User Guide (AN93)
 *   Available at skyworksinc.com under Si3217x support documents
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "proslic_hal.h"

// SPI host alias — ESP32-S3 doesn't define the legacy VSPI name.
// Per CLAUDE.md: VSPI (SPI2) = ProSLIC dedicated bus.
#ifndef VSPI_HOST
#define VSPI_HOST SPI2_HOST
#endif

static const char *TAG = "proslic_hal";

// SPI device handle for Si32177
static spi_device_handle_t slic_spi = NULL;

// ─── SPI Init ──────────────────────────────────────────────────────────────
esp_err_t proslic_spi_init(void) {
    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = SPI_SLIC_MOSI,
        .miso_io_num   = SPI_SLIC_MISO,
        .sclk_io_num   = SPI_SLIC_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };

    // ProSLIC SPI: mode 0 (CPOL=0, CPHA=0), max 10 MHz
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 5 * 1000 * 1000, // 5 MHz — conservative for bringup
        .mode           = 0,
        .spics_io_num   = SPI_SLIC_CS,
        .queue_size     = 4,
        .flags          = 0,
    };

    esp_err_t ret = spi_bus_initialize(VSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = spi_bus_add_device(VSPI_HOST, &dev_cfg, &slic_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ProSLIC SPI initialized at 5 MHz");
    return ESP_OK;
}

// ─── Hardware Reset ─────────────────────────────────────────────────────────
void proslic_reset(void) {
    gpio_set_direction(SLIC_RST_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(SLIC_RST_GPIO, 0);   // Assert reset (active low)
    vTaskDelay(pdMS_TO_TICKS(250));      // Hold 250ms
    gpio_set_level(SLIC_RST_GPIO, 1);   // Deassert
    vTaskDelay(pdMS_TO_TICKS(50));       // Allow ProSLIC boot
    ESP_LOGI(TAG, "ProSLIC hardware reset complete");
}

// ─── ProSLIC API HAL: ctrl_ResetWrapper ────────────────────────────────────
/**
 * Called by ProSLIC API to reset the hardware.
 * Signature matches ProSLIC API controlInterfaceType.Reset
 */
int ctrl_ResetWrapper(void *hctrl, int reset) {
    if (reset) {
        gpio_set_level(SLIC_RST_GPIO, 0);
    } else {
        gpio_set_level(SLIC_RST_GPIO, 1);
    }
    return 0;
}

// ─── SPI Transaction Helper ────────────────────────────────────────────────
/**
 * ProSLIC SPI protocol:
 *   Byte 0: [CID(2)|RW(1)|ADDR(5)] for register access
 *            CID = channel ID (always 0 for single channel)
 *            RW  = 1 for read, 0 for write
 *            ADDR = register address (5 bits, registers 0–31)
 *   For RAM access: different protocol — see ProSLIC API source
 */
static uint8_t spi_transfer_byte(uint8_t cmd, uint8_t data) {
    spi_transaction_t t = {
        .length    = 16,             // 2 bytes
        .tx_buffer = (uint8_t[]){cmd, data},
        .rx_buffer = NULL,
        .flags     = SPI_TRANS_USE_RXDATA,
    };
    spi_device_transmit(slic_spi, &t);
    return t.rx_data[1]; // Second byte is the returned data
}

// ─── ProSLIC API HAL: ReadReg / WriteReg ───────────────────────────────────
uint8_t ctrl_ReadRegWrapper(void *hctrl, uint8_t channel, uint8_t regAddr) {
    // Read command: bit7=0 (CID high), bit6=0 (CID low), bit5=1 (READ), addr[4:0]
    uint8_t cmd = (channel << 6) | 0x20 | (regAddr & 0x1F);
    uint8_t val = spi_transfer_byte(cmd, 0xFF);
    return val;
}

void ctrl_WriteRegWrapper(void *hctrl, uint8_t channel, uint8_t regAddr, uint8_t data) {
    // Write command: bit5=0 (WRITE)
    uint8_t cmd = (channel << 6) | 0x00 | (regAddr & 0x1F);
    spi_transfer_byte(cmd, data);
}

// ─── ProSLIC API HAL: ReadRAM / WriteRAM ───────────────────────────────────
/**
 * RAM access uses a different protocol — indirect register access.
 * Set address in RAMADDR regs, trigger, read/write RAMDATA regs.
 * See Si3217x datasheet Section 7.1 and ProSLIC API source si3217x_intf.c
 * 
 * These are implemented via sequences of ReadReg/WriteReg calls.
 * The ProSLIC API handles this internally — these wrappers just need
 * the underlying SPI to work.
 */
uint32_t ctrl_ReadRAMWrapper(void *hctrl, uint8_t channel, uint16_t ramAddr) {
    // Write address to RAMADDR_HI (reg 0x1E) and RAMADDR_LO (reg 0x1D)
    ctrl_WriteRegWrapper(hctrl, channel, 0x1E, (ramAddr >> 3) & 0xFF);
    ctrl_WriteRegWrapper(hctrl, channel, 0x1D, (ramAddr & 0x07) << 5);

    // Trigger read by writing 1 to RAM_ACC (bit 0 of RAMADDR_HI)
    uint8_t hi = ctrl_ReadRegWrapper(hctrl, channel, 0x1E);
    ctrl_WriteRegWrapper(hctrl, channel, 0x1E, hi | 0x01);

    // Wait for completion (poll until bit 0 clears)
    int timeout = 100;
    while ((ctrl_ReadRegWrapper(hctrl, channel, 0x1E) & 0x01) && timeout--) {
        vTaskDelay(1);
    }

    // Read 4 bytes of RAM data from RAMDATA regs 0x20–0x23
    uint32_t result = 0;
    result |= ((uint32_t)ctrl_ReadRegWrapper(hctrl, channel, 0x23) << 24);
    result |= ((uint32_t)ctrl_ReadRegWrapper(hctrl, channel, 0x22) << 16);
    result |= ((uint32_t)ctrl_ReadRegWrapper(hctrl, channel, 0x21) << 8);
    result |= ((uint32_t)ctrl_ReadRegWrapper(hctrl, channel, 0x20));
    return result;
}

void ctrl_WriteRAMWrapper(void *hctrl, uint8_t channel, uint16_t ramAddr, uint32_t data) {
    // Write data to RAMDATA regs first
    ctrl_WriteRegWrapper(hctrl, channel, 0x20, (data) & 0xFF);
    ctrl_WriteRegWrapper(hctrl, channel, 0x21, (data >> 8)  & 0xFF);
    ctrl_WriteRegWrapper(hctrl, channel, 0x22, (data >> 16) & 0xFF);
    ctrl_WriteRegWrapper(hctrl, channel, 0x23, (data >> 24) & 0xFF);

    // Write address and trigger write
    ctrl_WriteRegWrapper(hctrl, channel, 0x1E, ((ramAddr >> 3) & 0xFF));
    ctrl_WriteRegWrapper(hctrl, channel, 0x1D, ((ramAddr & 0x07) << 5) | 0x02);

    // Trigger write
    uint8_t hi = ctrl_ReadRegWrapper(hctrl, channel, 0x1E);
    ctrl_WriteRegWrapper(hctrl, channel, 0x1E, hi | 0x01);

    // Wait for completion
    int timeout = 100;
    while ((ctrl_ReadRegWrapper(hctrl, channel, 0x1E) & 0x01) && timeout--) {
        vTaskDelay(1);
    }
}

// ─── Bringup Self-Test ─────────────────────────────────────────────────────
/**
 * SPI communication verification — run this first after power-up.
 * Writes 0x5A to PCMRXLO (reg 0x22), reads it back.
 * Returns true if communication is working.
 */
bool proslic_verify_spi(void) {
    const uint8_t TEST_REG = 0x22; // PCMRXLO
    const uint8_t TEST_VAL = 0x5A;

    ctrl_WriteRegWrapper(NULL, 0, TEST_REG, TEST_VAL);
    uint8_t readback = ctrl_ReadRegWrapper(NULL, 0, TEST_REG);

    if (readback == TEST_VAL) {
        ESP_LOGI(TAG, "SPI verification PASSED (0x%02X)", readback);
        return true;
    } else {
        ESP_LOGE(TAG, "SPI verification FAILED (wrote 0x%02X, read 0x%02X)", TEST_VAL, readback);
        return false;
    }
}
