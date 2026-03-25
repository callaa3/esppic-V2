#!/bin/bash
#
# flashbmc.sh — Flash Dyson BMS (PIC16LF1847) via ESP32 programmer
# Downloads FW-Dyson-BMS hex, uploads to ESP32, triggers ICSP flash
# Designed for Windows Git Bash
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HEX_DIR="$SCRIPT_DIR/firmware"
FW_RELEASE_URL="https://github.com/tinfever/FW-Dyson-BMS/releases/download/release-v1"
FW_HEX_DEFAULT="FW-Dyson-BMS_V1.hex"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $1"; }
ok()    { echo -e "${GREEN}[OK]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
fail()  { echo -e "${RED}[FAIL]${NC} $1"; exit 1; }

echo ""
echo "=========================================="
echo "  ESPPIC-V2 — Flash Dyson BMS"
echo "  PIC16LF1847 ICSP Programmer"
echo "=========================================="
echo ""

# ------------------------------------------------------------------
# Safety warnings
# ------------------------------------------------------------------
echo -e "${RED}${BOLD}  ⚠  SAFETY WARNING  ⚠${NC}"
echo ""
echo "  You are about to reprogram a LIVE Li-ion battery pack."
echo ""
echo "  • Li-ion batteries can output 100+ amps if short-circuited"
echo "  • The firmware flash is IRREVERSIBLE — factory firmware CANNOT be restored"
echo "  • Do NOT connect VDD from ESP32 — the battery powers the PIC"
echo "  • Work in a safe area away from flammable materials"
echo ""
read -rp "  I understand the risks. Continue? [y/N]: " confirm
if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
    echo "  Aborted."
    exit 0
fi
echo ""

# ------------------------------------------------------------------
# Select Dyson model
# ------------------------------------------------------------------
echo "  Which Dyson battery do you have?"
echo ""
echo "    [1] Dyson V6 (SV04/SV09 — PCB 61462 or 188002)"
echo "    [2] Dyson V7 (SV11 — PCB 279857 or 228499)"
echo "    [3] Custom hex file (provide your own .hex)"
echo ""
read -rp "  Select [1/2/3]: " model_choice

case "$model_choice" in
    1)
        FW_HEX="$FW_HEX_DEFAULT"
        MODEL="Dyson V6"
        ;;
    2)
        FW_HEX="$FW_HEX_DEFAULT"
        MODEL="Dyson V7"
        ;;
    3)
        echo ""
        read -rp "  Enter path to .hex file: " custom_hex
        custom_hex="${custom_hex//\\/\/}"  # Convert backslashes
        if [[ ! -f "$custom_hex" ]]; then
            fail "File not found: $custom_hex"
        fi
        FW_HEX=$(basename "$custom_hex")
        MODEL="Custom"
        mkdir -p "$HEX_DIR"
        cp "$custom_hex" "$HEX_DIR/$FW_HEX"
        ;;
    *)
        fail "Invalid selection."
        ;;
esac

ok "Selected: $MODEL ($FW_HEX)"
echo ""

# ------------------------------------------------------------------
# Download firmware hex
# ------------------------------------------------------------------
mkdir -p "$HEX_DIR"

if [[ "$model_choice" != "3" ]]; then
    if [[ -f "$HEX_DIR/$FW_HEX" ]]; then
        ok "Firmware hex already downloaded: $HEX_DIR/$FW_HEX"
    else
        info "Downloading FW-Dyson-BMS firmware..."
        curl -fSL "$FW_RELEASE_URL/$FW_HEX" -o "$HEX_DIR/$FW_HEX" \
            || fail "Failed to download firmware. Check your internet connection."
        ok "Downloaded: $FW_HEX"
    fi
fi

# Verify hex file looks valid
if ! head -1 "$HEX_DIR/$FW_HEX" | grep -q "^:"; then
    fail "Downloaded file does not look like a valid Intel HEX file."
fi

HEXSIZE=$(wc -c < "$HEX_DIR/$FW_HEX")
ok "Hex file: $FW_HEX ($HEXSIZE bytes)"
echo ""

# ------------------------------------------------------------------
# Get ESP32 IP address
# ------------------------------------------------------------------

# Try to read saved IP from flashesp.sh
SAVED_IP_FILE="$SCRIPT_DIR/.esp32_ip"
if [[ -f "$SAVED_IP_FILE" ]]; then
    SAVED_IP=$(cat "$SAVED_IP_FILE" | tr -d '[:space:]')
    info "Found saved ESP32 IP: $SAVED_IP"
    # Test if it's still reachable
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 3 "http://$SAVED_IP/i.html" 2>/dev/null || echo "000")
    if [[ "$HTTP_CODE" == "200" ]]; then
        ok "ESP32 is reachable at $SAVED_IP!"
        ESP_IP="$SAVED_IP"
    else
        warn "Saved IP $SAVED_IP is not responding."
        ESP_IP=""
    fi
else
    ESP_IP=""
fi

if [[ -z "$ESP_IP" ]]; then
    echo ""
    info "Enter the IP address of your ESPPIC-V2 ESP32."
    echo ""
    echo "  (Check the serial monitor or your router's DHCP table)"
    echo ""
    read -rp "  ESP32 IP address: " ESP_IP

    # Validate IP format
    if [[ ! "$ESP_IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        fail "Invalid IP address format."
    fi

    # Test connectivity
    info "Testing connection to ESP32 at $ESP_IP..."
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 5 "http://$ESP_IP/i.html" 2>/dev/null || echo "000")

    if [[ "$HTTP_CODE" == "200" ]]; then
        ok "ESP32 is reachable!"
        echo "$ESP_IP" > "$SAVED_IP_FILE"
    elif [[ "$HTTP_CODE" == "000" ]]; then
        warn "Cannot reach ESP32 at $ESP_IP"
        echo ""
        echo "  Check that:"
        echo "    - ESP32 is powered on and connected to WiFi"
        echo "    - IP address is correct"
        echo ""
        read -rp "  Try anyway? [y/N]: " retry
        if [[ ! "$retry" =~ ^[Yy]$ ]]; then
            exit 1
        fi
    else
        info "Got HTTP $HTTP_CODE — ESP32 seems reachable, continuing..."
        echo "$ESP_IP" > "$SAVED_IP_FILE"
    fi
fi
echo ""

# ------------------------------------------------------------------
# Wiring check
# ------------------------------------------------------------------
echo -e "${YELLOW}${BOLD}  WIRING CHECKLIST${NC}"
echo ""
echo "  Verify these connections before continuing:"
echo ""
echo "    ESP32 GND      ──────── Dyson BMS GND"
echo "    ESP32 GPIO 25  ──────── ICSPDAT (PGD / Pin 13)"
echo "    ESP32 GPIO 26  ──────── ICSPCLK (PGC / Pin 12)"
echo "    ESP32 GPIO 27  ──[1K]── NPN Base → Collector → MCLR/VPP"
echo ""
echo "    VPP (8-9V)     ──────── Ready to connect to MCLR when prompted"
echo "    VDD             ─ NOT CONNECTED ─ (battery powers the PIC)"
echo ""
echo "  IMPORTANT: Wake the battery first!"
echo "    - V6: Press the trigger button"
echo "    - V7: Press button AND hold magnet on reed switch"
echo ""
read -rp "  Wiring verified and battery is awake? [y/N]: " wired
if [[ ! "$wired" =~ ^[Yy]$ ]]; then
    echo "  Aborted. Wire up your connections and try again."
    exit 0
fi
echo ""

# ------------------------------------------------------------------
# Upload hex file to ESP32
# ------------------------------------------------------------------
info "Uploading hex file to ESP32..."

UPLOAD_RESPONSE=$(curl -s -w "\n%{http_code}" \
    --connect-timeout 10 \
    -F "file=@$HEX_DIR/$FW_HEX" \
    "http://$ESP_IP/upload" 2>/dev/null)

UPLOAD_HTTP=$(echo "$UPLOAD_RESPONSE" | tail -1)
UPLOAD_BODY=$(echo "$UPLOAD_RESPONSE" | head -n -1)

if [[ "$UPLOAD_HTTP" == "200" ]]; then
    ok "Hex file uploaded to ESP32 successfully!"
else
    warn "Upload returned HTTP $UPLOAD_HTTP (the ESP32 may have auto-flashed after upload)"
    echo "  Response: $UPLOAD_BODY"
fi
echo ""

# ------------------------------------------------------------------
# VPP timing prompt (for HVP mode)
# ------------------------------------------------------------------
echo -e "${YELLOW}${BOLD}  VPP CONNECTION TIMING${NC}"
echo ""
echo "  The ESP32 will now attempt to program the PIC16LF1847."
echo "  Since HVP (High-Voltage Programming) mode is used:"
echo ""
echo "  If you have the AUTO-VPP circuit (GPIO 14 controls 8-9V):"
echo "    → VPP will be applied automatically. Just press Enter."
echo ""
echo "  If you are using MANUAL VPP:"
echo "    1. Have your 8-9V supply ready but NOT connected to MCLR"
echo "    2. Press Enter below — the ESP32 will pull MCLR low"
echo "    3. QUICKLY connect 8-9V to MCLR/VPP when prompted"
echo ""
read -rp "  Ready to flash? Press Enter to begin... "
echo ""

# ------------------------------------------------------------------
# Trigger flash
# ------------------------------------------------------------------
info "Triggering PIC flash via ESP32..."

# The upload handler auto-triggers flash after upload.
# If the hex was uploaded and auto-flashed, we can also trigger via /flash endpoint.
FLASH_RESPONSE=$(curl -s --connect-timeout 30 --max-time 120 "http://$ESP_IP/flash" 2>/dev/null || echo "")

if echo "$FLASH_RESPONSE" | grep -qi "FLASH DONE"; then
    ok "PIC16LF1847 flashed successfully!"
else
    warn "Flash response may not have completed cleanly."
    echo "  Response: $FLASH_RESPONSE"
    echo ""
    echo "  Check the ESP32 serial monitor for detailed output."
    echo "  You can also check the web UI at http://$ESP_IP/i.html"
fi

# ------------------------------------------------------------------
# Post-flash
# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}${BOLD}  POST-FLASH STEPS${NC}"
echo ""
echo "  1. REMOVE the 8-9V VPP supply from MCLR"
echo "  2. Disconnect all ICSP wires from the Dyson BMS"
echo "  3. Reassemble the battery pack"
echo ""
echo "  To verify the new firmware:"
echo "    - Press the trigger: should see Red-Green-Blue flash sequence"
echo "    - Connect charger: yellow flashes = cell balance indicator"
echo "    - Hold trigger + connect charger: white flashes = firmware version"
echo ""

# ------------------------------------------------------------------
# Read back config (optional)
# ------------------------------------------------------------------
read -rp "  Read back PIC config to verify? [y/N]: " readback
if [[ "$readback" =~ ^[Yy]$ ]]; then
    echo ""
    info "Reading PIC config..."
    CONFIG_RESPONSE=$(curl -s --connect-timeout 10 --max-time 30 "http://$ESP_IP/readconfigs" 2>/dev/null || echo "Failed to read config")
    echo ""
    echo "  Config dump:"
    echo "$CONFIG_RESPONSE" | sed 's/<br[^>]*>/\n/g; s/<[^>]*>//g' | head -20
    echo ""
fi

# ------------------------------------------------------------------
# Done
# ------------------------------------------------------------------
echo ""
echo "=========================================="
echo -e "  ${GREEN}Dyson BMS flash process complete!${NC}"
echo ""
echo "  Firmware: FW-Dyson-BMS by tinfever"
echo "  Target:   PIC16LF1847 ($MODEL)"
echo ""
echo "  For troubleshooting, see:"
echo "    https://github.com/tinfever/FW-Dyson-BMS"
echo "=========================================="
echo ""
