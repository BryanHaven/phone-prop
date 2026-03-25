/**
 * proslic_hal.h
 * ProSLIC SPI HAL for ESP32-S3 / ESP-IDF
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t proslic_spi_init(void);
void      proslic_reset(void);
bool      proslic_verify_spi(void);

// ProSLIC API HAL function signatures
// These are registered with the ProSLIC API controlInterfaceType struct
int      ctrl_ResetWrapper(void *hctrl, int reset);
uint8_t  ctrl_ReadRegWrapper(void *hctrl, uint8_t channel, uint8_t regAddr);
void     ctrl_WriteRegWrapper(void *hctrl, uint8_t channel, uint8_t regAddr, uint8_t data);
uint32_t ctrl_ReadRAMWrapper(void *hctrl, uint8_t channel, uint16_t ramAddr);
void     ctrl_WriteRAMWrapper(void *hctrl, uint8_t channel, uint16_t ramAddr, uint32_t data);
