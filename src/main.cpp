/*
  Example from WiFi > WiFiScan
  Complete details at https://RandomNerdTutorials.com/esp32-useful-wi-fi-functions-arduino/
*/

#include "WiFi.h"
#include "display/Transit.h"
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
#include "transit/providers/chicago/subway/style.h"

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

    const int logoRadius = 6;
    const int logoCenterX = logoRadius + 2; // keep col[0] empty for logo rows too
    const bool isBus = providerId == "mta-bus";
    const bool isCtaSubway = providerId == "cta-subway";

    const String eta = etaText.length() ? etaText : "--";

    int16_t etaX1, etaY1;
    uint16_t etaW, etaH;
    matrix->setTextWrap(false);
    matrix->setTextSize(1);
    matrix->getTextBounds(eta.c_str(), 0, 0, &etaX1, &etaY1, &etaW, &etaH);

    constexpr int LEFT_MARGIN_PX = 2; // match requested left margin for bus text
    const int etaX = matrix->width() - static_cast<int>(etaW) - 1; // fixed right margin = 1
    const String ctaBadgeText = transit::providers::chicago::subway::route_label(routeId);
    const int ctaBadgeW = 24; // fixed CTA badge slot width
    const int ctaBadgeH = 12;
    const int ctaBadgeX = LEFT_MARGIN_PX;
    const int ctaBadgeY = centerY - (ctaBadgeH / 2);
    const int baseTextStartX = isBus ? LEFT_MARGIN_PX : (isCtaSubway ? (ctaBadgeX + ctaBadgeW + 2) : (logoCenterX + logoRadius + 2));
    const int labelStartX = baseTextStartX;

    const int labelEndX = etaX - 1;
    const int labelPx = labelEndX - labelStartX + 1;
    const int labelChars = labelPx > 0 ? labelPx / 6 : 0;

    if (redrawFixed) {
        if (isCtaSubway) {
            uint16_t ctaColor =
                transit::providers::nyc::subway::color_from_hex(transit::providers::chicago::subway::route_color_hex(routeId), 40);
            matrix->fillRoundRect(ctaBadgeX, ctaBadgeY, ctaBadgeW, ctaBadgeH, 2, ctaColor);
            int16_t ctaX1, ctaY1;
            uint16_t ctaTextW, ctaTextH;
            matrix->getTextBounds(ctaBadgeText.c_str(), 0, 0, &ctaX1, &ctaY1, &ctaTextW, &ctaTextH);
            int ctaTextX = ctaBadgeX + ((ctaBadgeW - (int)ctaTextW) / 2);
            if (ctaTextX < ctaBadgeX + 1) ctaTextX = ctaBadgeX + 1;
            matrix->setTextColor(transit::providers::nyc::subway::color_from_name(transit::providers::chicago::subway::route_text_color(routeId), 80), ctaColor);
            matrix->setCursor(ctaTextX, centerY - 3);
            matrix->print(ctaBadgeText);
        } else if (!isBus) {
            if (line) {
                draw_transit_logo(
                    logoCenterX,
                    centerY,
                    line->symbol,
                    line->color_hex,
                    logoRadius,
                    20,
                    1,
                    false,
                    "white",
                    40);
            } else {
                char fallbackSymbol = 'B';
                if (routeId.length() > 0) {
                    fallbackSymbol = routeId[0];
                    if (fallbackSymbol >= 'a' && fallbackSymbol <= 'z') {
                        fallbackSymbol = fallbackSymbol - 'a' + 'A';
                    }
                } else if (providerId == "mta-subway" || providerId == "mta") {
                    fallbackSymbol = 'M';
                }
                draw_transit_logo(
                    logoCenterX,
                    centerY,
                    fallbackSymbol,
                    "#7C858C",
                    logoRadius,
                    20,
                    1,
                    false,
                    "white",
                    40);
            }
        }

        matrix->setTextColor(transit::providers::nyc::subway::color_from_name("white", 40));
        matrix->setCursor(etaX, centerY - 3);
        matrix->print(eta);
    }

    if (isBus || isCtaSubway) {
        // Ensure left-most column remains blank for bus text rows.
        matrix->drawFastVLine(0, centerY - 8, 16, 0);
    }

    String label = labelText;
    label.trim();
    if (isBus && routeId.length() > 0) {
        String route = routeId;
        route.trim();
        route.toUpperCase();
        if (label.length() > 0) label = route + " " + label;
        else label = route;
    }
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
    } else {
        currentRow2Provider = "";
        currentRow2RouteId = "";
        currentRow2Label = "";
        currentRow2Eta = "--";
    }

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
