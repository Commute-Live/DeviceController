#include "core/network_manager.h"

#include <WiFi.h>

#include "core/debug_log.h"
#include "network/wifi_manager.h"

namespace core {

NetworkManager *NetworkManager::activeInstance_ = nullptr;

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
      wifiEventRegistered_(false),
      wifiEventId_(0),
      savedSsid_(),
      savedPassword_(),
      savedUsername_(),
      nextRetryAtMs_(0),
      connectingStartMs_(0),
      lastDisconnectAtMs_(0),
      retryCount_(0),
      lastDisconnectReason_(0),
      lastDisconnectReasonValid_(false),
      callback_(nullptr),
      callbackCtx_(nullptr) {}

bool NetworkManager::begin(const NetworkConfig &config) {
  config_ = config;
  retryCount_ = 0;
  nextRetryAtMs_ = 0;
  connectingStartMs_ = 0;
  recoveryApEnabled_ = false;
  clear_last_disconnect_reason();

  activeInstance_ = this;
  if (!wifiEventRegistered_) {
    wifiEventId_ = WiFi.onEvent(&NetworkManager::on_wifi_event);
    wifiEventRegistered_ = true;
    debug::logf("WIFI", "Registered Wi-Fi lifecycle event handler");
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  delay(100);

  if (has_explicit_bootstrap_wifi(config_)) {
    savedSsid_ = config_.ssid;
    savedPassword_ = config_.password;
    savedUsername_ = config_.username;
    hasSavedCredentials_ = true;
    wifi_manager::save_credentials(savedSsid_, savedPassword_, savedUsername_);
    debug::logf("WIFI", "Credential source=bootstrap ssid='%s'%s",
                savedSsid_.c_str(),
                savedUsername_.length() > 0 ? " mode=WPA2-Enterprise" : " mode=WPA/WPA2-PSK");
  } else {
    String loadedSsid;
    String loadedPassword;
    String loadedUser;
    hasSavedCredentials_ = wifi_manager::load_credentials(loadedSsid, loadedPassword, loadedUser);

    if (hasSavedCredentials_) {
      savedSsid_ = loadedSsid;
      savedPassword_ = loadedPassword;
      savedUsername_ = loadedUser;
      debug::logf("WIFI", "Credential source=saved ssid='%s'%s",
                  savedSsid_.c_str(),
                  savedUsername_.length() > 0 ? " mode=WPA2-Enterprise" : " mode=WPA/WPA2-PSK");
    } else {
      debug::logf("WIFI", "Credential source=none; device will enter setup AP mode");
    }
  }

  if (!hasSavedCredentials_) {
    enable_recovery_ap("no Wi-Fi credentials available at boot");
    return false;
  }

  transition_to(NetworkState::kConnecting, "startup station connect requested");
  if (connect_station_now()) {
    retryCount_ = 0;
    nextRetryAtMs_ = millis();
    transition_to(NetworkState::kConnected, "blocking startup station connect succeeded");
    return true;
  }

  enable_recovery_ap("blocking startup station connect failed");
  return false;
}

void NetworkManager::tick(uint32_t nowMs) {
  if (WiFi.status() == WL_CONNECTED) {
    retryCount_ = 0;
    connectingStartMs_ = 0;
    if (state_ != NetworkState::kConnected) {
      recoveryApEnabled_ = false;
      transition_to(NetworkState::kConnected, "Wi-Fi station reports connected");
    }
    return;
  }

  if (connectingStartMs_ != 0) {
    if (nowMs - connectingStartMs_ < kConnectTimeoutMs) {
      return;
    }

    const uint32_t elapsedMs = nowMs - connectingStartMs_;
    connectingStartMs_ = 0;
    WiFi.disconnect(false, false);
    transition_to(NetworkState::kDisconnected, "async station connect timed out");
    if (lastDisconnectReasonValid_) {
      debug::logf("WIFI", "Async station connect timed out after %lu ms; lastReason=%u (%s)",
                  static_cast<unsigned long>(elapsedMs),
                  static_cast<unsigned>(lastDisconnectReason_),
                  debug::wifi_disconnect_reason_name(lastDisconnectReason_));
    } else {
      debug::logf("WIFI", "Async station connect timed out after %lu ms with no disconnect reason",
                  static_cast<unsigned long>(elapsedMs));
    }

    retryCount_++;
    const uint32_t waitMs = bounded_backoff(retryCount_);
    nextRetryAtMs_ = nowMs + waitMs;
    debug::logf("WIFI", "Scheduling retry %u in %lu ms",
                static_cast<unsigned>(retryCount_),
                static_cast<unsigned long>(waitMs));
    if (!recoveryApEnabled_ && retryCount_ >= kRecoveryApAttemptThreshold) {
      enable_recovery_ap("retry threshold reached after async station connect timeouts");
    }
    return;
  }

  if (!hasSavedCredentials_) {
    enable_recovery_ap("no saved Wi-Fi credentials available");
    return;
  }

  if (nowMs < nextRetryAtMs_) {
    return;
  }

  transition_to(NetworkState::kConnecting,
                retryCount_ == 0 ? "starting async station connect" : "retry backoff expired");
  clear_last_disconnect_reason();
  debug::logf("WIFI", "Starting async station connect attempt %u to ssid='%s'%s",
              static_cast<unsigned>(retryCount_ + 1),
              savedSsid_.c_str(),
              savedUsername_.length() > 0 ? " using WPA2-Enterprise" : "");
  wifi_manager::begin_station(savedSsid_.c_str(), savedPassword_.c_str(), savedUsername_.c_str());
  connectingStartMs_ = nowMs;
}

void NetworkManager::request_reconnect() {
  debug::logf("WIFI", "Reconnect requested: resetting retry state");
  connectingStartMs_ = 0;
  retryCount_ = 0;
  nextRetryAtMs_ = 0;
  clear_last_disconnect_reason();
  WiFi.disconnect(false, false);
  transition_to(NetworkState::kConnecting, "manual reconnect requested");
}

void NetworkManager::clear_credentials_and_enter_setup_mode() {
  debug::logf("WIFI", "Clearing Wi-Fi credentials and forcing setup AP mode");
  savedSsid_ = "";
  savedPassword_ = "";
  savedUsername_ = "";
  hasSavedCredentials_ = false;
  retryCount_ = 0;
  nextRetryAtMs_ = 0;
  connectingStartMs_ = 0;
  recoveryApEnabled_ = false;
  clear_last_disconnect_reason();

  WiFi.disconnect(true, true);
  delay(100);
  wifi_manager::clear_credentials();
  enable_recovery_ap("credentials cleared explicitly");
}

void NetworkManager::set_credentials(const char *ssid, const char *password, const char *username) {
  savedSsid_ = ssid ? ssid : "";
  savedPassword_ = password ? password : "";
  savedUsername_ = username ? username : "";
  hasSavedCredentials_ = savedSsid_.length() > 0;
  if (hasSavedCredentials_) {
    wifi_manager::save_credentials(savedSsid_, savedPassword_, savedUsername_);
    debug::logf("WIFI", "Credentials updated: ssid='%s'%s",
                savedSsid_.c_str(),
                savedUsername_.length() > 0 ? " mode=WPA2-Enterprise" : " mode=WPA/WPA2-PSK");
  } else {
    debug::logf("WIFI", "Credentials cleared from runtime request");
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

void NetworkManager::transition_to(NetworkState next, const char *reason) {
  if (state_ == next) {
    return;
  }

  const NetworkState previous = state_;
  if (next == NetworkState::kDisconnected && lastDisconnectReasonValid_) {
    debug::logf("WIFI", "State change: %s -> %s because %s; reason=%u (%s)",
                debug::network_state_name(previous),
                debug::network_state_name(next),
                reason ? reason : "unspecified",
                static_cast<unsigned>(lastDisconnectReason_),
                debug::wifi_disconnect_reason_name(lastDisconnectReason_));
  } else {
    debug::logf("WIFI", "State change: %s -> %s because %s",
                debug::network_state_name(previous),
                debug::network_state_name(next),
                reason ? reason : "unspecified");
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

  clear_last_disconnect_reason();
  debug::logf("WIFI", "Starting blocking station connect to ssid='%s'%s",
              savedSsid_.c_str(),
              savedUsername_.length() > 0 ? " using WPA2-Enterprise" : "");
  const bool connected = wifi_manager::connect_station(savedSsid_.c_str(), savedPassword_.c_str(), savedUsername_.c_str());
  if (!connected) {
    if (lastDisconnectReasonValid_) {
      debug::logf("WIFI", "Blocking station connect failed; lastReason=%u (%s)",
                  static_cast<unsigned>(lastDisconnectReason_),
                  debug::wifi_disconnect_reason_name(lastDisconnectReason_));
    } else {
      debug::logf("WIFI", "Blocking station connect failed with no disconnect reason");
    }
    transition_to(NetworkState::kDisconnected, "blocking station connect failed");
  }
  return connected;
}

void NetworkManager::enable_recovery_ap(const char *reason) {
  if (recoveryApEnabled_) {
    transition_to(NetworkState::kApMode, reason ? reason : "recovery AP already active");
    return;
  }

  const char *apSsid = config_.apSsid[0] ? config_.apSsid : "commutelive-setup";
  const char *apPassword = config_.apPassword[0] ? config_.apPassword : "commutelive-setup";

  debug::logf("WIFI", "Enabling recovery AP ssid='%s' because %s",
              apSsid,
              reason ? reason : "unspecified");
  if (wifi_manager::start_ap(apSsid, apPassword)) {
    recoveryApEnabled_ = true;
    transition_to(NetworkState::kApMode, reason ? reason : "recovery AP enabled");
  } else {
    debug::logf("WIFI", "Recovery AP start failed for ssid='%s'", apSsid);
    transition_to(NetworkState::kDisconnected, "recovery AP start failed");
  }
}

void NetworkManager::clear_last_disconnect_reason() {
  lastDisconnectAtMs_ = 0;
  lastDisconnectReason_ = 0;
  lastDisconnectReasonValid_ = false;
}

void NetworkManager::handle_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      lastDisconnectReason_ = debug::normalize_wifi_disconnect_reason(info.wifi_sta_disconnected.reason);
      lastDisconnectReasonValid_ = true;
      lastDisconnectAtMs_ = millis();
      debug::logf("WIFI", "STA disconnect event: reason=%u (%s)",
                  static_cast<unsigned>(lastDisconnectReason_),
                  debug::wifi_disconnect_reason_name(lastDisconnectReason_));
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      clear_last_disconnect_reason();
      debug::logf("WIFI", "Station connected: ssid='%s' ip=%s",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
      break;
    default:
      break;
  }
}

void NetworkManager::on_wifi_event(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (!activeInstance_) {
    return;
  }
  activeInstance_->handle_wifi_event(event, info);
}

}  // namespace core
