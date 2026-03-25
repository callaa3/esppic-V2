#!/bin/bash
#
# install.sh — Install toolchain for ESPPIC-V2
# Installs Arduino CLI, ESP32 board support, and required libraries
# Designed for Windows Git Bash
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="$SCRIPT_DIR/tools"
ARDUINO_CLI="$INSTALL_DIR/arduino-cli"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $1"; }
ok()    { echo -e "${GREEN}[OK]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
fail()  { echo -e "${RED}[FAIL]${NC} $1"; exit 1; }

echo ""
echo "=========================================="
echo "  ESPPIC-V2 Toolchain Installer"
echo "  ESP32 + PIC16LF1847 Programmer"
echo "=========================================="
echo ""

# ------------------------------------------------------------------
# Check for required system tools
# ------------------------------------------------------------------
info "Checking prerequisites..."

if ! command -v curl &>/dev/null; then
    fail "curl not found. Please install curl or use Git Bash (includes curl)."
fi

if ! command -v unzip &>/dev/null; then
    fail "unzip not found. Please install unzip."
fi

ok "Prerequisites found (curl, unzip)"

# ------------------------------------------------------------------
# Install Arduino CLI
# ------------------------------------------------------------------
mkdir -p "$INSTALL_DIR"

if [[ -f "$ARDUINO_CLI" || -f "$ARDUINO_CLI.exe" ]]; then
    ok "Arduino CLI already installed at $INSTALL_DIR"
else
    info "Downloading Arduino CLI..."

    ARCH="Windows_64bit"
    CLI_URL="https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_${ARCH}.zip"

    TMPZIP="$INSTALL_DIR/arduino-cli.zip"
    curl -fSL "$CLI_URL" -o "$TMPZIP" || fail "Failed to download Arduino CLI"
    unzip -o "$TMPZIP" -d "$INSTALL_DIR" || fail "Failed to extract Arduino CLI"
    rm -f "$TMPZIP"

    ok "Arduino CLI installed to $INSTALL_DIR"
fi

# Normalize path to CLI (handle .exe on Windows)
if [[ -f "$ARDUINO_CLI.exe" ]]; then
    ARDUINO_CLI="$ARDUINO_CLI.exe"
fi

"$ARDUINO_CLI" version

# ------------------------------------------------------------------
# Configure Arduino CLI
# ------------------------------------------------------------------
info "Configuring Arduino CLI..."
"$ARDUINO_CLI" config init --overwrite 2>/dev/null || true
"$ARDUINO_CLI" config set board_manager.additional_urls \
    "https://espressif.github.io/arduino-esp32/package_esp32_index.json"
ok "Board manager URL configured"

# ------------------------------------------------------------------
# Install ESP32 board support
# ------------------------------------------------------------------
info "Updating board index (this may take a moment)..."
"$ARDUINO_CLI" core update-index

if "$ARDUINO_CLI" core list 2>/dev/null | grep -q "esp32:esp32"; then
    ok "ESP32 board support already installed"
else
    info "Installing ESP32 board support..."
    "$ARDUINO_CLI" core install esp32:esp32
    ok "ESP32 board support installed"
fi

# ------------------------------------------------------------------
# Install required libraries
# ------------------------------------------------------------------
info "Installing required libraries..."

install_lib() {
    local lib_name="$1"
    if "$ARDUINO_CLI" lib list 2>/dev/null | grep -qi "$lib_name"; then
        ok "Library '$lib_name' already installed"
    else
        info "Installing library: $lib_name"
        "$ARDUINO_CLI" lib install "$lib_name" || warn "Could not install '$lib_name' — you may need to install it manually"
    fi
}

install_lib "WebSockets"

ok "All libraries installed"

# ------------------------------------------------------------------
# Verify ESP32 FQBN
# ------------------------------------------------------------------
info "Verifying ESP32 board availability..."
if "$ARDUINO_CLI" board listall 2>/dev/null | grep -q "esp32:esp32:esp32"; then
    ok "ESP32 board (esp32:esp32:esp32) is available"
else
    warn "ESP32 board FQBN not found. Try: $ARDUINO_CLI core install esp32:esp32"
fi

# ------------------------------------------------------------------
# Check USB drivers
# ------------------------------------------------------------------
echo ""
info "Checking for common ESP32 USB-serial drivers..."
echo ""
echo "  If your ESP32 isn't detected when plugged in, install one of:"
echo "    - CP210x: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers"
echo "    - CH340:  http://www.wch-ic.com/downloads/CH341SER_ZIP.html"
echo ""

# ------------------------------------------------------------------
# Done
# ------------------------------------------------------------------
echo ""
echo "=========================================="
echo -e "  ${GREEN}Installation complete!${NC}"
echo ""
echo "  Next steps:"
echo "    1. Plug in your ESP32 DevKit via USB"
echo "    2. Run: ./flashesp.sh"
echo "=========================================="
echo ""
