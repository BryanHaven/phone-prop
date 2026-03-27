# DEV-NOTES.md
# Waveshare ESP32-S3-ETH / POE-ETH — Development Board Notes

## Board Overview

The Waveshare ESP32-S3-ETH is used for firmware development while custom PCBs
are being designed and fabricated. It contains the same core components as the
production main board: ESP32-S3R8, W5500 Ethernet, TF card slot, USB-C, WS2812B.

Chip: ESP32-S3R8 — 16MB flash, 8MB PSRAM (PSRAM not needed for this project)

---

## Confirmed GPIO Map (from Waveshare schematic)

  W5500 Ethernet:   CLK=GPIO13  MOSI=GPIO11  MISO=GPIO12  CS=GPIO14  INT=GPIO10  RST=GPIO9
  SD card (TF):     CS=GPIO4    MISO=GPIO5   MOSI=GPIO6   CLK=GPIO7
  WS2812B LED:      GPIO21

---

## Full Conflict Map

  GPIO  | Waveshare Function | Our Production Use  | Status
  ------|--------------------|---------------------|---------------------------
  4     | SD_CS              | I2S_PCLK            | ✗ CONFLICT (SD vs I2S)
  5     | SD_MISO            | I2S_FSYNC           | ✗ CONFLICT (SD vs I2S)
  6     | SD_MOSI            | I2S_DRX             | ✗ CONFLICT (SD vs I2S)
  7     | SD_CLK             | I2S_DTX             | ✗ CONFLICT (SD vs I2S)
  9     | W5500_RST          | SLIC_RST            | ✗ CONFLICT (W5500 vs SLIC)
  10    | W5500_INT          | SLIC_CS             | ✗ CONFLICT (W5500 vs SLIC)
  11    | W5500_MOSI         | SLIC_MOSI           | ✗ CONFLICT (W5500 vs SLIC)
  12    | W5500_MISO         | SLIC_MISO           | ✗ CONFLICT (W5500 vs SLIC)
  13    | W5500_CLK          | SLIC_CLK            | ✗ CONFLICT (W5500 vs SLIC)
  14    | W5500_CS           | SD_CS (production)  | Reassigned — no issue
  21    | WS2812B            | WS2812B             | ✅ Perfect match

None of these conflicts affect the production PCB — on the custom board, all
three subsystems are on independent SPI buses with non-overlapping GPIO ranges.
The conflicts are purely a consequence of Waveshare's routing choices.

---

## Consequences and Solutions

### SD Card vs I2S (GPIO 4/5/6/7)

The Waveshare onboard TF slot uses the exact same four GPIOs needed for the
ProSLIC PCM audio interface (I2S peripheral).

**Onboard TF slot is unusable when I2S is active.**

Solution: Use an **external microSD breakout module** wired to free GPIOs on
the Waveshare expansion header. These are cheap ($1–2), widely available, and
let the I2S pins stay dedicated to their correct function.

Suggested external SD breakout GPIO assignments (confirmed free on Waveshare header):

  Function  | GPIO  | Adafruit ADA254 pin label
  ----------|-------|---------------------------
  SD_CLK    | 35    | CLK
  SD_MOSI   | 36    | DI
  SD_MISO   | 37    | DO
  SD_CS     | 38    | CS

GPIO 35–38 verified broken out with headers on the Waveshare board.
platformio.ini [env:waveshare-s3-eth] updated with confirmed values — no placeholders remain.

### W5500 vs ProSLIC (GPIO 9–13)

W5500 Ethernet and ProSLIC VSPI share the same physical pins on the Waveshare
board. They must be tested in separate phases. The ProSLIC can be remapped to
free header GPIOs for Phase 2 testing.

Suggested ProSLIC GPIO remap for Phase 2 (verify free on header):

  Function      | Production GPIO | Phase 2 Remap | Notes
  --------------|-----------------|---------------|---------------------------
  SLIC_CLK      | 13              | 39            | Verify free on header
  SLIC_MOSI     | 11              | 40            | Verify free on header
  SLIC_MISO     | 12              | 41            | Verify free on header
  SLIC_CS       | 10              | 42            | Verify free on header
  SLIC_RST      | 9               | 45            | Verify — check strapping
  SLIC_INT      | 8               | 8             | Likely free — unchanged
  I2S_PCLK      | 4               | 4             | Unchanged
  I2S_FSYNC     | 5               | 5             | Unchanged
  I2S_DRX       | 6               | 6             | Unchanged
  I2S_DTX       | 7               | 7             | Unchanged

Note on strapping pins: GPIO0, GPIO3, GPIO45, GPIO46 are strapping pins on
ESP32-S3 and may affect boot behavior if driven during reset. Avoid using them
for SPI signals. Check the ESP32-S3 datasheet before finalizing Phase 2 remap.

Create a separate [env:waveshare-proslic-only] environment in platformio.ini for
Phase 2 with Ethernet initialization disabled and ProSLIC on remapped GPIOs.

---

## Development Phases

### Phase 1A — Ethernet, MQTT, WiFi, LED
Build: `pio run -e waveshare-s3-eth`
No additional hardware needed beyond the Waveshare board and a Cat5e cable.

**Status: Complete. Phase 1A fully verified.**

Checklist:
  [✅] W5500 Ethernet driver initializes (Mar 2026 — driver starts, netif attached)
  [✅] W5500 Ethernet gets DHCP address (Mar 2026 — 192.168.1.103)
  [✅] MQTT connects over Ethernet to broker (Mar 2026 — confirmed)
  [✅] WiFi connects and gets DHCP (Mar 2026 — E2W, 192.168.1.100)
  [✅] MQTT connects over WiFi (Mar 2026 — test.mosquitto.org confirmed)
  [✅] MQTT publishes retained status/network/identity on connect (Mar 2026)
  [✅] MQTT subscribes to command/# wildcard (Mar 2026)
  [✅] MQTT continues over WiFi during Ethernet failover (Mar 2026 — both directions)
  [✅] WS2812B LED: red (no net) → amber (net/no MQTT) → green (MQTT ready) (Mar 2026)
  [✅] NVS config load: phone_mode, MQTT broker, base topic, device ID (Mar 2026)
  [✅] WebUI serves provisioning page — confirmed http accessible (Mar 2026)
  [✅] mDNS: phone-prop-01.local resolves on LAN (Mar 2026)
  [✅] Task WDT: armed at 30s after full init (Mar 2026)
  [✅] OTA firmware update — WebUI push + MQTT pull, confirmed clean build (Mar 2026)

Notable milestones (Mar 2026):
  - Boot loop fixed: esp_timer.h include missing; network_manager_init() was TODO
  - answer_delay_ms NVS field added (default 1500ms, WebUI configurable)
  - STATE_RING_AND_PLAY and STATE_ANSWER_DELAY added to state machine
  - command/queue_audio MQTT topic implemented (arms file for auto-play on answer)
  - SPIFFS mounts on boot; dial_rules.json missing is handled gracefully
  - W5500 DHCP bringup — three-part fix required (see memory/w5500_bringup_lessons.md):
      1. phy_cfg.reset_gpio_num = -1 (PHY reset clears entire W5500 chip incl. SHAR)
      2. ETHERNET_EVENT_START handler: push MAC into SHAR via ETH_CMD_S_MAC_ADDR
      3. esp_netif_set_mac() in same handler to fix lwIP hwaddr (set zero at attach time)
  - Ethernet/WiFi failover confirmed both directions (cable unplug → WiFi takes over;
    cable plug → Ethernet reclaims primary; MQTT seamless across both transitions)
  - GCC 14.2 IRA pass ICE (esp_lcd_panel_rgb.c at -O2) fixed via sdkconfig.defaults:
      CONFIG_COMPILER_OPTIMIZATION_SIZE=y  (must go in sdkconfig, not build_flags)
  - ProSLIC spi_bus_initialize() stomps W5500 GPIO 11/12/13 on Waveshare board;
    guarded by #ifndef DEV_BOARD_WAVESHARE_ETH in phone_prop_main.c
  - OTA implemented (Mar 2026):
      WebUI push:  POST /api/ota (raw .bin body, streaming write, no RAM buffer)
                   Firmware tab always-visible status box + 4-tab layout
      MQTT pull:   {base}/command/ota = "http://host/firmware.bin"
                   Refused during RINGING/RING_AND_PLAY/ANSWER_DELAY/PLAYING_AUDIO
                   Publishes {base}/ota/status: start → complete|failed
      sdkconfig:   CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y (plain HTTP for dev)
      Dev server:  cd .pio/build/waveshare-s3-eth && python -m http.server 8000

### Phase 1B — SD Card Audio Pipeline
Requires: External microSD breakout module wired to free header GPIOs.
Build: `pio run -e waveshare-s3-eth` (with SD GPIO defines updated)

Checklist:
  [ ] External SD breakout GPIO assignments confirmed and updated in platformio.ini
  [ ] SD card mounts successfully (SPI mode)
  [ ] WAV files readable from /audio/system/ and /audio/messages/
  [ ] I2S PCM stream outputs correct waveform (verify with logic analyzer or I2S DAC)
  [ ] Dial tone WAV plays and loops correctly
  [ ] Busy signal WAV plays and loops correctly
  [ ] Message WAV plays and stops at end of file
  [ ] MQTT play command triggers correct audio file

Note: I2S output during Phase 1B won't go to a ProSLIC — wire the I2S DATA_OUT
(GPIO6/DRX) to a simple I2S DAC breakout (e.g. MAX98357A) to hear the audio
and verify the pipeline is working before the SLIC arrives.

### Phase 2 — ProSLIC Bringup (WiFi only, no Ethernet)
Requires: SLIC daughterboard prototype or breadboard SLIC circuit.
Requires: ProSLIC GPIO remap verified and updated (see table above).
Build: `pio run -e waveshare-proslic-only` (env exists in platformio.ini — verify GPIOs before use)

Checklist:
  [ ] ProSLIC GPIO remap verified on physical header before powering up
  [ ] proslic_verify_spi() returns true (0x5A echo test passes)
  [ ] ProSLIC calibration completes (Si3217x_Cal() returns RC_NONE)
  [ ] Linefeed set to Forward Active — 48V present on TIP/RING
  [ ] Hook detection: lift handset → on_hook_change(true) fires → MQTT publishes
  [ ] Replace handset → on_hook_change(false) fires → MQTT publishes "on_hook"
  [ ] Dial tone plays through telephone handset
  [ ] Rotary dial: each digit fires on_pulse_detected(), MQTT publishes digit
  [ ] DTMF: touch-tone key press fires on_dtmf_digit(), MQTT publishes digit
  [ ] Star (*) and hash (#) published when dtmf_pass_star_hash=true
  [ ] Ring generation: phone rings on MQTT command
  [ ] Audio message plays through handset, "audio_complete" published at end

### Phase 3 — Full Integration
Target: Custom production PCB only. No code changes required.
Flash the `phone-prop` environment. All GPIO conflicts resolved by design.

---

## Phase 2 Environment

**Already created** — `[env:waveshare-proslic-only]` exists in platformio.ini.

Before using it:
- Verify GPIO 39–42 are physically broken out on the Waveshare header
- Confirm GPIO 45 boot behavior (strapping pin — check ESP32-S3 datasheet)
- Update the SLIC GPIO defines in platformio.ini if remap changes

`PHASE2_PROSLIC_ONLY=1` build flag gates W5500 init in network_manager.c,
falling back to WiFi-only MQTT. No source changes needed.

---

## Recommended Dev Hardware to Order Now

To support Phase 1B and Phase 2 without waiting for custom PCBs:

| Item | Purpose | ~Cost |
|------|---------|-------|
| External microSD SPI breakout | Phase 1B SD audio testing | $1–2 |
| MAX98357A I2S DAC breakout | Phase 1B audio verification (hear the tones) | $3–5 |
| 22AWG jumper wires (M-M, M-F) | Wiring breakouts to Waveshare header | $3 |
| Logic analyzer (e.g. Saleae clone) | SPI/I2S signal verification | $10–15 |

The MAX98357A is particularly useful — it's a mono I2S amplifier that takes the
PCM audio stream directly and drives a small speaker. You can verify dial tone,
busy signal, and message audio are correctly generated before connecting any SLIC.

---

## PoE Safety

DO NOT connect the PoE module and USB-C simultaneously.
No isolation between PoE power rail and USB ground on this board.
Risk: damage to ESP32-S3, USB port, or PoE switch.

Development rule: USB-C only at the bench. PoE only for standalone testing.

---

## Production Transition

When custom PCBs arrive, flash `phone-prop` environment. No firmware changes
needed — all GPIO differences are handled entirely in platformio.ini build flags.
The firmware source code is board-agnostic.
