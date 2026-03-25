# Main Board — PCB Layout Guide
## EasyEDA Standard

Board: main-board
Target size: 70mm × 80mm, 2-layer
Connector edge: SOUTH (bottom edge, 70mm span)

---

## Board Coordinate Convention

```
        NORTH EDGE (80mm) — J3 daughterboard header
        ┌──────────────────────────────────────────┐
        │                                          │ 80mm
   W    │         Component zone                   │    E
   E    │         (ESP32-S3, W5500,                │    A
   S    │          LDOs, SD card)                  │    S
   T    │                                          │    T
        │                                          │
        ├──────────────────────────────────────────┤
        │  J_PWR    J_ETH          J_USB   LED hole│
        └──────────────────────────────────────────┘
        SOUTH EDGE (70mm) — all panel connectors
```

---

## Zone 1: South Edge — Panel Connectors (HARD RULE)

ALL three connectors flush with or overhanging south edge.
No exceptions. No other connectors on this edge.

### Placement order left to right (west→east):

| Connector | Ref  | Width | Gap to next | Notes |
|-----------|------|-------|-------------|-------|
| Barrel jack | J_PWR | ~9mm | 3mm | Right-angle, PJ-002A |
| RJ45 jack | J_ETH | ~16mm | 3mm | HR911105A, right-angle |
| USB-C | J_USB | ~9mm | — | Mid-mount SMD |
| LED window | — | 5mm hole | 2mm from J_USB | 5mm panel hole for WS2812B |

Total span: ~43mm. Centered on 70mm edge leaves ~13.5mm margin each side.

### South edge clearance rules
- No signal vias within 3mm of south edge
- No copper pour within 2mm of south edge  
- Keep connector pads ≥1mm from board edge fab line
- EasyEDA: set board edge on "Board Outline" layer, verify DRC clearance

---

## Zone 2: North Edge — Daughterboard Interface

J3 (2×10 2.54mm male header) centered on north edge.
Cable/header exits toward the back of the enclosure.

- Center J3 on north edge, perpendicular to board
- Keep 5mm clearance north of J3 pads (header occupies 25.4mm span)
- Route J3 signal traces directly south — short runs to ESP32-S3

---

## Zone 3: East/West Edges — SD Card

J_SD (microSD slot) on EAST or WEST edge, top face.
Card ejects sideways, not toward the panel connectors.

- Prefer east edge if W5500 is placed toward west (keeps HSPI traces short)
- Card slot body overhangs edge by ~1–2mm — account for in enclosure cutout
  OR recess slightly so card is accessible via a slot in enclosure wall

---

## Zone 4: Top Face Center — Main Components

Primary placement area for active components.

### Suggested placement order (place in this sequence):

1. **U1 — ESP32-S3-WROOM-1-N16**
   - Center of board, slightly north of center
   - Antenna end faces NORTH (away from switching components)
   - Antenna keepout: no copper under module footprint (observe module datasheet keepout)
   - All GPIO traces route south from module pads

2. **U2 — W5500 (QFN-48)**
   - Place between ESP32-S3 and J_ETH (southwest quadrant)
   - HSPI traces (GPIO 18/19/20) should be short and direct to W5500
   - 25MHz crystal (Y1) immediately adjacent to W5500 pins 22/23
   - Crystal load caps (C_X1, C_X2) within 2mm of crystal pads
   - RBIAS resistor (R_BIAS) within 5mm of W5500 pin 43

3. **U5 — AMS1117-3.3 (SOT-223)**
   - Near south edge, west of J_PWR
   - Input cap (C_12V_A) and output cap (C_3V3_A) immediately adjacent

4. **U3 — AG9800MT (SOP-16, DNP)**
   - Northwest corner, isolated from signal traces
   - PoE flyback transformer (when populated) also northwest
   - Keep PoE switching components ≥10mm from W5500 crystal and Ethernet magnetics

5. **U4 — CH340N (SOP-8)**
   - Near J_USB on south edge
   - UART traces route directly to ESP32-S3 GPIO43/44

6. **U6 — LP2985-33 (SOT-23-5)**
   - Adjacent to U5 (AMS1117), west side
   - VDDA_FILT runs to J3 pin 1/3 (north edge)

---

## Trace Width Guidelines

| Net | Min Width | Preferred | Notes |
|-----|-----------|-----------|-------|
| 12V_RAIL | 1.0mm | 1.5mm | Moderate current (up to 1.5A with PoE) |
| 3V3 (power) | 0.5mm | 0.8mm | |
| GND (power) | 1.0mm | Pour | Use solid GND pour on B.Cu |
| SPI signals | 0.2mm | 0.25mm | Keep equal length if possible |
| USB D+/D– | 0.2mm | 0.2mm | Differential pair, match length ±0.5mm |
| Ethernet TX/RX | 0.2mm | 0.2mm | Differential pairs, keep away from switching nodes |
| VBAT (J3 — not present on main board) | N/A | — | HV stays on daughterboard |

---

## Ground Plane Strategy

- B.Cu: solid GND pour across entire board
- F.Cu: GND fill in unused areas, stitched to B.Cu with vias every ~5mm
- GND stitching vias: 0.4mm drill, 0.8mm pad, placed in grid around switching areas
- Analog GND (VDDA path): no special split needed on main board — VDDA isolation
  is handled on the daughterboard. Main board VDDA is just a filtered 3.3V.
- USB GND: connect USB-C shield/GND to board GND directly

---

## Differential Pair Routing

### USB D+/D– (CH340N to J_USB)
- Keep traces equal length, matched to ±0.5mm
- Route as a pair, minimum separation = 2× trace width
- No vias in the diff pair run if avoidable
- No 90° bends — use 45° or curved

### Ethernet TX+/TX– and RX+/RX– (W5500 to J_ETH)
- Route as differential pairs through the RJ45 integrated magnetics
- Keep diff pairs away from 12V switching nodes and crystal
- These are low-speed (100Mbit) — not as sensitive as USB 3.0 but still treat with care

---

## Crystal Placement (W5500 25MHz)

- Y1 (crystal): within 5mm of W5500 pins 22 and 23
- C_X1, C_X2 (22pF): within 2mm of crystal terminals, symmetric placement
- GND via directly adjacent to each load cap
- Surround with GND pour and stitching vias
- No signal traces routed under or near crystal

---

## PoE Section (Northwest corner, DNP for portable)

Layout the PoE components even for portable units — they're just unpopulated.
Keep the entire PoE section in the northwest corner:
- AG9800MT + flyback transformer + bridge rectifier
- SB_POE solder bridge: place at boundary between PoE section and 12V_RAIL
- Label silkscreen: "POE — DNP FOR PORTABLE" near this zone
- PoE switching node (SW_NODE_POE): keep trace short, away from all signal traces
- Minimum 5mm separation between PoE zone and W5500/crystal/Ethernet magnetics

---

## Silkscreen Requirements

- Board name and revision: "PHONE PROP MAIN BOARD v1.0"
- All connector labels: J_PWR (12V DC), J_ETH (ETHERNET), J_USB (PROG/DEBUG)
- J3 header: pin 1 marker + "TO SLIC DAUGHTERBOARD"
- SD card slot: "AUDIO SD CARD"
- Polarity mark on J_PWR: "CENTER +" 
- PoE zone: "POE — DNP FOR PORTABLE UNITS"
- SB_POE: "CLOSE FOR POE INSTALL"
- Test points: label each (TP1=12V, TP2=3V3, TP3=GND, TP4=W5500_INT, etc.)
- LED window marker: circle outline where panel hole aligns with WS2812B

---

## Pre-Layout Checklist (before placing first component)

- [ ] Board outline set to 70mm × 80mm in EasyEDA
- [ ] Grid set to 0.1mm (0.05mm for fine work)
- [ ] Design rules set: min trace 0.2mm, min clearance 0.2mm, min via 0.4mm drill
- [ ] Layer stack confirmed: F.Cu (top), B.Cu (bottom), F.SilkS, B.SilkS, F.Mask, B.Mask
- [ ] South edge connector keepout zone marked (3mm from edge)
- [ ] ESP32-S3 module antenna keepout zone marked
- [ ] Import all footprints and verify before placement
- [ ] Verify HR911105A RJ45 footprint matches datasheet land pattern
- [ ] Verify W5500 QFN-48 footprint (center pad thermal via grid)

---

## Post-Layout Checklist (before Gerber export)

- [ ] All three connectors on south edge — verified
- [ ] J3 header on north edge — verified
- [ ] USB D+/D– differential pair matched length
- [ ] Ethernet TX/RX differential pairs routed cleanly
- [ ] Crystal surrounded by GND pour with stitching vias
- [ ] No copper within 2mm of south edge
- [ ] No traces under ESP32-S3 antenna keepout
- [ ] GND pour complete on B.Cu, stitching vias placed
- [ ] Silkscreen labels on all connectors and key components
- [ ] DRC passes with 0 errors
- [ ] 3D view reviewed — connector heights clear enclosure ceiling
- [ ] Gerber + drill files exported and verified in Gerber viewer
- [ ] CPL rotation offsets verified (especially QFN, LGA, SOT packages)
