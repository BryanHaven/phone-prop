/**
 * dial_rules.c
 * SPIFFS-backed dial behaviour rules for Phone Prop.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"
#include "dial_rules.h"

static const char *TAG = "dial_rules";

// ─── Action string helpers ───────────────────────────────────────────────────

const char *dial_action_str(dial_action_t action) {
    switch (action) {
        case DIAL_ACTION_PLAY:      return "play";
        case DIAL_ACTION_BUSY:      return "busy";
        case DIAL_ACTION_REORDER:   return "reorder";
        case DIAL_ACTION_RINGBACK:  return "ringback";
        case DIAL_ACTION_DIAL_TONE: return "dial_tone";
        case DIAL_ACTION_SILENCE:   return "silence";
        case DIAL_ACTION_IGNORE:    return "ignore";
        default:                    return "ignore";
    }
}

dial_action_t dial_action_from_str(const char *s) {
    if (!s)                        return DIAL_ACTION_IGNORE;
    if (!strcmp(s, "play"))        return DIAL_ACTION_PLAY;
    if (!strcmp(s, "busy"))        return DIAL_ACTION_BUSY;
    if (!strcmp(s, "reorder"))     return DIAL_ACTION_REORDER;
    if (!strcmp(s, "ringback"))    return DIAL_ACTION_RINGBACK;
    if (!strcmp(s, "dial_tone"))   return DIAL_ACTION_DIAL_TONE;
    if (!strcmp(s, "silence"))     return DIAL_ACTION_SILENCE;
    return DIAL_ACTION_IGNORE;
}

// ─── Load ────────────────────────────────────────────────────────────────────

esp_err_t dial_rules_load(dial_ruleset_t *rs) {
    memset(rs, 0, sizeof(*rs));

    FILE *f = fopen(DIAL_RULES_PATH, "r");
    if (!f) {
        ESP_LOGI(TAG, "No %s — starting with empty rule set", DIAL_RULES_PATH);
        return ESP_OK;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 8192) {
        ESP_LOGW(TAG, "Unexpected file size %ld — ignoring", size);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *arr = cJSON_GetObjectItem(root, "rules");
    int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
    if (n > DIAL_RULES_MAX) n = DIAL_RULES_MAX;

    for (int i = 0; i < n; i++) {
        cJSON *obj = cJSON_GetArrayItem(arr, i);
        cJSON *num = cJSON_GetObjectItem(obj, "number");
        if (!cJSON_IsString(num) || !strlen(num->valuestring)) continue;

        dial_rule_t *r = &rs->rules[rs->count++];
        strncpy(r->number, num->valuestring, DIAL_RULE_NUMBER_MAX - 1);

        cJSON *act  = cJSON_GetObjectItem(obj, "action");
        r->action   = cJSON_IsString(act) ? dial_action_from_str(act->valuestring) : DIAL_ACTION_IGNORE;

        cJSON *file = cJSON_GetObjectItem(obj, "file");
        if (cJSON_IsString(file))
            strncpy(r->file, file->valuestring, DIAL_RULE_FILE_MAX - 1);

        cJSON *ev   = cJSON_GetObjectItem(obj, "mqtt_event");
        if (cJSON_IsString(ev))
            strncpy(r->mqtt_event, ev->valuestring, DIAL_RULE_EVENT_MAX - 1);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %d rule(s)", rs->count);
    return ESP_OK;
}

// ─── Save ────────────────────────────────────────────────────────────────────

esp_err_t dial_rules_save(const dial_ruleset_t *rs) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "rules");

    for (int i = 0; i < rs->count; i++) {
        const dial_rule_t *r = &rs->rules[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "number",     r->number);
        cJSON_AddStringToObject(obj, "action",     dial_action_str(r->action));
        cJSON_AddStringToObject(obj, "file",       r->file);
        cJSON_AddStringToObject(obj, "mqtt_event", r->mqtt_event);
        cJSON_AddItemToArray(arr, obj);
    }

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;

    FILE *f = fopen(DIAL_RULES_PATH, "w");
    if (!f) {
        free(json);
        ESP_LOGE(TAG, "Cannot write %s", DIAL_RULES_PATH);
        return ESP_FAIL;
    }
    fputs(json, f);
    fclose(f);
    free(json);

    ESP_LOGI(TAG, "Saved %d rule(s)", rs->count);
    return ESP_OK;
}

// ─── Lookup ──────────────────────────────────────────────────────────────────

const dial_rule_t *dial_rules_find(const dial_ruleset_t *rs, const char *number) {
    for (int i = 0; i < rs->count; i++) {
        if (!strcmp(rs->rules[i].number, number))
            return &rs->rules[i];
    }
    return NULL;
}
