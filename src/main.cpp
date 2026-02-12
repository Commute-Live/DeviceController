/*
  Example from WiFi > WiFiScan
  Complete details at https://RandomNerdTutorials.com/esp32-useful-wi-fi-functions-arduino/
*/

#include "WiFi.h"
#include "Transit.h"
#include "transit/nyc_subway_catalog.h"
#include "string.h"
#include <Arduino.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ctype.h>
#include <time.h>
#include "color.h"

constexpr uint16_t PANEL_WIDTH = 64;
constexpr uint8_t PANEL_CHAIN_LENGTH = 2;  // number of chained 64x32 panels

// LED MATRIX PINS (MatrixPortal S3)
#define R1_PIN 42
#define G1_PIN 40
#define B1_PIN 41
#define R2_PIN 38
#define G2_PIN 37
#define B2_PIN 39
#define A_PIN  45
#define B_PIN  36
#define C_PIN  48
#define D_PIN  35
#define E_PIN  21
#define LAT_PIN 47
#define OE_PIN  14
#define CLK_PIN 2

// MATRIX OBJECT
MatrixPanel_I2S_DMA *matrix = nullptr;

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
String currentRouteId = transit::nyc_subway::default_line().id;
String lastRenderedRouteId = "";
String currentInfoLine1 = "N1 --";
String currentInfoLine2 = "N2 --";
String currentInfoLine3 = "N3 --";

// Forward declarations
bool connect_ESP_to_mqtt();
void mqtt_publish_online();
void mqtt_callback(char *topic, byte *payload, unsigned int length);
const transit::LineDefinition *parse_route_command(const String &message);
void render_route_logo(const String &route_id);

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
    mqtt.setCallback(mqtt_callback);

    Serial.println("[MQTT] Connecting...");

    if (mqtt.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {

        Serial.println("[MQTT] Connected");

        String deviceTopic = "/device/" + deviceId + "/commands";
        mqtt.subscribe(deviceTopic.c_str());
        Serial.printf("[MQTT] Subscribed to: %s\n", deviceTopic.c_str());

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

const transit::LineDefinition *parse_route_command(const String &message) {
    String trimmed = message;
    trimmed.trim();
    if (trimmed.length() >= 2 &&
        ((trimmed[0] == '"' && trimmed[trimmed.length() - 1] == '"') ||
         (trimmed[0] == '\'' && trimmed[trimmed.length() - 1] == '\''))) {
        trimmed = trimmed.substring(1, trimmed.length() - 1);
    }

    if (const transit::LineDefinition *line = transit::nyc_subway::find_line(trimmed)) {
        return line;
    }

    if (const transit::LineDefinition *line = transit::nyc_subway::parse_line_from_message(message)) {
        return line;
    }

    // Handle double-encoded JSON payloads like:
    // "{\"provider\":\"mta\",\"line\":\"E\",...}"
    String unescaped = message;
    unescaped.replace("\\\"", "\"");
    unescaped.replace("\\\\", "\\");
    if (unescaped != message) {
        if (const transit::LineDefinition *line = transit::nyc_subway::parse_line_from_message(unescaped)) {
            return line;
        }
    }

    return nullptr;
}

static String extract_json_string_field(const String &json, const char *field) {
    String key = "\"";
    key += field;
    key += "\"";

    int keyPos = json.indexOf(key);
    if (keyPos < 0) return "";

    int colonPos = json.indexOf(':', keyPos + key.length());
    if (colonPos < 0) return "";

    int i = colonPos + 1;
    while (i < (int)json.length() && (json[i] == ' ' || json[i] == '\t')) i++;
    if (i >= (int)json.length()) return "";

    if (json[i] == '"') {
        int end = json.indexOf('"', i + 1);
        if (end < 0) return "";
        return json.substring(i + 1, end);
    }

    int end = i;
    while (end < (int)json.length()) {
        char c = json[end];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) break;
        end++;
    }

    if (end <= i) return "";
    return json.substring(i, end);
}

static bool parse_iso8601(const String &iso, time_t &out) {
    int y, mo, d, h, mi, s;
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) {
        return false;
    }
    struct tm tmv = {};
    tmv.tm_year = y - 1900;
    tmv.tm_mon = mo - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = h;
    tmv.tm_min = mi;
    tmv.tm_sec = s;
    tmv.tm_isdst = -1;
    out = mktime(&tmv);
    return out != (time_t)-1;
}

static int extract_next_arrival_list(const String &json, String outArrivals[], int maxCount) {
    int arrPos = json.indexOf("\"nextArrivals\"");
    if (arrPos < 0 || maxCount <= 0) return 0;

    int count = 0;
    int pos = arrPos;
    while (count < maxCount) {
        int keyPos = json.indexOf("\"arrivalTime\":\"", pos);
        if (keyPos < 0) break;
        int valueStart = keyPos + strlen("\"arrivalTime\":\"");
        int valueEnd = json.indexOf('"', valueStart);
        if (valueEnd < 0) break;
        outArrivals[count++] = json.substring(valueStart, valueEnd);
        pos = valueEnd + 1;
    }
    return count;
}

static String eta_label_for_arrival(const String &arrivalIso, time_t fetchedTs) {
    time_t arrivalTs;
    if (parse_iso8601(arrivalIso, arrivalTs)) {
        long diffSec = (long)difftime(arrivalTs, fetchedTs);
        if (diffSec < 0) diffSec = 0;
        long mins = (diffSec + 59) / 60;
        if (mins == 0) return "NOW";
        return String(mins) + "m";
    }
    if (arrivalIso.length() >= 16) {
        return arrivalIso.substring(11, 16);
    }
    return "--";
}

static void build_eta_lines(const String &message, String &line1, String &line2, String &line3) {
    line1 = "N1 --";
    line2 = "N2 --";
    line3 = "N3 --";

    String fetchedAt = extract_json_string_field(message, "fetchedAt");
    time_t fetchedTs = 0;
    bool hasFetchedTs = fetchedAt.length() > 0 && parse_iso8601(fetchedAt, fetchedTs);

    String arrivals[3];
    int n = extract_next_arrival_list(message, arrivals, 3);
    if (n <= 0) return;

    String labels[3];
    for (int i = 0; i < n; i++) {
        if (hasFetchedTs) {
            labels[i] = eta_label_for_arrival(arrivals[i], fetchedTs);
        } else if (arrivals[i].length() >= 16) {
            labels[i] = arrivals[i].substring(11, 16);
        } else {
            labels[i] = "--";
        }
    }

    if (n >= 1) line1 = "N1 " + labels[0];
    if (n >= 2) line2 = "N2 " + labels[1];
    if (n >= 3) line3 = "N3 " + labels[2];
}

void render_route_logo(const String &route_id) {
    const transit::LineDefinition *line = transit::nyc_subway::find_line(route_id);
    if (!line) {
        line = &transit::nyc_subway::default_line();
    }
    draw_transit_logo_large(line->symbol, line->color_hex);

    matrix->setTextWrap(false);
    matrix->setTextColor(color_from_name("white", 40));
    matrix->setTextSize(1);

    const int info_x = 34;
    matrix->setCursor(info_x, 2);
    matrix->print(currentInfoLine1);
    matrix->setCursor(info_x, 12);
    matrix->print(currentInfoLine2);
    matrix->setCursor(info_x, 22);
    matrix->print(currentInfoLine3);

    // Keep a guaranteed 1-pixel left gutter at x=0.
    matrix->drawFastVLine(0, 0, matrix->height(), 0);
}

void mqtt_callback(char *topic, byte *payload, unsigned int length) {
    String message;
    message.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.printf("[MQTT] Message on %s: %s\n", topic, message.c_str());
    String provider = extract_json_string_field(message, "provider");
    String lineRaw = extract_json_string_field(message, "line");
    String stop = extract_json_string_field(message, "stop");
    String stopId = extract_json_string_field(message, "stopId");
    String direction = extract_json_string_field(message, "direction");
    if (stop.length() > 0) {
        currentInfoLine1 = stop;
    } else if (stopId.length() > 0) {
        currentInfoLine1 = stopId;
    } else {
        currentInfoLine1 = "STOP --";
    }
    currentInfoLine2 = "";
    currentInfoLine3 = "";
    if (lineRaw.length() > 0 || provider.length() > 0 || stop.length() > 0 || stopId.length() > 0 || direction.length() > 0) {
        Serial.printf("[MQTT] Refresh payload provider=%s line=%s stop=%s stopId=%s direction=%s\n",
                      provider.length() ? provider.c_str() : "-",
                      lineRaw.length() ? lineRaw.c_str() : "-",
                      stop.length() ? stop.c_str() : "-",
                      stopId.length() ? stopId.c_str() : "-",
                      direction.length() ? direction.c_str() : "-");
    }

    const transit::LineDefinition *line = parse_route_command(message);
    if (!line) {
        Serial.println("[MQTT] Ignored command: missing/invalid route");
        return;
    }

    currentRouteId = line->id;
    render_route_logo(currentRouteId);
    Serial.printf("[MQTT] Rendered route logo: %s (%c)\n", line->id, line->symbol);
}


void setup() {

  Serial.begin(115200);
  mqtt.setBufferSize(4096);
  mqtt.setKeepAlive(30);

  // Configure matrix pins
  HUB75_I2S_CFG::i2s_pins _pins={
    R1_PIN, G1_PIN, B1_PIN, 
    R2_PIN, G2_PIN, B2_PIN, 
    A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, 
    LAT_PIN, OE_PIN, CLK_PIN
  };

  // Configure matrix
  HUB75_I2S_CFG mxconfig(
    64,                    // Panel width
    32,                    // Panel height
    PANEL_CHAIN_LENGTH,    // Chain length
    _pins                  // Pin mapping
  );

  // Create and initialize matrix
  matrix = new MatrixPanel_I2S_DMA(mxconfig);
  
  if(!matrix->begin()) {
    Serial.println("Matrix initialization failed!");
    while(1);
  }
  
  matrix->setBrightness8(255);  // brighten panels (0-255)
  matrix->clearScreen();
  matrix->setTextWrap(true);
  matrix->setTextSize(1);

  // Draw initial logo immediately
  render_route_logo(currentRouteId);
  lastRenderedRouteId = currentRouteId;

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

  if (lastRenderedRouteId != currentRouteId) {
      render_route_logo(currentRouteId);
      lastRenderedRouteId = currentRouteId;
  }

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

  delay(50);
}
