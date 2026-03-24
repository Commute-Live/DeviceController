#include "core/debug_log.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <stdarg.h>
#include <stdio.h>

#include "core/models.h"
#include "core/network_manager.h"

namespace core {
namespace debug {

namespace {

constexpr size_t kLogBufferLen = 256;

}  // namespace

void logf(const char *tag, const char *fmt, ...) {
  if (!fmt) {
    return;
  }

  char message[kLogBufferLen];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  Serial.printf("[%s] %s\n", tag ? tag : "LOG", message);
}

const char *network_state_name(NetworkState state) {
  switch (state) {
    case NetworkState::kDisconnected:
      return "DISCONNECTED";
    case NetworkState::kConnecting:
      return "CONNECTING";
    case NetworkState::kConnected:
      return "CONNECTED";
    case NetworkState::kApMode:
      return "AP_MODE";
  }
  return "UNKNOWN";
}

const char *ui_state_name(UiState state) {
  switch (state) {
    case UiState::kBooting:
      return "BOOTING";
    case UiState::kSetupMode:
      return "SETUP_MODE";
    case UiState::kNoWifi:
      return "NO_WIFI";
    case UiState::kWifiOkNoMqtt:
      return "WIFI_OK_NO_MQTT";
    case UiState::kConnectedWaitingData:
      return "CONNECTED_WAITING_DATA";
    case UiState::kTransit:
      return "TRANSIT";
  }
  return "UNKNOWN";
}

const char *mqtt_state_name(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT:
      return "CONNECTION_TIMEOUT";
    case MQTT_CONNECTION_LOST:
      return "CONNECTION_LOST";
    case MQTT_CONNECT_FAILED:
      return "CONNECT_FAILED";
    case MQTT_DISCONNECTED:
      return "DISCONNECTED";
    case MQTT_CONNECTED:
      return "CONNECTED";
    case MQTT_CONNECT_BAD_PROTOCOL:
      return "BAD_PROTOCOL";
    case MQTT_CONNECT_BAD_CLIENT_ID:
      return "BAD_CLIENT_ID";
    case MQTT_CONNECT_UNAVAILABLE:
      return "UNAVAILABLE";
    case MQTT_CONNECT_BAD_CREDENTIALS:
      return "BAD_CREDENTIALS";
    case MQTT_CONNECT_UNAUTHORIZED:
      return "UNAUTHORIZED";
    default:
      return "UNKNOWN";
  }
}

uint8_t normalize_wifi_disconnect_reason(uint8_t reason) {
  return reason == 0 ? WIFI_REASON_UNSPECIFIED : reason;
}

const char *wifi_disconnect_reason_name(uint8_t reason) {
  const uint8_t normalized = normalize_wifi_disconnect_reason(reason);
  return WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(normalized));
}

}  // namespace debug
}  // namespace core
