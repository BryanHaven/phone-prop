/**
 * network_manager.h
 * Ethernet (W5500) primary + WiFi fallback
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// ─── GPIO assignments ─────────────────────────────────────────────────────
// All pins driven by build flags in platformio.ini.
// Defaults below match the production PCB. The waveshare-s3-eth environment
// overrides these for the Waveshare ESP32-S3-ETH dev board.

// W5500 Ethernet (HSPI bus on production, same SPI host on Waveshare)
#ifndef W5500_INT
#define W5500_INT       16      // Production: GPIO16 | Waveshare: GPIO10
#endif
#ifndef W5500_RST
#define W5500_RST       17      // Production: GPIO17 | Waveshare: GPIO9
#endif
#ifndef SPI_W5500_CS
#define SPI_W5500_CS    15      // Production: GPIO15 | Waveshare: GPIO14
#endif
#ifndef SPI_HSPI_CLK
#define SPI_HSPI_CLK    18      // Production: GPIO18 | Waveshare: GPIO13
#endif
#ifndef SPI_HSPI_MOSI
#define SPI_HSPI_MOSI   19      // Production: GPIO19 | Waveshare: GPIO11
#endif
#ifndef SPI_HSPI_MISO
#define SPI_HSPI_MISO   20      // Production: GPIO20 | Waveshare: GPIO12
#endif

// SD card (HSPI bus on production, external ADA254 breakout on Waveshare)
#ifndef SPI_SD_CS
#define SPI_SD_CS       14      // Production: GPIO14 | Waveshare: GPIO38
#endif
#ifndef SPI_SD_CLK
#define SPI_SD_CLK      18      // Production: shared HSPI | Waveshare: GPIO35
#endif
#ifndef SPI_SD_MOSI
#define SPI_SD_MOSI     19      // Production: shared HSPI | Waveshare: GPIO36
#endif
#ifndef SPI_SD_MISO
#define SPI_SD_MISO     20      // Production: shared HSPI | Waveshare: GPIO37
#endif

typedef enum {
    NET_DISCONNECTED = 0,
    NET_ETHERNET,
    NET_WIFI
} network_status_t;

typedef void (*network_status_cb_t)(network_status_t status);

esp_err_t        network_manager_init(const char *wifi_ssid, const char *wifi_pass);
bool             network_wait_connected(uint32_t timeout_ms);
network_status_t network_get_status(void);
void             network_set_status_callback(network_status_cb_t cb);

/**
 * Copy the current IP address string into buf (e.g. "192.168.1.42").
 * Returns "—" if no IP is assigned.
 */
void             network_get_ip_str(char *buf, size_t len);

/**
 * Start WiFi in SoftAP mode for initial provisioning.
 * Call AFTER network_manager_init() (which sets up the event loop and netif).
 * AP is open (no password) — provisioning page is LAN-only anyway.
 * Default IP: 192.168.4.1
 *
 * @param ssid  AP SSID, e.g. "PhoneProp-01"
 */
esp_err_t        network_start_ap(const char *ssid);
