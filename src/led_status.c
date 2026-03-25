/**
 * led_status.c
 * WS2812B LED state machine — one LED on LED_GPIO (default 21).
 *
 * Uses the ESP-IDF led_strip component (RMT backend).
 * A dedicated low-priority FreeRTOS task drives the blink pattern.
 *
 * State priority (highest wins):
 *   MQTT connected   → GREEN solid
 *   Network, no MQTT → AMBER blink 500 ms
 *   No network       → RED blink 500 ms
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "network_manager.h"
#include "led_status.h"

#ifndef LED_GPIO
#define LED_GPIO 21
#endif

static const char *TAG = "led";

// ─── Internal state ──────────────────────────────────────────────────────────

typedef enum {
    LS_NO_NETWORK = 0,  // Red blink
    LS_NO_MQTT,         // Amber blink
    LS_MQTT_OK,         // Green solid
} led_state_t;

static led_strip_handle_t  s_strip    = NULL;
static volatile led_state_t s_state   = LS_NO_NETWORK;
static volatile bool        s_net_up  = false;
static volatile bool        s_mqtt_ok = false;

// ─── State resolver ──────────────────────────────────────────────────────────

static void resolve_state(void) {
    if (s_mqtt_ok) {
        s_state = LS_MQTT_OK;
    } else if (s_net_up) {
        s_state = LS_NO_MQTT;
    } else {
        s_state = LS_NO_NETWORK;
    }
}

// ─── Blink task ──────────────────────────────────────────────────────────────
// Runs forever; reads s_state on each iteration.
// Colours are tuned for visibility at low brightness (escape-room ambient).
//   RED:   R=40 G=0  B=0
//   AMBER: R=30 G=12 B=0  (warm orange)
//   GREEN: R=0  G=35 B=0

static void led_task(void *arg) {
    bool on = false;
    while (1) {
        switch (s_state) {
            case LS_MQTT_OK:
                led_strip_set_pixel(s_strip, 0, 0, 35, 0);
                led_strip_refresh(s_strip);
                vTaskDelay(pdMS_TO_TICKS(1000)); // Solid — check state every 1s
                break;

            case LS_NO_MQTT:
                on = !on;
                if (on) led_strip_set_pixel(s_strip, 0, 30, 12, 0);
                else    led_strip_clear(s_strip);
                led_strip_refresh(s_strip);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            case LS_NO_NETWORK:
            default:
                on = !on;
                if (on) led_strip_set_pixel(s_strip, 0, 40, 0, 0);
                else    led_strip_clear(s_strip);
                led_strip_refresh(s_strip);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
        }
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

esp_err_t led_status_init(void) {
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds       = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,  // 10 MHz RMT clock
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED strip init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(s_strip);
    xTaskCreate(led_task, "led", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "LED status init OK (GPIO %d)", LED_GPIO);
    return ESP_OK;
}

void led_status_set_network(network_status_t status) {
    s_net_up = (status != NET_DISCONNECTED);
    if (!s_net_up) {
        s_mqtt_ok = false; // Network lost implies MQTT lost
    }
    resolve_state();
}

void led_status_set_mqtt(bool connected) {
    s_mqtt_ok = connected;
    resolve_state();
}
