#pragma once

#include <stdint.h>
#include <WebServer.h>

#include "core/config_store.h"
#include "core/display_engine.h"
#include "core/layout_engine.h"
#include "core/mqtt_client.h"
#include "core/network_manager.h"
#include "core/transit_provider_registry.h"

namespace core {

class DeviceController final {
 public:
  struct Dependencies {
    ConfigStore *configStore;
    NetworkManager *networkManager;
    MqttClient *mqttClient;
    DisplayEngine *displayEngine;
    LayoutEngine *layoutEngine;
    TransitProviderRegistry *providerRegistry;
  };

  explicit DeviceController(const Dependencies &deps);

  bool begin();
  void tick(uint32_t nowMs);

 private:
  Dependencies deps_;
  DeviceRuntimeConfig runtimeConfig_;
  RenderModel renderModel_;
  DrawList drawList_;
  WebServer server_;
  uint32_t lastHeartbeatAtMs_;
  uint32_t lastTelemetryAtMs_;
  uint32_t lastRenderAtMs_;
  bool renderDirty_;
  static DeviceController *activeController_;

  static void on_network_state_change(NetworkState state, void *ctx);
  static void on_mqtt_command(const char *topic, const uint8_t *payload, size_t len, void *ctx);

  void handle_network_state(NetworkState state);
  void handle_command(const char *topic, const uint8_t *payload, size_t len);
  void setup_http_routes();
  static void http_connect_handler();
  static void http_device_info_handler();
  static void http_heartbeat_handler();
  void update_ui_state();
  void render_frame(uint32_t nowMs);
  void publish_display_state();
};

}  // namespace core
