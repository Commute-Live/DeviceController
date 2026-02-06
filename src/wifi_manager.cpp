#include "wifi_manager.h"

#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <Preferences.h>

static const char *AP_SSID = "CommuteLive-Setup-shimu";
static const char *AP_PASS = "12345678";

static String lastError = "";
static bool isConnecting = false;
static String lastScanJson = "[]";
static int lastScanCount = 0;
static unsigned long lastScanMs = 0;
static unsigned long scanStartMs = 0;
static bool scanInProgress = false;
static const unsigned long SCAN_TIMEOUT_MS = 15000;

static Preferences prefs;

static String jsonEscape(const String &input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '\"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

static String buildScanJson(int n) {
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    String ssid = jsonEscape(WiFi.SSID(i));
    int rssi = WiFi.RSSI(i);
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    bool secure = (auth != WIFI_AUTH_OPEN);
    json += "{";
    json += "\"ssid\":\"" + ssid + "\",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"secure\":" + String(secure ? "true" : "false");
    json += "}";
  }
  json += "]";
  return json;
}

static String wifiDisconnectReason(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "Wrong username or password";
    case WIFI_REASON_NO_AP_FOUND:
      return "Network not found";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_ASSOC_LEAVE:
      return "Wrong username or password";
    default:
      return "Disconnected from Wi-Fi";
  }
}

static void startSavedConnection() {
  prefs.begin("wifi", false);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  String savedUser = prefs.getString("user", "");
  prefs.end();

  if (savedSsid.length() == 0) {
    return;
  }

  isConnecting = true;
  if (savedUser.length() > 0) {
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)savedUser.c_str(), savedUser.length());
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)savedUser.c_str(), savedUser.length());
    if (savedPass.length() > 0) {
      esp_wifi_sta_wpa2_ent_set_password((uint8_t *)savedPass.c_str(), savedPass.length());
    }
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(savedSsid.c_str());
  } else {
    esp_wifi_sta_wpa2_ent_disable();
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  }
}

bool wifiManagerIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void wifiManagerInit(AsyncWebServer &server) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.softAP(AP_SSID, AP_PASS);

  startSavedConnection();

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      isConnecting = false;
      lastError = "";
      Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      isConnecting = false;
      lastError = wifiDisconnectReason(info.wifi_sta_disconnected.reason);
      Serial.printf("WiFi disconnected: %s\n", lastError.c_str());
    } else if (event == ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE) {
      lastError = "Auth mode changed";
      Serial.println("WiFi auth mode changed");
    }
  });

  // Scan Wi-Fi networks that are available
  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!scanInProgress && !isConnecting && (millis() - lastScanMs > 5000)) {
      WiFi.scanNetworks(/*async=*/true, /*hidden=*/true);
      scanInProgress = true;
      scanStartMs = millis();
      lastScanJson = "[]";
      lastScanCount = 0;
    }
    String json = "{";
    json += "\"scanning\":" + String(scanInProgress ? "true" : "false") + ",";
    json += "\"count\":" + String(lastScanCount) + ",";
    json += "\"results\":" + lastScanJson;
    json += "}";
    request->send(200, "application/json", json);
  });

  // Status of the wifi connection
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool connected = (WiFi.status() == WL_CONNECTED);
    String json = "{";
    json += "\"connected\":" + String(connected ? "true" : "false") + ",";
    json += "\"connecting\":" + String(isConnecting ? "true" : "false") + ",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"ip\":\"" + (connected ? WiFi.localIP().toString() : String("")) + "\",";
    json += "\"error\":\"" + lastError + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // Connect to selected wifi
  server.on("/api/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("ssid", true)) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing SSID\"}");
      return;
    }

    String ssid = request->getParam("ssid", true)->value();
    String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";
    String user = request->hasParam("user", true) ? request->getParam("user", true)->value() : "";

    lastError = "";
    isConnecting = true;

    WiFi.disconnect(true, true);
    delay(200);

    if (user.length() > 0) {
      prefs.begin("wifi", false);
      prefs.putString("ssid", ssid);
      prefs.putString("pass", pass);
      prefs.putString("user", user);
      prefs.end();

      // WPA2-Enterprise (e.g., campus Wi-Fi)
      esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)user.c_str(), user.length());
      esp_wifi_sta_wpa2_ent_set_username((uint8_t *)user.c_str(), user.length());
      if (pass.length() > 0) {
        esp_wifi_sta_wpa2_ent_set_password((uint8_t *)pass.c_str(), pass.length());
      }
      esp_wifi_sta_wpa2_ent_enable();
      WiFi.begin(ssid.c_str());
    } else {
      prefs.begin("wifi", false);
      prefs.putString("ssid", ssid);
      prefs.putString("pass", pass);
      prefs.putString("user", "");
      prefs.end();

      esp_wifi_sta_wpa2_ent_disable();
      WiFi.begin(ssid.c_str(), pass.c_str());
    }

    request->send(200, "application/json", "{\"ok\":true}");
  });

  // Disconnect and clear saved credentials
  server.on("/api/disconnect", HTTP_POST, [](AsyncWebServerRequest *request) {
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
    esp_wifi_sta_wpa2_ent_disable();
    WiFi.disconnect(true, true);
    isConnecting = false;
    lastError = "";
    request->send(200, "application/json", "{\"ok\":true}");
  });
}

void wifiManagerLoop() {
  if (scanInProgress) {
    int n = WiFi.scanComplete();
    if (n >= 0) {
      lastScanCount = n;
      lastScanJson = buildScanJson(n);
      lastScanMs = millis();
      scanInProgress = false;
      Serial.printf("Scan complete: %d networks\n", n);
      for (int i = 0; i < n && i < 5; i++) {
        Serial.printf("  %d) %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
      }
      WiFi.scanDelete();
    } else if (n == WIFI_SCAN_FAILED) {
      if (millis() - scanStartMs > SCAN_TIMEOUT_MS) {
        lastScanCount = 0;
        lastScanJson = "[]";
        lastScanMs = millis();
        scanInProgress = false;
        Serial.println("Scan failed (timeout)");
        WiFi.scanDelete();
      }
    }
  }
}
