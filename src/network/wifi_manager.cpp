#include "wifi_manager.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <string.h>

#include "core/logging.h"

namespace wifi_manager {

static Preferences prefs;

bool start_ap(const char *apSsid, const char *apPassword) {
  DCTRL_LOGI("WIFI", "Starting soft AP ssid=%s passwordLen=%u",
             core::logging::safe_str(apSsid),
             static_cast<unsigned>(apPassword ? strlen(apPassword) : 0));
  WiFi.mode(WIFI_AP_STA);

  bool success = WiFi.softAP(apSsid, apPassword);
  if (success) {
    DCTRL_LOGI("WIFI", "Soft AP started ssid=%s apIp=%s", core::logging::safe_str(apSsid),
               WiFi.softAPIP().toString().c_str());
  } else {
    DCTRL_LOGE("WIFI", "Soft AP start failed ssid=%s", core::logging::safe_str(apSsid));
  }

  return success;
}

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
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, false);
  delay(100);

  if (username && strlen(username) > 0) {
    DCTRL_LOGI("WIFI", "Configuring WPA2-Enterprise for async connect user=%s", username);
    esp_wifi_sta_wpa2_ent_disable();
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
             username && strlen(username) > 0 ? "enterprise" : "personal");
}

void clear_credentials() {
  prefs.begin("wifi", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.remove("user");
  prefs.end();
  DCTRL_LOGI("WIFI", "Cleared saved WiFi credentials");
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
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(true, true);
  delay(100);

  if (username && strlen(username) > 0) {
    DCTRL_LOGI("WIFI", "Configuring WPA2-Enterprise for blocking connect user=%s", username);
    esp_wifi_sta_wpa2_ent_disable();
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

  int timeout = 15;
  while (WiFi.status() != WL_CONNECTED && timeout--) {
    DCTRL_LOGD("WIFI", "Blocking connect poll remaining=%d status=%s (%d)",
               timeout,
               core::logging::wifi_status_name(WiFi.status()),
               WiFi.status());
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    DCTRL_LOGI("WIFI", "Blocking connect success ssid=%s ip=%s gateway=%s rssi=%d",
               WiFi.SSID().c_str(),
               WiFi.localIP().toString().c_str(),
               WiFi.gatewayIP().toString().c_str(),
               WiFi.RSSI());
    return true;
  }

  DCTRL_LOGW("WIFI", "Blocking connect failed ssid=%s finalStatus=%s (%d)",
             core::logging::safe_str(ssid),
             core::logging::wifi_status_name(WiFi.status()),
             WiFi.status());
  WiFi.disconnect(true, true);
  delay(200);
  return false;
}

static int fresh_scan_networks() {
  DCTRL_LOGI("WIFI", "Starting fresh network scan");
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.mode(WIFI_AP_STA);
  WiFi.scanDelete();
  delay(50);

  int n = WiFi.scanNetworks(false, true);

  WiFi.setAutoReconnect(true);
  DCTRL_LOGI("WIFI", "Finished network scan count=%d", n);
  return n;
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

  WiFi.mode(WIFI_AP_STA);
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
    if (!connect_station(homeSsid.c_str(), homePassword.c_str(), homeUser.c_str())) {
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

}  // namespace wifi_manager
