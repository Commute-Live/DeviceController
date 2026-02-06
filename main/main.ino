#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <Adafruit_Protomatter.h>
#include <Adafruit_GFX.h>

// ================= LED MATRIX PINS (MatrixPortal S3) =================
uint8_t rgbPins[]  = {42, 41, 40, 38, 39, 37}; // R1,G1,B1,R2,G2,B2
uint8_t addrPins[] = {45, 36, 48, 35, 21};     // A,B,C,D,E

uint8_t clockPin = 2;
uint8_t latchPin = 47;
uint8_t oePin    = 14;

// ================= MATRIX OBJECT (CORRECT SIGNATURE) =================
Adafruit_Protomatter matrix(
  64,          // matrix width
  6,           // bit depth
  6,           // number of RGB pins
  rgbPins,     // RGB pin list
  5,           // number of address pins
  addrPins,    // address pin list
  clockPin,
  latchPin,
  oePin,
  true         // double buffering
);

// ================= WIFI =================
const char* AP_SSID = "MATRIXPORTAL-SETUP-shimu";
const char* AP_PASS = "12345678";

AsyncWebServer server(80);

// ================= WIFI STATE =================
String lastError = "";
bool isConnecting = false;

// ================= DRAW TEXT FUNCTION =================
void drawText(const String& text) {
  matrix.fillScreen(0);
  matrix.setCursor(0, 0);
  matrix.setTextColor(matrix.color565(0, 60, 0));
  matrix.print(text);
  matrix.show();
}

String wifiDisconnectReason(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "Authentication failed (check password)";
    case WIFI_REASON_NO_AP_FOUND:
      return "Network not found";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_ASSOC_LEAVE:
      return "Association failed";
    default:
      return "Disconnected from Wi-Fi";
  }
}

void setup() {
  Serial.begin(115200);

  // ---- Start LED matrix ----
  if (matrix.begin() != PROTOMATTER_OK) {
    while (1); // halt if matrix fails
  }

  matrix.setTextWrap(true);
  matrix.setTextSize(1);

  // ---- Start filesystem ----
  if (!LittleFS.begin(true)) {
    drawText("FS FAIL");
    while (1);
  }

  // ---- Start Wi-Fi AP ----
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();

  // ---- Show setup instructions on LED ----
  drawText("CONNECT WIFI\n192.168.4.1");

  // ---- Wi-Fi events ----
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      isConnecting = false;
      lastError = "";
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      isConnecting = false;
      lastError = wifiDisconnectReason(info.wifi_sta_disconnected.reason);
    } else if (event == ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE) {
      lastError = "Auth mode changed";
    }
  });

  // ---- Static files ----
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // ---- Scan Wi-Fi ----
  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      String ssid = WiFi.SSID(i);
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
    request->send(200, "application/json", json);
  });

  // ---- Status ----
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

  // ---- Connect ----
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
      // WPA2-Enterprise (e.g., campus Wi-Fi)
      esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)user.c_str(), user.length());
      esp_wifi_sta_wpa2_ent_set_username((uint8_t*)user.c_str(), user.length());
      if (pass.length() > 0) {
        esp_wifi_sta_wpa2_ent_set_password((uint8_t*)pass.c_str(), pass.length());
      }
      esp_wifi_sta_wpa2_ent_enable();
      WiFi.begin(ssid.c_str());
    } else {
      WiFi.begin(ssid.c_str(), pass.c_str());
    }

    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();
}

void loop() {
  // nothing needed
}
