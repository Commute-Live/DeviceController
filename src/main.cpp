#ifndef CL_USE_CORE_CONTROLLER
#define CL_USE_CORE_CONTROLLER 0
#endif

#if CL_USE_CORE_CONTROLLER

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "core/config_store.h"
#include "core/device_controller.h"
#include "core/display_engine.h"
#include "core/layout_engine.h"
#include "core/mqtt_client.h"
#include "core/network_manager.h"
#include "core/transit_provider_registry.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "Using include/secrets.example.h defaults. Create include/secrets.h for real credentials."
#endif

namespace {

template <size_t N>
void copy_str(char (&dst)[N], const char *src) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, N - 1);
  dst[N - 1] = '\0';
}

void build_device_id(char *out, size_t outLen) {
  const uint64_t chipid = ESP.getEfuseMac();
  snprintf(out, outLen, "esp32-%04X%08X", static_cast<uint16_t>(chipid >> 32), static_cast<uint32_t>(chipid));
}

core::ConfigStore gConfigStore;
core::NetworkManager gNetworkManager;
core::MqttClient gMqttClient;
core::DisplayEngine gDisplayEngine;
core::LayoutEngine gLayoutEngine;
core::TransitProviderRegistry gProviderRegistry;
core::DeviceController::Dependencies gDeps{
    &gConfigStore, &gNetworkManager, &gMqttClient, &gDisplayEngine, &gLayoutEngine, &gProviderRegistry};
core::DeviceController gController(gDeps);

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  core::DeviceRuntimeConfig cfg{};
  cfg.schemaVersion = 1;
  build_device_id(cfg.deviceId, sizeof(cfg.deviceId));

  cfg.display.panelRows = 1;
  cfg.display.panelCols = 2;
  cfg.display.panelWidth = 64;
  cfg.display.panelHeight = 32;
  cfg.display.brightness = 80;
  cfg.display.serpentine = false;
  cfg.display.doubleBuffered = true;

  copy_str(cfg.network.ssid, COMMUTELIVE_WIFI_SSID);
  copy_str(cfg.network.password, COMMUTELIVE_WIFI_PASSWORD);
  copy_str(cfg.mqtt.host, COMMUTELIVE_MQTT_HOST);
  cfg.mqtt.port = static_cast<uint16_t>(COMMUTELIVE_MQTT_PORT);
  copy_str(cfg.mqtt.username, COMMUTELIVE_MQTT_USER);
  copy_str(cfg.mqtt.password, COMMUTELIVE_MQTT_PASS);
  copy_str(cfg.mqtt.clientId, cfg.deviceId);

  gConfigStore.set_bootstrap_config(cfg);
  if (!gController.begin()) {
    Serial.println("[CORE] Controller init failed");
  } else {
    Serial.printf("[CORE] Controller init ok: %s\n", cfg.deviceId);
  }
}

void loop() {
  gController.tick(millis());
  delay(50);
}

#else

/*
  Example from WiFi > WiFiScan
  Complete details at https://RandomNerdTutorials.com/esp32-useful-wi-fi-functions-arduino/
*/

#include "WiFi.h"
#include "display/Transit.h"
#include "display/layout_constants.h"
#include "display/providers/row_chrome.h"
#include "parsing/payload_parser.h"
#include "parsing/provider_parser_router.h"
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
String currentRow1Provider = "mta-subway";
String currentRow2Provider = "";
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
unsigned long lastLabelScrollMs = 0;
int rowLabelScrollOffset[2] = {0, 0};
String rowLabelLastText[2] = {"", ""};
int rowLabelPauseTicks[2] = {0, 0};
bool advanceLabelScroll = false;
bool recoveryApEnabled = false;

constexpr uint32_t WIFI_RETRY_BASE_MS = 1000;
constexpr uint32_t WIFI_RETRY_MAX_MS = 60000;
constexpr uint32_t MQTT_RETRY_BASE_MS = 1000;
constexpr uint32_t MQTT_RETRY_MAX_MS = 60000;
constexpr uint32_t RETRY_JITTER_MS = 750;
constexpr uint8_t WIFI_AP_RECOVERY_ATTEMPT_THRESHOLD = 3;
constexpr uint32_t LABEL_SCROLL_INTERVAL_MS = 400;
constexpr int LABEL_PAUSE_START_TICKS = 5;
constexpr int LABEL_PAUSE_END_TICKS = 7;

// Forward declarations
bool connect_ESP_to_mqtt();
void mqtt_publish_online();
void mqtt_publish_display_state();
void mqtt_callback(char *topic, byte *payload, unsigned int length);
const transit::LineDefinition *parse_route_command(const String &message);
void render_route_logo(const String &route_id, bool fullRedraw = true);
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

static String json_escape(const String &input) {
    String output;
    output.reserve(input.length() + 8);
    for (int i = 0; i < (int)input.length(); i++) {
        char c = input[i];
        if (c == '\\') output += "\\\\";
        else if (c == '"') output += "\\\"";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else output += c;
    }
    return output;
}

static String now_iso_utc() {
    time_t now = time(nullptr);
    if (now <= 0) {
        return String("");
    }
    struct tm tmUtc;
    gmtime_r(&now, &tmUtc);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
    return String(buf);
}

void mqtt_publish_display_state() {
    if (!mqtt.connected()) return;

    String topic = "devices/" + deviceId + "/display";
    String fetchedAt = now_iso_utc();
    if (fetchedAt.length() == 0) {
        fetchedAt = String((unsigned long)millis());
    }
    String payload = "{";
    payload += "\"deviceId\":\"" + json_escape(deviceId) + "\",";
    payload += "\"fetchedAt\":\"" + fetchedAt + "\",";
    payload += "\"row1\":{";
    payload += "\"provider\":\"" + json_escape(currentRow1Provider) + "\",";
    payload += "\"line\":\"" + json_escape(currentRow1RouteId) + "\",";
    payload += "\"label\":\"" + json_escape(currentRow1Label) + "\",";
    payload += "\"eta\":\"" + json_escape(currentRow1Eta) + "\"";
    payload += "},";
    payload += "\"row2\":{";
    payload += "\"provider\":\"" + json_escape(currentRow2Provider) + "\",";
    payload += "\"line\":\"" + json_escape(currentRow2RouteId) + "\",";
    payload += "\"label\":\"" + json_escape(currentRow2Label) + "\",";
    payload += "\"eta\":\"" + json_escape(currentRow2Eta) + "\"";
    payload += "}";
    payload += "}";

    mqtt.publish(topic.c_str(), payload.c_str(), false);
    Serial.printf("[MQTT] Published display state to %s\n", topic.c_str());
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
    // "{\"provider\":\"mta-subway\",\"line\":\"E\",...}"
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

static String compact_whitespace(const String &input) {
    String output;
    output.reserve(input.length());
    bool inSpace = false;

    for (int i = 0; i < (int)input.length(); i++) {
        char c = input[i];
        const bool isSpace = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (isSpace) {
            if (!inSpace && output.length() > 0) {
                output += ' ';
            }
            inSpace = true;
        } else {
            output += c;
            inSpace = false;
        }
    }

    output.trim();
    return output;
}

static String normalize_eta_text(String etaRaw) {
    etaRaw.trim();
    if (etaRaw.length() == 0) return "--";

    String upper = etaRaw;
    upper.toUpperCase();
    if (upper == "--") return "--";
    if (upper == "NOW" || upper == "DUE") return "DUE";

    const bool hasMinuteHint = upper.indexOf("MIN") >= 0 || upper.endsWith("M");
    if (hasMinuteHint) {
        int minutes = 0;
        bool seenDigit = false;
        for (int i = 0; i < (int)upper.length(); i++) {
            char c = upper[i];
            if (c >= '0' && c <= '9') {
                seenDigit = true;
                minutes = (minutes * 10) + (c - '0');
            } else if (seenDigit) {
                break;
            }
        }
        if (seenDigit) {
            if (minutes <= 1) return "DUE";
            return String(minutes) + "m";
        }
    }

    return etaRaw;
}

static int eta_minutes_for_sort(String etaRaw) {
    etaRaw.trim();
    if (etaRaw.length() == 0) return 100000;

    String upper = etaRaw;
    upper.toUpperCase();
    if (upper == "--") return 100000;
    if (upper == "NOW" || upper == "DUE") return 0;
    if (upper.indexOf(':') >= 0) return 100000;

    int minutes = 0;
    bool seenDigit = false;
    for (int i = 0; i < (int)upper.length(); i++) {
        char c = upper[i];
        if (c >= '0' && c <= '9') {
            seenDigit = true;
            minutes = (minutes * 10) + (c - '0');
        } else if (seenDigit) {
            break;
        }
    }

    if (seenDigit) return minutes;
    return 100000;
}

static uint16_t eta_color_for_text(const String &etaText) {
    const int minutes = eta_minutes_for_sort(etaText);
    if (minutes >= 100000) {
        return transit::providers::nyc::subway::color_from_name("gray", 80);
    }
    if (minutes <= 1) {
        return transit::providers::nyc::subway::color_from_hex("#FF5A5A", 120);
    }
    if (minutes <= 5) {
        return transit::providers::nyc::subway::color_from_hex("#F6BC26", 120);
    }
    if (minutes <= 12) {
        return transit::providers::nyc::subway::color_from_hex("#6EE7B7", 110);
    }
    return transit::providers::nyc::subway::color_from_name("white", 55);
}

static String normalize_label_text(const String &labelRaw,
                                   const String &routeId,
                                   bool prefixRoute) {
    String label = compact_whitespace(labelRaw);
    label.replace(" Station", "");
    label.replace(" station", "");
    label.replace(" St.", " St");
    label.replace(" Ave.", " Ave");
    label = compact_whitespace(label);

    if (label.length() == 0 && routeId.length() > 0) {
        label = routeId;
    }

    if (prefixRoute && routeId.length() > 0) {
        String routeUpper = routeId;
        routeUpper.trim();
        routeUpper.toUpperCase();

        String labelUpper = label;
        labelUpper.trim();
        labelUpper.toUpperCase();

        if (!(labelUpper == routeUpper || labelUpper.startsWith(routeUpper + " "))) {
            label = routeUpper + " " + label;
        }
    }

    return compact_whitespace(label);
}

static String build_scrolled_label(const String &label, int rowIndex, int maxChars) {
    String text = label;
    text.trim();
    if (maxChars <= 0) return "";
    if (text.length() <= 0) return "";

    if (rowIndex < 0 || rowIndex > 1) {
        rowIndex = 0;
    }

    if (text != rowLabelLastText[rowIndex]) {
        rowLabelLastText[rowIndex] = text;
        rowLabelScrollOffset[rowIndex] = 0;
        rowLabelPauseTicks[rowIndex] = LABEL_PAUSE_START_TICKS;
    }

    if ((int)text.length() <= maxChars) {
        rowLabelPauseTicks[rowIndex] = 0;
        String padded = text;
        while ((int)padded.length() < maxChars) {
            padded += ' ';
        }
        return padded;
    }

    int offset = rowLabelScrollOffset[rowIndex];
    int maxOffset = (int)text.length() - maxChars;
    if (maxOffset < 0) maxOffset = 0;
    if (offset < 0 || offset > maxOffset) {
        offset = 0;
        rowLabelScrollOffset[rowIndex] = 0;
    }

    if (advanceLabelScroll) {
        if (rowLabelPauseTicks[rowIndex] > 0) {
            rowLabelPauseTicks[rowIndex]--;
        } else if (offset < maxOffset) {
            offset++;
            rowLabelScrollOffset[rowIndex] = offset;
            if (offset >= maxOffset) {
                rowLabelPauseTicks[rowIndex] = LABEL_PAUSE_END_TICKS;
            }
        } else {
            rowLabelScrollOffset[rowIndex] = 0;
            rowLabelPauseTicks[rowIndex] = LABEL_PAUSE_START_TICKS;
            offset = 0;
        }
    }

    String window = "";
    for (int i = 0; i < maxChars; i++) {
        int idx = offset + i;
        if (idx >= 0 && idx < (int)text.length()) {
            window += text[idx];
        } else {
            window += ' ';
        }
    }
    return window;
}

static void draw_row_with_logo(const String &routeId,
                               const String &providerId,
                               const String &labelText,
                               const String &etaText,
                               int centerY,
                               int rowIndex,
                               bool redrawFixed) {
    const transit::LineDefinition *line = transit::registry::find_line(routeId);
    const String eta = normalize_eta_text(etaText.length() ? etaText : "--");

    int16_t etaX1, etaY1;
    uint16_t etaW, etaH;
    matrix->setTextWrap(false);
    matrix->setTextSize(1);
    matrix->getTextBounds(eta.c_str(), 0, 0, &etaX1, &etaY1, &etaW, &etaH);
    const display::layout::RowLayoutProfile rowLayout =
        display::layout::row_layout_profile_for_width(matrix->width());
    const int etaSlotX = matrix->width() - rowLayout.rightFixedWidth;
    const int etaSlotW = rowLayout.rightFixedWidth;
    int etaX = etaSlotX + etaSlotW - static_cast<int>(etaW) - 1;
    if (etaX < etaSlotX) etaX = etaSlotX;

    display::providers::RowChromeResult chrome =
        display::providers::draw_row_chrome(matrix, providerId, routeId, line, centerY, redrawFixed);
    int labelStartX = rowLayout.leftFixedWidth + rowLayout.middleGap;
    if (labelStartX < 1) labelStartX = 1;
    if (labelStartX >= etaSlotX) labelStartX = 1;

    const int labelEndX = etaSlotX - rowLayout.middleGap;
    const int labelPx = labelEndX - labelStartX + 1;
    const int labelChars = labelPx > 0 ? labelPx / 6 : 0;

    if (redrawFixed) {
        matrix->fillRect(etaSlotX, centerY - 8, etaSlotW, 16, 0);
        matrix->setTextColor(eta_color_for_text(eta));
        matrix->setCursor(etaX, centerY - 3);
        matrix->print(eta);
    }

    if (chrome.clearLeftColumn) {
        matrix->drawFastVLine(0, centerY - 8, 16, 0);
    }

    String label = normalize_label_text(labelText, routeId, chrome.prefixRouteInLabel && routeId.length() > 0);
    if (label.length() == 0) {
        label = routeId.length() > 0 ? routeId : (line ? String(line->id) : String("--"));
    }
    label = build_scrolled_label(label, rowIndex, labelChars);

    // Draw text with explicit background to reduce flicker from clear-and-redraw.
    matrix->setTextColor(transit::providers::nyc::subway::color_from_name("white", 40), 0);
    matrix->setCursor(labelStartX, centerY - 3);
    matrix->print(label);
}

void render_route_logo(const String &route_id, bool fullRedraw) {
    (void)route_id;
    if (fullRedraw) {
        matrix->fillScreen(0);
    }
    const int firstRowY = matrix->height() / 4;
    const int secondRowY = (matrix->height() * 3) / 4;
    draw_row_with_logo(currentRow1RouteId, currentRow1Provider, currentRow1Label, currentRow1Eta, firstRowY, 0, fullRedraw);
    if (currentRow2RouteId.length() > 0) {
        draw_row_with_logo(currentRow2RouteId, currentRow2Provider, currentRow2Label, currentRow2Eta, secondRowY, 1, fullRedraw);
    } else if (fullRedraw) {
        matrix->fillRect(0, secondRowY - 8, matrix->width(), 16, 0);
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
    parsing::ProviderPayload parsedPayload;
    if (!parsing::parse_provider_payload(provider, message, parsedPayload) || !parsedPayload.hasRow1) {
        Serial.println("[MQTT] Ignored command: parser returned no rows");
        return;
    }

    currentRow1Provider = parsedPayload.row1.provider.length() ? parsedPayload.row1.provider : provider;
    currentRow1RouteId = parsedPayload.row1.line.length() ? parsedPayload.row1.line : transit::registry::default_line().id;
    currentRow1Label = parsedPayload.row1.label.length() ? parsedPayload.row1.label : currentRow1RouteId;
    currentRow1Eta = parsedPayload.row1.eta.length() ? parsedPayload.row1.eta : "--";

    if (parsedPayload.hasRow2) {
        currentRow2Provider = parsedPayload.row2.provider;
        currentRow2RouteId = parsedPayload.row2.line;
        currentRow2Label = parsedPayload.row2.label.length() ? parsedPayload.row2.label : currentRow2RouteId;
        currentRow2Eta = parsedPayload.row2.eta.length() ? parsedPayload.row2.eta : "--";

        const int row1Mins = eta_minutes_for_sort(currentRow1Eta);
        const int row2Mins = eta_minutes_for_sort(currentRow2Eta);
        if (row2Mins < row1Mins) {
            String tmp;
            tmp = currentRow1Provider; currentRow1Provider = currentRow2Provider; currentRow2Provider = tmp;
            tmp = currentRow1RouteId; currentRow1RouteId = currentRow2RouteId; currentRow2RouteId = tmp;
            tmp = currentRow1Label; currentRow1Label = currentRow2Label; currentRow2Label = tmp;
            tmp = currentRow1Eta; currentRow1Eta = currentRow2Eta; currentRow2Eta = tmp;
        }
    } else {
        currentRow2Provider = "";
        currentRow2RouteId = "";
        currentRow2Label = "";
        currentRow2Eta = "--";
    }

    Serial.printf("[MQTT] Parsed rows r1={provider:%s line:%s eta:%s} r2={provider:%s line:%s eta:%s}\n",
                  currentRow1Provider.length() ? currentRow1Provider.c_str() : "-",
                  currentRow1RouteId.length() ? currentRow1RouteId.c_str() : "-",
                  currentRow1Eta.length() ? currentRow1Eta.c_str() : "-",
                  currentRow2Provider.length() ? currentRow2Provider.c_str() : "-",
                  currentRow2RouteId.length() ? currentRow2RouteId.c_str() : "-",
                  currentRow2Eta.length() ? currentRow2Eta.c_str() : "-");

    if (lineRaw.length() == 0) {
        lineRaw = currentRow1RouteId;
    }
    if (lineRaw.length() > 0 || provider.length() > 0 || stop.length() > 0 || stopId.length() > 0 || direction.length() > 0) {
        Serial.printf("[MQTT] Refresh payload provider=%s line=%s stop=%s stopId=%s direction=%s\n",
                      provider.length() ? provider.c_str() : "-",
                      lineRaw.length() ? lineRaw.c_str() : "-",
                      stop.length() ? stop.c_str() : "-",
                      stopId.length() ? stopId.c_str() : "-",
                      direction.length() ? direction.c_str() : "-");
    }

    const bool providerIsBus = provider == "mta-bus" || currentRow1Provider == "mta-bus";
    const bool providerIsNycSubway =
        provider == "mta-subway" || provider == "mta" || currentRow1Provider == "mta-subway" || currentRow1Provider == "mta";
    const transit::LineDefinition *line = nullptr;
    if (providerIsNycSubway) {
        if (currentRow1RouteId.length() > 0) {
            line = transit::registry::find_line(currentRow1RouteId);
        }
        if (!line) {
            line = parse_route_command(message);
        }
        if (!line) {
            Serial.println("[MQTT] Ignored command: missing/invalid route");
            return;
        }
    } else if (!providerIsBus) {
        // Non-NYC subway providers (for example cta-subway) do not use NYC route registry.
        line = nullptr;
    }

    currentRouteId = line ? line->id : (currentRow1RouteId.length() ? currentRow1RouteId : transit::registry::default_line().id);
    hasTransitData = true;
    render_route_logo(currentRouteId);
    mqtt_publish_display_state();
    if (line) {
        Serial.printf("[MQTT] Rendered route logo: %s (%c)\n", line->id, line->symbol);
    } else {
        Serial.printf("[MQTT] Rendered provider row: provider=%s line=%s\n",
                      provider.length() ? provider.c_str() : "-",
                      currentRow1RouteId.length() ? currentRow1RouteId.c_str() : "-");
    }
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
          recoveryApEnabled = false;
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
          recoveryApEnabled = true;
      }

  } else {
      hasSavedWifiCredentials = false;
      wifi_manager::start_ap(ESP_ssid, ESP_password);
      setupModeActive = true;
      recoveryApEnabled = true;
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
  bool labelScrollTick = false;
  if (nowMs - lastLabelScrollMs >= LABEL_SCROLL_INTERVAL_MS) {
      lastLabelScrollMs = nowMs;
      labelScrollTick = true;
  }
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
              recoveryApEnabled = false;
              reset_retry(wifiRetry, nowMs);
              timeSynced = false;
              lastTimeSyncAttemptMs = 0;
              reset_retry(mqttRetry, nowMs);
          } else {
              uint32_t waitMs = schedule_next_retry(
                  wifiRetry, nowMs, WIFI_RETRY_BASE_MS, WIFI_RETRY_MAX_MS, RETRY_JITTER_MS);
              Serial.printf("[WIFI] Retry scheduled in %lu ms (attempt %u)\n",
                            (unsigned long)waitMs, wifiRetry.attempt);
              if (!recoveryApEnabled && wifiRetry.attempt >= WIFI_AP_RECOVERY_ATTEMPT_THRESHOLD) {
                  if (wifi_manager::start_ap(ESP_ssid, ESP_password)) {
                      Serial.println("[WIFI] Recovery AP enabled");
                      setupModeActive = true;
                      recoveryApEnabled = true;
                  }
              }
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
      advanceLabelScroll = labelScrollTick;
      if (lastRenderedRouteId != currentRouteId) {
          render_route_logo(currentRouteId, true);
          lastRenderedRouteId = currentRouteId;
      } else if (labelScrollTick) {
          render_route_logo(currentRouteId, false);
          lastRenderedRouteId = currentRouteId;
      }
      advanceLabelScroll = false;
      lastStatusSignature = "";
  } else {
      advanceLabelScroll = false;
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

#endif  // CL_USE_CORE_CONTROLLER
