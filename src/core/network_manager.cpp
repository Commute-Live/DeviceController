#include "core/network_manager.h"

#include <string.h>
#include <WiFi.h>

#include "core/logging.h"
#include "network/wifi_manager.h"

namespace core {

namespace {
constexpr uint32_t kRetryBaseMs = 1000;
constexpr uint32_t kRetryMaxMs = 60000;
constexpr uint32_t kRetryJitterMs = 750;
constexpr uint8_t kRecoveryApAttemptThreshold = 3;
constexpr int kUnknownWifiStatus = 999;

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

const char *network_state_name(NetworkState state) {
  switch (state) {
    case NetworkState::kDisconnected:
      return "kDisconnected";
    case NetworkState::kConnecting:
      return "kConnecting";
    case NetworkState::kConnected:
      return "kConnected";
    case NetworkState::kApMode:
      return "kApMode";
    default:
      return "kUnknown";
  }
}

}  // namespace

NetworkManager::NetworkManager()
    : config_{},
      state_(NetworkState::kDisconnected),
      autoReconnectEnabled_(true),
      hasSavedCredentials_(false),
      recoveryApEnabled_(false),
      savedSsid_(),
      savedPassword_(),
      savedUsername_(),
      nextRetryAtMs_(0),
      connectingStartMs_(0),
      retryCount_(0),
      lastWifiStatus_(kUnknownWifiStatus),
      callback_(nullptr),
      callbackCtx_(nullptr) {}

bool NetworkManager::begin(const NetworkConfig &config) {
  config_ = config;
  autoReconnectEnabled_ = true;
  retryCount_ = 0;
  nextRetryAtMs_ = 0;
  recoveryApEnabled_ = false;
  lastWifiStatus_ = kUnknownWifiStatus;

  DCTRL_LOGI("WIFI",
             "Network manager begin bootstrapSsid=%s bootstrapPassword=%s bootstrapUser=%s apSsid=%s apPassword=%s",
             config_.ssid[0] ? config_.ssid : "(none)",
             config_.password[0] ? config_.password : "(empty)",
             config_.username[0] ? config_.username : "(none)",
             config_.apSsid[0] ? config_.apSsid : "(none)",
             config_.apPassword[0] ? config_.apPassword : "(empty)");

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  delay(100);

  if (has_explicit_bootstrap_wifi(config_)) {
    savedSsid_ = config_.ssid;
    savedPassword_ = config_.password;
    savedUsername_ = config_.username;
    hasSavedCredentials_ = true;
    wifi_manager::save_credentials(savedSsid_, savedPassword_, savedUsername_);
    DCTRL_LOGI("WIFI", "Using configured bootstrap credentials ssid=%s password=%s enterprise=%s",
               savedSsid_.c_str(),
               savedPassword_.c_str(),
               core::logging::bool_str(savedUsername_.length() > 0));
  } else {
    String loadedSsid;
    String loadedPassword;
    String loadedUser;
    hasSavedCredentials_ = wifi_manager::load_credentials(loadedSsid, loadedPassword, loadedUser);

    if (hasSavedCredentials_) {
      savedSsid_ = loadedSsid;
      savedPassword_ = loadedPassword;
      savedUsername_ = loadedUser;
      DCTRL_LOGI("WIFI", "Loaded saved credentials ssid=%s password=%s enterprise=%s",
                 savedSsid_.c_str(),
                 savedPassword_.c_str(),
                 core::logging::bool_str(savedUsername_.length() > 0));
    } else {
      DCTRL_LOGW("WIFI", "No saved WiFi credentials found at boot");
    }
  }

  transition_to(NetworkState::kConnecting);

  if (connect_station_now()) {
    retryCount_ = 0;
    nextRetryAtMs_ = millis();
    transition_to(NetworkState::kConnected);
    DCTRL_LOGI("WIFI", "Initial blocking station connect succeeded ssid=%s ip=%s rssi=%d",
               WiFi.SSID().c_str(),
               WiFi.localIP().toString().c_str(),
               WiFi.RSSI());
    return true;
  }

  DCTRL_LOGW("WIFI", "Initial blocking station connect failed; enabling recovery AP");
  enable_recovery_ap();
  return false;
}

void NetworkManager::tick(uint32_t nowMs) {
  const int wifiStatus = WiFi.status();
  if (wifiStatus != lastWifiStatus_) {
    DCTRL_LOGI("WIFI", "Observed status=%s (%d) state=%s retryCount=%u recoveryAp=%s",
               core::logging::wifi_status_name(wifiStatus),
               wifiStatus,
               network_state_name(state_),
               static_cast<unsigned>(retryCount_),
               core::logging::bool_str(recoveryApEnabled_));
    lastWifiStatus_ = wifiStatus;
  }

  if (wifiStatus == WL_CONNECTED) {
    retryCount_ = 0;
    connectingStartMs_ = 0;
    if (state_ != NetworkState::kConnected) {
      recoveryApEnabled_ = false;
      transition_to(NetworkState::kConnected);
      DCTRL_LOGI("WIFI", "Station connected ssid=%s ip=%s gateway=%s rssi=%d",
                 WiFi.SSID().c_str(),
                 WiFi.localIP().toString().c_str(),
                 WiFi.gatewayIP().toString().c_str(),
                 WiFi.RSSI());
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
    DCTRL_LOGW("WIFI", "Async connect timed out after %lu ms ssid=%s retryInMs=%lu attempt=%u status=%s",
               static_cast<unsigned long>(kConnectTimeoutMs),
               savedSsid_.c_str(),
               static_cast<unsigned long>(waitMs),
               static_cast<unsigned>(retryCount_),
               core::logging::wifi_status_name(WiFi.status()));
    if (!recoveryApEnabled_ && retryCount_ >= kRecoveryApAttemptThreshold) {
      enable_recovery_ap();
    }
    return;
  }

  if (!autoReconnectEnabled_) {
    if (recoveryApEnabled_) {
      transition_to(NetworkState::kApMode);
    } else {
      transition_to(NetworkState::kDisconnected);
    }
    return;
  }

  if (!hasSavedCredentials_) {
    DCTRL_LOGW("WIFI", "No credentials available; staying in recovery AP mode");
    enable_recovery_ap();
    return;
  }

  if (nowMs < nextRetryAtMs_) {
    return;
  }

  transition_to(NetworkState::kConnecting);
  DCTRL_LOGI("WIFI", "Starting async station connect ssid=%s password=%s enterprise=%s attempt=%u",
             savedSsid_.c_str(),
             savedPassword_.c_str(),
             core::logging::bool_str(savedUsername_.length() > 0),
             static_cast<unsigned>(retryCount_ + 1));
  wifi_manager::begin_station(savedSsid_.c_str(), savedPassword_.c_str(), savedUsername_.c_str());
  connectingStartMs_ = nowMs;
}

void NetworkManager::disconnect(bool clearCredentials, bool restartProvisioning) {
  autoReconnectEnabled_ = false;
  connectingStartMs_ = 0;
  retryCount_ = 0;
  nextRetryAtMs_ = 0;

  DCTRL_LOGI("WIFI", "Manual disconnect requested clearCredentials=%s restartProvisioning=%s currentState=%s ssid=%s",
             core::logging::bool_str(clearCredentials),
             core::logging::bool_str(restartProvisioning),
             network_state_name(state_),
             savedSsid_.c_str());

  if (clearCredentials) {
    savedSsid_ = "";
    savedPassword_ = "";
    savedUsername_ = "";
    hasSavedCredentials_ = false;
    wifi_manager::clear_credentials();
  }

  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, clearCredentials);

  if (restartProvisioning) {
    enable_recovery_ap();
    return;
  }

  transition_to(NetworkState::kDisconnected);
}

void NetworkManager::request_reconnect() {
  autoReconnectEnabled_ = true;
  DCTRL_LOGI("WIFI", "Reconnect requested currentState=%s ssid=%s",
             network_state_name(state_),
             savedSsid_.c_str());
  connectingStartMs_ = 0;
  retryCount_ = 0;
  nextRetryAtMs_ = 0;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  transition_to(NetworkState::kConnecting);
}

void NetworkManager::set_credentials(const char *ssid, const char *password, const char *username) {
  savedSsid_ = ssid ? ssid : "";
  savedPassword_ = password ? password : "";
  savedUsername_ = username ? username : "";
  autoReconnectEnabled_ = true;
  hasSavedCredentials_ = savedSsid_.length() > 0;
  DCTRL_LOGI("WIFI", "Updating credentials ssid=%s password=%s enterprise=%s",
             savedSsid_.c_str(),
             savedPassword_.c_str(),
             core::logging::bool_str(savedUsername_.length() > 0));
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
  const NetworkState previous = state_;
  state_ = next;
  DCTRL_LOGI("WIFI", "State transition %s -> %s wifi=%s recoveryAp=%s",
             network_state_name(previous),
             network_state_name(state_),
             core::logging::wifi_status_name(WiFi.status()),
             core::logging::bool_str(recoveryApEnabled_));
  if (callback_) {
    callback_(state_, callbackCtx_);
  }
}

bool NetworkManager::connect_station_now() {
  if (!hasSavedCredentials_) {
    DCTRL_LOGW("WIFI", "Blocking connect skipped because no credentials are available");
    return false;
  }

  DCTRL_LOGI("WIFI", "Starting blocking station connect ssid=%s password=%s enterprise=%s",
             savedSsid_.c_str(),
             savedPassword_.c_str(),
             core::logging::bool_str(savedUsername_.length() > 0));
  const bool connected = wifi_manager::connect_station(savedSsid_.c_str(), savedPassword_.c_str(), savedUsername_.c_str());
  if (!connected) {
    transition_to(NetworkState::kDisconnected);
    DCTRL_LOGW("WIFI", "Blocking station connect failed ssid=%s", savedSsid_.c_str());
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

  DCTRL_LOGI("WIFI", "Enabling recovery AP ssid=%s password=%s",
             apSsid,
             apPassword);
  if (wifi_manager::start_ap(apSsid, apPassword)) {
    recoveryApEnabled_ = true;
    transition_to(NetworkState::kApMode);
    DCTRL_LOGI("WIFI", "Recovery AP enabled ssid=%s apIp=%s",
               apSsid,
               WiFi.softAPIP().toString().c_str());
  } else {
    transition_to(NetworkState::kDisconnected);
    DCTRL_LOGE("WIFI", "Failed to enable recovery AP ssid=%s", apSsid);
  }
}

}  // namespace core
