/**
 * web_ui.h
 * HTTP provisioning server for Phone Prop.
 *
 * Serves a configuration page at http://<ip>/
 * JSON API:
 *   GET  /api/config  — returns current config (password excluded)
 *   POST /api/config  — accepts JSON body, saves to NVS
 *   POST /api/reboot  — schedules device restart
 *
 * Access:
 *   AP mode (no WiFi configured): http://192.168.4.1/
 *   STA / Ethernet:               http://<assigned-ip>/
 */

#pragma once
#include "esp_err.h"
#include "device_config.h"
#include "dial_rules.h"

/**
 * Start the HTTP provisioning server.
 * @param cfg  Pointer to the live config struct — updated in-place on POST /api/config.
 *             Must remain valid for the lifetime of the server.
 */
esp_err_t web_ui_start(device_config_t *cfg, dial_ruleset_t *rules);

void web_ui_stop(void);
