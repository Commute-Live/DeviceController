#!/bin/bash
set -euo pipefail

echo "=== LED Simulator — Linux Setup ==="

# Detect package manager
if command -v apt-get &>/dev/null; then
  echo "Installing dependencies via apt..."
  sudo apt-get update
  sudo apt-get install -y build-essential libsdl2-dev libmosquitto-dev pkg-config
elif command -v dnf &>/dev/null; then
  echo "Installing dependencies via dnf..."
  sudo dnf install -y gcc-c++ SDL2-devel mosquitto-devel pkg-config
elif command -v pacman &>/dev/null; then
  echo "Installing dependencies via pacman..."
  sudo pacman -S --noconfirm sdl2 mosquitto pkg-config base-devel
else
  echo "Unsupported package manager. Install manually: SDL2, libmosquitto, pkg-config, g++"
  exit 1
fi

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
make -C "$SCRIPT_DIR" CXX=g++ clean all

echo ""
echo "=== Setup complete ==="
echo "Run the simulator:  ./tools/led_simulator"
echo "Keys: 1-5 = preset, R = cycle rows, S = cycle scenario, Q = quit"
