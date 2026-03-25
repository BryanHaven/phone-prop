# Phone Prop Controller — Claude Code Project Context
# Architecture Rev 2 — Ethernet + PoE + 2x10 header confirmed

## What This Project Is

A custom hardware controller for an escape room telephone prop. Connects to a real
two-wire analog rotary telephone (POTS), emulates a complete FXS telephone line, decodes
rotary pulse dialing, plays audio messages through the handset, and communicates with
Mythric Mystery Master over MQTT.

Connectivity: Ethernet primary (W5500) + WiFi fallback/WebUI. CONFIRMED.
PoE: Design for both — AG9800MT footprint always present, DNP for portable. CONFIRMED.
Deploy profile: Mixed — permanent (PoE, fixed Ethernet) + portable (barrel jack, WiFi).

## Project Structure

Standard PlatformIO layout — open the root folder in VS Code.
PlatformIO finds platformio.ini at root; Claude Code sees everything.

```
phone-prop/                        ← open THIS folder in VS Code
├── CLAUDE.md                      ← Claude Code context (this file)
├── DEV-NOTES.md                   ← Waveshare dev board notes + phases
├── platformio.ini                 ← PlatformIO build environments
├── partitions.csv                 ← ESP32-S3 flash partition table
├── src/                           ← Firmware source (PlatformIO standard)
│   ├── phone_prop_main.c
│   ├── device_config.c
│   ├── network_manager.c
│   └── proslic_hal.c
├── include/                       ← Firmware headers (PlatformIO standard)
│   ├── device_config.h
│   ├── network_manager.h
│   └── proslic_hal.h
├── hardware/
│   ├── slic-daughterboard/        ← EasyEDA Pro schematic notes + BOM
│   └── main-board/                ← EasyEDA Pro schematic notes + BOM
└── tools/
    └── validate_bom_cpl.py        ← Run before every JLCPCB submission
```

Build commands:
  pio run -e waveshare-s3-eth      ← Phase 1 dev board
  pio run -e phone-prop            ← Production PCB

## EasyEDA Project

- Tool: EasyEDA Pro (desktop app)
- Single project, two PCB files: slic-daughterboard + main-board
- Assembly: JLCPCB (CPL/BOM workflow — same as raven controller)
- Export JSON/BOM/CPL into hardware/*/exports/ for Claude Code review

---

## Two-Board Architecture

### SLIC Daughterboard
- Si32177-C ProSLIC FXS SLIC — 42-pin LGA
- DC-DC boost → VBAT (–48V to –110V)
- RJ11 telephone connector
- 12V input, polyfuse, reverse polarity
- LDOs: AMS1117 3.3V digital + LP2985 3.3V VDDA
- TVS on TIP/RING, test points
- Interface: 2×10 2.54mm header (J3)
- Size: ~45×55mm, 2-layer

### Main Board
- ESP32-S3-WROOM-1-N16
- W5500 Ethernet (SPI/HSPI) + RJ45 w/ magnetics
- AG9800MT PoE PD module (DNP option — populate for PoE installs)
- 12V barrel jack (fallback power)
- microSD (SPI/HSPI, audio storage)
- CH340N USB-C (programming/debug)
- WS2812B status LED
- AMS1117-3.3 + LP2985-33 LDOs
- Interface: 2×10 2.54mm header (J3) to daughterboard
- Size: ~70×80mm, 2-layer

---

## 2x10 Header Pinout (J3)

Male on main board, female socket on daughterboard.

| Pin | Signal    | Pin | Signal    |
|-----|-----------|-----|-----------|
| 1   | 3V3       | 2   | GND       |
| 3   | 3V3       | 4   | GND       |
| 5   | SPI_CLK   | 6   | SPI_CS    |
| 7   | SPI_MOSI  | 8   | SPI_MISO  |
| 9   | PCM_PCLK  | 10  | PCM_FSYNC |
| 11  | PCM_DRX   | 12  | PCM_DTX   |
| 13  | SLIC_INT  | 14  | SLIC_RST  |
| 15  | SPARE     | 16  | SPARE     |
| 17  | SPARE     | 18  | SPARE     |
| 19  | GND       | 20  | GND       |

SPI = VSPI bus (ProSLIC only). PCM = I2S in PCM mode.

---

## GPIO Assignments (ESP32-S3)

| GPIO | Function       | Bus  | Dir       | Notes                          |
|------|----------------|------|-----------|--------------------------------|
| 4    | PCM_PCLK       | I2S  | OUT       | 2.048 MHz to SLIC              |
| 5    | PCM_FSYNC      | I2S  | OUT       | 8kHz frame sync                |
| 6    | PCM_DRX        | I2S  | OUT       | Audio to handset               |
| 7    | PCM_DTX        | I2S  | IN        | Audio from handset             |
| 8    | SLIC_RST       | GPIO | OUT       | Active-low, hold 250ms boot    |
| 9    | SLIC_INT       | GPIO | IN PULLUP | Open-drain hook/ring events    |
| 10   | SPI_SLIC_CS    | VSPI | OUT       | ProSLIC chip select            |
| 11   | SPI_VSPI_MOSI  | VSPI | OUT       | ProSLIC SDI                    |
| 12   | SPI_VSPI_MISO  | VSPI | IN        | ProSLIC SDO                    |
| 13   | SPI_VSPI_CLK   | VSPI | OUT       | ProSLIC SCLK max 10MHz         |
| 14   | SPI_SD_CS      | HSPI | OUT       | SD card chip select            |
| 15   | SPI_W5500_CS   | HSPI | OUT       | W5500 chip select              |
| 16   | W5500_INT      | GPIO | IN PULLUP | W5500 interrupt active-low     |
| 17   | W5500_RST      | GPIO | OUT       | W5500 reset active-low         |
| 18   | SPI_HSPI_CLK   | HSPI | OUT       | HSPI clock (W5500 + SD)        |
| 19   | SPI_HSPI_MOSI  | HSPI | OUT       | HSPI MOSI                      |
| 20   | SPI_HSPI_MISO  | HSPI | IN        | HSPI MISO                      |
| 21   | WS2812B_DATA   | GPIO | OUT       | Status LED, 330Ω series R      |
| 43   | UART0_TX       | UART | OUT       | CH340N / USB-C programming     |
| 44   | UART0_RX       | UART | IN        | CH340N                         |

SPI Bus Summary:
  VSPI (SPI2): 10/11/12/13 — ProSLIC dedicated (timing critical)
  HSPI (SPI3): 14/15/18/19/20 — W5500 + SD card (separate CS)

---

## Key ICs

Si32177-C-FM1R  — ProSLIC SLIC, LCSC C2676781, 42-pin LGA, Skyworks
W5500           — Ethernet controller, LCSC C32961, QFN-48, WIZnet
AG9800MT        — PoE PD controller, SOP-16, DNP option for non-PoE builds
CH340N          — USB-UART, LCSC C506813, SOP-8
AMS1117-3.3     — LDO 3.3V, LCSC C6186, SOT-223
LP2985-33DBVR   — LDO 3.3V low-noise VDDA, LCSC C99075, SOT-23-5
WS2812B         — RGB LED, LCSC C114586, 5050

---

## MQTT Topics

Publishes:
  escape/phone/status   → "on_hook" | "off_hook"
  escape/phone/dialed   → digit string e.g. "7"
  escape/phone/number   → complete number e.g. "911"
  escape/phone/event    → "ring_start" | "ring_stop" | "audio_complete"
  escape/phone/network  → "ethernet" | "wifi" | "disconnected"

Subscribes:
  escape/phone/command/ring    → "start" | "stop"
  escape/phone/command/play    → filename e.g. "msg_clue1.wav"
  escape/phone/command/hangup  → force on-hook
  escape/phone/command/reset   → full state reset

---

## Network Behavior

Ethernet (W5500): primary MQTT transport; link detected at startup
WiFi (ESP32-S3): fallback MQTT + WebUI access + OTA updates
PoE (AG9800MT):  extracts 12V from 802.3af PoE switch via RJ45
                 diode-OR'd with barrel jack — populate or DNP per install

---

## Firmware Stack

ESP-IDF framework
esp-mqtt (runs over W5500 or WiFi, same API)
ESP-IDF W5500 SPI Ethernet driver
ESP-IDF I2S driver (master, PCM short-frame)
VSPI: ProSLIC | HSPI: W5500 + SD card
LittleFS: WebUI assets + kit config
OTA: ESP-IDF over WiFi or Ethernet
ProSLIC: Skyworks ProSLIC API C library (HAL in firmware/src/proslic_hal.c)

---

## Partition Table (16MB flash)

nvs       20KB   WiFi creds, MQTT config, device ID
otadata    8KB   OTA slot tracking
app0     1.5MB   ota_0 running firmware
app1     1.5MB   ota_1 OTA staging
spiffs    ~12MB  WebUI assets, kit config JSON
(Audio files on SD card — not in flash)

---

## Audio Files (SD Card)

WAV, 8kHz, 8-bit µ-Law, mono
/audio/system/  : dial_tone, busy_signal, ringback, reorder_tone
/audio/messages/: msg_clue1.wav, msg_clue2.wav ... (MMM-triggered via MQTT)

---

## Safety

VBAT on daughterboard: –48V to –110V DC — hazardous voltage
Ring voltage: ~90VAC generated internally by ProSLIC
PCB: ≥2mm creepage VBAT to signal traces on daughterboard
Conformal coat daughterboard after assembly
TVS (SMBJ100CA) mandatory on TIP/RING
PoE section: 48VDC on RJ45 pins when PoE active

---

## Related Projects

Raven Animatronic Controller: same ESP32/JLCPCB/MQTT/MMM workflow
Parrot Kit: Mr. Chicken's Prop Shop (Jasper Anderson collaboration)
escape2win: escape room business website — relevant to prop deployment context

---

## Mixed Deployment Design Requirements (CONFIRMED)

Deploy profile: mixed — permanent (PoE + fixed Ethernet) and portable (barrel jack + WiFi).

### PoE DNP Strategy
- AG9800MT and flyback components: footprint always present, DNP for portable units
- D_OR_A (PoE path diode): DNP for portable — must not create floating nodes when absent
- SB_POE (0Ω solder bridge on PoE rectifier output): open=portable, closed=PoE install
- D_OR_B (barrel jack diode): always populated on all units
- When PoE section DNP: barrel jack path operates in full isolation, no floating nets

### Per-Unit NVS Config (WebUI provisioning page)
All deployment-specific settings in NVS, editable via WebUI:
  network_mode    : "eth_preferred" | "wifi_only" | "eth_only"
  wifi_ssid       : string
  wifi_password   : string (write-only field in WebUI)
  mqtt_broker     : e.g. "mqtt://192.168.1.100"
  mqtt_base_topic : e.g. "escape/room3/phone"  ← room-specific, key for multi-unit
  device_id       : e.g. "phone-prop-01"
  poe_installed   : bool (informational, published to MQTT on boot)

MQTT topics are rooted at mqtt_base_topic:
  {base}/status, {base}/dialed, {base}/number, {base}/event, {base}/network
  {base}/command/ring, {base}/command/play, {base}/command/hangup, {base}/command/reset

### Enclosure / PCB Layout Constraint (main board)
CONFIRMED HARD RULE — ALL external-facing connectors on the SOUTH edge of the PCB:
  LEFT TO RIGHT order on south edge:
    1. J_PWR  — 2.1mm barrel jack
    2. J_ETH  — RJ45 with magnetics (widest component, ~16mm)
    3. J_USB  — USB-C receptacle

  Rationale: single straight cutout in enclosure panel, clean cable exit,
  operator-friendly for portable setup/teardown.

  PCB layout rules derived from this:
  - All three connectors must be flush with or overhanging the south board edge
  - No signal vias, no copper pours within 3mm of south edge (mechanical clearance)
  - J3 daughterboard header: NORTH edge (opposite face from cable connectors)
  - SD card slot: top face, east or west edge (card ejects away from cables)
  - SW_RESET + SW_BOOT: top face, accessible but not on south edge (prevent accidental press)

  Enclosure target: Hammond 1591XXFLBK or 1590GBK series
    (verify RJ45 + barrel + USB-C horizontal span fits chosen box width)
  VIOLATION: any connector not on south edge must be flagged before layout is approved

---

## Claude Code Workflow (VS Code + EasyEDA Pro)

Claude Code runs in the VS Code integrated terminal. It CANNOT directly control
EasyEDA's UI — it works through files in this project folder.

### The productive loop:

  VS Code / Claude Code                EasyEDA Pro (separate window)
  ─────────────────────────            ─────────────────────────────
  Read schematic-notes.md        →     Place and wire components manually
  Generate validation scripts    →     Work through net list section by section
  Review exported JSON           ←     File → Export → EasyEDA Format (.json)
  Flag connection errors               Fix in EasyEDA, re-export
  Validate BOM CSV               ←     File → Export → BOM/CPL
  Check CPL rotation offsets     →     Correct before JLCPCB submission
  Review Gerber files            ←     Export Gerbers for final check

### What Claude Code CAN do for hardware:
  - Parse EasyEDA exported JSON and compare against schematic-notes.md netlists
  - Validate BOM CSV: LCSC part numbers, package names, DNP flags, quantities
  - Generate CPL rotation correction tables for QFN/LGA/SOT packages
  - Write Python validation scripts to automate BOM/CPL cross-checks
  - Cross-reference exported netlist against GPIO assignments in this file
  - Check Gerber files for layer completeness and common issues
  - Generate JLCPCB order checklist from BOM + board specs
  - Write and debug all firmware (main area of strength)

### What Claude Code CANNOT do:
  - Place components in EasyEDA directly
  - Click, drag, or control EasyEDA's UI in any way
  - View the EasyEDA canvas in real time

### Export files to save into project folder for Claude Code review:
  hardware/slic-daughterboard/exports/schematic.json
  hardware/slic-daughterboard/exports/bom.csv
  hardware/slic-daughterboard/exports/cpl.csv
  hardware/main-board/exports/schematic.json
  hardware/main-board/exports/bom.csv
  hardware/main-board/exports/cpl.csv

### BOM/CPL validation (run from project root):
  python3 tools/validate_bom_cpl.py hardware/slic-daughterboard/exports/
  python3 tools/validate_bom_cpl.py hardware/main-board/exports/
