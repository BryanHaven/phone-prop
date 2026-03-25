# Main Board — Schematic Notes
## For EasyEDA Pro

Board: main-board
Dimensions: ~70 × 80mm
Layers: 2
Assembly: JLCPCB
Status: Schematic entry pending (complete daughterboard first)

---

## Power Architecture

### PoE DNP Isolation Strategy

For portable units (no PoE), the AG9800MT section is fully unpopulated (DNP).
To prevent floating nets when the PoE components are absent:

  SB_POE: 0Ω solder bridge (1206 pad, leave open for portable, bridge for PoE)
    Placed between the PoE bridge rectifier output and the AG9800MT VIN.
    When open: PoE path is completely disconnected, no floating nodes.
    When bridged: PoE power flows to AG9800MT input.

  D_OR_A: Schottky diode on PoE output (DNP for portable)
    When DNP: no path from PoE section to 12V_RAIL. Barrel jack is sole supply.

  D_OR_B: Schottky diode on barrel jack (always populated)
    Always present. When PoE section is absent, this is the only power path.

  Permanent install BOM note: populate AG9800MT, D_OR_A, close SB_POE.
  Portable unit BOM note: DNP AG9800MT + all PoE passives + D_OR_A, leave SB_POE open.

### Input Options (diode-OR'd — both can be present, higher voltage wins)

```
Option A — PoE:
  RJ45 pins 4,5(+) and 7,8(–) [or 1,2 and 3,6 depending on PoE mode]
  → D_POE (bridge rectifier, 4× SS34 Schottky or DB107S)
  → AG9800MT PoE PD controller input
  → AG9800MT 12V output
  → D_OR_A (Schottky SS34) → 12V_RAIL

Option B — Barrel Jack:
  J_PWR (2.1mm barrel, center positive)
  → F1 (500mA polyfuse)
  → D_OR_B (Schottky SS34) → 12V_RAIL

12V_RAIL:
  → C_12V_A (100µF/25V electrolytic) to GND  [bulk]
  → C_12V_B (100nF 0402) to GND              [bypass]
  → U_LDO1 input (AMS1117-3.3)
  → J3 pin — NOT connected (12V stays on main board; daughter gets 3V3 only)
  → also feeds LED power indicator
```

### 3.3V Digital (U_LDO1 — AMS1117-3.3, SOT-223)
```
IN  → 12V_RAIL
OUT → 3V3 net
     → C_3V3A (100µF/16V electrolytic) to GND
     → C_3V3B (100nF 0402) to GND
     → ESP32-S3 module 3V3 pin
     → W5500 VCC (pin 6)
     → CH340N VCC
     → SD card Vcc
     → WS2812B Vcc (via 100Ω decoupling R)
     → J3 pins 1, 3 (3V3 to daughterboard)
```

### 3.3V Analog / VDDA (U_LDO2 — LP2985-33, SOT-23-5)
```
IN  → 3V3 net
OUT → VDDA net
     → C_VDDA_A (10µF 0805) to GND
     → C_VDDA_B (100nF 0402) to GND
     → L_FB (ferrite bead 600Ω/100MHz) → VDDA_FILT
     → VDDA_FILT → J3 pins 1,3 as well? NO —
     
NOTE: VDDA is generated on the DAUGHTERBOARD (it has its own LP2985).
The main board LP2985 is for the main board's own analog needs if any.
For rev 1, the main board LP2985 may be DNP. The daughterboard is
fully self-powered from 3V3 supplied via J3.
```

---

## ESP32-S3 Module (U1 — ESP32-S3-WROOM-1-N16)

```
3V3     → 3V3 net (multiple pins — connect all)
GND     → GND (multiple pins — connect all)
EN      → 10kΩ to 3V3 + 100nF to GND + SW_RESET to GND
GPIO0   → 10kΩ to 3V3 + SW_BOOT to GND

VSPI bus (ProSLIC → J3 header):
  GPIO10 (SPI_SLIC_CS)   → J3 pin 6
  GPIO11 (SPI_VSPI_MOSI) → J3 pin 7
  GPIO12 (SPI_VSPI_MISO) → J3 pin 8
  GPIO13 (SPI_VSPI_CLK)  → J3 pin 5

I2S/PCM bus (→ J3 header):
  GPIO4  (PCM_PCLK)  → J3 pin 9
  GPIO5  (PCM_FSYNC) → J3 pin 10
  GPIO6  (PCM_DRX)   → J3 pin 11
  GPIO7  (PCM_DTX)   → J3 pin 12

SLIC control (→ J3 header):
  GPIO8  (SLIC_RST)  → J3 pin 14  [+ 10kΩ pullup to 3V3]
  GPIO9  (SLIC_INT)  → J3 pin 13  [+ 4.7kΩ pullup to 3V3]

HSPI bus (W5500 + SD card):
  GPIO18 (SPI_HSPI_CLK)  → W5500 SCLK (pin 2) + SD CLK
  GPIO19 (SPI_HSPI_MOSI) → W5500 MOSI (pin 4) + SD MOSI
  GPIO20 (SPI_HSPI_MISO) → W5500 MISO (pin 3) + SD MISO
  GPIO15 (SPI_W5500_CS)  → W5500 SCSn (pin 1)
  GPIO14 (SPI_SD_CS)     → SD card CS

W5500 control:
  GPIO16 (W5500_INT) → W5500 INTn (pin 40)  [+ 4.7kΩ pullup to 3V3]
  GPIO17 (W5500_RST) → W5500 RSTn (pin 41)  [+ 10kΩ pullup to 3V3]

LED:
  GPIO21 (WS2812B_DATA) → 330Ω → WS2812B DIN

UART (→ CH340N):
  GPIO43 (TXD0) → CH340N RXD
  GPIO44 (RXD0) → CH340N TXD
```

---

## W5500 Ethernet Controller (U2 — W5500, QFN-48)

```
VCC (pin 6)     → 3V3 + 100nF bypass
GND             → GND (multiple pins)
SCSn (pin 1)    → GPIO15 (SPI_W5500_CS)
SCLK (pin 2)    → GPIO18 (SPI_HSPI_CLK)  [max 80 MHz; 20 MHz safe for proto]
MISO (pin 3)    → GPIO20 (SPI_HSPI_MISO)
MOSI (pin 4)    → GPIO19 (SPI_HSPI_MOSI)
INTn (pin 40)   → GPIO16 (W5500_INT) + 4.7kΩ pullup
RSTn (pin 41)   → GPIO17 (W5500_RST) + 10kΩ pullup

Crystal circuit (pins 22, 23 — X1, X2):
  25 MHz crystal (HC-49S SMD or equivalent)
  C_X1, C_X2: 22pF 0402 to GND each
  
Ethernet magnetics (built into RJ45 jack or external transformer):
  TXOP/TXON (pins 32, 33) → Transformer TX+/TX–
  RXIP/RXIN (pins 35, 36) → Transformer RX+/RX–
  
  Use RJ45 with integrated magnetics (HR911105A or equivalent)
  This eliminates separate transformer components.
  
RBIAS (pin 43): 12.4kΩ to GND (sets reference current — use 1% resistor)

Decoupling: 100nF on each VCC pin group (pins 6, 28, 38) + 10µF bulk on VCC
```

### RJ45 with Integrated Magnetics (J_ETH)
```
Use: HR911105A or HY911105A (common, cheap, LCSC available)
These include: 2× 1:1 transformers, common mode choke, LED pins for link/activity

TX+/TX– ← W5500 TXOP/TXON via transformer
RX+/RX– → W5500 RXIP/RXIN via transformer
LED_LINK (yellow) → 1kΩ → W5500 LINKLED (pin 42) or hardwire to GND
LED_ACT  (green)  → 1kΩ → W5500 ACTLED  (pin 39)

Centre tap connections (from magnetics midpoint):
  TX CT → 49.9Ω → 3V3 (or GND depending on W5500 application note)
  RX CT → 0.1µF → GND

PoE pins (when AG9800MT populated):
  RJ45 pins 4,5 (blue pair) and 7,8 (brown pair)
  → Bridge rectifier (D_POE) → AG9800MT input
```

---

## AG9800MT PoE PD Controller (U3 — SOP-16, DNP by default)

```
This section is populated ONLY for PoE installs.
Mark all PoE components DNP in BOM with note "populate for PoE".

The AG9800MT implements IEEE 802.3af PD (powered device):
- Classification: Class 0 (default, up to 15.4W)
- Input: 37–57V from PoE PSE
- Output: 12V regulated (feeds 12V_RAIL via D_OR_A)

Key external components needed:
  - Isolation transformer (flyback): AP4311 or similar
  - Output diode: fast recovery ≥ 200V/3A
  - Output cap: 470µF/16V electrolytic
  - Feedback resistors: set 12V output
  - RJ45 PoE pins → bridge rectifier → AG9800MT VIN

IMPORTANT: The PoE section requires careful layout — high-frequency
switching on the isolation transformer. Keep PoE components in their
own corner of the PCB, away from Ethernet signal traces.

Reference: AG9800MT datasheet application circuit.
Alternative simpler approach: use a pre-built PoE module
(e.g. UCTRONICS or similar 12V PoE splitter module) as an
external module, and connect its 12V output to the barrel jack.
This avoids designing the PoE section from scratch for v1.
```

---

## microSD Card (J_SD — microSD push-push slot)

```
Pin 1 (CS/DAT3)  → GPIO14 (SPI_SD_CS) + 10kΩ pullup to 3V3
Pin 2 (MOSI/CMD) → GPIO19 (SPI_HSPI_MOSI) + 10kΩ pullup to 3V3
Pin 3 (VSS)      → GND
Pin 4 (VDD)      → 3V3 + 100nF bypass
Pin 5 (CLK/SCLK) → GPIO18 (SPI_HSPI_CLK)
Pin 6 (VSS)      → GND
Pin 7 (MISO/DAT0)→ GPIO20 (SPI_HSPI_MISO) + 10kΩ pullup to 3V3
Pin 8 (DAT1)     → NC
Pin 9 (DAT2)     → NC
CD (card detect) → optional GPIO input with pullup (NC is fine for v1)
```

---

## CH340N USB-UART Bridge (U4 — SOP-8)

```
VCC (pin 8) → 3V3 + 100nF bypass
GND (pin 7) → GND
TXD (pin 2) → ESP32-S3 GPIO44 (UART0_RX)
RXD (pin 3) → ESP32-S3 GPIO43 (UART0_TX)
V3 (pin 4)  → 100nF to GND (internal 3.3V reference)
UD+ (pin 5) → USB D+ (via 27Ω series R to J_USB D+)
UD- (pin 6) → USB D– (via 27Ω series R to J_USB D–)

J_USB (USB-C 2.0 receptacle):
  VBUS → 5V (not used for power — CH340N runs on 3V3)
         Add 5.1kΩ from each CC pin to GND (required for USB-C)
  D+   → 27Ω → CH340N UD+
  D–   → 27Ω → CH340N UD–
  GND  → GND
  
Auto-reset circuit (for flashing):
  CH340N DTR → 100nF → ESP32-S3 EN (via RC reset circuit)
  CH340N RTS → 10kΩ  → ESP32-S3 GPIO0
  This enables esptool.py to auto-reset into flash mode.
```

---

## WS2812B Status LED (D_LED)

```
VCC → 3V3 (100Ω series R for decoupling)
GND → GND
DIN → 330Ω series R → GPIO21

Status colors (firmware defined):
  Blue       → IDLE (on-hook, connected)
  Green      → OFF-HOOK (handset lifted)
  Amber      → RINGING or DIALING
  Green blink → PLAYING AUDIO
  Red        → FAULT or no network
  Purple blink → OTA update in progress
```

---

## Test Points (Main Board)

```
TP1: 12V_RAIL
TP2: 3V3
TP3: GND
TP4: W5500_INT signal
TP5: SPI_HSPI_CLK (scope probe during Ethernet debug)
TP6: UART_TX (serial debug without USB-C connected)
```

---

## Switches

```
SW_RESET: Tactile → ESP32-S3 EN to GND (standard ESP32 reset)
SW_BOOT:  Tactile → GPIO0 to GND (firmware flash mode)
Both: 3×4mm SMD tactile, placed near module edge for access
```

---

## Connector Placement — CONFIRMED HARD RULE

ALL external-facing connectors on the SOUTH edge of the PCB, left to right:

```
SOUTH EDGE (panel cutout face)
◄──────────────────────────────────────────────────────►
  [J_PWR]          [J_ETH]               [J_USB]
  2.1mm barrel     RJ45 w/ magnetics     USB-C
  ~9mm wide        ~16mm wide            ~9mm wide
  ←3mm→            ←3mm→                 ←3mm→
                ↑ gap between each
```

Approximate total span: ~9 + 3 + 16 + 3 + 9 = ~40mm minimum
Board width must be ≥ 50mm to accommodate with margin.
Target board: 70mm wide × 80mm tall (connectors on bottom 70mm edge).

### Connector footprint orientations
- J_PWR (PJ-002A): right-angle, pins face south, barrel opening faces south edge
- J_ETH (HR911105A): right-angle, ports face south, PCB tabs on top
- J_USB (USB-C): SMD mid-mount or right-angle, port opening faces south

### Other component placement derived from this
- J3 (2×10 daughterboard header): NORTH edge — cables exit back of enclosure
- J_SD (microSD slot): EAST or WEST edge, top face — card ejects sideways
- SW_RESET / SW_BOOT: top face, near NORTH edge — not on panel, not accidental
- WS2812B status LED: top face, near SOUTH edge but set back ~5mm
  (visible through a 5mm hole in enclosure panel between J_ETH and J_USB)

### Enclosure candidates
Hammond 1591XXFLBK  — 121×66×40mm  — too wide, overkill for v1
Hammond 1593KBK     — 100×60×25mm  — check connector height clearance
Hammond 1590GBK     — 92×59×31mm   — good candidate, verify RJ45 height
Hammond 1591EFLBK   — 70×50×28mm   — verify 50mm width fits connector span

Verify chosen enclosure: connector span + margins ≤ enclosure panel width.
RJ45 body height above PCB ≈ 13–15mm — enclosure internal height must clear this.

---

## EasyEDA Pro Notes

- Multi-sheet schematics supported — use separate sheets for Power, SLIC Interface,
  Ethernet, and USB/Debug if the main schematic gets crowded
- Net names are global across sheets — a net named 3V3 on the power sheet connects
  to 3V3 on the ESP32 sheet automatically; use this intentionally
- Power symbols: use EasyEDA Pro's built-in PWR symbols (VCC, GND, +3V3, +12V)
  for clean ERC — do not use plain wire stubs for power rails
- Component search: LCSC part numbers work directly in the Pro parts panel
  W5500: search C32961 | AG9800MT: search directly or use generic IC body
- DRC configuration: set custom rules for PoE section (2mm clearance from 48V nodes)
  and for south-edge connectors (verify pad-to-edge clearance ≥ 1mm)
- Export CPL: Tools → Export BOM/CPL for JLCPCB; verify rotation offsets on
  QFN/LGA packages before submitting — common gotcha for QFN-48 (W5500)
- Two PCB files in one project: use the Project panel (left sidebar) to switch
  between slic-daughterboard and main-board PCB files

## Claude Code Integration with EasyEDA Pro

Claude Code runs in VS Code terminal — it cannot directly control EasyEDA's UI.
The productive workflow is:

  1. Work in EasyEDA Pro on one monitor
  2. Periodically export: File → Export → EasyEDA Format (.json)
  3. Save the exported JSON into the project folder
  4. Ask Claude Code to review the export against schematic-notes.md
  5. Claude Code flags missing connections, wrong pins, package mismatches
  6. Fix in EasyEDA, re-export, repeat

Claude Code can also:
  - Parse exported BOM CSV and validate LCSC part numbers and packages
  - Generate CPL rotation offset corrections for JLCPCB submission
  - Cross-check the EasyEDA netlist against the GPIO table in CLAUDE.md
  - Write validation scripts (Python) to automate BOM/CPL checks
  - Review Gerber exports for obvious issues before ordering

---

## ERC Expected Warnings (Main Board)

- USB-C VBUS unconnected to power rail — this is intentional (bus-powered from PC
  only during programming; the board runs from 12V/PoE in operation)
- W5500 crystal pins may flag if crystal load caps aren't explicitly connected
- AG9800MT DNP components will flag if not marked with "No Connect" symbols
```
