#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENV_FILE="$SCRIPT_DIR/../.env.simulator"
PORT="${1:-8090}"

# Read .env.simulator values
MQTT_HOST="localhost"
MQTT_WS_PORT="9001"
MQTT_USERNAME=""
MQTT_PASSWORD=""
DEVICE_ID=""

if [ -f "$ENV_FILE" ]; then
  while IFS='=' read -r key value; do
    [[ "$key" =~ ^#.*$ || -z "$key" ]] && continue
    value="${value%\"}"
    value="${value#\"}"
    case "$key" in
      MQTT_HOST)     MQTT_HOST="$value" ;;
      MQTT_WS_PORT)  MQTT_WS_PORT="$value" ;;
      MQTT_USERNAME) MQTT_USERNAME="$value" ;;
      MQTT_PASSWORD) MQTT_PASSWORD="$value" ;;
      DEVICE_ID)     DEVICE_ID="$value" ;;
    esac
  done < "$ENV_FILE"
  echo "Loaded .env.simulator (device: $DEVICE_ID)"
else
  echo "No .env.simulator found, using defaults"
fi

# Generate a config.js that the HTML will load
cat > "$SCRIPT_DIR/config.js" <<JSEOF
// Auto-generated from .env.simulator — do not edit
window.SIM_CONFIG = {
  broker: "ws://${MQTT_HOST}:${MQTT_WS_PORT}",
  username: "${MQTT_USERNAME}",
  password: "${MQTT_PASSWORD}",
  deviceId: "${DEVICE_ID}"
};
JSEOF

echo "Serving at http://localhost:$PORT"
cd "$SCRIPT_DIR"
python3 -m http.server "$PORT"
