#!/bin/bash
set -euo pipefail

echo "=== LED Simulator — macOS Setup ==="

# Check for Homebrew
if ! command -v brew &>/dev/null; then
  echo "Homebrew not found. Installing..."
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
else
  echo "Homebrew found."
fi

# Install dependencies
echo "Installing SDL2 and Mosquitto..."
brew install sdl2 mosquitto pkg-config

# Create .env.simulator from example if it doesn't exist
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ ! -f "$SCRIPT_DIR/.env.simulator" ]; then
  cp "$SCRIPT_DIR/.env.simulator.example" "$SCRIPT_DIR/.env.simulator"
  echo "Created .env.simulator from example — edit it with your MQTT credentials."
else
  echo ".env.simulator already exists, skipping."
fi

# Build
echo "Building simulator..."
make -C "$SCRIPT_DIR" clean all

echo ""
echo "=== Setup complete ==="
echo "Run the simulator:  ./tools/led_simulator"
echo "Keys: 1-5 = preset, R = cycle rows, S = cycle scenario, Q = quit"
