# SLIC Daughterboard

## Purpose
Self-contained high-voltage analog telephone line interface.  
Plugs into the main board via a 2×10 2.54mm header.

## What's On This Board
- **Si32177-C** ProSLIC FXS SLIC (42-pin LGA)
- **DC-DC boost converter** (generates –48V to –110V VBAT from 12V input)
- **RJ11 telephone connector** (TIP/RING to rotary telephone)
- **Reverse polarity + fuse protection** on 12V input
- **TVS surge protection** on TIP/RING lines
- **LDO regulators** (3.3V digital + 3.3V clean analog VDDA)
- **Test points** on all key nodes

## Board Dimensions
Target: ~45mm × 55mm, 2-layer

## Status
- [ ] Schematic entry (EasyEDA Pro)
- [ ] Q1 MOSFET substitute selected (see BOM-FLAG-Q1-MOSFET.md)
- [ ] Si32177 LGA42 footprint verified (see footprint-checklist.md)
- [ ] PCB layout
- [ ] DRC / ERC clean
- [ ] Gerber + CPL + BOM exported for JLCPCB
- [ ] First article assembled
- [ ] SPI bringup test passed (proslic_verify_spi)
- [ ] ProSLIC calibration passed
- [ ] Hook detection verified
- [ ] Audio playback verified
- [ ] Ring generation verified

## Files
- `schematic-notes.md` — full net list and connection guide for EasyEDA entry
- `footprint-checklist.md` — Si32177 LGA42 footprint verification
- `bom.csv` — component BOM for JLCPCB assembly
- `BOM-FLAG-Q1-MOSFET.md` — ⚠ required before ordering

## 2×10 Header Pinout (J3)
```
Pin  1: 3V3         Pin  2: GND
Pin  3: 3V3         Pin  4: GND
Pin  5: SPI_CLK     Pin  6: SPI_CS
Pin  7: SPI_MOSI    Pin  8: SPI_MISO
Pin  9: PCM_PCLK    Pin 10: PCM_FSYNC
Pin 11: PCM_DRX     Pin 12: PCM_DTX
Pin 13: SLIC_INT    Pin 14: SLIC_RST
Pin 15: SPARE       Pin 16: SPARE
Pin 17: SPARE       Pin 18: SPARE
Pin 19: GND         Pin 20: GND
```

## Safety
⚠ VBAT rail operates at –48V to –110V DC.  
⚠ Ring voltage is ~90VAC generated internally.  
Treat all work on this board as hazardous voltage.  
Conformal coat after assembly.
