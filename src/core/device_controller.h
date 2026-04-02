#pragma once

#include <stdint.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>

#include "ble/ble_provisioner.h"
#include "core/config_store.h"
#include "core/display_engine.h"
#include "core/layout_engine.h"
#include "core/mqtt_client.h"
#include "core/network_manager.h"
namespace core {

class DeviceController final {
 public:
  struct Dependencies {
    ConfigStore *configStore;
    NetworkManager *networkManager;
    MqttClient *mqttClient;
    DisplayEngine *displayEngine;
    LayoutEngine *layoutEngine;
  };

  enum class RenderMode : uint8_t {
    kNone,
    kFull,
    kEtaOnly,
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
  ble::BleProvisioner bleProvisioner_;
  char pendingProvisionToken_[48];
  char pendingProvisionServerUrl_[128];
  volatile bool bleScanPending_;
  uint32_t bootCount_;
  uint32_t lastBreadcrumbPersistAtMs_;
  uint32_t lastHeartbeatAtMs_;
  uint32_t lastTelemetryAtMs_;
  uint32_t lastDeviceLogHeartbeatAtMs_;
  uint32_t lastLowMemoryWarningAtMs_;
  uint32_t lastRenderAtMs_;
  uint32_t lastWifiDisconnectAtMs_;
  uint32_t lastMqttDisconnectAtMs_;
  bool renderDirty_;
  bool lastMqttConnected_;
  bool bootLogPublished_;
  bool pendingCrashReport_;
  bool pendingWifiConnectedLog_;
  bool pendingWifiDisconnectLog_;
  bool pendingMqttDisconnectLog_;
  RenderMode pendingRenderMode_;
  uint8_t etaDirtyRowMask_;
  char pendingCrashReportMetadata_[256];
  static DeviceController *activeController_;

  static void on_network_state_change(NetworkState state, void *ctx);
  static void on_mqtt_command(const char *topic, const uint8_t *payload, size_t len, void *ctx);

  void handle_network_state(NetworkState state);
  void handle_command(const char *topic, const uint8_t *payload, size_t len);
  void handle_disconnect_wifi_command(const String &message);
  bool perform_ota_update(const String& url);
  void setup_http_routes();
  static void http_connect_handler();
  static void http_device_info_handler();
  static void http_heartbeat_handler();
  static void http_status_handler();
  void schedule_full_render();
  void schedule_eta_render(uint8_t rowMask);
  void schedule_no_render();
  void update_ui_state();
  void render_frame(uint32_t nowMs);
  void render_eta_updates();
  bool publish_device_log(const char *status,
                          const char *eventType,
                          const char *message,
                          const char *metadataJson = nullptr);
  void persist_runtime_breadcrumbs(uint32_t nowMs, bool force = false);
  void publish_display_state();
  bool call_provision_api(const char *serverUrl, const char *deviceId, const char *token);
};

}  // namespace core
