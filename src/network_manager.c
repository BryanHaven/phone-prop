/**
 * network_manager.c
 *
 * Manages Ethernet (W5500) primary + WiFi fallback for the phone prop.
 * MQTT client connects over whichever interface is available,
 * preferring Ethernet when link is detected.
 *
 * W5500 on HSPI bus (GPIO 18/19/20), CS on GPIO15, INT on GPIO16, RST on GPIO17
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "network_manager.h"

// SPI host alias — ESP32-S3 doesn't define the legacy HSPI name.
// Per CLAUDE.md: HSPI (SPI3) = W5500 + SD card bus.
#ifndef HSPI_HOST
#define HSPI_HOST SPI3_HOST
#endif

static const char *TAG = "net_mgr";

static char s_ip_str[16] = "\xe2\x80\x94"; // em dash — shown when no IP assigned

// Event group bits
#define ETH_CONNECTED_BIT    BIT0
#define ETH_GOT_IP_BIT       BIT1
#define WIFI_CONNECTED_BIT   BIT2
#define WIFI_GOT_IP_BIT      BIT3
#define NET_CONNECTED_BIT    BIT4  // Either interface is up

static EventGroupHandle_t net_events;
static network_status_t   net_status = NET_DISCONNECTED;

// Callback for network status changes (set by phone_prop_main)
static network_status_cb_t status_callback = NULL;

// ─── Status Reporting ───────────────────────────────────────────────────────
static void report_status(network_status_t new_status) {
    if (new_status == net_status) return;
    net_status = new_status;
    ESP_LOGI(TAG, "Network status: %s",
        new_status == NET_ETHERNET  ? "ETHERNET" :
        new_status == NET_WIFI      ? "WIFI" : "DISCONNECTED");
    if (status_callback) status_callback(new_status);
}

network_status_t network_get_status(void) {
    return net_status;
}

void network_set_status_callback(network_status_cb_t cb) {
    status_callback = cb;
}

// ─── Ethernet Event Handler ─────────────────────────────────────────────────
static void eth_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet link up");
            xEventGroupSetBits(net_events, ETH_CONNECTED_BIT);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Ethernet link down");
            xEventGroupClearBits(net_events, ETH_CONNECTED_BIT | ETH_GOT_IP_BIT);
            strncpy(s_ip_str, "\xe2\x80\x94", sizeof(s_ip_str));
            // Fall back to WiFi if configured
            if (xEventGroupGetBits(net_events) & WIFI_GOT_IP_BIT) {
                report_status(NET_WIFI);
            } else {
                report_status(NET_DISCONNECTED);
            }
            break;
        default:
            break;
    }
}

// ─── IP Event Handler ───────────────────────────────────────────────────────
static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    if (event_id == IP_EVENT_ETH_GOT_IP) {
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Ethernet got IP: %s", s_ip_str);
        xEventGroupSetBits(net_events, ETH_GOT_IP_BIT | NET_CONNECTED_BIT);
        report_status(NET_ETHERNET);

    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        // Only use WiFi if Ethernet isn't up
        if (!(xEventGroupGetBits(net_events) & ETH_GOT_IP_BIT)) {
            ESP_LOGI(TAG, "WiFi got IP: %s", s_ip_str);
            xEventGroupSetBits(net_events, WIFI_GOT_IP_BIT | NET_CONNECTED_BIT);
            report_status(NET_WIFI);
        } else {
            ESP_LOGI(TAG, "WiFi got IP (standby — Ethernet is primary)");
            xEventGroupSetBits(net_events, WIFI_GOT_IP_BIT);
        }
    }
}

// ─── W5500 Ethernet Init ────────────────────────────────────────────────────
// HSPI bus must be initialized by app_main before this function is called.
// The W5500 driver reads spi_host_id + spi_devcfg from eth_w5500_config_t
// and internally calls spi_bus_add_device() — do NOT add the device manually.
static esp_err_t ethernet_init(void) {
    ESP_LOGI(TAG, "Initializing W5500 Ethernet...");

    // Hardware reset W5500 before driver init
    gpio_set_direction(W5500_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(W5500_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(W5500_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // INT pin — active low, needs pullup
    gpio_set_direction(W5500_INT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(W5500_INT, GPIO_PULLUP_ONLY);

    // W5500 SPI device config — passed to the driver which adds it to HSPI bus
    spi_device_interface_config_t w5500_devcfg = {
        .command_bits   = 16,  // W5500 SPI frame: 16-bit addr + 8-bit ctrl
        .address_bits   = 8,
        .mode           = 0,
        .clock_speed_hz = 20 * 1000 * 1000, // 20 MHz — safe for proto
        .spics_io_num   = SPI_W5500_CS,
        .queue_size     = 20,
        .input_delay_ns = 0,
    };

    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(HSPI_HOST, &w5500_devcfg);
    w5500_cfg.int_gpio_num = W5500_INT;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = W5500_RST;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    if (!mac || !phy) {
        ESP_LOGE(TAG, "W5500 MAC/PHY create failed");
        return ESP_FAIL;
    }

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;

    esp_err_t ret = esp_eth_driver_install(&eth_cfg, &eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "W5500 driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    esp_eth_netif_glue_handle_t eth_glue = esp_eth_new_netif_glue(eth_handle);
    esp_netif_attach(eth_netif, eth_glue);

    esp_eth_start(eth_handle);

    ESP_LOGI(TAG, "W5500 Ethernet driver started");
    return ESP_OK;
}

// ─── WiFi Init ──────────────────────────────────────────────────────────────
static bool s_wifi_inited = false;  // Guard against double esp_wifi_init()

static esp_err_t wifi_init(const char *ssid, const char *password) {
    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "No WiFi SSID configured — WiFi disabled");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WiFi (SSID: %s)...", ssid);

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    s_wifi_inited = true;

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid,     ssid,     sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    return ESP_OK;
}

// ─── IP String ──────────────────────────────────────────────────────────────
void network_get_ip_str(char *buf, size_t len) {
    strncpy(buf, s_ip_str, len - 1);
    buf[len - 1] = '\0';
}

// ─── Public Init ────────────────────────────────────────────────────────────
esp_err_t network_manager_init(const char *wifi_ssid, const char *wifi_pass) {
    net_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               ip_event_handler, NULL));

    // Init Ethernet (primary)
    esp_err_t eth_ret = ethernet_init();
    if (eth_ret != ESP_OK) {
        ESP_LOGW(TAG, "Ethernet init failed — will use WiFi only");
    }

    // Init WiFi (fallback)
    wifi_init(wifi_ssid, wifi_pass);

    return ESP_OK;
}

// ─── Wait for Connection ────────────────────────────────────────────────────
bool network_wait_connected(uint32_t timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(
        net_events,
        NET_CONNECTED_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );
    return (bits & NET_CONNECTED_BIT) != 0;
}

// ─── SoftAP Mode (provisioning) ─────────────────────────────────────────────
esp_err_t network_start_ap(const char *ssid) {
    ESP_LOGI(TAG, "Starting SoftAP: SSID=%s", ssid);

    esp_netif_create_default_wifi_ap();

    if (!s_wifi_inited) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        s_wifi_inited = true;
    }

    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len      = strlen(ssid);
    ap_cfg.ap.authmode      = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.channel       = 6;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started — connect to '%s', then open http://192.168.4.1/", ssid);
    return ESP_OK;
}
