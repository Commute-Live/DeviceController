#include "wifi_manager.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <string.h>

namespace wifi_manager {

static Preferences prefs;

bool start_ap(const char *apSsid, const char *apPassword) {
  Serial.println("[ESP] Starting ESP32 WIFI");
  WiFi.mode(WIFI_AP_STA);

  bool success = WiFi.softAP(apSsid, apPassword);
  if (success) {
    Serial.println("[ESP] ESP WiFi started!");
  } else {
    Serial.println("[ESP] Failed to connect to ESP WiFi");
  }

  return success;
}

void save_credentials(const String &ssid, const String &password, const String &user) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.putString("user", user);
  prefs.end();
  Serial.println("[ESP] WiFi credentials saved");
}

bool load_credentials(String &ssid, String &password, String &user) {
  prefs.begin("wifi", true);
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("pass", "");
  user = prefs.getString("user", "");
  prefs.end();

  if (ssid.length() > 0) {
    Serial.println("[ESP] Found saved WiFi credentials");
    return true;
  }
  return false;
}

bool connect_station(const char *ssid, const char *password, const char *username) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(true, true);
  delay(100);

  if (username && strlen(username) > 0) {
    Serial.println("[ESP] Using WPA2-Enterprise");
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
    Serial.print("trying...");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.disconnect(true, true);
  delay(200);
  return false;
}

static int fresh_scan_networks() {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.mode(WIFI_AP_STA);
  WiFi.scanDelete();
  delay(50);

  int n = WiFi.scanNetworks(false, true);

  WiFi.setAutoReconnect(true);
  return n;
}

bool handle_connect_request(WebServer &server, String &connectedSsid, String &connectedPassword, String &connectedUser) {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    server.send(400, "application/json", "{\"error\":\"Missing ssid or password\"}");
    return false;
  }

  String homeSsid = server.arg("ssid");
  String homePassword = server.arg("password");
  String homeUser = server.hasArg("user") ? server.arg("user") : "";

  WiFi.mode(WIFI_AP_STA);
  int networkCount = fresh_scan_networks();

  if (networkCount == 0) {
    Serial.println("no networks found");
    server.send(400, "application/json", "{\"error\":\"No Eligible WiFi networks found\"}");
    return false;
  }

  Serial.print(networkCount);
  Serial.println(" networks found");

  for (int i = 0; i < networkCount; ++i) {
    if (strcmp(WiFi.SSID(i).c_str(), homeSsid.c_str()) != 0) {
      delay(10);
      continue;
    }

    Serial.println("[ESP] Found target WiFi network!");
    if (!connect_station(homeSsid.c_str(), homePassword.c_str(), homeUser.c_str())) {
      Serial.println("[ESP] Failed to connect to WiFi.");
      server.send(400, "application/json", "{\"error\":\"WiFi credentials wrong\"}");
      return false;
    }

    Serial.println("[ESP] Successfully connected to WiFi!");
    save_credentials(homeSsid, homePassword, homeUser);

    connectedSsid = homeSsid;
    connectedPassword = homePassword;
    connectedUser = homeUser;
    return true;
  }

  server.send(400, "application/json", "{\"error\":\"Target WiFi network not found\"}");
  return false;
}

}  // namespace wifi_manager
