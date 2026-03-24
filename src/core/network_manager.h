#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <stdint.h>

#include "core/models.h"

namespace core {

enum class NetworkState : uint8_t {
  kDisconnected,
  kConnecting,
  kConnected,
  kApMode,
};

struct NetworkConfig {
  char ssid[64];
  char password[64];
  char username[64];
  char apSsid[64];
  char apPassword[64];
};

class NetworkManager final {
 public:
  using StateCallback = void (*)(NetworkState state, void *ctx);

  NetworkManager();

  bool begin(const NetworkConfig &config);
  void tick(uint32_t nowMs);
  void request_reconnect();
  void clear_credentials_and_enter_setup_mode();
  void set_credentials(const char *ssid, const char *password, const char *username);
  void set_state_callback(StateCallback callback, void *ctx);

  NetworkState state() const;
  bool is_connected() const;
  bool setup_mode_active() const;

 private:
  NetworkConfig config_;
  NetworkState state_;
  bool hasSavedCredentials_;
  bool recoveryApEnabled_;
  bool wifiEventRegistered_;
  WiFiEventId_t wifiEventId_;
  String savedSsid_;
  String savedPassword_;
  String savedUsername_;

  uint32_t nextRetryAtMs_;
  uint32_t connectingStartMs_;
  uint32_t lastDisconnectAtMs_;
  uint8_t retryCount_;
  uint8_t lastDisconnectReason_;
  bool lastDisconnectReasonValid_;
  StateCallback callback_;
  void *callbackCtx_;

  static constexpr uint32_t kConnectTimeoutMs = 15000;

  void transition_to(NetworkState next, const char *reason);
  bool connect_station_now();
  void enable_recovery_ap(const char *reason);
  void clear_last_disconnect_reason();
  void handle_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info);
  static void on_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info);
  static NetworkManager *activeInstance_;
};

}  // namespace core
