# ESPPIC-V2 — ESP32-based PIC Programmer for Dyson BMS

> Forked from [mengstr/esppic](https://github.com/mengstr/esppic) and updated for ESP32 (ESP-WROOM-32) + PIC16LF1847 (Dyson V6/V7 Battery Management System).

Flash the [FW-Dyson-BMS](https://github.com/tinfever/FW-Dyson-BMS) replacement firmware onto your Dyson vacuum battery using a cheap ESP32 dev board — no PICkit required.

## What's Changed (vs original esppic)

- **Ported from ESP8266 to ESP32** (ESP-WROOM-32 DevKit)
- **Added HVP (High-Voltage Programming)** support — required for Dyson BMS (factory firmware has LVP disabled)
- **PIC16LF1847 support** — config word definitions, correct flash layout
- **Config words read from hex file** — no more hardcoded User IDs / CONFIG values
- **Fixed Issue [#2](https://github.com/mengstr/esppic/issues/2)**: `filename` variable conflict with ESP WebServer (PR [#4](https://github.com/mengstr/esppic/pull/4))
- **Fixed Issue [#3](https://github.com/mengstr/esppic/issues/3)**: Upload page inaccessible after WiFi setup (PR [#5](https://github.com/mengstr/esppic/pull/5))
- **Removed hardcoded WiFi credentials** — clean AP configuration portal
- **Three wrapper scripts** for Windows Git Bash: `install.sh`, `flashesp.sh`, `flashbmc.sh`

## Requirements

- **Windows** with [Git Bash](https://git-scm.com/downloads) (or WSL)
- **ESP-WROOM-32 DevKit** (any ESP32 dev board with USB)
- **USB cable** (micro-USB or USB-C depending on your board)
- **Jumper wires** (5x female-to-female or similar)
- **NPN transistor** (2N2222, 2N3904, or similar) + **1K resistor** for VPP control
- **8-9V power supply** (bench supply, 9V battery, or boost converter) for HVP
- **Dyson V6/V7 battery** (disassembled, with ICSP pads accessible)

## Wiring

### ESP32 DevKit to Dyson BMS (PIC16LF1847 ICSP)

The NPN transistor protects the ESP32 from the 8-9V VPP. When GPIO 27 goes HIGH, the transistor pulls MCLR to GND. When GPIO 27 goes LOW, MCLR is released and the external 8-9V reaches the pin.

```
                         ┌─────────────────────────────────────────────────────┐
    ESP32 DevKit         │              Dyson BMS Board (PIC16LF1847)         │
   ┌──────────────┐      │   ┌──────────┐                                     │
   │              │      │   │          │  Pin 5 (VSS)                        │
   │          GND ├──────┼───┤ GND      │◄─────────────────── GND             │
   │              │      │   │          │                                     │
   │      GPIO 25 ├──────┼───┤ ICSPDAT  │  Pin 13 (RB7/PGD)                  │
   │              │      │   │          │◄─────────────────── Data            │
   │      GPIO 26 ├──────┼───┤ ICSPCLK  │  Pin 12 (RB6/PGC)                  │
   │              │      │   │          │◄─────────────────── Clock           │
   │              │      │   │          │                                     │
   │      GPIO 27 ├──┐   │   │ MCLR/VPP │  Pin 4                             │
   │              │  │   │   │          │◄──────┐                             │
   │      GPIO 14 ├──┼─┐ │   │          │       │  VPP                       │
   │              │  │ │ │   │  (VDD)   │  Not Connected (battery powers it) │
   └──────────────┘  │ │ │   └──────────┘       │                             │
                     │ │ │                       │                             │
                     │ │ │   ┌───────────────────┘                             │
                     │ │ │   │                                                 │
                     │ │ │   │   VPP CONTROL CIRCUIT                          │
                     │ │ │   │                                                 │
                     │ │ │   │         Optional Auto-VPP:                     │
                     │ │ │   │                                                 │
                     │ │ └───┤  GPIO 14 ──[1K]──► NPN2 Base                   │
                     │ │     │                    NPN2 Emitter ── GND          │
                     │ │     │                    NPN2 Collector──┐            │
                     │ │     │                                    │            │
                     │ │     │              8-9V ──[Switch]──►────┤            │
                     │ │     │                                    │            │
                     │ │     │         Required MCLR Pull-down:   │            │
                     │ │     │                                    ▼            │
                     │ └─────┤  GPIO 27 ──[1K]──► NPN1 Base    MCLR/VPP      │
                     │       │                    NPN1 Emitter ── GND          │
                     │       │                    NPN1 Collector──┘            │
                     │       │                                                 │
                     └───────┘                                                 │
                                                                               │
                         └─────────────────────────────────────────────────────┘

    NPN transistors: 2N2222, 2N3904, BC547 — any small-signal NPN works
    Resistors: 1K ohm (base resistors for transistors)
```

### Simplified Wiring (Manual VPP)

If you don't have the parts for the full auto-VPP circuit, you **still need one NPN transistor on GPIO 27** — this is required for the HVP entry sequence (the ESP32 must pull MCLR to GND before VPP is applied). You can manually connect 8-9V when the script tells you to:

```
    ESP32 DevKit                    Dyson BMS (ICSP Pads)
    ────────────                    ─────────────────────
    GND          ──────────────────  GND
    GPIO 25      ──────────────────  ICSPDAT  (PGD / Pin 13)
    GPIO 26      ──────────────────  ICSPCLK  (PGC / Pin 12)
    GPIO 27      ──[1K]──NPN──GND    MCLR/VPP (Pin 4)
                         └─────────► │
                                     │ ◄── 8-9V (connect ONLY when prompted!)
```

> **WARNING**: Do NOT connect the 8-9V supply directly to an ESP32 GPIO pin.
> The NPN transistor on GPIO 27 is **required** — it pulls MCLR to GND during
> HVP entry. Without it, the PIC will not enter programming mode and all
> reads will return 0x3FFF (blank).

### Pin Summary

| ESP32 GPIO | Function     | Dyson BMS PIC16LF1847 Pin | Notes                           |
|------------|-------------|---------------------------|----------------------------------|
| GND        | Ground       | Pin 5 (VSS)              | Common ground                    |
| GPIO 25    | ICSPDAT      | Pin 13 (RB7/PGD)         | Bidirectional data line          |
| GPIO 26    | ICSPCLK      | Pin 12 (RB6/PGC)         | Clock output                     |
| GPIO 27    | MCLR control | Pin 4 (MCLR/VPP)         | Via NPN transistor (pull to GND) |
| GPIO 14    | VPP enable   | —                         | Optional: controls 8-9V switch   |
| —          | VDD          | Pin 14 (VDD)              | NOT connected (battery powered)  |

## Quick Start (3-Script Process)

All scripts run in **Git Bash** on Windows.

### Step 1: Install toolchain
```bash
./install.sh
```
Installs Arduino CLI, ESP32 board support, and required libraries.

### Step 2: Flash the ESP32 programmer firmware
```bash
./flashesp.sh
```
Compiles and uploads the programmer firmware to your ESP32 via USB.

### Step 3: Flash the Dyson BMS
```bash
./flashbmc.sh
```
Downloads the FW-Dyson-BMS hex file, connects to the ESP32 over WiFi, uploads the hex, and triggers programming. Interactive — guides you through wiring and VPP timing.

## How It Works

1. **ESP32 firmware** acts as a PIC ICSP programmer with a web interface
2. **WiFi setup**: On first boot, ESP32 creates an "ESPPIC" access point — connect and enter your WiFi credentials
3. **Web UI** at `http://<esp32-ip>/i.html` lets you upload `.hex` files, read config, and trigger flash
4. **HVP mode**: ESP32 controls MCLR via transistor, then signals to apply 8-9V VPP for programming entry
5. **ICSP protocol**: Standard Enhanced Mid-Range 6-bit commands flash the PIC16LF1847

## Dyson BMS Firmware

This tool is designed to flash the **[FW-Dyson-BMS](https://github.com/tinfever/FW-Dyson-BMS)** replacement firmware by tinfever.

### Compatible batteries:
- Dyson V7 (SV11) — PCB 279857 ✓
- Dyson V6 (SV04/SV09) — PCB 61462 ✓  
- Dyson V6 (SV04) — PCB 188002 ✓

### Safety warnings:
- **Li-ion batteries are dangerous** — can output 100+ amps if short-circuited
- **The firmware flash is irreversible** — factory firmware cannot be restored
- **Do NOT connect VDD** — let the battery power the PIC through its own regulator
- Wake the battery first (press button / magnet on reed switch for V7) before programming

## Project Structure

```
esppic-V2/
├── esppic/              # ESP32 Arduino firmware
│   ├── esppic.ino       # Main webserver + WebSocket handler
│   ├── espconnect.ino   # WiFi STA/AP connection manager
│   ├── prg_pic.ino      # PIC ICSP programming (LVP + HVP)
│   ├── radix.ino        # Hex/binary conversion utilities
│   └── *.h              # Embedded web assets
├── assets/              # Web UI source files
│   ├── index.html       # Main interface
│   ├── configinfo.js    # PIC16LF1847 config word definitions
│   ├── dragupload.js    # Drag-and-drop file upload
│   └── winsocket.js     # WebSocket client
├── install.sh           # Step 1: Install toolchain
├── flashesp.sh          # Step 2: Flash ESP32 firmware
├── flashbmc.sh          # Step 3: Flash Dyson BMS
└── README.md
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| ESP32 not detected on USB | Install [CP210x](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) or [CH340](http://www.wch-ic.com/downloads/CH341SER_ZIP.html) USB driver |
| Can't connect to ESPPIC AP | Reset ESP32, wait 30s for AP to appear |
| "File error" during flash | Make sure `.hex` file uploaded successfully first |
| PIC not responding | Check wiring, ensure battery is awake (press button/magnet) |
| Flash succeeds but PIC doesn't run | Verify VPP was removed after programming |
| Config reads all 0x3FFF (blank) | NPN transistor on GPIO 27 is missing — required for HVP entry |
| Compile error on `filename` | Already fixed in this fork (Issue #2) |

## Credits

- **Original esppic**: [Mats Engstrom / SmallRoomLabs](https://github.com/mengstr/esppic) (MIT License, 2016)
- **FW-Dyson-BMS**: [tinfever](https://github.com/tinfever/FW-Dyson-BMS)
- **Bug fixes**: [harish2704](https://github.com/harish2704) (PR #4, PR #5)

## License

MIT — see [LICENSE.md](LICENSE.md)
