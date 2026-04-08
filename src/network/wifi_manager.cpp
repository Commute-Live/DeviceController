#include "wifi_manager.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <string.h>

#include "core/logging.h"

namespace wifi_manager {

static Preferences prefs;

namespace {

constexpr uint8_t kProvisioningConnectAttempts = 3;
constexpr uint32_t kProvisioningRetryDelayMs = 200;

enum class StationAuthMode : uint8_t {
  kPersonal = 0,
  kEnterprise = 1,
};

int fresh_scan_networks();
void ensure_wifi_event_handler();
bool failure_status_is_auth_related(int wifiStatus, int disconnectReason);
int failure_priority(int wifiStatus, int disconnectReason);
void emit_provisioning_progress(ProvisioningProgressCallback progressCb,
                                const char *phase,
                                int wifiStatus,
                                uint8_t attempt,
                                uint8_t totalAttempts,
                                void *progressCtx);

volatile int gLastDisconnectReason = WIFI_REASON_UNSPECIFIED;
bool gWifiEventHandlerRegistered = false;

void clear_enterprise_credentials() {
  esp_wifi_sta_wpa2_ent_disable();
  esp_wifi_sta_wpa2_ent_clear_identity();
  esp_wifi_sta_wpa2_ent_clear_username();
  esp_wifi_sta_wpa2_ent_clear_password();
  esp_wifi_sta_wpa2_ent_clear_new_password();
  esp_wifi_sta_wpa2_ent_clear_ca_cert();
  esp_wifi_sta_wpa2_ent_clear_cert_key();
}

const char *station_auth_mode_name(StationAuthMode mode) {
  switch (mode) {
    case StationAuthMode::kPersonal:
      return "personal";
    case StationAuthMode::kEnterprise:
      return "enterprise";
    default:
      return "unknown";
  }
}

const char *wifi_auth_mode_name(wifi_auth_mode_t authMode) {
  switch (authMode) {
    case WIFI_AUTH_OPEN:
      return "OPEN";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2_ENTERPRISE";
    default:
      return "OTHER";
  }
}

void on_wifi_event(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    gLastDisconnectReason = info.wifi_sta_disconnected.reason;
    DCTRL_LOGW("WIFI", "STA disconnected ssid=%s reason=%d (%s)",
               reinterpret_cast<const char *>(info.wifi_sta_disconnected.ssid),
               static_cast<int>(info.wifi_sta_disconnected.reason),
               WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason)));
  } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED || event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    gLastDisconnectReason = WIFI_REASON_UNSPECIFIED;
  }
}

void ensure_wifi_event_handler() {
  if (gWifiEventHandlerRegistered) {
    return;
  }
  WiFi.onEvent(on_wifi_event, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(on_wifi_event, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(on_wifi_event, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  gWifiEventHandlerRegistered = true;
}

void reset_last_disconnect_reason() {
  gLastDisconnectReason = WIFI_REASON_UNSPECIFIED;
}

bool failure_status_is_auth_related(int wifiStatus, int disconnectReason) {
  if (wifiStatus == WL_CONNECT_FAILED) {
    return true;
  }

  switch (disconnectReason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_802_1X_AUTH_FAILED:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_CONNECTION_FAIL:
    case WIFI_REASON_AKMP_INVALID:
    case WIFI_REASON_BAD_CIPHER_OR_AKM:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return true;
    default:
      return false;
  }
}

int failure_priority(int wifiStatus, int disconnectReason) {
  if (failure_status_is_auth_related(wifiStatus, disconnectReason)) {
    return 3;
  }
  if (wifiStatus == WL_CONNECTION_LOST ||
      disconnectReason == WIFI_REASON_BEACON_TIMEOUT ||
      disconnectReason == WIFI_REASON_TIMEOUT) {
    return 2;
  }
  if (wifiStatus == WL_NO_SSID_AVAIL || disconnectReason == WIFI_REASON_NO_AP_FOUND) {
    return 1;
  }
  return 0;
}

void emit_provisioning_progress(ProvisioningProgressCallback progressCb,
                                const char *phase,
                                int wifiStatus,
                                uint8_t attempt,
                                uint8_t totalAttempts,
                                void *progressCtx) {
  if (!progressCb || !phase || phase[0] == '\0') {
    return;
  }
  progressCb(phase, wifiStatus, attempt, totalAttempts, progressCtx);
}

bool scan_for_ssid(const char *ssid,
                   wifi_auth_mode_t *authModeOut,
                   int *rssiOut,
                   int *channelOut,
                   ProvisioningProgressCallback progressCb,
                   uint8_t attempt,
                   uint8_t totalAttempts,
                   void *progressCtx) {
  if (!ssid || ssid[0] == '\0') {
    return false;
  }

  emit_provisioning_progress(progressCb, "scanning", WL_IDLE_STATUS, attempt, totalAttempts, progressCtx);
  const int networkCount = fresh_scan_networks();
  if (networkCount <= 0) {
    WiFi.scanDelete();
    return false;
  }

  for (int i = 0; i < networkCount; ++i) {
    if (strcmp(WiFi.SSID(i).c_str(), ssid) != 0) {
      continue;
    }

    if (authModeOut) {
      *authModeOut = static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i));
    }
    if (rssiOut) {
      *rssiOut = WiFi.RSSI(i);
    }
    if (channelOut) {
      *channelOut = WiFi.channel(i);
    }

    DCTRL_LOGI("WIFI", "Resolved target ssid=%s auth=%s rssi=%d channel=%d",
               ssid,
               wifi_auth_mode_name(static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i))),
               WiFi.RSSI(i),
               WiFi.channel(i));
    WiFi.scanDelete();
    return true;
  }

  DCTRL_LOGW("WIFI", "Target ssid=%s not present in fresh scan before connect", ssid);
  WiFi.scanDelete();
  return false;
}

StationAuthMode resolve_station_auth_mode(const char *ssid,
                                          const char *username,
                                          ProvisioningProgressCallback progressCb,
                                          uint8_t attempt,
                                          uint8_t totalAttempts,
                                          void *progressCtx) {
  const bool hasUsername = username && username[0] != '\0';
  if (!hasUsername) {
    return StationAuthMode::kPersonal;
  }

  wifi_auth_mode_t authMode = WIFI_AUTH_OPEN;
  int rssi = 0;
  int channel = 0;
  if (!scan_for_ssid(ssid, &authMode, &rssi, &channel, progressCb, attempt, totalAttempts, progressCtx)) {
    DCTRL_LOGW("WIFI",
               "Username was provided for ssid=%s but the SSID was not visible during auth detection; keeping enterprise mode",
               core::logging::safe_str(ssid));
    return StationAuthMode::kEnterprise;
  }

  if (authMode == WIFI_AUTH_WPA2_ENTERPRISE) {
    DCTRL_LOGI("WIFI", "SSID=%s confirmed as enterprise auth=%s rssi=%d channel=%d",
               core::logging::safe_str(ssid),
               wifi_auth_mode_name(authMode),
               rssi,
               channel);
    return StationAuthMode::kEnterprise;
  }

  DCTRL_LOGW("WIFI",
             "Username was provided for ssid=%s but scan shows auth=%s rssi=%d channel=%d; using personal WPA/WPA2 mode",
             core::logging::safe_str(ssid),
             wifi_auth_mode_name(authMode),
             rssi,
             channel);
  return StationAuthMode::kPersonal;
}

bool connect_station_once(const char *ssid,
                          const char *password,
                          const char *username,
                          int *finalStatusOut,
                          ProvisioningProgressCallback progressCb,
                          uint8_t attempt,
                          uint8_t totalAttempts,
                          void *progressCtx) {
  ensure_wifi_event_handler();
  reset_last_disconnect_reason();

  if (finalStatusOut) {
    *finalStatusOut = WL_IDLE_STATUS;
  }

  reset_station_state(true);

  const StationAuthMode authMode = resolve_station_auth_mode(ssid, username, progressCb, attempt, totalAttempts, progressCtx);
  if (authMode == StationAuthMode::kEnterprise) {
    DCTRL_LOGI("WIFI", "Configuring enterprise auth for blocking connect ssid=%s user=%s",
               core::logging::safe_str(ssid),
               core::logging::safe_str(username));
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)username, strlen(username));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)username, strlen(username));
    if (password && strlen(password) > 0) {
      esp_wifi_sta_wpa2_ent_set_password((uint8_t *)password, strlen(password));
    }
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(ssid);
  } else {
    esp_wifi_sta_wpa2_ent_disable();
    DCTRL_LOGI("WIFI", "Configuring personal auth for blocking connect ssid=%s", core::logging::safe_str(ssid));
    WiFi.begin(ssid, password);
  }
  emit_provisioning_progress(progressCb, "wifi_connecting", WiFi.status(), attempt, totalAttempts, progressCtx);

  int timeout = 15;
  int lastReportedStatus = 999;
  while (WiFi.status() != WL_CONNECTED && timeout--) {
    const int currentStatus = WiFi.status();
    if (currentStatus != lastReportedStatus) {
      emit_provisioning_progress(progressCb, "wifi_connecting", currentStatus, attempt, totalAttempts, progressCtx);
      lastReportedStatus = currentStatus;
    }
    DCTRL_LOGD("WIFI", "Blocking connect poll remaining=%d status=%s (%d)",
               timeout,
               core::logging::wifi_status_name(currentStatus),
               currentStatus);
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    emit_provisioning_progress(progressCb, "wifi_connected", WL_CONNECTED, attempt, totalAttempts, progressCtx);
    DCTRL_LOGI("WIFI", "Blocking connect success ssid=%s ip=%s gateway=%s rssi=%d",
               WiFi.SSID().c_str(),
               WiFi.localIP().toString().c_str(),
               WiFi.gatewayIP().toString().c_str(),
               WiFi.RSSI());
    return true;
  }

  const int finalStatus = WiFi.status();
  const int disconnectReason = gLastDisconnectReason;
  if (finalStatusOut) {
    *finalStatusOut = finalStatus;
  }

  DCTRL_LOGW("WIFI", "Blocking connect failed ssid=%s authMode=%s finalStatus=%s (%d) disconnectReason=%d (%s)",
             core::logging::safe_str(ssid),
             station_auth_mode_name(authMode),
             core::logging::wifi_status_name(finalStatus),
             finalStatus,
             disconnectReason,
             WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(disconnectReason)));
  reset_station_state(true);
  return false;
}

}  // namespace

void save_credentials(const String &ssid, const String &password, const String &user) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.putString("user", user);
  prefs.end();
  DCTRL_LOGI("WIFI", "Saved credentials ssid=%s passwordLen=%u enterprise=%s",
             ssid.c_str(),
             static_cast<unsigned>(password.length()),
             core::logging::bool_str(user.length() > 0));
}

bool load_credentials(String &ssid, String &password, String &user) {
  prefs.begin("wifi", true);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("pass", "");
  user = prefs.getString("user", "");
  prefs.end();

  if (ssid.length() > 0) {
    DCTRL_LOGI("WIFI", "Loaded saved credentials ssid=%s passwordLen=%u enterprise=%s",
               ssid.c_str(),
               static_cast<unsigned>(password.length()),
               core::logging::bool_str(user.length() > 0));
    return true;
  }
  DCTRL_LOGW("WIFI", "No saved WiFi credentials found in NVS");
  return false;
}

void begin_station(const char *ssid, const char *password, const char *username) {
  DCTRL_LOGI("WIFI", "begin_station ssid=%s passwordLen=%u enterprise=%s",
             core::logging::safe_str(ssid),
             static_cast<unsigned>(password ? strlen(password) : 0),
             core::logging::bool_str(username && strlen(username) > 0));
  reset_station_state(false);

  const StationAuthMode authMode = resolve_station_auth_mode(ssid, username, nullptr, 0, 0, nullptr);
  if (authMode == StationAuthMode::kEnterprise) {
    DCTRL_LOGI("WIFI", "Configuring enterprise auth for async connect ssid=%s user=%s",
               core::logging::safe_str(ssid),
               core::logging::safe_str(username));
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)username, strlen(username));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)username, strlen(username));
    if (password && strlen(password) > 0) {
      esp_wifi_sta_wpa2_ent_set_password((uint8_t *)password, strlen(password));
    }
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(ssid);
  } else {
    esp_wifi_sta_wpa2_ent_disable();
    WiFi.begin(ssid, password);
  }
  DCTRL_LOGI("WIFI", "Async WiFi.begin issued ssid=%s mode=%s",
             core::logging::safe_str(ssid),
             station_auth_mode_name(authMode));
}

void clear_credentials() {
  prefs.begin("wifi", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.remove("user");
  prefs.end();
  DCTRL_LOGI("WIFI", "Cleared saved WiFi credentials");
}

void reset_station_state(bool erasePersistentConfig) {
  DCTRL_LOGI("WIFI", "Resetting station state erasePersistentConfig=%s",
             core::logging::bool_str(erasePersistentConfig));
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  clear_enterprise_credentials();
  WiFi.disconnect(true, erasePersistentConfig);
  delay(200);
}

void build_ap_ssid(char *out, size_t outLen) {
  const uint64_t chipid = ESP.getEfuseMac();
  snprintf(out, outLen, "CommuteLive-%04X", static_cast<uint16_t>(chipid));
}

String generate_or_load_ap_password() {
  prefs.begin("wifi", false);
  String pass = prefs.getString("ap_pass", "");
  if (pass.length() == 0) {
    // Unambiguous charset — excludes 0/O/1/I/l to avoid transcription errors.
    const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    char buf[9];
    for (int i = 0; i < 8; ++i) {
      buf[i] = charset[esp_random() % (sizeof(charset) - 1)];
    }
    buf[8] = '\0';
    pass = String(buf);
    prefs.putString("ap_pass", pass);
    DCTRL_LOGI("WIFI", "Generated new AP password passwordLen=%u", static_cast<unsigned>(pass.length()));
  }
  prefs.end();
  return pass;
}

bool connect_station(const char *ssid, const char *password, const char *username) {
  DCTRL_LOGI("WIFI", "connect_station ssid=%s passwordLen=%u enterprise=%s",
             core::logging::safe_str(ssid),
             static_cast<unsigned>(password ? strlen(password) : 0),
             core::logging::bool_str(username && strlen(username) > 0));
  return connect_station_once(ssid, password, username, nullptr, nullptr, 0, 0, nullptr);
}

bool connect_station_for_provisioning(const char *ssid,
                                      const char *password,
                                      const char *username,
                                      int *finalStatusOut,
                                      ProvisioningProgressCallback progressCb,
                                      void *progressCtx) {
  DCTRL_LOGI("WIFI", "Provisioning connect ssid=%s passwordLen=%u enterprise=%s attempts=%u",
             core::logging::safe_str(ssid),
             static_cast<unsigned>(password ? strlen(password) : 0),
             core::logging::bool_str(username && strlen(username) > 0),
             static_cast<unsigned>(kProvisioningConnectAttempts));

  int finalStatus = WL_IDLE_STATUS;
  int bestStatus = WL_IDLE_STATUS;
  int bestDisconnectReason = WIFI_REASON_UNSPECIFIED;
  for (uint8_t attempt = 1; attempt <= kProvisioningConnectAttempts; ++attempt) {
    DCTRL_LOGI("WIFI", "Provisioning connect attempt=%u/%u ssid=%s",
               static_cast<unsigned>(attempt),
               static_cast<unsigned>(kProvisioningConnectAttempts),
               core::logging::safe_str(ssid));
    if (connect_station_once(ssid,
                             password,
                             username,
                             &finalStatus,
                             progressCb,
                             attempt,
                             kProvisioningConnectAttempts,
                             progressCtx)) {
      if (finalStatusOut) {
        *finalStatusOut = WL_CONNECTED;
      }
      return true;
    }

    const int disconnectReason = last_disconnect_reason();
    if (failure_priority(finalStatus, disconnectReason) >= failure_priority(bestStatus, bestDisconnectReason)) {
      bestStatus = finalStatus;
      bestDisconnectReason = disconnectReason;
    }

    if (attempt < kProvisioningConnectAttempts) {
      delay(kProvisioningRetryDelayMs);
    }
  }

  if (finalStatusOut) {
    *finalStatusOut = bestStatus != WL_IDLE_STATUS ? bestStatus : finalStatus;
  }
  DCTRL_LOGW("WIFI", "Provisioning connect exhausted retries ssid=%s bestStatus=%s (%d) bestDisconnectReason=%d (%s)",
             core::logging::safe_str(ssid),
             core::logging::wifi_status_name(bestStatus != WL_IDLE_STATUS ? bestStatus : finalStatus),
             bestStatus != WL_IDLE_STATUS ? bestStatus : finalStatus,
             bestDisconnectReason,
             WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(bestDisconnectReason)));
  return false;
}

namespace {

int fresh_scan_networks() {
  DCTRL_LOGI("WIFI", "Starting fresh network scan");
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, true);
  DCTRL_LOGI("WIFI", "Finished network scan count=%d", n);
  return n;
}

}  // namespace

int last_disconnect_reason() {
  return gLastDisconnectReason;
}

const char *disconnect_reason_name(int reason) {
  return WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason));
}

bool handle_connect_request(WebServer &server, String &connectedSsid, String &connectedPassword, String &connectedUser) {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    DCTRL_LOGW("WIFI", "Rejecting /connect request because ssid or password was missing");
    server.send(400, "application/json", "{\"error\":\"Missing ssid or password\"}");
    return false;
  }

  String homeSsid = server.arg("ssid");
  String homePassword = server.arg("password");
  String homeUser = server.hasArg("user") ? server.arg("user") : "";
  DCTRL_LOGI("WIFI", "Handling /connect request targetSsid=%s passwordLen=%u enterprise=%s",
             homeSsid.c_str(),
             static_cast<unsigned>(homePassword.length()),
             core::logging::bool_str(homeUser.length() > 0));

  WiFi.mode(WIFI_STA);
  int networkCount = fresh_scan_networks();

  if (networkCount == 0) {
    DCTRL_LOGW("WIFI", "No networks found while processing /connect request");
    server.send(400, "application/json", "{\"error\":\"No Eligible WiFi networks found\"}");
    return false;
  }

  DCTRL_LOGI("WIFI", "Evaluating %d scanned network(s) for target ssid=%s", networkCount, homeSsid.c_str());

  for (int i = 0; i < networkCount; ++i) {
    if (strcmp(WiFi.SSID(i).c_str(), homeSsid.c_str()) != 0) {
      delay(10);
      continue;
    }

    DCTRL_LOGI("WIFI", "Found target network ssid=%s rssi=%d secure=%s channel=%d",
               homeSsid.c_str(),
               WiFi.RSSI(i),
               core::logging::bool_str(WiFi.encryptionType(i) != WIFI_AUTH_OPEN),
               WiFi.channel(i));
    if (!connect_station_for_provisioning(homeSsid.c_str(), homePassword.c_str(), homeUser.c_str())) {
      DCTRL_LOGW("WIFI", "Target network connect failed ssid=%s", homeSsid.c_str());
      server.send(400, "application/json", "{\"error\":\"WiFi credentials wrong\"}");
      return false;
    }

    DCTRL_LOGI("WIFI", "Target network connect succeeded ssid=%s", homeSsid.c_str());
    save_credentials(homeSsid, homePassword, homeUser);

    connectedSsid = homeSsid;
    connectedPassword = homePassword;
    connectedUser = homeUser;
    return true;
  }

  DCTRL_LOGW("WIFI", "Target network ssid=%s not found in scan results", homeSsid.c_str());
  server.send(400, "application/json", "{\"error\":\"Target WiFi network not found\"}");
  return false;
}

int scan_and_emit(void (*emitChunk)(const char *json, void *ctx), void *ctx) {
  if (!emitChunk) return 0;

  const int n = fresh_scan_networks();
  if (n <= 0) {
    emitChunk("{\"c\":0,\"t\":1,\"n\":[]}", ctx);
    WiFi.scanDelete();
    return 0;
  }

  static constexpr int kPerChunk = 3;
  const int totalChunks = (n + kPerChunk - 1) / kPerChunk;

  for (int chunk = 0; chunk < totalChunks; chunk++) {
    char buf[256];
    int pos = snprintf(buf, sizeof(buf), "{\"c\":%d,\"t\":%d,\"n\":[", chunk, totalChunks);

    const int start = chunk * kPerChunk;
    const int end   = (start + kPerChunk < n) ? start + kPerChunk : n;

    for (int i = start; i < end && pos < static_cast<int>(sizeof(buf)) - 40; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      if (ssid.length() > 32) ssid = ssid.substring(0, 32);

      // Map wifi_auth_mode_t to compact enum: 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=Enterprise
      uint8_t enc = 3;
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN:            enc = 0; break;
        case WIFI_AUTH_WEP:             enc = 1; break;
        case WIFI_AUTH_WPA_PSK:         enc = 2; break;
        case WIFI_AUTH_WPA2_PSK:        enc = 3; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: enc = 4; break;
        default:                        enc = 3; break;
      }

      if (i > start) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                       "{\"s\":\"%s\",\"r\":%d,\"e\":%u}",
                       ssid.c_str(), WiFi.RSSI(i), static_cast<unsigned>(enc));
    }

    snprintf(buf + pos, sizeof(buf) - pos, "]}");
    emitChunk(buf, ctx);
    delay(50);  // small gap between notifications
  }

  WiFi.scanDelete();
  DCTRL_LOGI("WIFI", "Scan results emitted networks=%d chunks=%d", n, totalChunks);
  return n;
}

}  // namespace wifi_manager
