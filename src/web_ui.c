/**
 * web_ui.c
 * HTTP provisioning + management server for Phone Prop.
 *
 * Three-tab interface:
 *   Config     — device, network, MQTT, hardware, phone, audio/timing settings
 *   Dial Rules — per-number behaviour (play file, tone, ignore, …) stored in SPIFFS
 *   Audio      — file list + upload to SD card (/sdcard/audio/messages/)
 *
 * API:
 *   GET  /             — HTML page (embedded, no SPIFFS dependency)
 *   GET  /api/config   — current config JSON (password excluded)
 *   POST /api/config   — save config to NVS
 *   POST /api/reboot   — 500 ms delayed restart
 *   GET  /api/rules    — dial ruleset JSON
 *   POST /api/rules    — save ruleset to SPIFFS
 *   GET  /api/audio    — file list from SD card
 *   POST /api/audio    — upload WAV; ?name=filename.wav, raw binary body
 *   DELETE /api/audio  — delete file; ?name=filename.wav
 *
 * SD card access uses the VFS path /sdcard (mounted by Phase 1B sd_card_init).
 * If not mounted, audio endpoints return {"mounted":false}.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "cJSON.h"
#include "device_config.h"
#include "dial_rules.h"
#include "web_ui.h"

static const char *TAG     = "web_ui";
static httpd_handle_t s_server = NULL;
static device_config_t  *s_cfg   = NULL;
static dial_ruleset_t   *s_rules = NULL;

#define AUDIO_DIR "/sdcard/audio/messages"

// ─── Embedded HTML ───────────────────────────────────────────────────────────
// Single-quoted HTML attributes and JS strings throughout — no C escaping needed.
// DOM API used for all dynamic content — no innerHTML attribute-quoting issues.
static const char HTML[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Phone Prop</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:sans-serif;background:#1a1a2e;color:#eee;padding:20px}"
    "h1{color:#e94560;margin-bottom:16px}"
    ".tabs{display:flex;gap:4px;margin-bottom:16px}"
    ".tab{padding:8px 16px;border:none;border-radius:4px;cursor:pointer;"
         "background:#16213e;color:#aaa;font-size:.9em}"
    ".tab.on{background:#e94560;color:#fff}"
    ".s{background:#16213e;border-radius:8px;padding:16px;margin-bottom:16px}"
    "h2{font-size:.85em;text-transform:uppercase;letter-spacing:1px;"
        "color:#8ecae6;margin-bottom:12px}"
    "label{display:block;margin-bottom:4px;font-size:.85em;color:#aaa}"
    "input,select{width:100%;padding:8px;background:#0f3460;border:1px solid #333;"
        "border-radius:4px;color:#eee;font-size:1em;margin-bottom:12px}"
    "input[type=checkbox]{width:auto;margin-right:8px}"
    "input[type=file]{background:transparent;border:none;padding:4px 0}"
    ".cr{display:flex;align-items:center;margin-bottom:12px}"
    ".cr label{margin:0;color:#eee}"
    "button{padding:8px 16px;border:none;border-radius:4px;"
        "cursor:pointer;font-size:.9em;margin-right:6px;margin-bottom:6px}"
    ".sv{background:#e94560;color:#fff}"
    ".rb{background:#444;color:#eee}"
    ".del{background:#7f1d1d;color:#eee;padding:4px 8px}"
    ".add{background:#1b4332;color:#eee}"
    "#msg-cfg,#msg-rules,#msg-audio"
        "{padding:10px;border-radius:4px;margin-bottom:12px;display:none}"
    ".ok{background:#1b4332;border:1px solid #40916c}"
    ".er{background:#7f1d1d;border:1px solid #991b1b}"
    "table{width:100%;border-collapse:collapse;margin-bottom:12px}"
    "th{text-align:left;padding:6px 8px;color:#8ecae6;border-bottom:1px solid #333;"
       "font-size:.85em}"
    "td{padding:6px 8px;border-bottom:1px solid #222;font-size:.9em}"
    "small{color:#888;font-size:.8em}"
    "</style></head><body>"
    "<h1>Phone Prop</h1>"
    "<div class='tabs'>"
    "<button class='tab on' onclick='showTab(0)'>Config</button>"
    "<button class='tab'    onclick='showTab(1)'>Dial Rules</button>"
    "<button class='tab'    onclick='showTab(2)'>Audio Files</button>"
    "</div>"

    // ── Config tab ───────────────────────────────────────────────────────────
    "<div id='t0'>"
    "<div id='msg-cfg'></div>"
    "<form id='f'>"
    "<div class='s'><h2>Device</h2>"
    "<label>Device ID</label>"
    "<input name='device_id' maxlength='31'>"
    "</div>"
    "<div class='s'><h2>Network</h2>"
    "<label>Mode</label>"
    "<select name='net_mode'>"
    "<option value='0'>Ethernet preferred (WiFi fallback)</option>"
    "<option value='1'>WiFi only</option>"
    "<option value='2'>Ethernet only</option>"
    "</select>"
    "<label>WiFi SSID</label>"
    "<input name='wifi_ssid' maxlength='63'>"
    "<label>WiFi Password <small>(blank = keep current)</small></label>"
    "<input name='wifi_password' type='password' maxlength='63' placeholder='leave blank to keep'>"
    "</div>"
    "<div class='s'><h2>MQTT</h2>"
    "<label>Broker URL</label>"
    "<input name='mqtt_broker' maxlength='127'>"
    "<label>Base Topic</label>"
    "<input name='mqtt_base_topic' maxlength='127'>"
    "</div>"
    "<div class='s'><h2>Hardware</h2>"
    "<div class='cr'><input type='checkbox' name='poe_installed' id='poe'>"
    "<label for='poe'>PoE module installed</label></div>"
    "</div>"
    "<div class='s'><h2>Phone</h2>"
    "<label>Dial Mode</label>"
    "<select name='phone_mode'>"
    "<option value='0'>Auto (rotary + DTMF)</option>"
    "<option value='1'>Rotary only</option>"
    "<option value='2'>Touch-tone only</option>"
    "</select>"
    "<div class='cr'><input type='checkbox' name='dtmf_pass_star_hash' id='sh'>"
    "<label for='sh'>Pass * and # to MQTT</label></div>"
    "</div>"
    "<div class='s'><h2>Audio &amp; Timing</h2>"
    "<label>Volume (0-100)</label>"
    "<input name='audio_volume' type='number' min='0' max='100'>"
    "<label>Inter-digit gap (ms)</label>"
    "<input name='inter_digit_ms' type='number' min='100' max='2000'>"
    "<label>Number complete timeout (ms)</label>"
    "<input name='number_complete_ms' type='number' min='1000' max='10000'>"
    "</div>"
    "<button type='submit' class='sv'>Save Configuration</button>"
    "<button type='button' class='rb' onclick='rb()'>Reboot</button>"
    "</form>"
    "</div>"

    // ── Dial Rules tab ───────────────────────────────────────────────────────
    "<div id='t1' style='display:none'>"
    "<div id='msg-rules'></div>"
    "<div class='s'><h2>Add Rule</h2>"
    "<label>Dialed Number</label>"
    "<input id='rn' maxlength='15' placeholder='e.g. 911'>"
    "<label>Action</label>"
    "<select id='ra' onchange='toggleFile()'>"
    "<option value='play'>Play File</option>"
    "<option value='busy'>Busy Tone</option>"
    "<option value='reorder'>Reorder Tone</option>"
    "<option value='ringback'>Ringback</option>"
    "<option value='dial_tone'>Dial Tone</option>"
    "<option value='silence'>Silence</option>"
    "<option value='ignore'>Ignore</option>"
    "</select>"
    "<div id='rf'>"
    "<label>Audio File <small>(filename on SD card)</small></label>"
    "<input id='rfile' maxlength='63' placeholder='e.g. msg_clue1.wav'>"
    "</div>"
    "<label>MQTT Event <small>(optional — published to {base}/event)</small></label>"
    "<input id='rev' maxlength='63' placeholder='e.g. dialed_911'>"
    "<button class='add' onclick='addRule()'>Add Rule</button>"
    "</div>"
    "<div class='s'><h2>Current Rules</h2>"
    "<table>"
    "<thead><tr><th>Number</th><th>Action</th><th>File</th>"
    "<th>MQTT Event</th><th></th></tr></thead>"
    "<tbody id='rtb'></tbody>"
    "</table>"
    "<button class='sv' onclick='saveRules()'>Save All Rules</button>"
    "</div>"
    "</div>"

    // ── Audio Files tab ──────────────────────────────────────────────────────
    "<div id='t2' style='display:none'>"
    "<div id='msg-audio'></div>"
    "<div class='s'><h2>Upload Audio File</h2>"
    "<small>Format: WAV, 8 kHz, 8-bit &#181;-Law, mono</small>"
    "<input id='afile' type='file' accept='.wav' style='margin-top:8px'>"
    "<button class='sv' onclick='uploadAudio()' style='margin-top:4px'>Upload</button>"
    "</div>"
    "<div class='s'><h2>Files on SD Card</h2>"
    "<div id='alist'></div>"
    "</div>"
    "</div>"

    // ── JavaScript ───────────────────────────────────────────────────────────
    "<script>"

    // Tab switching
    "const TABS=['t0','t1','t2'];"
    "function showTab(n){"
      "TABS.forEach((id,i)=>{"
        "document.getElementById(id).style.display=i===n?'block':'none';"
      "});"
      "document.querySelectorAll('.tab').forEach((b,i)=>{"
        "b.classList.toggle('on',i===n);"
      "});"
      "if(n===1)loadRules();"
      "if(n===2)loadAudio();"
    "}"

    // ── Config tab JS ────────────────────────────────────────────────────────
    "async function loadCfg(){"
      "const r=await fetch('/api/config');"
      "const d=await r.json();"
      "const f=document.getElementById('f');"
      "Object.keys(d).forEach(k=>{"
        "const e=f.elements[k];"
        "if(!e)return;"
        "e.type==='checkbox'?e.checked=d[k]:e.value=d[k];"
      "});"
    "}"
    "document.getElementById('f').onsubmit=async e=>{"
      "e.preventDefault();"
      "const f=e.target,d={};"
      "[...f.elements].forEach(el=>{"
        "if(!el.name)return;"
        "d[el.name]=el.type==='checkbox'?el.checked:"
                   "el.type==='number'?Number(el.value):el.value;"
      "});"
      "const m=document.getElementById('msg-cfg');"
      "try{"
        "const r=await fetch('/api/config',{method:'POST',"
          "headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});"
        "const j=await r.json();"
        "m.textContent=j.status==='ok'?'Saved! Reboot to apply changes.':'Error: '+j.error;"
        "m.className=j.status==='ok'?'ok':'er';"
      "}catch(ex){m.textContent='Save failed: '+ex.message;m.className='er';}"
      "m.style.display='block';"
    "};"
    "async function rb(){"
      "await fetch('/api/reboot',{method:'POST'});"
      "const m=document.getElementById('msg-cfg');"
      "m.textContent='Rebooting\u2026';m.className='ok';m.style.display='block';"
    "}"

    // ── Dial Rules tab JS ────────────────────────────────────────────────────
    "let rules=[];"
    "const ACTION_LABELS={"
      "play:'Play File',busy:'Busy Tone',reorder:'Reorder Tone',"
      "ringback:'Ringback',dial_tone:'Dial Tone',silence:'Silence',ignore:'Ignore'"
    "};"
    "function toggleFile(){"
      "document.getElementById('rf').style.display="
        "document.getElementById('ra').value==='play'?'':'none';"
    "}"
    "async function loadRules(){"
      "const r=await fetch('/api/rules');"
      "const d=await r.json();"
      "rules=d.rules||[];"
      "renderRules();"
    "}"
    "function renderRules(){"
      "const tb=document.getElementById('rtb');"
      "tb.innerHTML='';"
      "rules.forEach((r,i)=>{"
        "const tr=tb.insertRow();"
        "tr.insertCell().textContent=r.number;"
        "tr.insertCell().textContent=ACTION_LABELS[r.action]||r.action;"
        "tr.insertCell().textContent=r.action==='play'?r.file:'-';"
        "tr.insertCell().textContent=r.mqtt_event||'-';"
        "const btn=document.createElement('button');"
        "btn.className='del';btn.textContent='X';btn.onclick=()=>delRule(i);"
        "tr.insertCell().appendChild(btn);"
      "});"
    "}"
    "function addRule(){"
      "const num=document.getElementById('rn').value.trim();"
      "if(!num)return;"
      "const act=document.getElementById('ra').value;"
      "rules.push({number:num,action:act,"
        "file:act==='play'?document.getElementById('rfile').value.trim():'',"
        "mqtt_event:document.getElementById('rev').value.trim()});"
      "renderRules();"
      "document.getElementById('rn').value='';"
      "document.getElementById('rfile').value='';"
      "document.getElementById('rev').value='';"
    "}"
    "function delRule(i){rules.splice(i,1);renderRules();}"
    "async function saveRules(){"
      "const m=document.getElementById('msg-rules');"
      "try{"
        "const r=await fetch('/api/rules',{method:'POST',"
          "headers:{'Content-Type':'application/json'},"
          "body:JSON.stringify({rules:rules})});"
        "const j=await r.json();"
        "m.textContent=j.status==='ok'?'Rules saved.':'Error: '+j.error;"
        "m.className=j.status==='ok'?'ok':'er';"
      "}catch(ex){m.textContent='Save failed: '+ex.message;m.className='er';}"
      "m.style.display='block';"
    "}"

    // ── Audio Files tab JS ───────────────────────────────────────────────────
    "async function loadAudio(){"
      "const div=document.getElementById('alist');"
      "try{"
        "const r=await fetch('/api/audio');"
        "const d=await r.json();"
        "if(!d.mounted){"
          "div.innerHTML='<div class=er style=display:block>SD card not available \u2014 Phase 1B</div>';"
          "return;"
        "}"
        "if(!d.files||!d.files.length){"
          "div.textContent='No files on SD card.';"
          "return;"
        "}"
        "const tbl=document.createElement('table');"
        "const hrow=tbl.insertRow();"
        "['File','Size',''].forEach(h=>{"
          "const th=document.createElement('th');"
          "th.textContent=h;hrow.appendChild(th);"
        "});"
        "d.files.forEach(f=>{"
          "const tr=tbl.insertRow();"
          "tr.insertCell().textContent=f.name;"
          "tr.insertCell().textContent=(f.size/1024).toFixed(1)+' KB';"
          "const btn=document.createElement('button');"
          "btn.className='del';btn.textContent='X';"
          "btn.onclick=()=>delAudio(f.name);"
          "tr.insertCell().appendChild(btn);"
        "});"
        "div.innerHTML='';"
        "div.appendChild(tbl);"
      "}catch(ex){"
        "div.textContent='Error loading file list: '+ex.message;"
      "}"
    "}"
    "async function uploadAudio(){"
      "const input=document.getElementById('afile');"
      "const file=input.files[0];"
      "const m=document.getElementById('msg-audio');"
      "if(!file){m.textContent='Select a file first.';m.className='er';m.style.display='block';return;}"
      "m.textContent='Uploading '+file.name+'\u2026';m.className='ok';m.style.display='block';"
      "try{"
        "const r=await fetch('/api/audio?name='+encodeURIComponent(file.name),"
          "{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:file});"
        "const j=await r.json();"
        "m.textContent=j.status==='ok'?'Uploaded: '+file.name:'Error: '+j.error;"
        "m.className=j.status==='ok'?'ok':'er';"
        "if(j.status==='ok'){input.value='';loadAudio();}"
      "}catch(ex){m.textContent='Upload failed: '+ex.message;m.className='er';}"
    "}"
    "async function delAudio(name){"
      "const m=document.getElementById('msg-audio');"
      "try{"
        "const r=await fetch('/api/audio?name='+encodeURIComponent(name),{method:'DELETE'});"
        "const j=await r.json();"
        "m.textContent=j.status==='ok'?'Deleted: '+name:'Error: '+j.error;"
        "m.className=j.status==='ok'?'ok':'er';"
        "if(j.status==='ok')loadAudio();"
      "}catch(ex){m.textContent='Delete failed: '+ex.message;m.className='er';}"
      "m.style.display='block';"
    "}"

    // Init: load config on page open
    "loadCfg();"
    "</script></body></html>";

// ─── Helpers ─────────────────────────────────────────────────────────────────

/** Read at most max_len bytes of POST body into heap-allocated buffer. */
static char *read_body(httpd_req_t *req, size_t max_len) {
    if (req->content_len == 0 || req->content_len > max_len) return NULL;
    char *buf = malloc(req->content_len + 1);
    if (!buf) return NULL;
    int total = 0;
    while (total < (int)req->content_len) {
        int n = httpd_req_recv(req, buf + total, req->content_len - total);
        if (n <= 0) { free(buf); return NULL; }
        total += n;
    }
    buf[total] = '\0';
    return buf;
}

/** Extract ?name= query parameter into dest. Returns true on success. */
static bool get_query_name(httpd_req_t *req, char *dest, size_t dest_len) {
    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    return httpd_query_key_value(query, "name", dest, dest_len) == ESP_OK;
}

static bool sd_is_mounted(void) {
    struct stat st;
    return stat("/sdcard", &st) == 0;
}

// ─── GET / ───────────────────────────────────────────────────────────────────

static esp_err_t handler_root(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML, sizeof(HTML) - 1);
    return ESP_OK;
}

// ─── 404 → redirect to / (captive-portal behaviour in AP mode) ───────────────

static esp_err_t handler_404(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ─── GET /api/config ─────────────────────────────────────────────────────────

static esp_err_t handler_get_config(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id",           s_cfg->device_id);
    cJSON_AddNumberToObject(root, "net_mode",            s_cfg->net_mode);
    cJSON_AddStringToObject(root, "wifi_ssid",           s_cfg->wifi_ssid);
    // wifi_password excluded — write-only, never echoed back
    cJSON_AddStringToObject(root, "mqtt_broker",         s_cfg->mqtt_broker);
    cJSON_AddStringToObject(root, "mqtt_base_topic",     s_cfg->mqtt_base_topic);
    cJSON_AddBoolToObject  (root, "poe_installed",       s_cfg->poe_installed);
    cJSON_AddNumberToObject(root, "phone_mode",          s_cfg->phone_mode);
    cJSON_AddNumberToObject(root, "audio_volume",        s_cfg->audio_volume);
    cJSON_AddNumberToObject(root, "inter_digit_ms",      s_cfg->inter_digit_ms);
    cJSON_AddNumberToObject(root, "number_complete_ms",  s_cfg->number_complete_ms);
    cJSON_AddBoolToObject  (root, "dtmf_pass_star_hash", s_cfg->dtmf_pass_star_hash);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

// ─── POST /api/config ────────────────────────────────────────────────────────

static esp_err_t handler_post_config(httpd_req_t *req) {
    char *buf = read_body(req, 2048);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *item;
#define APPLY_STR(key, field, max) \
    item = cJSON_GetObjectItem(root, key); \
    if (cJSON_IsString(item)) strncpy(s_cfg->field, item->valuestring, (max) - 1);
#define APPLY_NUM(key, field, type) \
    item = cJSON_GetObjectItem(root, key); \
    if (cJSON_IsNumber(item)) s_cfg->field = (type)item->valuedouble;
#define APPLY_BOOL(key, field) \
    item = cJSON_GetObjectItem(root, key); \
    if (cJSON_IsBool(item)) s_cfg->field = cJSON_IsTrue(item);

    APPLY_STR ("device_id",           device_id,           32)
    APPLY_NUM ("net_mode",            net_mode,            net_mode_t)
    APPLY_STR ("wifi_ssid",           wifi_ssid,           64)
    APPLY_STR ("mqtt_broker",         mqtt_broker,         CONFIG_STR_MAX)
    APPLY_STR ("mqtt_base_topic",     mqtt_base_topic,     CONFIG_STR_MAX)
    APPLY_BOOL("poe_installed",       poe_installed)
    APPLY_NUM ("phone_mode",          phone_mode,          phone_mode_t)
    APPLY_NUM ("audio_volume",        audio_volume,        uint8_t)
    APPLY_NUM ("inter_digit_ms",      inter_digit_ms,      uint32_t)
    APPLY_NUM ("number_complete_ms",  number_complete_ms,  uint32_t)
    APPLY_BOOL("dtmf_pass_star_hash", dtmf_pass_star_hash)
    // wifi_password: only overwrite if user actually typed one
    item = cJSON_GetObjectItem(root, "wifi_password");
    if (cJSON_IsString(item) && strlen(item->valuestring) > 0)
        strncpy(s_cfg->wifi_password, item->valuestring, 63);

    cJSON_Delete(root);

    esp_err_t ret = config_save(s_cfg);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ret == ESP_OK ? "{\"status\":\"ok\"}"
                                          : "{\"status\":\"error\",\"error\":\"NVS save failed\"}");
    return ESP_OK;
}

// ─── GET /api/rules ──────────────────────────────────────────────────────────

static esp_err_t handler_get_rules(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "rules");

    for (int i = 0; i < s_rules->count; i++) {
        const dial_rule_t *r = &s_rules->rules[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "number",     r->number);
        cJSON_AddStringToObject(obj, "action",     dial_action_str(r->action));
        cJSON_AddStringToObject(obj, "file",       r->file);
        cJSON_AddStringToObject(obj, "mqtt_event", r->mqtt_event);
        cJSON_AddItemToArray(arr, obj);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

// ─── POST /api/rules ─────────────────────────────────────────────────────────

static esp_err_t handler_post_rules(httpd_req_t *req) {
    char *buf = read_body(req, 8192);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *arr = cJSON_GetObjectItem(root, "rules");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing rules array");
        return ESP_OK;
    }

    dial_ruleset_t new_rules = {0};
    int n = cJSON_GetArraySize(arr);
    if (n > DIAL_RULES_MAX) n = DIAL_RULES_MAX;

    for (int i = 0; i < n; i++) {
        cJSON *obj = cJSON_GetArrayItem(arr, i);
        cJSON *num = cJSON_GetObjectItem(obj, "number");
        if (!cJSON_IsString(num) || !strlen(num->valuestring)) continue;

        dial_rule_t *r = &new_rules.rules[new_rules.count++];
        strncpy(r->number, num->valuestring, DIAL_RULE_NUMBER_MAX - 1);

        cJSON *act = cJSON_GetObjectItem(obj, "action");
        r->action  = cJSON_IsString(act) ? dial_action_from_str(act->valuestring) : DIAL_ACTION_IGNORE;

        cJSON *file = cJSON_GetObjectItem(obj, "file");
        if (cJSON_IsString(file)) strncpy(r->file, file->valuestring, DIAL_RULE_FILE_MAX - 1);

        cJSON *ev = cJSON_GetObjectItem(obj, "mqtt_event");
        if (cJSON_IsString(ev)) strncpy(r->mqtt_event, ev->valuestring, DIAL_RULE_EVENT_MAX - 1);
    }

    cJSON_Delete(root);

    *s_rules = new_rules;
    esp_err_t ret = dial_rules_save(s_rules);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ret == ESP_OK ? "{\"status\":\"ok\"}"
                                          : "{\"status\":\"error\",\"error\":\"SPIFFS save failed\"}");
    return ESP_OK;
}

// ─── GET /api/audio ──────────────────────────────────────────────────────────

static esp_err_t handler_get_audio(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    if (!sd_is_mounted()) {
        cJSON_AddBoolToObject(root, "mounted", false);
    } else {
        cJSON_AddBoolToObject(root, "mounted", true);
        cJSON *files = cJSON_AddArrayToObject(root, "files");

        DIR *dir = opendir(AUDIO_DIR);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type != DT_REG) continue;
                char path[320];
                snprintf(path, sizeof(path), "%s/%s", AUDIO_DIR, entry->d_name);
                struct stat st;
                stat(path, &st);
                cJSON *f = cJSON_CreateObject();
                cJSON_AddStringToObject(f, "name", entry->d_name);
                cJSON_AddNumberToObject(f, "size", (double)st.st_size);
                cJSON_AddItemToArray(files, f);
            }
            closedir(dir);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

// ─── POST /api/audio ─────────────────────────────────────────────────────────
// Streams raw binary body to SD card — no full-file RAM buffer needed.

static esp_err_t handler_post_audio(httpd_req_t *req) {
    char name[64] = {0};
    if (!get_query_name(req, name, sizeof(name)) || !strlen(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ?name=");
        return ESP_OK;
    }

    if (!sd_is_mounted()) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"error\":\"SD card not available\"}");
        return ESP_OK;
    }

    char path[320];
    snprintf(path, sizeof(path), "%s/%s", AUDIO_DIR, name);

    FILE *f = fopen(path, "w");
    if (!f) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"error\":\"Cannot open file for write\"}");
        return ESP_OK;
    }

    char *chunk = malloc(4096);
    if (!chunk) { fclose(f); return ESP_FAIL; }

    int remaining = req->content_len;
    while (remaining > 0) {
        int to_read = remaining > 4096 ? 4096 : remaining;
        int n = httpd_req_recv(req, chunk, to_read);
        if (n <= 0) { fclose(f); free(chunk); return ESP_FAIL; }
        fwrite(chunk, 1, n, f);
        remaining -= n;
    }
    fclose(f);
    free(chunk);

    ESP_LOGI(TAG, "Audio uploaded: %s (%d bytes)", name, (int)req->content_len);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// ─── DELETE /api/audio ───────────────────────────────────────────────────────

static esp_err_t handler_delete_audio(httpd_req_t *req) {
    char name[64] = {0};
    if (!get_query_name(req, name, sizeof(name)) || !strlen(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ?name=");
        return ESP_OK;
    }

    if (!sd_is_mounted()) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"error\":\"SD card not available\"}");
        return ESP_OK;
    }

    char path[320];
    snprintf(path, sizeof(path), "%s/%s", AUDIO_DIR, name);

    if (remove(path) != 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"error\":\"File not found\"}");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Audio deleted: %s", name);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// ─── POST /api/reboot ────────────────────────────────────────────────────────

static void reboot_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t handler_post_reboot(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// ─── Public API ──────────────────────────────────────────────────────────────

esp_err_t web_ui_start(device_config_t *cfg, dial_ruleset_t *rules) {
    s_cfg   = cfg;
    s_rules = rules;

    httpd_config_t config     = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers   = 12;
    config.stack_size         = 12288;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    static const httpd_uri_t routes[] = {
        { .uri="/",          .method=HTTP_GET,    .handler=handler_root         },
        { .uri="/api/config",.method=HTTP_GET,    .handler=handler_get_config   },
        { .uri="/api/config",.method=HTTP_POST,   .handler=handler_post_config  },
        { .uri="/api/rules", .method=HTTP_GET,    .handler=handler_get_rules    },
        { .uri="/api/rules", .method=HTTP_POST,   .handler=handler_post_rules   },
        { .uri="/api/audio", .method=HTTP_GET,    .handler=handler_get_audio    },
        { .uri="/api/audio", .method=HTTP_POST,   .handler=handler_post_audio   },
        { .uri="/api/audio", .method=HTTP_DELETE, .handler=handler_delete_audio },
        { .uri="/api/reboot",.method=HTTP_POST,   .handler=handler_post_reboot  },
    };
    for (int i = 0; i < (int)(sizeof(routes)/sizeof(routes[0])); i++)
        httpd_register_uri_handler(s_server, &routes[i]);

    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, handler_404);

    ESP_LOGI(TAG, "WebUI started — http://192.168.4.1/ (AP) or http://<ip>/");
    return ESP_OK;
}

void web_ui_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
