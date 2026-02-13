/*
  Example from WiFi > WiFiScan
  Complete details at https://RandomNerdTutorials.com/esp32-useful-wi-fi-functions-arduino/
*/

#include "WiFi.h"
#include "display/Transit.h"
#include "parsing/payload_parser.h"
#include "network/retry_backoff.h"
#include "network/wifi_manager.h"
#include "device/led_config.h"
#include "transit/registry.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "Using include/secrets.example.h defaults. Create include/secrets.h for real credentials."
#endif
#include "string.h"
#include <Arduino.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <time.h>
#include "transit/providers/nyc/subway/colors.h"

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

const char* ESP_ssid = COMMUTELIVE_AP_SSID;
const char* ESP_password = COMMUTELIVE_AP_PASSWORD;

const char* MQTT_HOST = COMMUTELIVE_MQTT_HOST;
const int MQTT_PORT = COMMUTELIVE_MQTT_PORT;

const char* MQTT_USER = COMMUTELIVE_MQTT_USER;
const char* MQTT_PASS = COMMUTELIVE_MQTT_PASS;

// initialize the MQTT client with the WiFi client
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Global variable to store the device ID
String deviceId;
String currentRouteId = transit::registry::default_line().id;
String lastRenderedRouteId = "";
String currentRow1RouteId = transit::registry::default_line().id;
String currentRow2RouteId = "";
String currentRow1Label = "";
String currentRow2Label = "";
String currentRow1Eta = "--";
String currentRow2Eta = "--";
bool timeSynced = false;
unsigned long lastTimeSyncAttemptMs = 0;
String savedWifiSsid;
String savedWifiPassword;
String savedWifiUser;
bool hasSavedWifiCredentials = false;
RetrySchedule wifiRetry;
RetrySchedule mqttRetry;
device::LedConfig ledConfig = device::defaults();
bool rebootScheduled = false;
unsigned long rebootAtMs = 0;
bool setupModeActive = false;
bool hasTransitData = false;
String lastStatusSignature = "";

constexpr uint32_t WIFI_RETRY_BASE_MS = 1000;
constexpr uint32_t WIFI_RETRY_MAX_MS = 60000;
constexpr uint32_t MQTT_RETRY_BASE_MS = 1000;
constexpr uint32_t MQTT_RETRY_MAX_MS = 60000;
constexpr uint32_t RETRY_JITTER_MS = 750;

// Forward declarations
bool connect_ESP_to_mqtt();
void mqtt_publish_online();
void mqtt_callback(char *topic, byte *payload, unsigned int length);
const transit::LineDefinition *parse_route_command(const String &message);
void render_route_logo(const String &route_id);
void render_brand_status(const String &status, const String &detail = "");
void get_device_config();
void set_device_config();

static void draw_centered_text(int y, const String &text, uint16_t color) {
    matrix->setTextWrap(false);
    matrix->setTextSize(1);
    matrix->setTextColor(color);
    int16_t x1, y1;
    uint16_t w, h;
    matrix->getTextBounds(text.c_str(), 0, 0, &x1, &y1, &w, &h);
    int16_t x = (matrix->width() - static_cast<int16_t>(w)) / 2;
    if (x < 0) x = 0;
    matrix->setCursor(x, y);
    matrix->print(text);
}

static bool sync_time_utc(uint32_t timeoutMs = 10000) {
    setenv("TZ", "UTC0", 1);
    tzset();
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

    time_t now = 0;
    unsigned long start = millis();
    while (now < 1700000000 && (millis() - start) < timeoutMs) {
        delay(200);
        time(&now);
    }

    if (now >= 1700000000) {
        timeSynced = true;
        Serial.printf("[TIME] Synced UTC epoch=%ld\n", (long)now);
        return true;
    }

    Serial.println("[TIME] NTP sync timeout");
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

void get_device_config() {
  String response = "{";
  response += "\"deviceId\":\"" + deviceId + "\",";
  response += "\"config\":";
  response += device::to_json(ledConfig);
  response += "}";
  server.send(200, "application/json", response);
}

void set_device_config() {
  device::LedConfig next = ledConfig;
  String error;
  if (!device::apply_update_from_request(server, next, error)) {
    String errJson = "{\"error\":\"" + error + "\"}";
    server.send(400, "application/json", errJson);
    return;
  }

  const bool geometryChanged = next.panelWidth != ledConfig.panelWidth ||
                               next.panelHeight != ledConfig.panelHeight ||
                               next.chainLength != ledConfig.chainLength ||
                               next.serpentine != ledConfig.serpentine;

  ledConfig = next;
  device::save(ledConfig);

  if (matrix) {
    matrix->setBrightness8(ledConfig.brightness);
  }

  String response = "{";
  response += "\"ok\":true,";
  response += "\"config\":";
  response += device::to_json(ledConfig);
  response += ",\"restartRequired\":";
  response += geometryChanged ? "true" : "false";
  response += "}";
  server.send(200, "application/json", response);

  if (geometryChanged) {
    rebootScheduled = true;
    rebootAtMs = millis() + 800;
  }
}

void render_brand_status(const String &status, const String &detail) {
  matrix->fillScreen(0);
  draw_centered_text(2, "Commute Live", transit::providers::nyc::subway::color_from_hex("#5CE1E6", 120));
  draw_centered_text(matrix->height() / 2 - 3, status, transit::providers::nyc::subway::color_from_name("white", 40));
  if (detail.length() > 0) {
    draw_centered_text(matrix->height() - 9, detail, transit::providers::nyc::subway::color_from_name("gray", 40));
  }
}

// provisioning endpoint
void phone_to_ESP_connection() {
  String connectedSsid;
  String connectedPassword;
  String connectedUser;
  if (!wifi_manager::handle_connect_request(server, connectedSsid, connectedPassword, connectedUser)) {
      return;
  }

  hasSavedWifiCredentials = true;
  setupModeActive = false;
  savedWifiSsid = connectedSsid;
  savedWifiPassword = connectedPassword;
  savedWifiUser = connectedUser;
  reset_retry(wifiRetry, millis());
  sync_time_utc();

  if (connect_ESP_to_mqtt()) {
      reset_retry(mqttRetry, millis());
      mqtt_publish_online();
  } else {
      Serial.println("[MQTT] Failed to connect");
      uint32_t waitMs = schedule_next_retry(
          mqttRetry, millis(), MQTT_RETRY_BASE_MS, MQTT_RETRY_MAX_MS, RETRY_JITTER_MS);
      Serial.printf("[MQTT] Retry scheduled in %lu ms (attempt %u)\n",
                    (unsigned long)waitMs, mqttRetry.attempt);
      server.send(400, "application/json", "{\"error\":\"MQTT failed\"}");
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

    if (const transit::LineDefinition *line = transit::registry::find_line(trimmed)) {
        return line;
    }

    if (const transit::LineDefinition *line = transit::registry::parse_line_from_message(message)) {
        return line;
    }

    // Handle double-encoded JSON payloads like:
    // "{\"provider\":\"mta\",\"line\":\"E\",...}"
    String unescaped = message;
    unescaped.replace("\\\"", "\"");
    unescaped.replace("\\\\", "\\");
    if (unescaped != message) {
        if (const transit::LineDefinition *line = transit::registry::parse_line_from_message(unescaped)) {
            return line;
        }
    }

    return nullptr;
}

static void draw_row_with_logo(const String &routeId, const String &labelText, const String &etaText, int centerY) {
    const transit::LineDefinition *line = transit::registry::find_line(routeId);
    if (!line) {
        line = &transit::registry::default_line();
    }

    draw_transit_logo(
        8,
        centerY,
        line->symbol,
        line->color_hex,
        6,
        20,
        1,
        false,
        "white",
        40);

    matrix->setTextWrap(false);
    matrix->setTextColor(transit::providers::nyc::subway::color_from_name("white", 40));
    matrix->setTextSize(1);
    matrix->setCursor(16, centerY - 3);
    String eta = etaText.length() ? etaText : "--";
    String label = labelText;
    label.trim();
    if (label.length() == 0) label = line->id;

    const int maxChars = 17;
    const int reserve = eta.length() + 1;  // trailing space + eta
    int labelChars = maxChars - reserve;
    if (labelChars < 3) labelChars = 3;
    if ((int)label.length() > labelChars) {
        label = label.substring(0, labelChars - 1) + ".";
    }

    String rowText = label + " " + eta;
    matrix->print(rowText);
}

void render_route_logo(const String &route_id) {
    (void)route_id;
    matrix->fillScreen(0);
    const int firstRowY = matrix->height() / 4;
    const int secondRowY = (matrix->height() * 3) / 4;
    draw_row_with_logo(currentRow1RouteId, currentRow1Label, currentRow1Eta, firstRowY);
    if (currentRow2RouteId.length() > 0) {
        draw_row_with_logo(currentRow2RouteId, currentRow2Label, currentRow2Eta, secondRowY);
    }
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
    String directionLabel = extract_json_string_field(message, "directionLabel");
    String stopLabel = extract_json_string_field(message, "stop");
    String multiPrimaryLine;
    String multiRow1Label;
    String multiRow1Eta;
    String multiRow2Line;
    String multiRow2Label;
    String multiRow2Eta;
    bool hasMultiLines = parse_lines_payload(
        message, multiPrimaryLine, multiRow1Label, multiRow1Eta, multiRow2Line, multiRow2Label, multiRow2Eta);

    if (hasMultiLines) {
        currentRow1RouteId = multiPrimaryLine.length() ? multiPrimaryLine : transit::registry::default_line().id;
        currentRow1Label = multiRow1Label.length() ? multiRow1Label : currentRow1RouteId;
        currentRow1Eta = multiRow1Eta.length() ? multiRow1Eta : "--";
        currentRow2RouteId = multiRow2Line;
        currentRow2Label = multiRow2Label.length() ? multiRow2Label : currentRow2RouteId;
        currentRow2Eta = multiRow2Eta.length() ? multiRow2Eta : "--";
    } else {
        String etaLine1, etaLine2, etaLine3;
        build_eta_lines(message, etaLine1, etaLine2, etaLine3);

        auto etaValue = [](const String &l) -> String {
            int spacePos = l.indexOf(' ');
            if (spacePos >= 0 && spacePos + 1 < (int)l.length()) {
                return l.substring(spacePos + 1);
            }
            return l;
        };

        String e1 = etaValue(etaLine1);
        String e2 = etaValue(etaLine2);
        String e3 = etaValue(etaLine3);
        String arrivalsCompact = "--";
        if (e1 != "--") arrivalsCompact = e1;
        else if (e2 != "--") arrivalsCompact = e2;
        else if (e3 != "--") arrivalsCompact = e3;

        String fallbackLine = lineRaw.length() ? lineRaw : "";
        if (fallbackLine.length() == 0) {
            const transit::LineDefinition *parsed = parse_route_command(message);
            if (parsed) fallbackLine = parsed->id;
        }
        currentRow1RouteId = fallbackLine.length() ? fallbackLine : transit::registry::default_line().id;
        if (directionLabel.length() > 0) {
            currentRow1Label = directionLabel;
        } else if (stopLabel.length() > 0) {
            currentRow1Label = stopLabel;
        } else if (direction == "N") {
            currentRow1Label = "Uptown";
        } else if (direction == "S") {
            currentRow1Label = "Downtown";
        } else {
            currentRow1Label = currentRow1RouteId;
        }
        currentRow1Eta = arrivalsCompact;
        currentRow2RouteId = "";
        currentRow2Label = "";
        currentRow2Eta = "--";
    }
    if (lineRaw.length() > 0 || provider.length() > 0 || stop.length() > 0 || stopId.length() > 0 || direction.length() > 0) {
        Serial.printf("[MQTT] Refresh payload provider=%s line=%s stop=%s stopId=%s direction=%s\n",
                      provider.length() ? provider.c_str() : "-",
                      lineRaw.length() ? lineRaw.c_str() : "-",
                      stop.length() ? stop.c_str() : "-",
                      stopId.length() ? stopId.c_str() : "-",
                      direction.length() ? direction.c_str() : "-");
    }

    const transit::LineDefinition *line = nullptr;
    if (hasMultiLines && currentRow1RouteId.length() > 0) {
        line = transit::registry::find_line(currentRow1RouteId);
    }
    if (!line) {
        line = parse_route_command(message);
    }
    if (!line) {
        Serial.println("[MQTT] Ignored command: missing/invalid route");
        return;
    }

    currentRouteId = line->id;
    hasTransitData = true;
    render_route_logo(currentRouteId);
    Serial.printf("[MQTT] Rendered route logo: %s (%c)\n", line->id, line->symbol);
}


void setup() {

  Serial.begin(115200);
  device::load(ledConfig);
  randomSeed(esp_random());
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
    ledConfig.panelWidth,   // Panel width
    ledConfig.panelHeight,  // Panel height
    ledConfig.chainLength,  // Chain length
    _pins                  // Pin mapping
  );

  // Create and initialize matrix
  matrix = new MatrixPanel_I2S_DMA(mxconfig);
  
  if(!matrix->begin()) {
    Serial.println("Matrix initialization failed!");
    while(1);
  }
  
  matrix->setBrightness8(ledConfig.brightness);
  matrix->clearScreen();
  matrix->setTextWrap(true);
  matrix->setTextSize(1);

  render_brand_status("BOOTING", "Starting device");

  deviceId = get_device_id();

  Serial.print("[ESP] Device ID: ");
  Serial.println(deviceId);

  WiFi.mode(WIFI_AP_STA);

  WiFi.disconnect();

  delay(100);


  // NEW — TRY AUTO CONNECT FIRST
  if (wifi_manager::load_credentials(savedWifiSsid, savedWifiPassword, savedWifiUser)) {
      hasSavedWifiCredentials = true;

      Serial.println("[ESP] Connecting to saved WiFi");

      if (wifi_manager::connect_station(
          savedWifiSsid.c_str(),
          savedWifiPassword.c_str(),
          savedWifiUser.c_str())) {

          Serial.println("[ESP] Connected using saved credentials");
          setupModeActive = false;
          reset_retry(wifiRetry, millis());
          sync_time_utc();

          if (connect_ESP_to_mqtt()) {
              reset_retry(mqttRetry, millis());
              mqtt_publish_online();
          } else {
              uint32_t waitMs = schedule_next_retry(
                  mqttRetry, millis(), MQTT_RETRY_BASE_MS, MQTT_RETRY_MAX_MS, RETRY_JITTER_MS);
              Serial.printf("[MQTT] Retry scheduled in %lu ms (attempt %u)\n",
                            (unsigned long)waitMs, mqttRetry.attempt);
          }

      } else {

          Serial.println("[ESP] Saved WiFi failed, starting AP");
          uint32_t waitMs = schedule_next_retry(
              wifiRetry, millis(), WIFI_RETRY_BASE_MS, WIFI_RETRY_MAX_MS, RETRY_JITTER_MS);
          Serial.printf("[WIFI] Retry scheduled in %lu ms (attempt %u)\n",
                        (unsigned long)waitMs, wifiRetry.attempt);

          wifi_manager::start_ap(ESP_ssid, ESP_password);
          setupModeActive = true;
      }

  } else {
      hasSavedWifiCredentials = false;
      wifi_manager::start_ap(ESP_ssid, ESP_password);
      setupModeActive = true;
  }

  server.on("/connect",
  HTTP_POST,
  phone_to_ESP_connection);

  server.on("/device-info",
  HTTP_GET,
  get_device_info);

  server.on("/device-config",
  HTTP_GET,
  get_device_config);

  server.on("/device-config",
  HTTP_POST,
  set_device_config);

  server.on("/heartbeat",
  HTTP_GET,
  []() {
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();

  Serial.println("[ESP] Setup done");
}


void loop() {

  unsigned long nowMs = millis();
  bool isSomeoneConnected =
  WiFi.softAPgetStationNum() > 0;

  if (isSomeoneConnected) {

    Serial.println("[ESP] A device is connected to ESP WiFi!");}

  // Retry Wi-Fi with exponential backoff + jitter.
  if (WiFi.status() != WL_CONNECTED && hasSavedWifiCredentials) {
      if (retry_due(nowMs, wifiRetry.nextAtMs)) {
          Serial.println("[WIFI] Attempting reconnect");
          if (wifi_manager::connect_station(savedWifiSsid.c_str(), savedWifiPassword.c_str(), savedWifiUser.c_str())) {
              Serial.println("[WIFI] Reconnected");
              setupModeActive = false;
              reset_retry(wifiRetry, nowMs);
              timeSynced = false;
              lastTimeSyncAttemptMs = 0;
              reset_retry(mqttRetry, nowMs);
          } else {
              uint32_t waitMs = schedule_next_retry(
                  wifiRetry, nowMs, WIFI_RETRY_BASE_MS, WIFI_RETRY_MAX_MS, RETRY_JITTER_MS);
              Serial.printf("[WIFI] Retry scheduled in %lu ms (attempt %u)\n",
                            (unsigned long)waitMs, wifiRetry.attempt);
          }
      }
  } else if (WiFi.status() == WL_CONNECTED) {
      wifiRetry.attempt = 0;
  }

  // Retry MQTT with exponential backoff + jitter.
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) {
      if (retry_due(nowMs, mqttRetry.nextAtMs)) {
          Serial.println("[MQTT] Attempting reconnect");
          if (connect_ESP_to_mqtt()) {
              reset_retry(mqttRetry, nowMs);
              mqtt_publish_online();
          } else {
              uint32_t waitMs = schedule_next_retry(
                  mqttRetry, nowMs, MQTT_RETRY_BASE_MS, MQTT_RETRY_MAX_MS, RETRY_JITTER_MS);
              Serial.printf("[MQTT] Retry scheduled in %lu ms (attempt %u)\n",
                            (unsigned long)waitMs, mqttRetry.attempt);
          }
      }
  } else if (mqtt.connected()) {
      mqttRetry.attempt = 0;
  }

  if (WiFi.status() == WL_CONNECTED && !timeSynced) {
      if (nowMs - lastTimeSyncAttemptMs >= 30000) {
          lastTimeSyncAttemptMs = nowMs;
          sync_time_utc(3000);
      }
  }

  if (mqtt.connected()) {
      mqtt.loop();
  }

  if (WiFi.status() != WL_CONNECTED || !mqtt.connected()) {
      hasTransitData = false;
  }

  if (hasTransitData && WiFi.status() == WL_CONNECTED && mqtt.connected()) {
      if (lastRenderedRouteId != currentRouteId) {
          render_route_logo(currentRouteId);
          lastRenderedRouteId = currentRouteId;
      }
      lastStatusSignature = "";
  } else {
      String status = "BOOTING";
      String detail = "";
      if (setupModeActive && WiFi.status() != WL_CONNECTED) {
          status = "SETUP MODE";
          detail = "Connect to device Wi-Fi";
      } else if (WiFi.status() != WL_CONNECTED) {
          status = "NO WIFI";
          detail = "Trying reconnect...";
      } else if (!mqtt.connected()) {
          status = "WIFI OK";
          detail = "MQTT offline";
      } else {
          status = "CONNECTED";
          detail = "Waiting transit data";
      }

      String signature = status + "|" + detail;
      if (signature != lastStatusSignature) {
          render_brand_status(status, detail);
          lastStatusSignature = signature;
          lastRenderedRouteId = "";
      }
  }

  server.handleClient();

  if (rebootScheduled && static_cast<long>(millis() - rebootAtMs) >= 0) {
    Serial.println("[ESP] Rebooting to apply device config");
    ESP.restart();
  }

  delay(50);
}
