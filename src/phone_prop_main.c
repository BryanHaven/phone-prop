/**
 * phone_prop_main.c
 *
 * Escape Room Telephone Prop Controller
 * ESP32-S3 + Si32177-C ProSLIC
 *
 * State machine, MQTT integration, network init.
 * ProSLIC API integration deferred to Phase 2.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "driver/spi_master.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "device_config.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "network_manager.h"
#include "proslic_hal.h"
#include "dial_rules.h"
#include "web_ui.h"

// SPI host aliases — ESP32-S3 doesn't define the legacy HSPI/VSPI names.
// Per CLAUDE.md: VSPI (SPI2) = ProSLIC bus, HSPI (SPI3) = W5500 + SD bus.
#ifndef HSPI_HOST
#define HSPI_HOST SPI3_HOST
#endif

static const char *TAG = "phone_prop";

// ─── GPIO Defaults (overridden by build flags in platformio.ini) ────────────
// Defaults match the production PCB pinout.
#ifndef SLIC_INT_GPIO
#define SLIC_INT_GPIO    9
#endif
#ifndef SLIC_RST_GPIO
#define SLIC_RST_GPIO    8
#endif
#ifndef SPI_SLIC_CLK
#define SPI_SLIC_CLK    13
#endif
#ifndef SPI_SLIC_CS
#define SPI_SLIC_CS     10
#endif
#ifndef SPI_SLIC_MOSI
#define SPI_SLIC_MOSI   11
#endif
#ifndef SPI_SLIC_MISO
#define SPI_SLIC_MISO   12
#endif
#ifndef SPI_HSPI_CLK
#define SPI_HSPI_CLK    18
#endif
#ifndef SPI_HSPI_MOSI
#define SPI_HSPI_MOSI   19
#endif
#ifndef SPI_HSPI_MISO
#define SPI_HSPI_MISO   20
#endif
#ifndef I2S_PCLK
#define I2S_PCLK         4
#endif
#ifndef I2S_FSYNC
#define I2S_FSYNC        5
#endif
#ifndef I2S_DRX
#define I2S_DRX          6
#endif
#ifndef I2S_DTX
#define I2S_DTX          7
#endif
#ifndef LED_GPIO
#define LED_GPIO        21
#endif

// ─── MQTT Topics (built from mqtt_base_topic at startup) ───────────────────
// All topic strings are populated by build_mqtt_topics() before first use.
static char topic_status[128];
static char topic_dialed[128];
static char topic_number[128];
static char topic_event[128];
static char topic_network[128];
static char topic_cmd_ring[128];
static char topic_cmd_play[128];
static char topic_cmd_hangup[128];
static char topic_cmd_reset[128];
static char topic_cmd_wildcard[132]; // base/command/# — slightly longer than others

static void build_mqtt_topics(const char *base) {
    snprintf(topic_status,       sizeof(topic_status),       "%s/status",         base);
    snprintf(topic_dialed,       sizeof(topic_dialed),       "%s/dialed",         base);
    snprintf(topic_number,       sizeof(topic_number),       "%s/number",         base);
    snprintf(topic_event,        sizeof(topic_event),        "%s/event",          base);
    snprintf(topic_network,      sizeof(topic_network),      "%s/network",        base);
    snprintf(topic_cmd_ring,     sizeof(topic_cmd_ring),     "%s/command/ring",   base);
    snprintf(topic_cmd_play,     sizeof(topic_cmd_play),     "%s/command/play",   base);
    snprintf(topic_cmd_hangup,   sizeof(topic_cmd_hangup),   "%s/command/hangup", base);
    snprintf(topic_cmd_reset,    sizeof(topic_cmd_reset),    "%s/command/reset",  base);
    snprintf(topic_cmd_wildcard, sizeof(topic_cmd_wildcard), "%s/command/#",      base);
}

// ─── Phone State Machine ───────────────────────────────────────────────────
typedef enum {
    STATE_IDLE,             // On-hook, waiting
    STATE_OFF_HOOK,         // Handset lifted, playing dial tone
    STATE_DIALING,          // Receiving rotary pulse digits
    STATE_NUMBER_COMPLETE,  // Full number dialed, awaiting MMM command
    STATE_PLAYING_AUDIO,    // Playing message through handset
    STATE_BUSY,             // Playing busy signal
    STATE_RINGING,          // Ringing the phone (inbound)
    STATE_FAULT             // Error state
} phone_state_t;

static phone_state_t phone_state = STATE_IDLE;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static device_config_t g_cfg;    // Loaded from NVS at boot
static dial_ruleset_t  g_rules;  // Loaded from SPIFFS at boot

// ─── Dialing State (shared by both rotary and DTMF paths) ─────────────────
// Both paths produce single digits and feed the same number accumulator.
// Rotary:    pulses → digit via on_pulse_detected()
// DTMF:      tone pair → digit via on_dtmf_digit()
// Number assembly and MQTT publish are path-agnostic.

static int     digit_count     = 0;
static char    dialed_number[16] = {0};

// Rotary-specific state
static int     pulse_count     = 0;
static int64_t last_pulse_time = 0;

// Input source tracking (for diagnostics / MQTT)
typedef enum { INPUT_UNKNOWN, INPUT_ROTARY, INPUT_DTMF } input_source_t;
static input_source_t last_input_source = INPUT_UNKNOWN;

// ─── I2S PCM Handles ───────────────────────────────────────────────────────
static i2s_chan_handle_t i2s_tx_handle = NULL;
static i2s_chan_handle_t i2s_rx_handle = NULL;

// ─── MQTT Publish Helper ───────────────────────────────────────────────────
static void mqtt_publish(const char *topic, const char *data) {
    if (mqtt_client == NULL) return;
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, data, 0, 1, 0);
    ESP_LOGI(TAG, "MQTT publish [%s]: %s (msg_id=%d)", topic, data, msg_id);
}

// ─── Network Status Callback ───────────────────────────────────────────────
static void on_network_status(network_status_t status) {
    const char *status_str =
        status == NET_ETHERNET ? "ethernet" :
        status == NET_WIFI     ? "wifi"     : "disconnected";
    mqtt_publish(topic_network, status_str);
}

// ─── State Transition ──────────────────────────────────────────────────────
static void set_state(phone_state_t new_state) {
    ESP_LOGI(TAG, "State: %d → %d", phone_state, new_state);
    phone_state = new_state;

    switch (new_state) {
        case STATE_IDLE:
            mqtt_publish(topic_status, "on_hook");
            // TODO: proslic_stop_audio();
            // TODO: proslic_stop_ring();
            // TODO: led_set_color(BLUE);
            break;

        case STATE_OFF_HOOK:
            mqtt_publish(topic_status, "off_hook");
            // TODO: proslic_play_audio("dial_tone.wav");
            // TODO: led_set_color(GREEN);
            // Reset both rotary and DTMF accumulator state
            pulse_count       = 0;
            digit_count       = 0;
            last_input_source = INPUT_UNKNOWN;
            memset(dialed_number, 0, sizeof(dialed_number));
            break;

        case STATE_RINGING:
            mqtt_publish(topic_event, "ring_start");
            // TODO: proslic_start_ring();
            // TODO: led_set_color(AMBER_BLINK);
            break;

        case STATE_PLAYING_AUDIO:
            // TODO: led_set_color(GREEN_BLINK);
            break;

        case STATE_BUSY:
            // TODO: proslic_play_audio("busy_signal.wav");
            break;

        case STATE_FAULT:
            ESP_LOGE(TAG, "FAULT state entered");
            // TODO: led_set_color(RED);
            break;

        default:
            break;
    }
}

// ─── Shared Digit Handler ──────────────────────────────────────────────────
/**
 * Common entry point for both rotary and DTMF digit delivery.
 * Appends to dialed_number, publishes to MQTT, transitions state.
 *
 * digit_char: '0'–'9', '*', or '#'
 * source:     INPUT_ROTARY or INPUT_DTMF
 *
 * Note: '*' and '#' only arrive from DTMF path. They are passed through
 * to MQTT if dtmf_pass_star_hash is true in device config. MMM can
 * choose to act on them or ignore them.
 */
static void on_digit(char digit_char, input_source_t source) {
    // Filter * and # based on config
    if ((digit_char == '*' || digit_char == '#') && !g_cfg.dtmf_pass_star_hash) {
        ESP_LOGI(TAG, "Suppressing '%c' (dtmf_pass_star_hash=false)", digit_char);
        return;
    }

    // Append to number buffer (guard against overflow)
    if (digit_count < (int)(sizeof(dialed_number) - 1)) {
        dialed_number[digit_count++] = digit_char;
        dialed_number[digit_count]   = '\0';
    }

    char digit_str[3] = {digit_char, '\0', '\0'}; // handles *, #, 0-9
    last_input_source = source;

    mqtt_publish(topic_dialed, digit_str);
    ESP_LOGI(TAG, "Digit [%s]: '%c' (number so far: %s)",
             source == INPUT_DTMF ? "DTMF" : "ROTARY",
             digit_char, dialed_number);

    set_state(STATE_DIALING);
}

// ─── DTMF Handler ─────────────────────────────────────────────────────────
/**
 * Called when Si32177 ProSLIC reports a DTMF digit detection event.
 *
 * The Si32177-C hardware DSP detects the dual-tone pair and stores the
 * decoded digit in the DTMF status register. The INT pin fires to
 * signal a new digit is available.
 *
 * ProSLIC API call (when integrated):
 *   Si3217x_DTMFReadDigit(pProslic, &dtmf_digit);
 *
 * dtmf_digit values from ProSLIC API:
 *   0x0–0x9: digits 0–9
 *   0xA:     *  (star)
 *   0xB:     #  (hash / pound)
 *   0xC–0xF: A, B, C, D (extended DTMF — rare, ignore for phone prop)
 *
 * DTMF detection requires Si32177-C (or Si32175-C / Si32171-C).
 * Si32176-C does NOT have hardware DTMF — do not substitute.
 *
 * Only active when phone_mode is PHONE_AUTO or PHONE_TOUCHTONE.
 * TODO: Wire to ProSLIC API interrupt callback for DTMF event type.
 */
static void on_dtmf_digit(uint8_t proslic_dtmf_val) {
    if (g_cfg.phone_mode == PHONE_ROTARY) return; // DTMF disabled by config

    if (phone_state != STATE_OFF_HOOK &&
        phone_state != STATE_DIALING  &&
        phone_state != STATE_NUMBER_COMPLETE) {
        ESP_LOGW(TAG, "DTMF digit received in unexpected state %d — ignored",
                 phone_state);
        return;
    }

    char digit_char;
    switch (proslic_dtmf_val) {
        case 0x0: case 0x1: case 0x2: case 0x3: case 0x4:
        case 0x5: case 0x6: case 0x7: case 0x8: case 0x9:
            digit_char = '0' + proslic_dtmf_val;
            break;
        case 0xA: digit_char = '*'; break;
        case 0xB: digit_char = '#'; break;
        default:
            ESP_LOGD(TAG, "Extended DTMF 0x%X ignored", proslic_dtmf_val);
            return;
    }

    on_digit(digit_char, INPUT_DTMF);
}

// ─── Rotary Pulse Decoder ─────────────────────────────────────────────────
/**
 * Called from ProSLIC INT interrupt handler on each loop-current break.
 * Each break represents one pulse. Pulses are counted per burst;
 * the inter-digit gap (>inter_digit_ms) separates digits.
 *
 * Pulse-to-digit mapping (standard North American rotary):
 *   1 pulse  = '1' ... 9 pulses = '9', 10 pulses = '0'
 *
 * Timing: break ~60ms, make ~40ms, inter-digit >300ms.
 *
 * Only active when phone_mode is PHONE_AUTO or PHONE_ROTARY.
 * TODO: Wire to ProSLIC API interrupt callback for hook-flash event type.
 */
static void on_pulse_detected(void) {
    if (g_cfg.phone_mode == PHONE_TOUCHTONE) return; // Rotary disabled by config

    int64_t now = esp_timer_get_time(); // microseconds
    int64_t inter_digit_us = (int64_t)g_cfg.inter_digit_ms * 1000;

    if (pulse_count > 0 && (now - last_pulse_time) > inter_digit_us) {
        // Inter-digit gap detected — commit the accumulated pulse count as a digit
        int digit = (pulse_count == 10) ? 0 : pulse_count;
        on_digit('0' + digit, INPUT_ROTARY);
        pulse_count = 0;
    }

    pulse_count++;
    last_pulse_time = now;
}

/**
 * Periodic check: if rotary dialing has gone quiet for number_complete_ms,
 * commit the final pending digit and mark the number complete.
 * DTMF doesn't need this — each tone is a discrete event with no ambiguity.
 */
static void check_dialing_timeout(void) {
    if (phone_state != STATE_DIALING) return;
    if (g_cfg.phone_mode == PHONE_TOUCHTONE) return; // DTMF has no timeout concept

    int64_t now = esp_timer_get_time();
    int64_t complete_us = (int64_t)g_cfg.number_complete_ms * 1000;

    if ((now - last_pulse_time) > complete_us) {
        // Commit any final pending pulse burst
        if (pulse_count > 0) {
            int digit = (pulse_count == 10) ? 0 : pulse_count;
            on_digit('0' + digit, INPUT_ROTARY);
            pulse_count = 0;
        }

        if (digit_count > 0) {
            ESP_LOGI(TAG, "Number complete [%s]: %s",
                     last_input_source == INPUT_DTMF ? "DTMF" : "ROTARY",
                     dialed_number);
            mqtt_publish(topic_number, dialed_number);
            set_state(STATE_NUMBER_COMPLETE);
        }
    }
}

/**
 * For DTMF: # is a natural "number complete" signal.
 * If the caller dials digits followed by #, treat # as "send" and
 * immediately publish the number (minus the #, which is already
 * published separately as a digit if dtmf_pass_star_hash is true).
 * This mirrors how modern phone systems treat # as a confirm key.
 */
static void check_dtmf_number_complete(char last_digit) {
    if (last_digit != '#') return;
    if (digit_count <= 1) return; // Only # was dialed, nothing to complete

    // Strip trailing # from the number before publishing
    char number_without_hash[16] = {0};
    strncpy(number_without_hash, dialed_number, digit_count - 1);

    ESP_LOGI(TAG, "Number complete [DTMF, # terminated]: %s", number_without_hash);
    mqtt_publish(topic_number, number_without_hash);
    set_state(STATE_NUMBER_COMPLETE);
}

// ─── Hook State Handler ────────────────────────────────────────────────────
/**
 * Called when ProSLIC reports hook state change.
 * TODO: Wire to ProSLIC API interrupt callback (hook detection event)
 */
static void on_hook_change(bool off_hook) {
    ESP_LOGI(TAG, "Hook state: %s", off_hook ? "OFF-HOOK" : "ON-HOOK");

    if (off_hook) {
        if (phone_state == STATE_IDLE || phone_state == STATE_RINGING) {
            set_state(STATE_OFF_HOOK);
        }
    } else {
        // Capture ring state before set_state() overwrites it
        bool was_ringing = (phone_state == STATE_RINGING);
        set_state(STATE_IDLE);
        if (was_ringing) {
            mqtt_publish(topic_event, "ring_stop");
        }
    }
}

// ─── MQTT Command Handler ──────────────────────────────────────────────────
static void on_mqtt_command(const char *topic, const char *data) {
    ESP_LOGI(TAG, "MQTT command [%s]: %s", topic, data);

    if (strcmp(topic, topic_cmd_ring) == 0) {
        if (strcmp(data, "start") == 0 && phone_state == STATE_IDLE) {
            set_state(STATE_RINGING);
        } else if (strcmp(data, "stop") == 0) {
            // TODO: proslic_stop_ring();
            mqtt_publish(topic_event, "ring_stop");
            set_state(STATE_IDLE);
        }

    } else if (strcmp(topic, topic_cmd_play) == 0) {
        if (phone_state == STATE_OFF_HOOK ||
            phone_state == STATE_NUMBER_COMPLETE ||
            phone_state == STATE_DIALING) {
            // TODO: proslic_play_audio(data);  // data = filename
            set_state(STATE_PLAYING_AUDIO);
        }

    } else if (strcmp(topic, topic_cmd_hangup) == 0) {
        set_state(STATE_IDLE);

    } else if (strcmp(topic, topic_cmd_reset) == 0) {
        set_state(STATE_IDLE);
        pulse_count       = 0;
        digit_count       = 0;
        last_input_source = INPUT_UNKNOWN;
        memset(dialed_number, 0, sizeof(dialed_number));
    }
}

// ─── MQTT Event Handler ────────────────────────────────────────────────────
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            esp_mqtt_client_subscribe(mqtt_client, topic_cmd_wildcard, 1);
            // Announce current state on reconnect
            mqtt_publish(topic_status, "on_hook");
            mqtt_publish(topic_network,
                network_get_status() == NET_ETHERNET ? "ethernet" :
                network_get_status() == NET_WIFI     ? "wifi"     : "disconnected");
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected — will auto-reconnect");
            break;

        case MQTT_EVENT_DATA:
            {
                char topic[128] = {0};
                char data[256]  = {0};
                int topic_len = event->topic_len < 127 ? event->topic_len : 127;
                int data_len  = event->data_len  < 255 ? event->data_len  : 255;
                strncpy(topic, event->topic, topic_len);
                strncpy(data,  event->data,  data_len);
                on_mqtt_command(topic, data);
            }
            break;

        default:
            break;
    }
}

// ─── I2S PCM Init ──────────────────────────────────────────────────────────
/**
 * Configures I2S0 as PCM master for Si32177 audio interface.
 *
 * Phase 1A: allocates the peripheral and configures GPIOs.
 * Phase 2 note: ProSLIC PCLK must be 2.048 MHz (8 kHz × 256 clocks/frame).
 *   I2S_STD_CLK_DEFAULT_CONFIG(8000) with 16-bit mono gives BCLK ≈ 128 kHz —
 *   this is too slow for the ProSLIC. Phase 2: set clk_cfg.bclk_div to
 *   achieve 2.048 MHz on the BCLK line, and verify with a scope against
 *   the Si32177 datasheet Figure 31 PCM timing diagram.
 */
static esp_err_t i2s_pcm_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(8000),
        .slot_cfg = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_PCLK,    // 2×10 header pin 9 → Si32177 PCLK
            .ws   = I2S_FSYNC,   // 2×10 header pin 10 → Si32177 FSYNC
            .dout = I2S_DRX,     // 2×10 header pin 11 → Si32177 DRX (ESP32 out)
            .din  = I2S_DTX,     // 2×10 header pin 12 ← Si32177 DTX (ESP32 in)
            .invert_flags = {0},
        },
    };

    ret = i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S TX init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S RX init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_handle));

    ESP_LOGI(TAG, "I2S PCM initialized (8 kHz, 16-bit, mono, master)");
    ESP_LOGW(TAG, "I2S PCM: BCLK ~128 kHz — Phase 2 must tune to 2.048 MHz for ProSLIC");
    return ESP_OK;
}

// ─── SD Card Init (Phase 1B) ───────────────────────────────────────────────
/**
 * Phase 1B: implement SDSPI driver init here.
 *   Production: SPI_SD_CS on shared HSPI bus with W5500.
 *   Waveshare:  SPI_SD_CS=38, separate SPI bus on GPIO 35-38 breakout.
 *   Audio files: 8 kHz, 8-bit µ-law WAV in /audio/system/ and /audio/messages/.
 */
static esp_err_t sd_card_init(void) {
    ESP_LOGI(TAG, "SD card init: deferred to Phase 1B");
    return ESP_OK;
}

// ─── Main Application Task ─────────────────────────────────────────────────
static void phone_prop_task(void *pvParameters) {
    ESP_LOGI(TAG, "Phone prop task started");

    while (1) {
        // Periodic checks
        check_dialing_timeout();

        // TODO: Poll ProSLIC status register for hook/ring events
        //       (or handle via INT GPIO interrupt — preferred for Phase 2)

        vTaskDelay(pdMS_TO_TICKS(10)); // 10 ms polling interval
    }
}

// ─── App Main ──────────────────────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "Phone Prop Controller starting...");

    // ── NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ── SPIFFS (dial rules + future WebUI assets)
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path              = "/spiffs",
        .partition_label        = "spiffs",
        .max_files              = 8,
        .format_if_mount_failed = true,
    };
    ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK)
        ESP_LOGW(TAG, "SPIFFS mount failed: %s (dial rules unavailable)", esp_err_to_name(ret));

    // ── Per-unit config (falls back to defaults if NVS is empty)
    config_load(&g_cfg);
    ESP_LOGI(TAG, "Phone mode: %s | * and # pass-through: %s",
             config_phone_mode_str(g_cfg.phone_mode),
             g_cfg.dtmf_pass_star_hash ? "enabled" : "disabled");

    // ── MQTT topic strings — built from per-unit base topic in NVS
    // ── Dial rules (SPIFFS-backed, empty set if file absent)
    dial_rules_load(&g_rules);

    build_mqtt_topics(g_cfg.mqtt_base_topic);
    ESP_LOGI(TAG, "MQTT base topic: %s | broker: %s",
             g_cfg.mqtt_base_topic, g_cfg.mqtt_broker);

    // ── GPIO ISR service — required by W5500 INT pin and any other GPIO interrupts.
    //    Must be installed before network_manager_init() registers the W5500 ISR.
    gpio_install_isr_service(0);

    // ── HSPI bus (W5500 + SD card on production; W5500 only on Waveshare Phase 1A)
    //    Must be initialized before network_manager_init() adds the W5500 device.
    spi_bus_config_t hspi_bus_cfg = {
        .mosi_io_num     = SPI_HSPI_MOSI,
        .miso_io_num     = SPI_HSPI_MISO,
        .sclk_io_num     = SPI_HSPI_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };
    ret = spi_bus_initialize(HSPI_HOST, &hspi_bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means bus already initialized — safe to continue
        ESP_LOGE(TAG, "HSPI bus init failed: %s", esp_err_to_name(ret));
    }

    // ── Network: Ethernet (W5500) primary, WiFi fallback
    //    network_manager_init() creates the default event loop and netif.
    network_set_status_callback(on_network_status);
    ESP_ERROR_CHECK(network_manager_init(g_cfg.wifi_ssid, g_cfg.wifi_password));

    // ── SoftAP: start when no WiFi SSID is configured (first-boot provisioning).
    //    Not started for ETH_ONLY deployments — Ethernet must be connected for access.
    if (strlen(g_cfg.wifi_ssid) == 0 && g_cfg.net_mode != NET_MODE_ETH_ONLY) {
        char ap_ssid[48];  // "PhoneProp-" (10) + device_id (≤31) + null
        snprintf(ap_ssid, sizeof(ap_ssid), "PhoneProp-%s", g_cfg.device_id);
        network_start_ap(ap_ssid);
    }

    // ── WebUI: provisioning page always available (AP IP, STA IP, or Ethernet IP)
    web_ui_start(&g_cfg, &g_rules);

    if (!network_wait_connected(30000)) {
        ESP_LOGW(TAG, "No network after 30 s — MQTT will connect when available");
    }

    // ── ProSLIC SPI bus (VSPI, Phase 1A: init bus and verify comms if board present)
    ret = proslic_spi_init();
    if (ret == ESP_OK) {
        if (proslic_verify_spi()) {
            ESP_LOGI(TAG, "ProSLIC SPI communication verified");
        } else {
            ESP_LOGW(TAG, "ProSLIC SPI verify failed — no daughterboard connected?");
        }
    } else {
        ESP_LOGE(TAG, "ProSLIC SPI init failed: %s", esp_err_to_name(ret));
    }
    // Phase 2: proslic_reset(); then ProSLIC API init + DTMF config

    // ── I2S PCM (Phase 1A: configure peripheral; BCLK timing tuned in Phase 2)
    ret = i2s_pcm_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S PCM init failed: %s", esp_err_to_name(ret));
    }

    // ── SD card (Phase 1B)
    sd_card_init();

    // ── WebUI provisioning server (Phase 2+)
    // TODO: webui_server_init();

    // ── MQTT client — started after network init; auto-reconnects if not yet up
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = g_cfg.mqtt_broker,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    // ── Main prop task
    xTaskCreate(phone_prop_task, "phone_prop", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Initialization complete. State: IDLE");
}
