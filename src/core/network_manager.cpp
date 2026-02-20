#include "core/network_manager.h"

#include <string.h>

namespace core {

namespace {
constexpr uint32_t kRetryBaseMs = 1000;
constexpr uint32_t kRetryMaxMs = 60000;
}  // namespace

NetworkManager::NetworkManager()
    : config_{},
      state_(NetworkState::kDisconnected),
      nextRetryAtMs_(0),
      retryCount_(0),
      callback_(nullptr),
      callbackCtx_(nullptr) {}

bool NetworkManager::begin(const NetworkConfig &config) {
  config_ = config;
  retryCount_ = 0;
  nextRetryAtMs_ = 0;
  transition_to(NetworkState::kConnecting);
  return true;
}

void NetworkManager::tick(uint32_t nowMs) {
  if (state_ == NetworkState::kConnected) {
    return;
  }
  if (nowMs < nextRetryAtMs_) {
    return;
  }

  if (retryCount_ >= 3) {
    transition_to(NetworkState::kApMode);
    return;
  }

  retryCount_++;
  transition_to(NetworkState::kConnecting);
  const uint32_t backoff = kRetryBaseMs << (retryCount_ > 5 ? 5 : retryCount_);
  nextRetryAtMs_ = nowMs + (backoff > kRetryMaxMs ? kRetryMaxMs : backoff);
}

void NetworkManager::request_reconnect() {
  retryCount_ = 0;
  nextRetryAtMs_ = 0;
  transition_to(NetworkState::kConnecting);
}

void NetworkManager::set_state_callback(StateCallback callback, void *ctx) {
  callback_ = callback;
  callbackCtx_ = ctx;
}

NetworkState NetworkManager::state() const { return state_; }

bool NetworkManager::is_connected() const { return state_ == NetworkState::kConnected; }

void NetworkManager::transition_to(NetworkState next) {
  if (state_ == next) {
    return;
  }
  state_ = next;
  if (callback_) {
    callback_(state_, callbackCtx_);
  }
}

}  // namespace core
