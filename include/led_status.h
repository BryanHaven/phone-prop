/**
 * led_status.h
 * WS2812B status LED state machine for Phone Prop.
 *
 * One WS2812B on LED_GPIO (default GPIO 21, overridden by build flag).
 *
 * Colour/pattern mapping:
 *   RED blink   (500 ms)  — no network connection
 *   AMBER blink (500 ms)  — network up, MQTT not connected
 *   GREEN solid           — MQTT connected and ready
 *
 * Call led_status_init() once at startup, then call
 * led_status_set_network() and led_status_set_mqtt() from
 * the network status callback and MQTT event handler respectively.
 */

#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "network_manager.h"

/**
 * Initialise the LED strip and start the blink task.
 * Must be called from app_main after the scheduler is running.
 */
esp_err_t led_status_init(void);

/**
 * Update LED to reflect current network state.
 * Call from the network_status_cb_t registered with network_set_status_callback().
 */
void led_status_set_network(network_status_t status);

/**
 * Update LED to reflect MQTT connection state.
 * Call from the MQTT event handler on MQTT_EVENT_CONNECTED / DISCONNECTED.
 */
void led_status_set_mqtt(bool connected);
