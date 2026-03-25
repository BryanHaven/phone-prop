# ⚠ BOM Flag: SLIC Variant — Do Not Substitute Si32176

## The Issue

The Si3217x family contains multiple variants. They look nearly identical,
share the same 42-pin LGA package and pinout, and are often similarly priced.
However they differ critically in one feature relevant to this design:

| Variant     | DTMF Detection | Notes                              |
|-------------|----------------|------------------------------------|
| Si32171-C   | ✅ Yes         | –110V, PCM/SPI                     |
| Si32172-C   | ✅ Yes         | –110V, ISI interface               |
| Si32173-C   | ✅ Yes         | –140V, ISI interface               |
| Si32175-C   | ✅ Yes         | –110V, ISI + pulse metering        |
| **Si32176-C** | ❌ **NO**    | –110V, PCM/SPI — **NO DTMF**      |
| **Si32177-C** | ✅ **Yes**   | **–140V, PCM/SPI — SPECIFIED**     |

## Why This Matters

The phone prop is designed to work with **any POTS phone** — rotary, touch-tone,
or hybrid — without hardware changes. Touch-tone phones generate DTMF tones
which the Si32177's onboard DSP detects in hardware.

If a Si32176-C is substituted:
- Rotary phones will continue to work (pulse decoding is unaffected)
- Touch-tone phones will appear to work (dial tone plays, handset audio works)
- **Touch-tone digit detection will silently fail** — no digits received,
  no error reported, no obvious indication of the problem
- This failure mode is extremely difficult to diagnose in the field

## Approved Substitutes (all include DTMF)

If Si32177-C-FM1R (LCSC C2676781) is out of stock:

| Part Number    | VBAT   | Interface | DTMF | Notes                     |
|----------------|--------|-----------|------|---------------------------|
| Si32177-C-FM1R | –140V  | PCM/SPI   | ✅   | **Primary spec**          |
| Si32171-C-FM1R | –110V  | PCM/SPI   | ✅   | Less VBAT headroom, OK    |
| Si32175-C-FM1R | –110V  | PCM/SPI   | ✅   | Adds pulse metering (unused) |

All three use the same PCB footprint (42-pin LGA, 5×7mm) and the same
ProSLIC API calls. Firmware change required: none.

## NOT Approved

| Part Number    | Reason                                      |
|----------------|---------------------------------------------|
| Si32176-C      | No DTMF — will break touch-tone support     |
| Si32172/3-C    | ISI interface — different pinout, wrong bus |
| Si3210/11      | Older generation, different package         |
| Any grey-market "Si32177" from AliExpress | Counterfeits confirmed |

## At Scale (50+ units)

If ordering in volume, contact Arrow or Avnet for formal quote on Si32177-C-FM1R.
Specify the exact part number — do not accept substitutions from distributors
without verifying the variant number matches an approved substitute above.

## Verification During Bringup

After first power-up, before connecting a telephone, run the SPI verification
test (proslic_verify_spi). This confirms communication but does NOT confirm
DTMF capability.

To verify DTMF is functional, after full ProSLIC init:
1. Connect a touch-tone phone
2. Lift handset
3. Press any digit
4. Confirm firmware receives on_dtmf_digit() callback with correct value
5. If no callback fires, verify Si32177 variant and DTMF enable register

ProSLIC API to enable DTMF detection:
  Si3217x_DTMFEnable(pProslic);   // Call after calibration, before linefeed active
