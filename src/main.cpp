/*
  Example from WiFi > WiFiScan
  Complete details at https://RandomNerdTutorials.com/esp32-useful-wi-fi-functions-arduino/
*/

#include "WiFi.h"
#include "Transit.h"
#include "string.h"
#include <Arduino.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <PubSubClient.h>
#include <Preferences.h>   // ADDED
#include <Adafruit_Protomatter.h>

constexpr uint16_t PANEL_WIDTH = 64;
constexpr uint8_t PANEL_CHAIN_LENGTH = 2;  // number of chained 64x32 panels
constexpr uint16_t MATRIX_WIDTH = PANEL_WIDTH * PANEL_CHAIN_LENGTH;

// LED MATRIX PINS (MatrixPortal S3)
uint8_t rgbPins[]  = {42, 40, 41, 38, 37, 39}; // R1,B1,G1,R2,B2,G2
uint8_t addrPins[] = {45, 36, 48, 35, 21};     // A,B,C,D,E

uint8_t clockPin = 2;
uint8_t latchPin = 47;
uint8_t oePin    = 14;

// MATRIX OBJECT
Adafruit_Protomatter matrix(
  MATRIX_WIDTH,           // Total width (64px panel * 2 panels)
  6,                      // Bit depth; 6 is library default for ESP32
  PANEL_CHAIN_LENGTH,     // Number of daisy-chained panels
  rgbPins,
  sizeof(addrPins),       // Number of address pins (5 for 64-row panels)
  addrPins,
  clockPin,
  latchPin,
  oePin,
  true                    // Double-buffer for smoother updates
);

WebServer server(80);

const char* ESP_ssid = "nikul-ESP32-AP";
const char* ESP_password = "12345678";

const char* MQTT_HOST = "198.211.104.174";
const int MQTT_PORT = 1883;

const char* MQTT_USER = "commutex";
const char* MQTT_PASS = "lebron";

// initialize the MQTT client with the WiFi client
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Preferences storage
Preferences prefs;

// Global variable to store the device ID
String deviceId;

// Forward declarations
bool connect_ESP_to_mqtt();
void mqtt_publish_online();

// starts the ESP32 in Access Point mode with the specified SSID and password
boolean start_ESP_wifi() {

  Serial.println("[ESP] Starting ESP32 WIFI");

  WiFi.mode(WIFI_AP_STA);

  bool success = WiFi.softAP(ESP_ssid, ESP_password);

  if (success) {
      Serial.println("[ESP] ESP WiFi started!");
  } else {
      Serial.println("[ESP] Failed to connect to ESP WiFi");
  }

  return success;
}


// NEW — save WiFi credentials
void save_wifi_credentials(String ssid, String password, String user = "") {

    prefs.begin("wifi", false);

    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.putString("user", user);
    prefs.end();
    Serial.println("[ESP] WiFi credentials saved");
}

// NEW — load WiFi credentials
bool load_wifi_credentials(String &ssid, String &password, String &user) {

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

// checks if the given username and password are correct
boolean connect_to_wifi(const char* ssid, const char* password, const char* username = "") {

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

// function to convert device ID to string
String get_device_id() {

    uint64_t chipid = ESP.getEfuseMac();

    char id[32];

    sprintf(id, "esp32-%04X%08X",
        (uint16_t)(chipid >> 32),
        (uint32_t)chipid);

    return String(id);
}


// API function that returns device info
void get_device_info() {

    String response = "{";
    response += "\"deviceId\":\"" + deviceId + "\"";
    response += "}";

    server.send(200, "application/json", response);

    Serial.println("[ESP] Sent deviceId to phone");
}

// Perform fresh scan
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

// provisioning endpoint
void phone_to_ESP_connection() {

  if (!server.hasArg("ssid") || !server.hasArg("password")) {

      server.send(400, "application/json",
      "{\"error\":\"Missing ssid or password\"}");

      return;
  }

  String HOME_ssid = server.arg("ssid");
  String HOME_password = server.arg("password");
  String HOME_user = server.hasArg("user") ? server.arg("user") : "";

  WiFi.mode(WIFI_AP_STA);

  int n = fresh_scan_networks();

  if (n == 0) {

      Serial.println("no networks found");

      server.send(400, "application/json",
      "{\"error\":\"No Eligible WiFi networks found\"}");

  } else {

      Serial.print(n);
      Serial.println(" networks found");

      for (int i = 0; i < n; ++i) {

        if (strcmp(WiFi.SSID(i).c_str(), HOME_ssid.c_str()) == 0) {

          Serial.println("[ESP] Found target WiFi network!");

          if (connect_to_wifi(
              HOME_ssid.c_str(),
              HOME_password.c_str(),
              HOME_user.c_str())) {

            Serial.println("[ESP] Successfully connected to WiFi!");

            // NEW — SAVE CREDENTIALS
            save_wifi_credentials(HOME_ssid, HOME_password, HOME_user);

            // connect to MQTT broker
            if (connect_ESP_to_mqtt()) {

                mqtt_publish_online();

            } else {

                Serial.println("[MQTT] Failed to connect");

                server.send(400,
                "application/json",
                "{\"error\":\"MQTT failed\"}");
            }

            return;

          } else {

            Serial.println("[ESP] Failed to connect to WiFi.");

            server.send(400,
            "application/json",
            "{\"error\":\"WiFi credentials wrong\"}");
          }

          break;
        }

        delay(10);
      }

      server.send(400,
      "application/json",
      "{\"error\":\"Target WiFi network not found\"}");
  }
}

// MQTT connect
bool connect_ESP_to_mqtt() {

    mqtt.setServer(MQTT_HOST, MQTT_PORT);

    Serial.println("[MQTT] Connecting...");

    if (mqtt.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {

        Serial.println("[MQTT] Connected");

        String topic =
        "devices/" + deviceId + "/commands";

        mqtt.subscribe(topic.c_str());

        Serial.println("[MQTT] Subscribed to commands");

        return true;
    }

    Serial.print("[MQTT] Failed rc=");
    Serial.println(mqtt.state());

    return false;
}


// MQTT publish online
void mqtt_publish_online() {

    String topic =
    "devices/" + deviceId + "/status";

    mqtt.publish(topic.c_str(), "online", true);

    Serial.println("[MQTT] Published online");
}


void setup() {

  Serial.begin(115200);

  deviceId = get_device_id();

  Serial.print("[ESP] Device ID: ");
  Serial.println(deviceId);

  WiFi.mode(WIFI_AP_STA);

  WiFi.disconnect();

  delay(100);


  // NEW — TRY AUTO CONNECT FIRST
  String savedSSID;
  String savedPassword;
  String savedUser;

  if (load_wifi_credentials(savedSSID, savedPassword, savedUser)) {

      Serial.println("[ESP] Connecting to saved WiFi");

      if (connect_to_wifi(
          savedSSID.c_str(),
          savedPassword.c_str(),
          savedUser.c_str())) {

          Serial.println("[ESP] Connected using saved credentials");

          if (connect_ESP_to_mqtt()) {

              mqtt_publish_online();
          }

      } else {

          Serial.println("[ESP] Saved WiFi failed, starting AP");

          start_ESP_wifi();
      }

  } else {

      start_ESP_wifi();
  }

  server.on("/connect",
  HTTP_POST,
  phone_to_ESP_connection);

  server.on("/device-info",
  HTTP_GET,
  get_device_info);

  server.on("/heartbeat",
  HTTP_GET,
  []() {
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();

  Serial.println("[ESP] Setup done");
}


void loop() {

  bool isSomeoneConnected =
  WiFi.softAPgetStationNum() > 0;

  if (isSomeoneConnected) {

    Serial.println("[ESP] A device is connected to ESP WiFi!");}

  // Reconnect to MQTT if Wi-Fi is up but MQTT dropped.
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) {
      connect_ESP_to_mqtt();
  }

  if (mqtt.connected()) {

      mqtt.loop();
  }

  server.handleClient();

  Serial.print("[ESP] looping.... deviceId=");
  Serial.println(deviceId);

  delay(50);
}
