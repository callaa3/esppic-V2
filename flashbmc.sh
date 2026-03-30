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
read -rp "  Wiring verified and battery is awake? [y/N/d=diag]: " wired
if [[ "$wired" =~ ^[Dd]$ ]]; then
    echo ""
    info "Running wiring diagnostics on ESP32..."
    echo "  (Make sure 8-9V is connected if using auto-VPP, or connected to MCLR for manual)"
    echo ""
    DIAG_RESPONSE=$(curl -s --connect-timeout 10 --max-time 15 "http://$ESP_IP/diag" 2>/dev/null || echo "Failed to reach ESP32")
    echo "$DIAG_RESPONSE"
    echo ""
    read -rp "  Continue to flash? [y/N]: " wired
fi
if [[ ! "$wired" =~ ^[Yy]$ ]]; then
    echo "  Aborted. Wire up your connections and try again."
    exit 0
fi
echo ""

# ------------------------------------------------------------------
# VPP mode selection
# ------------------------------------------------------------------
echo -e "${YELLOW}${BOLD}  VPP MODE${NC}"
echo ""
echo "  How is your 8-9V VPP supply connected?"
echo ""
echo "    [1] AUTO-VPP — GPIO 14 transistor circuit switches 8-9V automatically"
echo "    [2] MANUAL VPP — I will connect 8-9V to MCLR by hand when prompted"
echo ""
read -rp "  Select [1/2]: " vpp_choice

case "$vpp_choice" in
    1) VPP_MODE="auto" ;;
    2) VPP_MODE="manual" ;;
    *) VPP_MODE="manual"; warn "Defaulting to manual VPP mode." ;;
esac

ok "VPP mode: $VPP_MODE"
echo ""

# For manual VPP, set the flag on ESP32 BEFORE upload to prevent auto-flash
if [[ "$VPP_MODE" == "manual" ]]; then
    info "Setting manual VPP mode on ESP32..."
    PREP_EARLY=$(curl -s --connect-timeout 10 "http://$ESP_IP/prep_hvp" 2>/dev/null || echo "FAIL")
    if [[ "$PREP_EARLY" != "OK" ]]; then
        fail "Failed to set manual VPP mode. Is the ESP32 firmware up to date? Re-run ./flashesp.sh first."
    fi
    ok "Manual VPP mode set — auto-flash after upload disabled."
    echo ""
fi

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
if [[ "$VPP_MODE" == "manual" ]]; then
    echo -e "${YELLOW}${BOLD}  MANUAL VPP — CONNECT 8-9V${NC}"
    echo ""
    echo "  The ESP32 has PGD/PGC held low (ready for HVP entry)."
    echo ""
    echo -e "  ${RED}${BOLD}>>> NOW CONNECT 8-9V TO MCLR/VPP <<<${NC}"
    echo ""
    echo "  Connect your 8-9V supply to the MCLR/VPP pin now."
    echo "  The PIC will enter HVP programming mode when voltage is applied."
    echo ""
    read -rp "  8-9V connected? Press Enter to start flashing... "
    echo ""

else
    echo -e "${YELLOW}${BOLD}  VPP CONNECTION TIMING${NC}"
    echo ""
    echo "  The ESP32 will handle VPP automatically via the GPIO 14 circuit."
    echo ""
    read -rp "  Ready to flash? Press Enter to begin... "
    echo ""
fi

# ------------------------------------------------------------------
# Trigger flash
# ------------------------------------------------------------------
info "Triggering PIC flash via ESP32..."
echo -ne "  Flashing: "

# Run curl in background so we can show a spinner
FLASH_TMPFILE=$(mktemp)
curl -s --connect-timeout 30 --max-time 120 "http://$ESP_IP/flash" > "$FLASH_TMPFILE" 2>/dev/null &
CURL_PID=$!

# Spinner with elapsed time
SPIN_CHARS='|/-\'
SECONDS=0
i=0
while kill -0 "$CURL_PID" 2>/dev/null; do
    printf "\r  Flashing: ${SPIN_CHARS:i%4:1} %ds elapsed " "$SECONDS"
    i=$((i+1))
    sleep 0.25
done
wait "$CURL_PID"
printf "\r  Flashing: done (%ds)          \n" "$SECONDS"

FLASH_RESPONSE=$(cat "$FLASH_TMPFILE")
rm -f "$FLASH_TMPFILE"

if echo "$FLASH_RESPONSE" | grep -qi "FLASH DONE"; then
    ok "PIC16LF1847 flashed successfully!"
    FLASH_OK=true
elif echo "$FLASH_RESPONSE" | grep -qi "FLASH FAILED\|not responding\|Check wiring"; then
    echo ""
    fail "PIC did not respond — it never entered programming mode!

  The ESP32 could not communicate with the PIC. This means:
    - The NPN transistor on GPIO 27 may not be wired correctly
      (2N3904 pinout flat side facing you: Emitter-Base-Collector, left to right)
    - The 8-9V supply may not be reaching MCLR/VPP pin
    - The ICSP data/clock wires may be on wrong pins
    - The battery may not be awake (press trigger first for V6)

  Check your connections and try again."
else
    warn "Flash response may not have completed cleanly."
    echo "  Response: $FLASH_RESPONSE"
    echo ""
    echo "  Check the ESP32 serial monitor for detailed output."
    echo "  You can also check the web UI at http://$ESP_IP/i.html"
    FLASH_OK=false
fi

# ------------------------------------------------------------------
# Read back config (optional, BEFORE disconnecting VPP)
# ------------------------------------------------------------------
if [[ "$FLASH_OK" == "true" && "$VPP_MODE" == "manual" ]]; then
    echo ""
    echo -e "  ${YELLOW}Keep 8-9V connected for now — you can verify the flash first.${NC}"
    echo ""
    read -rp "  Read back PIC config to verify? [y/N]: " readback
    if [[ "$readback" =~ ^[Yy]$ ]]; then
        info "Prepping manual HVP for config read..."
        curl -s --connect-timeout 10 "http://$ESP_IP/prep_hvp" >/dev/null 2>&1
        sleep 0.5
        info "Reading PIC config..."
        CONFIG_RESPONSE=$(curl -s --connect-timeout 10 --max-time 30 "http://$ESP_IP/readconfigs" 2>/dev/null || echo "Failed to read config")
        echo ""
        echo "  Config dump:"
        CONFIG_TEXT=$(echo "$CONFIG_RESPONSE" | sed 's/<br[^>]*>/\n/g; s/<[^>]*>//g' | head -20)
        echo "$CONFIG_TEXT"
        echo ""
        if echo "$CONFIG_TEXT" | grep -q "3fff.*3fff.*3fff.*3fff"; then
            warn "All values are 0x3FFF — PIC may not have been programmed."
            echo "  This usually means HVP entry failed. Check transistor wiring."
        fi
    fi
    echo ""
    echo -e "  ${RED}${BOLD}Now REMOVE the 8-9V VPP supply from MCLR.${NC}"
    read -rp "  VPP removed? Press Enter to continue... "
elif [[ "$FLASH_OK" == "true" ]]; then
    echo ""
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
fi

# ------------------------------------------------------------------
# Post-flash
# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}${BOLD}  POST-FLASH STEPS${NC}"
echo ""
echo "  1. Disconnect all ICSP wires from the Dyson BMS"
echo "  2. Reassemble the battery pack"
echo ""
echo "  To verify the new firmware:"
echo "    - Press the trigger: should see Red-Green-Blue flash sequence"
echo "    - Connect charger: yellow flashes = cell balance indicator"
echo "    - Hold trigger + connect charger: white flashes = firmware version"
echo ""

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
