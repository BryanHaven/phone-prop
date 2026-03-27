/**
 * device_config.h / device_config.c
 *
 * Per-unit NVS configuration for mixed permanent/portable deployment.
 *
 * All settings editable via WebUI provisioning page.
 * Defaults are compiled in; NVS values override on every boot.
 *
 * Key design decision: mqtt_base_topic is per-unit so multiple props
 * can exist on the same broker without topic collision.
 * e.g. permanent Room 3 unit: "escape/room3/phone"
 *      portable unit at venue: "escape/phone"
 *
 * Phone mode: PHONE_AUTO is the default and recommended setting.
 * Both rotary pulse decoding and DTMF detection run simultaneously —
 * the Si32177-C hardware handles both independently. PHONE_AUTO means
 * any POTS phone works without reconfiguration: rotary, touch-tone,
 * or hybrid (some modern phones support both).
 *
 * IMPORTANT — BOM NOTE:
 *   DTMF detection requires the Si32177-C (or Si32175-C / Si32171-C).
 *   The Si32176-C does NOT include hardware DTMF detection and will
 *   silently fail to detect touch-tone digits if substituted to cut cost.
 *   Do NOT substitute Si32176 in any variant of this board.
 *   See hardware/slic-daughterboard/BOM-FLAG-SLIC-VARIANT.md
 */

#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define CONFIG_STR_MAX 128

typedef enum {
    NET_MODE_ETH_PREFERRED = 0,   // Ethernet primary, WiFi fallback (default)
    NET_MODE_WIFI_ONLY,           // WiFi only (portable, no Ethernet hardware)
    NET_MODE_ETH_ONLY,            // Ethernet only (permanent install)
} net_mode_t;

typedef enum {
    PHONE_AUTO = 0,     // Default: rotary + DTMF both active simultaneously.
                        // Works with any POTS phone — rotary, touch-tone, hybrid.
                        // Rotary: loop-current pulse counting via ProSLIC INT.
                        // DTMF:   hardware tone detection in Si32177-C DSP.
                        // No ambiguity: rotary makes no audio tones; touch-tone
                        // makes no loop breaks. Both can run without conflict.

    PHONE_ROTARY,       // Pulse decoding only. DTMF detection disabled in ProSLIC
                        // registers. Use if a touch-tone phone is known to never
                        // be connected and you want to suppress any false DTMF
                        // events from line noise. Rarely needed.

    PHONE_TOUCHTONE,    // DTMF detection only. Pulse decoding ISR disabled.
                        // Use for touch-tone-only installs where rotary pulse
                        // events would be spurious (e.g. very noisy phone lines).
                        // Note: disables * and # from rotary (they don't exist
                        // on rotary anyway, so this is mostly a code clarity flag).
} phone_mode_t;

typedef struct {
    // Network
    net_mode_t  net_mode;
    char        wifi_ssid[64];
    char        wifi_password[64];      // Write-only in WebUI — never read back to browser

    // MQTT
    char        mqtt_broker[CONFIG_STR_MAX];    // e.g. "mqtt://192.168.1.100"
    char        mqtt_base_topic[CONFIG_STR_MAX]; // e.g. "escape/room3/phone"
    char        device_id[32];                  // e.g. "phone-prop-01"

    // Hardware flags
    bool        poe_installed;          // Informational — published to MQTT on boot

    // Phone mode — controls which dialing input methods are active
    phone_mode_t phone_mode;            // Default: PHONE_AUTO (recommended)

    // Audio
    uint8_t     audio_volume;           // 0–100 (maps to ProSLIC gain register)

    // Timing — rotary specific (ignored in PHONE_TOUCHTONE mode)
    uint32_t    inter_digit_ms;         // Inter-digit gap timeout (default 300ms)
    uint32_t    number_complete_ms;     // Full number timeout (default 3000ms)

    // Ring-and-play sequence
    uint32_t    answer_delay_ms;        // Pause after off-hook before queued audio plays.
                                        // Gives player time to get handset to ear.
                                        // Range: 500–5000ms. Default: 1500ms.
                                        // Configurable per-unit via WebUI.

    // DTMF options
    bool        dtmf_pass_star_hash;    // Publish * and # to MQTT (default true)
                                        // Some M3 configs may not handle these;
                                        // set false to suppress if needed
} device_config_t;

// Defaults — used if NVS is empty or corrupt
// DEFAULT_MQTT_BROKER and DEFAULT_MQTT_BASE_TOPIC are defined in device_config.c
// (not here) so they can be blank by default — must be provisioned via WebUI.
#define DEFAULT_DEVICE_ID           "phone-prop-01"
#define DEFAULT_AUDIO_VOLUME        75
#define DEFAULT_INTER_DIGIT_MS      300
#define DEFAULT_NUMBER_COMPLETE_MS  3000
#define DEFAULT_ANSWER_DELAY_MS     1500
#define DEFAULT_DTMF_PASS_STAR_HASH true

esp_err_t   config_load(device_config_t *cfg);
esp_err_t   config_save(const device_config_t *cfg);
esp_err_t   config_reset_to_defaults(device_config_t *cfg);
const char *config_net_mode_str(net_mode_t mode);
const char *config_phone_mode_str(phone_mode_t mode);
