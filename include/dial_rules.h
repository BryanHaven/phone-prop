/**
 * dial_rules.h
 *
 * Per-unit dial behaviour rules — stored in SPIFFS at /spiffs/dial_rules.json.
 *
 * A rule maps a dialed number string to an action.  Rules are evaluated in
 * order; the first match wins.  Unmatched numbers are silently ignored.
 *
 * MQTT: when a rule fires its mqtt_event (if non-empty) is published to
 *       {base}/event.  The normal {base}/number publish still happens
 *       regardless.
 */

#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define DIAL_RULE_NUMBER_MAX  16    // e.g. "1234567890*#"
#define DIAL_RULE_FILE_MAX    64    // e.g. "msg_clue1.wav"
#define DIAL_RULE_EVENT_MAX   64    // e.g. "dialed_911"
#define DIAL_RULES_MAX        32

#define DIAL_RULES_PATH       "/spiffs/dial_rules.json"

typedef enum {
    DIAL_ACTION_PLAY = 0,   // play WAV file from SD card — requires file field
    DIAL_ACTION_BUSY,       // busy signal tone
    DIAL_ACTION_REORDER,    // fast busy / reorder tone
    DIAL_ACTION_RINGBACK,   // ringback tone
    DIAL_ACTION_DIAL_TONE,  // dial tone
    DIAL_ACTION_SILENCE,    // silence / dead air
    DIAL_ACTION_IGNORE,     // no response
} dial_action_t;

typedef struct {
    char          number[DIAL_RULE_NUMBER_MAX];
    dial_action_t action;
    char          file[DIAL_RULE_FILE_MAX];        // only used for DIAL_ACTION_PLAY
    char          mqtt_event[DIAL_RULE_EVENT_MAX]; // empty string = no MQTT publish
} dial_rule_t;

typedef struct {
    dial_rule_t rules[DIAL_RULES_MAX];
    int         count;
} dial_ruleset_t;

/**
 * Load rules from SPIFFS.  Populates rs; starts empty if file not found.
 * SPIFFS must be mounted before calling.
 */
esp_err_t           dial_rules_load(dial_ruleset_t *rs);

/**
 * Save rules to SPIFFS, replacing the existing file.
 */
esp_err_t           dial_rules_save(const dial_ruleset_t *rs);

/**
 * Return the first rule matching number, or NULL if none.
 */
const dial_rule_t  *dial_rules_find(const dial_ruleset_t *rs, const char *number);

const char         *dial_action_str(dial_action_t action);
dial_action_t       dial_action_from_str(const char *s);
