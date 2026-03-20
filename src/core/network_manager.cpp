#include "core/network_manager.h"

#include <WiFi.h>

#include "network/wifi_manager.h"

namespace core {

namespace {
constexpr uint32_t kRetryBaseMs = 1000;
constexpr uint32_t kRetryMaxMs = 60000;
constexpr uint32_t kRetryJitterMs = 750;
constexpr uint8_t kRecoveryApAttemptThreshold = 3;

uint32_t bounded_backoff(uint8_t attempt) {
  uint32_t waitMs = kRetryBaseMs;
  const uint8_t capped = attempt > 8 ? 8 : attempt;
  for (uint8_t i = 0; i < capped; ++i) {
    if (waitMs >= kRetryMaxMs / 2U) {
      waitMs = kRetryMaxMs;
      break;
    }
    waitMs *= 2U;
  }
  const uint32_t jitter = random(kRetryJitterMs + 1);
  if (waitMs > kRetryMaxMs - jitter) {
    return kRetryMaxMs;
  }
  return waitMs + jitter;
}

bool has_explicit_bootstrap_wifi(const NetworkConfig &config) {
  if (config.ssid[0] == '\0') {
    return false;
  }
  if (config.username[0] != '\0') {
    return true;
  }
  return strcmp(config.ssid, config.apSsid) != 0 || strcmp(config.password, config.apPassword) != 0;
}

}  // namespace

NetworkManager::NetworkManager()
    : config_{},
      state_(NetworkState::kDisconnected),
      hasSavedCredentials_(false),
      recoveryApEnabled_(false),
      savedSsid_(),
      savedPassword_(),
      savedUsername_(),
      nextRetryAtMs_(0),
      connectingStartMs_(0),
      retryCount_(0),
      callback_(nullptr),
      callbackCtx_(nullptr) {}

bool NetworkManager::begin(const NetworkConfig &config) {
  config_ = config;
  retryCount_ = 0;
  nextRetryAtMs_ = 0;
  recoveryApEnabled_ = false;

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  delay(100);

  if (has_explicit_bootstrap_wifi(config_)) {
    savedSsid_ = config_.ssid;
    savedPassword_ = config_.password;
    savedUsername_ = config_.username;
    hasSavedCredentials_ = true;
    wifi_manager::save_credentials(savedSsid_, savedPassword_, savedUsername_);
    Serial.printf("[WIFI] Using configured bootstrap WiFi: %s\n", savedSsid_.c_str());
  } else {
    String loadedSsid;
    String loadedPassword;
    String loadedUser;
    hasSavedCredentials_ = wifi_manager::load_credentials(loadedSsid, loadedPassword, loadedUser);

    if (hasSavedCredentials_) {
      savedSsid_ = loadedSsid;
      savedPassword_ = loadedPassword;
      savedUsername_ = loadedUser;
    }
  }

  transition_to(NetworkState::kConnecting);

  if (connect_station_now()) {
    retryCount_ = 0;
    nextRetryAtMs_ = millis();
    transition_to(NetworkState::kConnected);
    return true;
  }

  enable_recovery_ap();
  return false;
}

void NetworkManager::tick(uint32_t nowMs) {
  if (WiFi.status() == WL_CONNECTED) {
    retryCount_ = 0;
    connectingStartMs_ = 0;
    if (state_ != NetworkState::kConnected) {
      recoveryApEnabled_ = false;
      transition_to(NetworkState::kConnected);
    }
    return;
  }

  // Waiting for an async WiFi.begin() to resolve — check for timeout.
  if (connectingStartMs_ != 0) {
    if (nowMs - connectingStartMs_ < kConnectTimeoutMs) {
      return;
    }
    connectingStartMs_ = 0;
    WiFi.disconnect(false, false);
    transition_to(NetworkState::kDisconnected);
    retryCount_++;
    const uint32_t waitMs = bounded_backoff(retryCount_);
    nextRetryAtMs_ = nowMs + waitMs;
    Serial.printf("[WIFI] Connect timed out, retry in %lu ms (attempt %u)\n",
                  static_cast<unsigned long>(waitMs), retryCount_);
    if (!recoveryApEnabled_ && retryCount_ >= kRecoveryApAttemptThreshold) {
      enable_recovery_ap();
    }
    return;
  }

  if (!hasSavedCredentials_) {
    enable_recovery_ap();
    return;
  }

  if (nowMs < nextRetryAtMs_) {
    return;
  }

  transition_to(NetworkState::kConnecting);
  wifi_manager::begin_station(savedSsid_.c_str(), savedPassword_.c_str(), savedUsername_.c_str());
  connectingStartMs_ = nowMs;
}

void NetworkManager::request_reconnect() {
  connectingStartMs_ = 0;
  retryCount_ = 0;
  nextRetryAtMs_ = 0;
  WiFi.disconnect(false, false);
  transition_to(NetworkState::kConnecting);
}

void NetworkManager::set_credentials(const char *ssid, const char *password, const char *username) {
  savedSsid_ = ssid ? ssid : "";
  savedPassword_ = password ? password : "";
  savedUsername_ = username ? username : "";
  hasSavedCredentials_ = savedSsid_.length() > 0;
  if (hasSavedCredentials_) {
    wifi_manager::save_credentials(savedSsid_, savedPassword_, savedUsername_);
  }
  request_reconnect();
}

void NetworkManager::set_state_callback(StateCallback callback, void *ctx) {
  callback_ = callback;
  callbackCtx_ = ctx;
}

NetworkState NetworkManager::state() const { return state_; }

bool NetworkManager::is_connected() const { return WiFi.status() == WL_CONNECTED; }

bool NetworkManager::setup_mode_active() const {
  return state_ == NetworkState::kApMode || recoveryApEnabled_;
}

void NetworkManager::transition_to(NetworkState next) {
  if (state_ == next) {
    return;
  }
  state_ = next;
  if (callback_) {
    callback_(state_, callbackCtx_);
  }
}

bool NetworkManager::connect_station_now() {
  if (!hasSavedCredentials_) {
    return false;
  }

  const bool connected = wifi_manager::connect_station(savedSsid_.c_str(), savedPassword_.c_str(), savedUsername_.c_str());
  if (!connected) {
    transition_to(NetworkState::kDisconnected);
  }
  return connected;
}

void NetworkManager::enable_recovery_ap() {
  if (recoveryApEnabled_) {
    transition_to(NetworkState::kApMode);
    return;
  }

  const char *apSsid = config_.apSsid[0] ? config_.apSsid : "commutelive-setup";
  const char *apPassword = config_.apPassword[0] ? config_.apPassword : "commutelive-setup";

  if (wifi_manager::start_ap(apSsid, apPassword)) {
    recoveryApEnabled_ = true;
    transition_to(NetworkState::kApMode);
    Serial.println("[WIFI] Recovery AP enabled");
  } else {
    transition_to(NetworkState::kDisconnected);
  }
}

}  // namespace core
