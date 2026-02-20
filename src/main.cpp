#include <Arduino.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

#include "core/config_store.h"
#include "core/device_controller.h"
#include "core/display_engine.h"
#include "core/layout_engine.h"
#include "core/mqtt_client.h"
#include "core/network_manager.h"
#include "core/transit_provider_registry.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "Using include/secrets.example.h defaults. Create include/secrets.h for real credentials."
#endif

#ifndef COMMUTELIVE_WIFI_SSID
#define COMMUTELIVE_WIFI_SSID COMMUTELIVE_AP_SSID
#endif

#ifndef COMMUTELIVE_WIFI_PASSWORD
#define COMMUTELIVE_WIFI_PASSWORD COMMUTELIVE_AP_PASSWORD
#endif

namespace {

template <size_t N>
void copy_str(char (&dst)[N], const char *src) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, N - 1);
  dst[N - 1] = '\0';
}

void build_device_id(char *out, size_t outLen) {
  const uint64_t chipid = ESP.getEfuseMac();
  snprintf(out, outLen, "esp32-%04X%08X", static_cast<uint16_t>(chipid >> 32), static_cast<uint32_t>(chipid));
}

core::ConfigStore gConfigStore;
core::NetworkManager gNetworkManager;
core::MqttClient gMqttClient;
core::DisplayEngine gDisplayEngine;
core::LayoutEngine gLayoutEngine;
core::TransitProviderRegistry gProviderRegistry;
core::DeviceController::Dependencies gDeps{
    &gConfigStore, &gNetworkManager, &gMqttClient, &gDisplayEngine, &gLayoutEngine, &gProviderRegistry};
core::DeviceController gController(gDeps);

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  randomSeed(esp_random());

  core::DeviceRuntimeConfig cfg{};
  cfg.schemaVersion = 1;
  build_device_id(cfg.deviceId, sizeof(cfg.deviceId));

  cfg.display.panelRows = 1;
  cfg.display.panelCols = 2;
  cfg.display.panelWidth = 64;
  cfg.display.panelHeight = 32;
  cfg.display.brightness = 80;
  cfg.display.serpentine = false;
  cfg.display.doubleBuffered = true;

  copy_str(cfg.network.ssid, COMMUTELIVE_WIFI_SSID);
  copy_str(cfg.network.password, COMMUTELIVE_WIFI_PASSWORD);
  copy_str(cfg.network.username, "");
  copy_str(cfg.network.apSsid, COMMUTELIVE_AP_SSID);
  copy_str(cfg.network.apPassword, COMMUTELIVE_AP_PASSWORD);

  copy_str(cfg.mqtt.host, COMMUTELIVE_MQTT_HOST);
  cfg.mqtt.port = static_cast<uint16_t>(COMMUTELIVE_MQTT_PORT);
  copy_str(cfg.mqtt.username, COMMUTELIVE_MQTT_USER);
  copy_str(cfg.mqtt.password, COMMUTELIVE_MQTT_PASS);
  copy_str(cfg.mqtt.clientId, cfg.deviceId);

  gConfigStore.set_bootstrap_config(cfg);

  if (!gController.begin()) {
    Serial.println("[CORE] Controller init failed");
    return;
  }

  Serial.printf("[CORE] Controller init ok: %s\n", cfg.deviceId);
}

void loop() {
  gController.tick(millis());
  delay(50);
}
