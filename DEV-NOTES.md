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

Checklist:
  [ ] W5500 Ethernet initializes, gets DHCP address
  [ ] MQTT connects over Ethernet to broker
  [ ] MQTT publishes and subscribes correctly
  [ ] WiFi fallback connects when Ethernet cable removed
  [ ] MQTT continues over WiFi during failover
  [ ] WS2812B LED color changes with state machine transitions (GPIO21)
  [ ] NVS config load/save: phone_mode, MQTT broker, base topic, device ID
  [ ] WebUI serves from LittleFS over WiFi (provisioning page)
  [ ] OTA firmware update completes successfully

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
Build: `pio run -e waveshare-proslic-only` (create this env, see below)

Checklist:
  [ ] ProSLIC GPIO remap finalized and added to platformio.ini
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

## Creating the Phase 2 Environment

Add this to platformio.ini after Phase 2 GPIO remap is confirmed:

```ini
[env:waveshare-proslic-only]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
board_build.flash_size = 16MB
board_build.partitions = partitions.csv
monitor_speed = 115200
upload_speed = 921600

build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM=1
    -DDEV_BOARD_WAVESHARE_ETH=1
    -DPHASE2_PROSLIC_ONLY=1          ; Disables W5500 init in network_manager.c
    -DMQTT_BROKER_DEFAULT="mqtt://192.168.1.100"
    -DMQTT_TOPIC_BASE="escape/phone"

    ; I2S unchanged
    -DI2S_PCLK=4
    -DI2S_FSYNC=5
    -DI2S_DRX=6
    -DI2S_DTX=7

    ; ProSLIC remapped — UPDATE THESE once free GPIOs confirmed
    -DSPI_SLIC_CLK=39
    -DSPI_SLIC_MOSI=40
    -DSPI_SLIC_MISO=41
    -DSPI_SLIC_CS=42
    -DSLIC_RST_GPIO=45
    -DSLIC_INT_GPIO=8

    ; SD card on Adafruit ADA254 breakout — GPIO 35-38 confirmed
    -DSPI_SD_CLK=35
    -DSPI_SD_MOSI=36
    -DSPI_SD_MISO=37
    -DSPI_SD_CS=38

    -DLED_GPIO=21
```

Add `#ifdef PHASE2_PROSLIC_ONLY` guards in `network_manager.c` to skip W5500
initialization when this flag is set, using WiFi only for MQTT connectivity.

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
