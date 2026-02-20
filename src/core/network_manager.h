#pragma once

#include <Arduino.h>
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
  String savedSsid_;
  String savedPassword_;
  String savedUsername_;

  uint32_t nextRetryAtMs_;
  uint8_t retryCount_;
  StateCallback callback_;
  void *callbackCtx_;

  void transition_to(NetworkState next);
  bool connect_station_now();
  void enable_recovery_ap();
};

}  // namespace core
