# Si32177-C LGA42 Footprint Verification Checklist
## EasyEDA Pro — Before Sending to JLCPCB

Source document: Skyworks Si3217x-C Datasheet, Section 10 (PCB Land Pattern)
Package: 42-pin LGA, 5.0 × 7.0mm body

---

## Land Pattern Dimensions (from Skyworks datasheet)

### Pad Array
- Total pins: 42 (including center exposed thermal pad)
- Body size: 5.0mm × 7.0mm
- Pad pitch: 0.65mm (both X and Y)
- Pad size: 0.40mm × 0.40mm (signal pads)
- Center exposed pad: 3.0mm × 4.8mm (GND thermal pad)

### Pin Layout (top view, pin 1 at top-left)
```
        ← 5.0mm →

  ┌─┬─┬─┬─┬─┬─┬─┐    ↑
  1 2 3 4 5 6 7  │    │
  │               │   7.0mm
  │     (GND)    │    │
  │               │    ↓
  │  36 37 38 39 40 41 42
  └─┴─┴─┴─┴─┴─┴─┘

Pins 1–7:   Top row (left to right)
Pins 8–14:  Right column (top to bottom) — Note: verify orientation in datasheet
Pins 15–21: Bottom row (right to left)
Pins 22–28: Left column (bottom to top)
Pins 29–42: Inner rows (check datasheet figure carefully)
```

> ⚠ The exact pin numbering for LGA42 is in the datasheet Figure/Table.
> Do NOT assume — open the Si3217x-C datasheet Section 8 (Pin Descriptions)
> and cross-reference with Section 10 (Package Outline / Land Pattern).
> Skyworks URL: https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/data-sheets/si3217x-c.pdf

---

## Checklist — Verify Before Placing on PCB

### Symbol Checks
- [ ] All 42 pins present in EasyEDA symbol
- [ ] Pin numbers match datasheet pin descriptions exactly
- [ ] VBAT (pin 1) correctly identified — this is the negative HV rail
- [ ] DCDRV (pin 38) and DCDM (pin 39) present and correctly named
- [ ] TIP (pin 14) and RING (pin 13) correctly identified
- [ ] SPI pins (31–34: SCLK, CS, SDI, SDO) correct
- [ ] PCM pins (27–30: PCLK, FSYNC, DTX, DRX) correct
- [ ] INT (pin 36) and RST (pin 37) correct
- [ ] Power supply pins (40=VDDD, 41=VDDA, 42=VDDHV) correct
- [ ] GND / AGND / DGND pins all connected to GND net
- [ ] FXO-related pins (Si32178/9 only) marked as NC

### Footprint Checks
- [ ] Overall footprint dimensions: 5.0mm × 7.0mm courtyard minimum
- [ ] 42 signal pads: 0.40mm × 0.40mm each
- [ ] Pad pitch: 0.65mm — measured with ruler tool in EasyEDA
- [ ] Center exposed pad: verify dimensions from datasheet
- [ ] Center pad subdivided into 4×4 thermal via grid (see below)
- [ ] Pin 1 marker clearly visible on silkscreen (dot or notch)
- [ ] Silkscreen outline does NOT overlap any pads
- [ ] Courtyard clearance ≥ 0.25mm from pad edges
- [ ] Paste layer present on all signal pads AND center pad (with 20% relief)

### Center Thermal Pad / Via Requirements
- [ ] Center pad tied to GND net in EasyEDA
- [ ] 4×4 grid of thermal vias through center pad
- [ ] Via drill: 0.3mm
- [ ] Via pad: 0.6mm
- [ ] Via fill: tented (covered by solder mask) to prevent solder wicking
- [ ] Vias connect to GND copper pour on B.Cu

---

## Getting the Footprint

### Option A: LCSC/EasyEDA Library (Fastest)
Search EasyEDA component library for "C2676781" (LCSC number for Si32177-C-FM1R).
If the component exists in the LCSC library it will include a verified footprint.
Inspect it carefully against the checklist above before using.

### Option B: Ultra Librarian or SnapEDA
- Go to https://www.ultralibrarian.com or https://www.snapeda.com
- Search "Si32177" or "Si3217x"
- Download EasyEDA/KiCad format
- Import into EasyEDA Pro and verify against checklist

### Option C: Manual Entry in EasyEDA Pro
If neither library has a verified footprint:
1. Open EasyEDA Pro → Footprint Editor → New Footprint
2. Set grid to 0.05mm
3. Use "Pad" tool, set pad size to 0.40×0.40mm
4. Place first pad at origin, use array tool with 0.65mm pitch
5. Refer to the datasheet land pattern figure exactly
6. Add center pad as a separate larger pad, assign to GND net
7. Add thermal vias manually (or via via array tool)
8. Add silkscreen outline and pin 1 marker
9. Set courtyard to 5.5mm × 7.5mm (0.25mm clearance)

---

## JLCPCB Assembly Notes for LGA42

- LGA is fully supported by JLCPCB SMT assembly
- Ensure stencil aperture for center pad has 60–70% coverage (not 100%)
  to prevent excessive solder paste causing bridging or tombstoning
- Request X-ray or AOI inspection on the ProSLIC — bridged pads under
  the IC are very difficult to rework given the LGA package
- Paste layer: use 0.12mm stencil (standard JLCPCB default is fine)
- In your CPL file, rotation offset for LGA42 packages is often 0° or 180°
  — verify during JLCPCB review step; they will flag if wrong

---

## Bringup Test: SPI Communication Verification

Before testing any analog function, verify SPI is working:

```c
// Write 0x5A to PCMRXLO register (reg 0x22), read it back
// If read returns 0x5A, SPI and ProSLIC are communicating correctly
uint8_t test_val = proslic_read_reg(CHANNEL_0, PCMRXLO);
// Expected: 0x00 (reset default)
proslic_write_reg(CHANNEL_0, PCMRXLO, 0x5A);
test_val = proslic_read_reg(CHANNEL_0, PCMRXLO);
// Expected: 0x5A — if not, SPI wiring or footprint issue
```

This is the first thing to run after power-up. If it fails:
1. Check VDDD (3.3V) at Si32177 pin 40
2. Check RST is deasserted (high) after boot delay
3. Check SPI CLK polarity (mode 0: CPOL=0, CPHA=0)
4. Check CS is being driven low during transaction
5. Probe SCLK and SDI with logic analyzer to confirm waveform
