#include <Arduino.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

#include "core/config_store.h"
#include "core/display_calibration.h"
#include "core/device_controller.h"
#include "core/display_engine.h"
#include "core/layout_engine.h"
#include "core/logging.h"
#include "core/mqtt_client.h"
#include "core/network_manager.h"
#include "network/wifi_manager.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "Using include/secrets.example.h defaults. Create include/secrets.h for real credentials."
#endif

#ifndef COMMUTELIVE_ENABLE_DISPLAY_CALIBRATION
#define COMMUTELIVE_ENABLE_DISPLAY_CALIBRATION 0
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
core::DeviceController::Dependencies gDeps{
    &gConfigStore, &gNetworkManager, &gMqttClient, &gDisplayEngine, &gLayoutEngine};
core::DeviceController gController(gDeps);

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  randomSeed(esp_random());
  DCTRL_LOGI("BOOT", "Serial ready baud=115200 freeHeap=%lu sdk=%s",
             static_cast<unsigned long>(ESP.getFreeHeap()),
             ESP.getSdkVersion());

  core::DeviceRuntimeConfig cfg{};
  cfg.schemaVersion = 1;
  build_device_id(cfg.deviceId, sizeof(cfg.deviceId));

  cfg.display.panelRows = 1;
  cfg.display.panelCols = 2;
  cfg.display.panelWidth = 64;
  cfg.display.panelHeight = 32;
  cfg.display.brightness = 32;
  cfg.display.serpentine = false;
  cfg.display.doubleBuffered = false;
  cfg.display.chainMode = 0;
  cfg.display.xOffset = 0;
  cfg.display.yOffset = 0;

  // WiFi credentials are not set here — provisioned at runtime via BLE.
  cfg.network.ssid[0]     = '\0';
  cfg.network.password[0] = '\0';
  cfg.network.username[0] = '\0';

  // Per-device unique AP: SSID derived from chip MAC, password generated once at first boot.
  char generatedApSsid[64];
  wifi_manager::build_ap_ssid(generatedApSsid, sizeof(generatedApSsid));
  copy_str(cfg.network.apSsid, generatedApSsid);

  const String generatedApPass = wifi_manager::generate_or_load_ap_password();
  copy_str(cfg.network.apPassword, generatedApPass.c_str());

  copy_str(cfg.mqtt.host, COMMUTELIVE_MQTT_HOST);
  cfg.mqtt.port = static_cast<uint16_t>(COMMUTELIVE_MQTT_PORT);
  copy_str(cfg.mqtt.username, COMMUTELIVE_MQTT_USER);
  copy_str(cfg.mqtt.password, COMMUTELIVE_MQTT_PASS);
  copy_str(cfg.mqtt.clientId, cfg.deviceId);
  DCTRL_LOGI("BOOT", "Bootstrap config deviceId=%s apSsid=%s apPassword=%s mqttHost=%s mqttPort=%u",
             cfg.deviceId,
             cfg.network.apSsid,
             cfg.network.apPassword,
             cfg.mqtt.host,
             static_cast<unsigned>(cfg.mqtt.port));

#if COMMUTELIVE_ENABLE_DISPLAY_CALIBRATION
  core::calibration::maybe_run(gDisplayEngine, gConfigStore, cfg, 5000);
#endif

  gConfigStore.set_bootstrap_config(cfg);

  if (!gController.begin()) {
    DCTRL_LOGE("BOOT", "Controller init failed deviceId=%s", cfg.deviceId);
    return;
  }

  DCTRL_LOGI("BOOT", "Controller init ok deviceId=%s", cfg.deviceId);
}

void loop() {
  gController.tick(millis());
  delay(50);
}
