# ⚠ BOM Issue: Q1 MOSFET Substitution Required

**Status: RESOLVED — AP2310GJ selected (hand-solder from Mouser/DigiKey)**

---

## Problem

The AO3400 (C700953) specified in the original BOM for Q1 (DC-DC boost switch)
is rated for **Vds = 30V maximum**.

The VBAT rail reaches **–48V to –110V**. The boost converter switching node
(SW_NODE) will see voltage spikes above VBAT during switch-off transients.
Using an AO3400 here will result in **immediate MOSFET breakdown and failure**.

---

## Circuit Requirements

Q1 is the switch in the Si32177 DCDRV-controlled boost converter:

- Gate driven by Si32177 pin 38 (DCDRV) — logic output referenced to 3.3V VDD
- DCDRV output swings 0V → VDD (approximately 3.3V)
- Source → PGND (GND pour, must be very short)
- Drain → SW_NODE (inductor junction, spikes during turn-off)

**Required specification:**

| Parameter | Minimum | Preferred | Rationale |
|-----------|---------|-----------|-----------|
| Vds       | 200V    | 300V      | VBAT up to 110V + switch-off spikes |
| Vgs(th)   | —       | < 2V      | Gate drive = 3.3V max; needs headroom |
| Id (cont) | 500mA   | > 500mA   | L1 inductor rated 500mA |
| Rds(on)   | < 5Ω at Vgs=3V | < 2Ω | Lower = less heat in boost switch |
| Package   | SOT-23  | SOT-23    | SOT-89, SOT-223 acceptable |

> **Gate drive note:** The Si32177 DCDRV pin is a logic-level output powered from
> VDDD (3.3V digital supply). Vgs(th) *max* must be comfortably below 3.3V.
> Any part with Vgs(th) max > 3.0V is unreliable at 3.3V drive — some units
> won't be fully enhanced, causing excessive Rds(on) and heat.

---

## Candidate Evaluation

| Part | Vds | Vgs(th) | Id | Rds(on) | Package | LCSC # | Stock | Verdict |
|------|-----|---------|----|---------|---------|--------|-------|---------|
| **AP2310GJ-HF** | **300V** | **1.5V** | **500mA** | **~3Ω @3V** | **SOT-23** | Not on LCSC | Mouser/DigiKey | ✅ **SELECTED** |
| BSP130,115 (Nexperia) | 300V | 2.0V typ | 350mA | 6Ω @10V | SOT-223 | C549735 | Out of stock | Good fallback if restocked |
| STN1HNK60 (STMicro) | 600V | 3.7V max | 400mA | 8.5Ω @10V | SOT-223 | C39303 | ~9,000 | ⚠ Vgs(th) max too high for 3.3V drive |
| ZXMN10A07FTA (Diodes) | 100V | 4.0V | 700mA | 700mΩ @10V | SOT-23 | C140566 | ~6,000 | ❌ Vds too low, Vth too high |
| BSS127 (Infineon) | 600V | 2.5V | 70mA | 75Ω @5V | SOT-23 | C41383973 | 0 | ❌ Rds(on) far too high, Id far too low |
| AO3400 (original) | 30V | 1.0V | 5.8A | 28mΩ | SOT-23 | C700953 | — | ❌ Vds catastrophically low |

### Why STN1HNK60 (C39303) was rejected despite LCSC availability

The STN1HNK60 has a **maximum** Vgs(th) of 3.7V. With a 3.3V gate drive:
- Some units (at the high end of Vgs(th) distribution) will never fully enhance
- Rds(on) at Vgs = 3.3V is poorly specified and likely > 20Ω
- High Rds(on) causes thermal runaway in a switching converter
- Not safe for a production prop that must run unattended

### Why AP2310GJ-HF was selected

- Vgs(th) max 1.8V — fully enhanced at 3.3V with 1.5V of headroom
- Vds 300V — 3× safety margin over worst-case spike
- Id 500mA continuous — matches inductor limit exactly; peak rating is higher
- Rds(on) ~3Ω at Vgs = 3V — acceptable switching loss for this low-frequency boost converter
- Used in similar Si3217x POTS line supply circuits per community reports

---

## Resolution & BOM Action

**Q1 final selection: AP2310GJ-HF (Anpec)**

- Source: Mouser or DigiKey (not on LCSC — must hand-solder)
- Package: SOT-23 — no footprint change required from original AO3400
- In JLCPCB BOM: mark Q1 as **DNP** (do not place)
- Hand-solder Q1 after boards arrive

**BOM update required:**

| Field | Old value | New value |
|-------|-----------|-----------|
| Q1 Designator | Q1 | Q1 |
| Part Number | AO3400 | AP2310GJ-HF |
| LCSC # | C700953 | DNP (hand-solder) |
| Package | SOT-23 | SOT-23 (no change) |
| Notes | — | Source from Mouser/DigiKey; DNP on JLCPCB BOM |

---

## Pre-Bringup Verification (Mandatory)

- [ ] Verify Si32177 AN340 application note recommends a compatible MOSFET
      (Skyworks may have a tested/approved part list — check before ordering)
- [ ] On first bringup: scope DCDRV (TP8) to confirm gate drive voltage
      Expected: 0V → ~3.3V square wave at boost switching frequency
- [ ] On first bringup: scope SW_NODE (TP7) to confirm voltage stays below 200V peak
- [ ] Confirm VBAT settles to target value (–48V typical; –70V to –90V under ring load)
- [ ] Monitor Q1 temperature under ring load (should be warm, not hot)

---

## If AP2310GJ-HF Is Unavailable

Second choice: **Nexperia BSP130,115 (LCSC C549735)**
- Monitor LCSC for restock (was listed, just out of stock)
- Acceptable Vgs(th) typ 2.0V; fully enhanced at 3.3V for most units
- SOT-223 package — footprint change required in EasyEDA (longer pads, higher current)
- Update Q1 footprint to SOT-223-3 if using this part

Third choice: Obtain via DigiKey/Mouser **Infineon BSS126 (note: enhancement mode version)**
- Do not confuse with BSS126 depletion-mode clone listings — verify datasheet
- Or search DigiKey for "N-channel MOSFET SOT-23 200V+ enhancement logic-level"
  filtered by Vgs(th) < 2.5V and Id > 300mA
