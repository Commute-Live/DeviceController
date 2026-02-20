#pragma once

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
};

class NetworkManager final {
 public:
  using StateCallback = void (*)(NetworkState state, void *ctx);

  NetworkManager();

  bool begin(const NetworkConfig &config);
  void tick(uint32_t nowMs);
  void request_reconnect();
  void set_state_callback(StateCallback callback, void *ctx);

  NetworkState state() const;
  bool is_connected() const;

 private:
  NetworkConfig config_;
  NetworkState state_;
  uint32_t nextRetryAtMs_;
  uint8_t retryCount_;
  StateCallback callback_;
  void *callbackCtx_;

  void transition_to(NetworkState next);
};

}  // namespace core
