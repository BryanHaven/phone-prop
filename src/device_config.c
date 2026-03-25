/**
 * device_config.c
 * NVS-backed per-unit configuration
 */

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "device_config.h"

static const char *TAG = "config";
#define NVS_NAMESPACE "phone_prop"

// NVS key names (max 15 chars each)
#define KEY_NET_MODE        "net_mode"
#define KEY_WIFI_SSID       "wifi_ssid"
#define KEY_WIFI_PASS       "wifi_pass"
#define KEY_MQTT_BROKER     "mqtt_broker"
#define KEY_MQTT_TOPIC      "mqtt_topic"
#define KEY_DEVICE_ID       "device_id"
#define KEY_POE_INSTALLED   "poe_installed"
#define KEY_PHONE_MODE      "phone_mode"
#define KEY_AUDIO_VOL       "audio_vol"
#define KEY_INTER_DIGIT     "inter_digit"
#define KEY_NUM_COMPLETE    "num_complete"
#define KEY_DTMF_STAR_HASH  "dtmf_star_hash"

esp_err_t config_reset_to_defaults(device_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->net_mode               = NET_MODE_ETH_PREFERRED;
    cfg->phone_mode             = PHONE_AUTO;
    cfg->poe_installed          = false;
    cfg->audio_volume           = DEFAULT_AUDIO_VOLUME;
    cfg->inter_digit_ms         = DEFAULT_INTER_DIGIT_MS;
    cfg->number_complete_ms     = DEFAULT_NUMBER_COMPLETE_MS;
    cfg->dtmf_pass_star_hash    = DEFAULT_DTMF_PASS_STAR_HASH;
    strncpy(cfg->mqtt_broker,     DEFAULT_MQTT_BROKER,     CONFIG_STR_MAX - 1);
    strncpy(cfg->mqtt_base_topic, DEFAULT_MQTT_BASE_TOPIC, CONFIG_STR_MAX - 1);
    strncpy(cfg->device_id,       DEFAULT_DEVICE_ID,       31);
    return ESP_OK;
}

esp_err_t config_load(device_config_t *cfg) {
    config_reset_to_defaults(cfg);  // Start with defaults

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config — using defaults");
        return ESP_OK;
    }
    if (ret != ESP_OK) return ret;

    // Load each key, silently skip if not found (default stays)
    uint8_t  u8_val;
    uint32_t u32_val;
    size_t   len;

    if (nvs_get_u8(handle, KEY_NET_MODE, &u8_val) == ESP_OK)
        cfg->net_mode = (net_mode_t)u8_val;

    len = sizeof(cfg->wifi_ssid);
    nvs_get_str(handle, KEY_WIFI_SSID, cfg->wifi_ssid, &len);

    len = sizeof(cfg->wifi_password);
    nvs_get_str(handle, KEY_WIFI_PASS, cfg->wifi_password, &len);

    len = sizeof(cfg->mqtt_broker);
    nvs_get_str(handle, KEY_MQTT_BROKER, cfg->mqtt_broker, &len);

    len = sizeof(cfg->mqtt_base_topic);
    nvs_get_str(handle, KEY_MQTT_TOPIC, cfg->mqtt_base_topic, &len);

    len = sizeof(cfg->device_id);
    nvs_get_str(handle, KEY_DEVICE_ID, cfg->device_id, &len);

    if (nvs_get_u8(handle, KEY_POE_INSTALLED, &u8_val) == ESP_OK)
        cfg->poe_installed = (bool)u8_val;

    if (nvs_get_u8(handle, KEY_PHONE_MODE, &u8_val) == ESP_OK)
        cfg->phone_mode = (phone_mode_t)u8_val;

    if (nvs_get_u8(handle, KEY_AUDIO_VOL, &u8_val) == ESP_OK)
        cfg->audio_volume = u8_val;

    if (nvs_get_u32(handle, KEY_INTER_DIGIT, &u32_val) == ESP_OK)
        cfg->inter_digit_ms = u32_val;

    if (nvs_get_u32(handle, KEY_NUM_COMPLETE, &u32_val) == ESP_OK)
        cfg->number_complete_ms = u32_val;

    if (nvs_get_u8(handle, KEY_DTMF_STAR_HASH, &u8_val) == ESP_OK)
        cfg->dtmf_pass_star_hash = (bool)u8_val;

    nvs_close(handle);

    ESP_LOGI(TAG, "Config loaded: id=%s broker=%s topic=%s net=%s phone=%s",
             cfg->device_id, cfg->mqtt_broker, cfg->mqtt_base_topic,
             config_net_mode_str(cfg->net_mode),
             config_phone_mode_str(cfg->phone_mode));
    return ESP_OK;
}

esp_err_t config_save(const device_config_t *cfg) {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    nvs_set_u8 (handle, KEY_NET_MODE,     (uint8_t)cfg->net_mode);
    nvs_set_str(handle, KEY_WIFI_SSID,    cfg->wifi_ssid);
    nvs_set_str(handle, KEY_MQTT_BROKER,  cfg->mqtt_broker);
    nvs_set_str(handle, KEY_MQTT_TOPIC,   cfg->mqtt_base_topic);
    nvs_set_str(handle, KEY_DEVICE_ID,    cfg->device_id);
    nvs_set_u8 (handle, KEY_POE_INSTALLED,(uint8_t)cfg->poe_installed);
    nvs_set_u8 (handle, KEY_PHONE_MODE,   (uint8_t)cfg->phone_mode);
    nvs_set_u8 (handle, KEY_AUDIO_VOL,    cfg->audio_volume);
    nvs_set_u32(handle, KEY_INTER_DIGIT,  cfg->inter_digit_ms);
    nvs_set_u32(handle, KEY_NUM_COMPLETE, cfg->number_complete_ms);
    nvs_set_u8 (handle, KEY_DTMF_STAR_HASH, (uint8_t)cfg->dtmf_pass_star_hash);

    // Only save password if non-empty (avoid overwriting with blank from WebUI)
    if (strlen(cfg->wifi_password) > 0)
        nvs_set_str(handle, KEY_WIFI_PASS, cfg->wifi_password);

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK)
        ESP_LOGI(TAG, "Config saved");
    return ret;
}

const char *config_net_mode_str(net_mode_t mode) {
    switch (mode) {
        case NET_MODE_ETH_PREFERRED: return "eth_preferred";
        case NET_MODE_WIFI_ONLY:     return "wifi_only";
        case NET_MODE_ETH_ONLY:      return "eth_only";
        default:                     return "unknown";
    }
}

const char *config_phone_mode_str(phone_mode_t mode) {
    switch (mode) {
        case PHONE_AUTO:       return "auto";
        case PHONE_ROTARY:     return "rotary";
        case PHONE_TOUCHTONE:  return "touchtone";
        default:               return "unknown";
    }
}
