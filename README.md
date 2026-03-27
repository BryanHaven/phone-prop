# Phone Prop Controller

Escape room telephone prop controller. Connects a real POTS rotary or touch-tone
telephone to Mythric Mystery Master over MQTT. Decodes rotary pulse dialing and DTMF,
plays audio through the handset, and emulates a complete FXS telephone line.

**Hardware:** Custom two-board design — ESP32-S3 main board + Si32177-C ProSLIC
daughterboard. Ethernet (W5500) primary, WiFi fallback. PoE or barrel jack power.

---

## Project Status

| Phase | Description | Status |
|-------|-------------|--------|
| **1A** | Ethernet + WiFi + MQTT + WebUI provisioning | ✅ Complete |
| **1B** | SD card audio pipeline (external breakout) | ⏳ Waiting on parts |
| **2**  | ProSLIC bringup — hook detect, dial, ring, audio | ⏳ Waiting on parts |
| **3**  | Full integration on production PCB | 🔲 Not started |

### Phase 1A — What's working
- W5500 Ethernet (primary) with DHCP
- WiFi STA fallback with automatic switchover
- SoftAP provisioning mode on first boot (`PhoneProp-{device_id}`, open, `192.168.4.1`)
- NVS-backed per-unit config (MQTT broker, base topic, device ID, network mode, phone mode, etc.)
- WebUI at `http://<ip>/` — always-visible status bar (network, IP, MQTT, uptime, heap; auto-refreshes every 5 s) above four tabs:
  - **Config** — all device settings, save to NVS, reboot
  - **Dial Rules** — map dialed numbers to actions (play file, tones, ignore) + optional MQTT event; saved to SPIFFS
  - **Audio Files** — upload/delete WAV files to SD card (active in Phase 1B)
  - **Firmware** — flash firmware .bin via browser upload; shows running/next partition
- OTA firmware update — WebUI push (`POST /api/ota`, streaming write) and MQTT pull (`command/ota` = URL)
- MQTT client with auto-reconnect
- mDNS — device reachable as `{device_id}.local` on all interfaces
- WS2812B status LED state machine (red blink = no network, amber blink = no MQTT, green = ready)
- Phone prop state machine running (IDLE state)
- ProSLIC SPI bus init + echo self-test (no daughterboard yet)
- I2S PCM bus init (Phase 2 BCLK tuning pending)

---

## Hardware Architecture

### Two-Board Design

```
┌─────────────────────────────┐      2×10 header (J3)      ┌──────────────────────────┐
│        Main Board           │◄──────────────────────────►│    SLIC Daughterboard    │
│                             │                             │                          │
│  ESP32-S3-WROOM-1-N16       │  SPI, PCM, INT, RST, 3V3   │  Si32177-C ProSLIC FXS   │
│  W5500 Ethernet + RJ45      │                             │  DC-DC boost → VBAT      │
│  AG9800MT PoE PD (DNP opt.) │                             │  RJ11 telephone jack     │
│  microSD (SPI/HSPI)         │                             │  TVS on TIP/RING         │
│  CH340N USB-C               │                             │  –48V to –110V ⚠         │
│  WS2812B status LED         │                             │                          │
│  ~70×80mm, 2-layer          │                             │  ~45×55mm, 2-layer       │
└─────────────────────────────┘                             └──────────────────────────┘
```

### Key ICs

| IC | Part | Function |
|----|------|----------|
| U1 | ESP32-S3-WROOM-1-N16 | Main MCU, 16MB flash, WiFi/BT |
| U2 | W5500 | SPI Ethernet controller |
| U3 | Si32177-C-FM1R | ProSLIC FXS SLIC (DTMF + rotary) |
| U4 | AG9800MT | PoE PD — DNP for portable units |
| U5 | CH340N | USB-UART for programming |
| D1 | WS2812B | RGB status LED |

### SPI Bus Summary

| Bus | Host | GPIOs | Devices |
|-----|------|-------|---------|
| VSPI | SPI2 | 10/11/12/13 | ProSLIC (dedicated, timing-critical) |
| HSPI | SPI3 | 18/19/20 + CS14/15 | W5500 + SD card (separate CS) |

---

## Build & Flash

### Requirements
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP-IDF framework (installed automatically by PlatformIO)

### Environments

```bash
# Development — Waveshare ESP32-S3-ETH dev board (Phase 1A/1B)
pio run -e waveshare-s3-eth
pio run -e waveshare-s3-eth --target upload --upload-port COMx

# Development — Waveshare, ProSLIC only / WiFi (Phase 2)
pio run -e waveshare-proslic-only
pio run -e waveshare-proslic-only --target upload --upload-port COMx

# Production — Custom PCB (ESP32-S3-WROOM-1-N16)
pio run -e phone-prop
pio run -e phone-prop --target upload --upload-port COMx

# Serial monitor
pio device monitor --port COMx --baud 115200
```

> **Note:** On first build, PlatformIO runs the IDF Component Manager to download
> `espressif/led_strip` and `espressif/mdns` from components.espressif.com.
> This requires internet access once; components are cached in `managed_components/`.

### sdkconfig overrides

Persistent Kconfig overrides live in `sdkconfig.defaults` (applied to all envs)
and `sdkconfig.<env>` (env-specific). Do not edit the generated `sdkconfig` directly —
it is git-ignored and regenerated on clean builds.

---

## First-Boot Provisioning

On first boot (no WiFi credentials in NVS), the device starts a SoftAP:

1. Connect to WiFi network **`PhoneProp-{device_id}`** (open, no password)
2. Navigate to **`http://192.168.4.1/`**
3. Fill in WiFi SSID/password, MQTT broker URL, base topic, device ID
4. Click **Save Configuration**, then **Reboot**

On subsequent boots the AP does not start. The WebUI remains accessible at:
- **`http://{device_id}.local/`** — mDNS hostname (works on most networks)
- **`http://<assigned-ip>/`** — direct IP (always works)

---

## WebUI — Dial Rules

Rules map a dialed number to an action. Evaluated in order; first match wins.
Unmatched numbers are silently ignored.

| Action | Description |
|--------|-------------|
| `play` | Play a WAV file from SD card |
| `busy` | Busy signal tone |
| `reorder` | Reorder / fast-busy tone |
| `ringback` | Ringback tone |
| `dial_tone` | Dial tone |
| `silence` | Silence / dead air |
| `ignore` | No response |

Each rule optionally publishes a custom string to `{base}/event` when it fires.
Rules are stored in `/spiffs/dial_rules.json` and survive firmware updates.

---

## MQTT Topics

All topics rooted at `mqtt_base_topic` (default `escape/phone`, configurable per unit).

### Publishes

| Topic | Payload | Retained | When |
|-------|---------|----------|------|
| `{base}/status` | `on_hook` \| `off_hook` | ✅ | Hook state changes |
| `{base}/dialed` | `"7"` | — | Each digit as dialed |
| `{base}/number` | `"911"` | — | Complete number after inter-digit timeout |
| `{base}/event` | `ring_start` \| `ring_stop` \| `audio_start` \| `audio_complete` \| custom | — | Events |
| `{base}/network` | `ethernet` \| `wifi` \| `disconnected` \| `offline` (LWT) | ✅ | Network state changes |
| `{base}/identity` | `{"device_id":…,"ip":…,"firmware":…,"uptime_s":…}` | ✅ | On MQTT connect |
| `{base}/ota/status` | `start` \| `complete` \| `failed` \| `refused` | — | OTA progress |

### Subscribes

| Topic | Payload | Action |
|-------|---------|--------|
| `{base}/command/ring` | `start` \| `stop` | Start/stop ring generator |
| `{base}/command/queue_audio` | `msg_clue1.wav` | Arm file for auto-play after answer delay |
| `{base}/command/play` | `msg_clue1.wav` | Play audio file immediately (off-hook only) |
| `{base}/command/hangup` | — | Force on-hook state |
| `{base}/command/reset` | — | Full state machine reset |
| `{base}/command/ota` | `http://host/firmware.bin` | Trigger HTTP pull OTA |

---

## Deployment Profiles

### Permanent Install (PoE + Ethernet)
- Populate AG9800MT and flyback components
- Close `SB_POE` solder bridge
- Set `network_mode = eth_only` in WebUI
- Fixed IP or DHCP reservation recommended

### Portable (Barrel Jack + WiFi)
- AG9800MT and PoE components: DNP
- Leave `SB_POE` solder bridge open
- Set `network_mode = wifi_only` in WebUI
- Provision via SoftAP at new venue

### Mixed / Auto (default)
- Both Ethernet and WiFi configured
- Ethernet preferred when link detected
- Automatic failover to WiFi

---

## Flash Partition Table

| Partition | Size | Purpose |
|-----------|------|---------|
| nvs | 20 KB | WiFi creds, MQTT config, device settings |
| otadata | 8 KB | OTA slot tracking |
| app0 | 1.5 MB | Running firmware (ota_0) |
| app1 | 1.5 MB | OTA staging slot (ota_1) |
| spiffs | ~12 MB | WebUI assets, dial rules JSON |

Audio files are on the SD card — not in flash.

---

## Audio Files

Format: **WAV, 8 kHz, 8-bit µ-Law, mono**

| Path on SD | Contents |
|------------|----------|
| `/audio/system/` | dial_tone, busy_signal, ringback, reorder_tone |
| `/audio/messages/` | msg_clue1.wav, msg_clue2.wav … (MMM-triggered) |

Upload via the **Audio Files** tab in the WebUI (requires SD card — Phase 1B).

---

## Development Phases — Detail

### Phase 1B — SD Card + Audio Pipeline
**Requires:** Adafruit MicroSD Breakout+ (ADA254) wired to GPIO 35–38 on the Waveshare header.

```
ADA254 pin   →  Waveshare header GPIO
CLK          →  35
DI (MOSI)    →  36
DO (MISO)    →  37
CS           →  38
```

Checklist:
- [ ] SD card mounts, files readable
- [ ] WAV playback pipeline: dial tone, busy signal, message
- [ ] I2S output verified (MAX98357A DAC breakout recommended for audio check)
- [ ] MQTT `play` command triggers correct file
- [ ] Audio file upload/delete via WebUI

### Phase 2 — ProSLIC Bringup
**Requires:** SLIC daughterboard prototype (or breadboard Si32177-C circuit).
**Requires:** Skyworks ProSLIC API C library added to `lib/`.
**Note:** W5500 and ProSLIC share GPIOs on the Waveshare board — Phase 2 uses WiFi only with ProSLIC remapped to free header pins (see `DEV-NOTES.md` for remap table).

Checklist:
- [ ] `proslic_verify_spi()` passes (0x5A echo test)
- [ ] ProSLIC calibration completes
- [ ] 48V present on TIP/RING (linefeed active)
- [ ] Hook detect: lift → MQTT `off_hook`, replace → MQTT `on_hook`
- [ ] Rotary dialing: each pulse → MQTT digit
- [ ] DTMF: key press → MQTT digit
- [ ] Ring generation on MQTT command
- [ ] Audio playback through handset

### Phase 3 — Production PCB
Flash `phone-prop` environment. All GPIO conflicts resolved by PCB design.
No firmware changes needed — GPIO assignments are entirely in `platformio.ini` build flags.

---

---

## Safety

> ⚠ **VBAT on the SLIC daughterboard is –48V to –110V DC — hazardous voltage.**
> Ring voltage is ~90 VAC generated internally by the ProSLIC.
> Maintain ≥2mm creepage between VBAT and signal traces.
> Conformal coat the daughterboard after assembly.
> TVS (SMBJ100CA) is mandatory on TIP/RING.

> ⚠ **Do NOT connect PoE and USB-C simultaneously on the Waveshare dev board.**
> No isolation between PoE power rail and USB ground.

---

## Related Projects

- [Raven Animatronic Controller](https://github.com/BryanHaven/raven-animatronic) — same ESP32/JLCPCB/MQTT/MMM workflow
- Mythric Mystery Master — escape room control system (MQTT broker)
