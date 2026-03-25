# SLIC Daughterboard — Schematic Notes
## For EasyEDA Pro Entry

Board: slic-daughterboard  
Dimensions: ~45 × 55mm  
Layers: 2 (F.Cu / B.Cu)  
Assembly: JLCPCB  
Status: Schematic entry in progress

---

## Net List & Component Connections

### Power Entry

```
12V_IN (J1 pin 1)
  → F1 (polyfuse, 500mA) → 12V_FUSED net
  → Q2 gate (P-ch reverse polarity, AO3401)

Q2 (AO3401, SOT-23)
  Source → 12V_IN
  Gate   → 12V_IN via 10kΩ (R_Q2G), Gate also → GND via 100kΩ (R_Q2GND)
  Drain  → 12V_SWITCHED net

Note: P-ch gate resistors create a simple discrete reverse-polarity
protection. When 12V is correctly applied, Q2 gate is pulled below
source threshold, FET conducts. Reverse polarity: gate = source, FET off.
```

### 3.3V Digital Supply (U3 — AMS1117-3.3)

```
IN  → 12V_SWITCHED
OUT → 3V3_DIGITAL net
     → C_U3A (100µF electrolytic) to GND
     → C_U3B (100nF 0402) to GND
     → Header J3 pins 1, 3 (3V3 to main board)

IMPORTANT: U3 only generates 3V3 here as a backup / standalone option.
In the final architecture the main board supplies 3V3 via J3 pins 1&3.
Populate U3 for standalone daughterboard testing; may DNP in production.
```

### 3.3V Analog Supply (U4 — LP2985-33, SOT-23-5)

```
IN  → 3V3_DIGITAL (from U3 or J3)
EN  → 3V3_DIGITAL (always enabled)
OUT → 3V3_ANALOG net (VDDA for Si32177)
     → C_U4A (10µF 0805) to GND
     → C_U4B (100nF 0402) to GND
     → L2 (ferrite bead 600Ω@100MHz) → VDDA_FILTERED net
     → VDDA_FILTERED → Si32177 pins 41 (VDDA), 42 (VDDHV)
     → Additional 100nF at each Si32177 VDDA/VDDHV pin to GND

Note: LP2985 provides lower noise than AMS1117 on the analog rail.
The ferrite + cap form a final LC filter for extra PSRR.
```

### DC-DC Boost Converter (Q1, L1, D1, C7)

```
Driven by: Si32177 pin 38 (DCDRV) — internal charge-pump gate driver

Q1 (AP2310GJ-HF, N-ch, SOT-23, 300V/500mA — hand-solder, DNP on JLCPCB BOM)
  Gate   → Si32177 DCDRV (pin 38) via 10Ω R_GATE (damps switching ringing)
  Source → GND (PGND — keep short, direct to GND pour)
  Drain  → SW_NODE net
  NOTE: AO3400 original is WRONG (30V Vds). Use AP2310GJ-HF. See BOM-FLAG-Q1-MOSFET.md.

L1 (100µH, 500mA, CDRH8D43 or SRR8028)
  Pin 1  → 12V_SWITCHED
  Pin 2  → SW_NODE

D1 (SS2200, Schottky, 200V/1A, SMA)
  Anode  → SW_NODE
  Cathode → VBAT net (negative of supply — VBAT is negative voltage)

C7 (100µF/200V electrolytic)
  Positive → GND  ← NOTE: VBAT is negative; cap installed INVERTED
  Negative → VBAT net

Si32177 DCDM (pin 39) → VBAT net (DC-DC monitor/feedback)
Si32177 VBAT (pin 1)  → VBAT net

VBAT net label: VBAT (–48V to –110V, negative rail)
⚠ Mark VBAT on silkscreen and in EasyEDA net color as high-voltage.
```

### Si32177-C ProSLIC Connections

```
Power pins:
  Pin 1  (VBAT)   → VBAT net
  Pin 40 (VDDD)   → 3V3_DIGITAL + 100nF bypass to GND
  Pin 41 (VDDA)   → VDDA_FILTERED + 100nF bypass to GND
  Pin 42 (VDDHV)  → VDDA_FILTERED + 100nF bypass to GND
  AGND/DGND pins  → GND (star ground under IC)

SPI Interface (to J3 header → main board):
  Pin 31 (SCLK)   → J3 pin 5  (SPI_CLK)
  Pin 32 (CS)     → J3 pin 6  (SPI_CS)
  Pin 33 (SDI)    → J3 pin 7  (SPI_MOSI)
  Pin 34 (SDO)    → J3 pin 8  (SPI_MISO)

PCM Interface (to J3 header → main board):
  Pin 27 (PCLK)   → J3 pin 9  (PCM_PCLK)
  Pin 28 (FSYNC)  → J3 pin 10 (PCM_FSYNC)
  Pin 29 (DRX)    → J3 pin 11 (PCM_DRX)
  Pin 30 (DTX)    → J3 pin 12 (PCM_DTX)

Control (to J3 header → main board):
  Pin 36 (INT)    → J3 pin 13 (SLIC_INT)
                    + 4.7kΩ pullup to 3V3_DIGITAL
  Pin 37 (RST)    → J3 pin 14 (SLIC_RST)
                    + 10kΩ pullup to 3V3_DIGITAL

DC-DC Controller:
  Pin 38 (DCDRV)  → Q1 Gate (via 10Ω R_GATE)
  Pin 39 (DCDM)   → VBAT net (feedback monitor)

Analog Line (via protection components to J2):
  Pin 14 (TIP)    → R4A (200Ω PTC) → TVS_TIP → J2 pin 4 (TIP)
  Pin 13 (RING)   → R4B (200Ω PTC) → TVS_RING → J2 pin 3 (RING)

Unused pins: Leave all ISI/FXO pins (Si32178/9 only) unconnected.
Per datasheet, Si32177 does not have FXO pins — all 42 pins are defined.
```

### TIP/RING Protection Chain

```
Si32177 TIP (pin 14)
  → C5A (1µF/250V film, AC coupling) [optional — omit if direct DC feed preferred]
  → R4A (RXEF010 PTC thermistor, 200Ω)
  → TP1 (test point pad)
  → D2A anode (SMBJ100CA TVS, bidirectional)
  → J2 pin 4 (TIP to telephone)

D2A cathode → J2 pin 4 (other side of TVS also at tip)
D2A center  → GND  (TVS clamps to GND in both polarities)

Same structure mirrored for RING (pin 13) → D2B → J2 pin 3
```

### Header J3 — 2×10, 2.54mm (Main Board Interface)

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

### RJ11 Connector J2 — 6P4C

```
Pin 1: NC
Pin 2: NC
Pin 3: RING (from protection chain)
Pin 4: TIP  (from protection chain)
Pin 5: NC
Pin 6: NC
```

### Test Points

```
TP1: TIP line (between PTC and TVS, before RJ11)
TP2: RING line (between PTC and TVS, before RJ11)
TP3: VBAT (–48V to –110V) ← label with ⚠ HV on silkscreen
TP4: 3V3_DIGITAL
TP5: 3V3_ANALOG (VDDA_FILTERED)
TP6: GND
TP7: SW_NODE (boost converter switching node — for scope probe during bringup)
TP8: DCDRV signal (gate drive waveform probe point)
```

---

## EasyEDA Symbol Search Terms

When searching LCSC/EasyEDA library for each component:

| Component       | Search Term / LCSC #         | Notes |
|-----------------|------------------------------|-------|
| Si32177-C-FM1R  | C2676781                     | May need manual symbol; use generic IC body |
| AP2310GJ-HF N-FET | DNP (hand-solder)          | SOT-23; replaces AO3400 — see BOM-FLAG-Q1-MOSFET.md |
| AO3401 P-FET    | C15127                       | SOT-23 |
| AMS1117-3.3     | C6186                        | SOT-223 |
| LP2985-33DBVR   | C99075                       | SOT-23-5 |
| CDRH8D43-101    | search "100uH power inductor" | Verify Isat > 600mA |
| SS2200          | search "SS2200 SMA"          | Or 1N4936 if SS2200 unavail |
| SMBJ100CA       | search "SMBJ100CA"           | Bidirectional TVS |
| RXEF010 PTC     | search "RXEF010"             | Or MF-R010 |
| 6P4C RJ11 jack  | C381104 or search "RJ11 SMD" | Verify horizontal vs vertical |

---

## Schematic Entry Order (Recommended)

1. Place power nets and ground symbols first
2. Place U3 (AMS1117) with input/output caps → establish 3V3_DIGITAL
3. Place U4 (LP2985) with filter → establish VDDA_FILTERED  
4. Place Si32177 central — route power pins first, then SPI, then PCM, then DCDRV
5. Place Q1, L1, D1, C7 (boost converter) connecting to DCDRV and DCDM
6. Place reverse polarity protection (Q2, F1) at power entry
7. Place J3 header and connect all interface signals
8. Place J2 (RJ11) with TIP/RING protection chain
9. Place all test points
10. Add net labels, power flags, ERC cleanup

---

## ERC (Electrical Rules Check) Expected Warnings

- Si32177 LGA symbol may flag unused pins — this is expected for the
  FXO-related pins (Si32178/9 variant only); add "no connect" markers
- VBAT net will flag as undriven if EasyEDA doesn't recognize the
  DC-DC feedback loop — add a PWR_FLAG symbol on VBAT net
- Open-drain INT pin needs explicit pullup or ERC will warn about it

---

## Critical Layout Notes (for when PCB view opens)

1. Si32177 LGA42 land pattern: pull from Skyworks datasheet Section 10
   exactly. Center exposed pad = GND, must have thermal vias (4×4 grid,
   0.3mm drill, 0.6mm pad, filled).

2. Boost converter loop: L1 → Q1 → D1 → C7 must be as tight as possible.
   Keep SW_NODE trace short and away from SPI/PCM signal traces.

3. VBAT copper pour: keep on bottom layer, isolated from top-layer signals
   by ≥2mm. Mark "HV" on silkscreen adjacent to pour.

4. Star ground: single GND via cluster directly under Si32177 center pad.
   Analog ground (AGND) and digital ground (DGND) meet here only.

5. Bypass caps on Si32177: all four 100nF caps must be within 0.5mm of
   their respective supply pads. Route to GND via the nearest via.

6. TIP/RING trace width: ≥1mm for the run from Si32177 to J2. These
   carry up to 500mA AC during ring generation.
