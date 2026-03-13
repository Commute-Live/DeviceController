#include "core/device_controller.h"

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "parsing/payload_parser.h"
#include "parsing/provider_parser_router.h"
#include "display/BadgeRenderer.h"
#include "network/wifi_manager.h"

namespace core {

DeviceController *DeviceController::activeController_ = nullptr;

namespace {

constexpr uint32_t kHeartbeatEveryMs = 15000;
constexpr uint32_t kTelemetryEveryMs = 30000;
constexpr uint32_t kMinRenderGapMs = 40;
constexpr uint8_t kMinDisplayType = 1;
constexpr uint8_t kMaxDisplayType = 5;
display::BadgeRenderer gBadgeRenderer;

void copy_str(char *dst, size_t dstLen, const char *src) {
  if (dstLen == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

int parse_eta_minutes(const char *etaRaw) {
  if (!etaRaw || etaRaw[0] == '\0') {
    return 100000;
  }

  if (strcmp(etaRaw, "DUE") == 0 || strcmp(etaRaw, "NOW") == 0) {
    return 0;
  }

  int minutes = 0;
  bool foundDigit = false;
  for (const char *p = etaRaw; *p != '\0'; ++p) {
    const char c = *p;
    if (c >= '0' && c <= '9') {
      foundDigit = true;
      minutes = (minutes * 10) + (c - '0');
    } else if (foundDigit) {
      break;
    }
  }

  return foundDigit ? minutes : 100000;
}

void normalize_eta(const String &input, char *out, size_t outLen) {
  String eta = input;
  eta.trim();
  eta.toUpperCase();

  if (eta.length() == 0 || eta == "--") {
    copy_str(out, outLen, "--");
    return;
  }
  if (eta == "NOW" || eta == "DUE") {
    copy_str(out, outLen, "0m");
    return;
  }

  // Preserve multi-arrival strings like "DUE/3M/8M" generated from nextArrivals.
  if (eta.indexOf('/') >= 0) {
    String normalized = "";
    int start = 0;
    while (start <= eta.length()) {
      int sep = eta.indexOf('/', start);
      String token;
      if (sep < 0) {
        token = eta.substring(start);
      } else {
        token = eta.substring(start, sep);
      }
      token.trim();
      if (token == "NOW" || token == "DUE") token = "0m";

      if (token.length() > 0 && token != "--") {
        if (normalized.length() > 0) normalized += "/";
        normalized += token;
      }

      if (sep < 0) break;
      start = sep + 1;
    }

    if (normalized.length() == 0) {
      copy_str(out, outLen, "--");
      return;
    }
    copy_str(out, outLen, normalized.c_str());
    return;
  }

  int minutes = 0;
  bool foundDigit = false;
  for (int i = 0; i < eta.length(); ++i) {
    const char c = eta[i];
    if (c >= '0' && c <= '9') {
      foundDigit = true;
      minutes = (minutes * 10) + (c - '0');
    } else if (foundDigit) {
      break;
    }
  }

  if (foundDigit) {
    if (minutes <= 1) {
      copy_str(out, outLen, "DUE");
    } else {
      char tmp[12];
      snprintf(tmp, sizeof(tmp), "%dm", minutes);
      copy_str(out, outLen, tmp);
    }
    return;
  }

  copy_str(out, outLen, input.c_str());
}

void set_default_rows(RenderModel &model) {
  model.displayType = kMinDisplayType;
  model.activeRows = 1;
  copy_str(model.rows[0].providerId, sizeof(model.rows[0].providerId), "");
  copy_str(model.rows[0].routeId, sizeof(model.rows[0].routeId), "--");
  copy_str(model.rows[0].direction, sizeof(model.rows[0].direction), "");
  copy_str(model.rows[0].destination, sizeof(model.rows[0].destination), "Waiting data");
  copy_str(model.rows[0].eta, sizeof(model.rows[0].eta), "--");
  copy_str(model.rows[0].etaExtra, sizeof(model.rows[0].etaExtra), "");

  for (uint8_t i = 1; i < kMaxTransitRows; ++i) {
    copy_str(model.rows[i].providerId, sizeof(model.rows[i].providerId), "");
    copy_str(model.rows[i].routeId, sizeof(model.rows[i].routeId), "");
    copy_str(model.rows[i].direction, sizeof(model.rows[i].direction), "");
    copy_str(model.rows[i].destination, sizeof(model.rows[i].destination), "");
    copy_str(model.rows[i].eta, sizeof(model.rows[i].eta), "");
    copy_str(model.rows[i].etaExtra, sizeof(model.rows[i].etaExtra), "");
  }
}

int clamp_rows_to_display(int value) {
  if (value < 1) return 1;
  if (value > static_cast<int>(kMaxTransitRows)) return static_cast<int>(kMaxTransitRows);
  return value;
}

uint8_t clamp_display_type(int value) {
  if (value < static_cast<int>(kMinDisplayType)) return kMinDisplayType;
  if (value > static_cast<int>(kMaxDisplayType)) return kMaxDisplayType;
  return static_cast<uint8_t>(value);
}

int find_matching_bracket(const String &text, int openPos, char openCh, char closeCh) {
  if (openPos < 0 || openPos >= static_cast<int>(text.length()) || text[openPos] != openCh) return -1;
  int depth = 0;
  bool inQuotes = false;
  bool escapeNext = false;
  for (int i = openPos; i < static_cast<int>(text.length()); ++i) {
    const char c = text[i];
    if (escapeNext) {
      escapeNext = false;
      continue;
    }
    if (c == '\\') {
      escapeNext = true;
      continue;
    }
    if (c == '"') {
      inQuotes = !inQuotes;
      continue;
    }
    if (inQuotes) continue;
    if (c == openCh) depth++;
    else if (c == closeCh) {
      depth--;
      if (depth == 0) return i;
    }
  }
  return -1;
}

bool extract_lines_object_at(const String &message, uint8_t index, String &outObject) {
  outObject = "";
  const int linesKeyPos = message.indexOf("\"lines\"");
  if (linesKeyPos < 0) return false;
  const int arrayOpen = message.indexOf('[', linesKeyPos);
  if (arrayOpen < 0) return false;
  const int arrayClose = find_matching_bracket(message, arrayOpen, '[', ']');
  if (arrayClose < 0) return false;

  const String linesJson = message.substring(arrayOpen + 1, arrayClose);
  int cursor = 0;
  uint8_t seen = 0;
  while (true) {
    const int objStart = linesJson.indexOf('{', cursor);
    if (objStart < 0) break;
    const int objEnd = find_matching_bracket(linesJson, objStart, '{', '}');
    if (objEnd < 0) break;

    if (seen == index) {
      outObject = linesJson.substring(objStart, objEnd + 1);
      return true;
    }
    seen++;
    cursor = objEnd + 1;
  }
  return false;
}

String direction_from_code(const String &directionCode) {
  String code = directionCode;
  code.trim();
  code.toUpperCase();
  if (code == "N") return "Uptown";
  if (code == "S") return "Downtown";
  if (code == "E") return "Eastbound";
  if (code == "W") return "Westbound";
  return "";
}

String extract_row_direction_label(const String &message, uint8_t rowIndex) {
  String item;
  if (extract_lines_object_at(message, rowIndex, item)) {
    String dirLabel = extract_json_string_field(item, "directionLabel");
    if (dirLabel.length() > 0) return dirLabel;
    dirLabel = direction_from_code(extract_json_string_field(item, "direction"));
    if (dirLabel.length() > 0) return dirLabel;
  }

  if (rowIndex == 0) {
    String topLevel = extract_json_string_field(message, "directionLabel");
    if (topLevel.length() > 0) return topLevel;
    topLevel = direction_from_code(extract_json_string_field(message, "direction"));
    if (topLevel.length() > 0) return topLevel;
  }
  return "";
}

int extract_eta_values_from_line_object(const String &lineObject, String outEtas[], int maxCount) {
  if (maxCount <= 0) return 0;
  int count = 0;
  int pos = 0;
  while (count < maxCount) {
    const int keyPos = lineObject.indexOf("\"eta\":\"", pos);
    if (keyPos < 0) break;
    const int valueStart = keyPos + static_cast<int>(strlen("\"eta\":\""));
    const int valueEnd = lineObject.indexOf('"', valueStart);
    if (valueEnd < 0) break;
    outEtas[count++] = lineObject.substring(valueStart, valueEnd);
    pos = valueEnd + 1;
  }
  return count;
}

String normalize_eta_token(String token) {
  token.trim();
  token.toUpperCase();
  if (token == "NOW" || token == "DUE") return "0m";
  if (token.length() == 0 || token == "--") return "";
  return token;
}

void extract_row_eta_extra(const String &message, uint8_t rowIndex, char *out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';

  String lineObj;
  if (!extract_lines_object_at(message, rowIndex, lineObj)) return;

  String etaValues[3];
  const int etaCount = extract_eta_values_from_line_object(lineObj, etaValues, 3);
  if (etaCount <= 1) return;

  String merged = "";
  for (int i = 1; i < etaCount; ++i) {
    const String token = normalize_eta_token(etaValues[i]);
    if (token.length() == 0) continue;
    if (merged.length() > 0) merged += ",";
    merged += token;
  }
  copy_str(out, outLen, merged.c_str());
}

int split_eta_tokens(const char *etaRaw, char out[kMaxTransitRows][kMaxEtaLen]) {
  for (uint8_t i = 0; i < kMaxTransitRows; ++i) {
    out[i][0] = '\0';
  }

  if (!etaRaw || etaRaw[0] == '\0') {
    return 0;
  }

  String eta = etaRaw;
  int count = 0;
  int start = 0;
  while (start <= eta.length() && count < static_cast<int>(kMaxTransitRows)) {
    int sep = eta.indexOf('/', start);
    String token = sep < 0 ? eta.substring(start) : eta.substring(start, sep);
    token.trim();
    if (token == "NOW" || token == "DUE") token = "0m";
    if (token.length() > 0 && token != "--") {
      copy_str(out[count], kMaxEtaLen, token.c_str());
      count++;
    }
    if (sep < 0) break;
    start = sep + 1;
  }
  return count;
}

void clear_row(TransitRowModel &row) {
  copy_str(row.providerId, sizeof(row.providerId), "");
  copy_str(row.routeId, sizeof(row.routeId), "");
  copy_str(row.direction, sizeof(row.direction), "");
  copy_str(row.destination, sizeof(row.destination), "");
  copy_str(row.eta, sizeof(row.eta), "");
  copy_str(row.etaExtra, sizeof(row.etaExtra), "");
}

void json_escape(const char *src, char *dst, size_t dstLen) {
  if (dstLen == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }

  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < dstLen; ++i) {
    const char c = src[i];
    if ((c == '"' || c == '\\') && j + 2 < dstLen) {
      dst[j++] = '\\';
      dst[j++] = c;
      continue;
    }
    if (c == '\n' && j + 2 < dstLen) {
      dst[j++] = '\\';
      dst[j++] = 'n';
      continue;
    }
    if (c == '\r' && j + 2 < dstLen) {
      dst[j++] = '\\';
      dst[j++] = 'r';
      continue;
    }
    dst[j++] = c;
  }
  dst[j] = '\0';
}

}  // namespace

DeviceController::DeviceController(const Dependencies &deps)
    : deps_(deps),
      runtimeConfig_{},
      renderModel_{},
      drawList_{},
      server_(80),
      lastHeartbeatAtMs_(0),
      lastTelemetryAtMs_(0),
      lastRenderAtMs_(0),
      renderDirty_(true) {
  memset(&renderModel_, 0, sizeof(renderModel_));
  renderModel_.uiState = UiState::kBooting;
  copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "BOOTING");
  copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "Starting device");
  set_default_rows(renderModel_);
}

bool DeviceController::begin() {
  if (!deps_.configStore || !deps_.networkManager || !deps_.mqttClient || !deps_.displayEngine ||
      !deps_.layoutEngine || !deps_.providerRegistry) {
    return false;
  }

  if (!deps_.configStore->begin() || !deps_.configStore->load(runtimeConfig_)) {
    return false;
  }

  MqttTopics topics{};
  if (!MqttClient::build_default_topics(runtimeConfig_.deviceId, topics)) {
    return false;
  }

  if (!deps_.mqttClient->begin(runtimeConfig_.mqtt, topics)) {
    return false;
  }

  deps_.networkManager->set_state_callback(&DeviceController::on_network_state_change, this);
  deps_.mqttClient->set_command_callback(&DeviceController::on_mqtt_command, this);
  activeController_ = this;
  setup_http_routes();
  deps_.networkManager->begin(runtimeConfig_.network);
  server_.begin();
  Serial.println("[HTTP] Core API ready");

  if (!deps_.displayEngine->begin(runtimeConfig_.display)) {
    return false;
  }

  deps_.layoutEngine->set_viewport(deps_.displayEngine->geometry().totalWidth,
                                   deps_.displayEngine->geometry().totalHeight);

  update_ui_state();
  renderDirty_ = true;
  render_frame(millis());
  return true;
}

void DeviceController::tick(uint32_t nowMs) {
  deps_.networkManager->tick(nowMs);
  deps_.mqttClient->tick(nowMs);
  server_.handleClient();
  update_ui_state();

  if (deps_.mqttClient->connected()) {
    if (nowMs - lastHeartbeatAtMs_ >= kHeartbeatEveryMs) {
      lastHeartbeatAtMs_ = nowMs;

      char payload[128];
      snprintf(payload, sizeof(payload),
               "{\"deviceId\":\"%s\",\"uptimeMs\":%lu,\"freeHeap\":%lu}",
               runtimeConfig_.deviceId,
               static_cast<unsigned long>(nowMs),
               static_cast<unsigned long>(ESP.getFreeHeap()));
      deps_.mqttClient->publish_heartbeat(payload);
    }

    if (nowMs - lastTelemetryAtMs_ >= kTelemetryEveryMs) {
      lastTelemetryAtMs_ = nowMs;
      char payload[160];
      snprintf(payload, sizeof(payload),
               "{\"freeHeap\":%lu,\"maxAlloc\":%lu,\"wifiRssi\":%d}",
               static_cast<unsigned long>(ESP.getFreeHeap()),
               static_cast<unsigned long>(ESP.getMaxAllocHeap()),
               WiFi.RSSI());
      deps_.mqttClient->publish_telemetry(payload);
    }
  }

  render_frame(nowMs);
}

void DeviceController::on_network_state_change(NetworkState state, void *ctx) {
  if (!ctx) {
    return;
  }
  static_cast<DeviceController *>(ctx)->handle_network_state(state);
}

void DeviceController::on_mqtt_command(const char *topic, const uint8_t *payload, size_t len, void *ctx) {
  if (!ctx) {
    return;
  }
  static_cast<DeviceController *>(ctx)->handle_command(topic, payload, len);
}

void DeviceController::handle_network_state(NetworkState state) {
  (void)state;
  update_ui_state();
}

void DeviceController::handle_command(const char *topic, const uint8_t *payload, size_t len) {
  if (!payload || len == 0 || len >= kMaxPayloadLen) {
    return;
  }

  char messageBuf[kMaxPayloadLen];
  memcpy(messageBuf, payload, len);
  messageBuf[len] = '\0';
  const String message(messageBuf);
  const int arrivalsToDisplay = clamp_rows_to_display(extract_json_int_field(message, "arrivalsToDisplay", 1));
  const uint8_t displayType = clamp_display_type(extract_json_int_field(message, "displayType", 1));
  Serial.printf("[MQTT] Incoming topic=%s len=%u\n", topic ? topic : "(null)", static_cast<unsigned>(len));
  Serial.printf("[MQTT] Payload=%s\n", message.c_str());

  String cmdType = extract_json_string_field(message, "type");
  if (cmdType == "ota_update") {
    String url = extract_json_string_field(message, "url");
    if (url.length() > 0) {
      perform_ota_update(url);
    }
    return;
  }

  String provider = extract_json_string_field(message, "provider");
  if (provider.length() == 0 || !parsing::is_supported_provider_id(provider)) {
    Serial.printf("[MQTT] Ignored: unsupported provider '%s'\n", provider.c_str());
    return;
  }

  parsing::ProviderPayload parsed{};
  if (!parsing::parse_provider_payload(provider, message, parsed) || !parsed.hasRow1) {
    Serial.println("[MQTT] Ignored: no row data");
    return;
  }

  const String row1Direction = extract_row_direction_label(message, 0);
  const String row2Direction = extract_row_direction_label(message, 1);
  char row1EtaExtra[kMaxDestinationLen];
  char row2EtaExtra[kMaxDestinationLen];
  row1EtaExtra[0] = '\0';
  row2EtaExtra[0] = '\0';
  const bool compactExtraEtaPreset = (displayType == 4 || displayType == 5);
  if (compactExtraEtaPreset) {
    extract_row_eta_extra(message, 0, row1EtaExtra, sizeof(row1EtaExtra));
    extract_row_eta_extra(message, 1, row2EtaExtra, sizeof(row2EtaExtra));
  }

  const String row1Provider = parsed.row1.provider.length() ? parsed.row1.provider : provider;
  if (!parsing::is_supported_provider_id(row1Provider)) {
    Serial.printf("[MQTT] Ignored: unsupported row1 provider '%s'\n", row1Provider.c_str());
    return;
  }

  if (parsed.hasRow2) {
    const String row2Provider = parsed.row2.provider.length() ? parsed.row2.provider : provider;
    if (!parsing::is_supported_provider_id(row2Provider)) {
      Serial.printf("[MQTT] Ignored: unsupported row2 provider '%s'\n", row2Provider.c_str());
      return;
    }
  }

  copy_str(renderModel_.rows[0].providerId, sizeof(renderModel_.rows[0].providerId),
           row1Provider.c_str());
  copy_str(renderModel_.rows[0].routeId, sizeof(renderModel_.rows[0].routeId),
           parsed.row1.line.length() ? parsed.row1.line.c_str() : "--");
  copy_str(renderModel_.rows[0].direction, sizeof(renderModel_.rows[0].direction),
           row1Direction.c_str());
  copy_str(renderModel_.rows[0].destination, sizeof(renderModel_.rows[0].destination),
           parsed.row1.label.length() ? parsed.row1.label.c_str() : renderModel_.rows[0].routeId);
  normalize_eta(parsed.row1.eta, renderModel_.rows[0].eta, sizeof(renderModel_.rows[0].eta));
  copy_str(renderModel_.rows[0].etaExtra, sizeof(renderModel_.rows[0].etaExtra),
           compactExtraEtaPreset ? row1EtaExtra : "");

  if (parsed.hasRow2) {
    copy_str(renderModel_.rows[1].providerId, sizeof(renderModel_.rows[1].providerId),
             parsed.row2.provider.c_str());
    copy_str(renderModel_.rows[1].routeId, sizeof(renderModel_.rows[1].routeId),
             parsed.row2.line.length() ? parsed.row2.line.c_str() : "--");
    copy_str(renderModel_.rows[1].direction, sizeof(renderModel_.rows[1].direction),
             row2Direction.c_str());
    copy_str(renderModel_.rows[1].destination, sizeof(renderModel_.rows[1].destination),
             parsed.row2.label.length() ? parsed.row2.label.c_str() : renderModel_.rows[1].routeId);
    normalize_eta(parsed.row2.eta, renderModel_.rows[1].eta, sizeof(renderModel_.rows[1].eta));
    copy_str(renderModel_.rows[1].etaExtra, sizeof(renderModel_.rows[1].etaExtra),
             compactExtraEtaPreset ? row2EtaExtra : "");

    const int row1Eta = parse_eta_minutes(renderModel_.rows[0].eta);
    const int row2Eta = parse_eta_minutes(renderModel_.rows[1].eta);
    if (row2Eta < row1Eta) {
      TransitRowModel tmp = renderModel_.rows[0];
      renderModel_.rows[0] = renderModel_.rows[1];
      renderModel_.rows[1] = tmp;
    }
    renderModel_.activeRows = 2;
    for (uint8_t i = 2; i < kMaxTransitRows; ++i) {
      clear_row(renderModel_.rows[i]);
    }
  } else {
    char etaParts[kMaxTransitRows][kMaxEtaLen];
    const int etaCount = split_eta_tokens(renderModel_.rows[0].eta, etaParts);
    int rowsToRender = arrivalsToDisplay;
    if (etaCount > 0 && rowsToRender > etaCount) {
      rowsToRender = etaCount;
    }
    if (compactExtraEtaPreset) {
      rowsToRender = 1;
    } else {
      // For single-line payloads, always show at least the next two ETAs when available.
      if (etaCount >= 2 && rowsToRender < 2) {
        rowsToRender = 2;
      }
    }
    if (rowsToRender < 1) {
      rowsToRender = 1;
    }

    if (etaCount > 0) {
      copy_str(renderModel_.rows[0].eta, sizeof(renderModel_.rows[0].eta), etaParts[0]);
    }
    if (compactExtraEtaPreset && row1EtaExtra[0] != '\0') {
      copy_str(renderModel_.rows[0].etaExtra, sizeof(renderModel_.rows[0].etaExtra), row1EtaExtra);
    } else if (compactExtraEtaPreset && etaCount > 1) {
      char extraBuf[kMaxDestinationLen];
      extraBuf[0] = '\0';
      for (int i = 1; i < etaCount; ++i) {
        if (etaParts[i][0] == '\0') continue;
        if (extraBuf[0] != '\0') {
          strncat(extraBuf, ",", sizeof(extraBuf) - strlen(extraBuf) - 1);
        }
        strncat(extraBuf, etaParts[i], sizeof(extraBuf) - strlen(extraBuf) - 1);
      }
      copy_str(renderModel_.rows[0].etaExtra, sizeof(renderModel_.rows[0].etaExtra), extraBuf);
    } else {
      copy_str(renderModel_.rows[0].etaExtra, sizeof(renderModel_.rows[0].etaExtra), "");
    }

    for (int i = 1; i < rowsToRender; ++i) {
      copy_str(renderModel_.rows[i].providerId, sizeof(renderModel_.rows[i].providerId),
               renderModel_.rows[0].providerId);
      copy_str(renderModel_.rows[i].routeId, sizeof(renderModel_.rows[i].routeId),
               renderModel_.rows[0].routeId);
      copy_str(renderModel_.rows[i].direction, sizeof(renderModel_.rows[i].direction),
               renderModel_.rows[0].direction);
      copy_str(renderModel_.rows[i].destination, sizeof(renderModel_.rows[i].destination),
               renderModel_.rows[0].destination);
      copy_str(renderModel_.rows[i].etaExtra, sizeof(renderModel_.rows[i].etaExtra), "");
      if (i < etaCount) {
        copy_str(renderModel_.rows[i].eta, sizeof(renderModel_.rows[i].eta), etaParts[i]);
      } else {
        copy_str(renderModel_.rows[i].eta, sizeof(renderModel_.rows[i].eta), "--");
      }
    }

    renderModel_.activeRows = static_cast<uint8_t>(rowsToRender);
    for (int i = rowsToRender; i < static_cast<int>(kMaxTransitRows); ++i) {
      clear_row(renderModel_.rows[i]);
    }
  }

  renderModel_.hasData = true;
  renderModel_.displayType = displayType;
  renderModel_.uiState = UiState::kTransit;
  renderModel_.updatedAtMs = millis();
  renderDirty_ = true;
  publish_display_state();
}

void DeviceController::setup_http_routes() {
  server_.on("/connect", HTTP_POST, &DeviceController::http_connect_handler);
  server_.on("/device-info", HTTP_GET, &DeviceController::http_device_info_handler);
  server_.on("/heartbeat", HTTP_GET, &DeviceController::http_heartbeat_handler);
}

void DeviceController::http_connect_handler() {
  if (!activeController_) {
    return;
  }

  String ssid;
  String pass;
  String user;
  if (!wifi_manager::handle_connect_request(activeController_->server_, ssid, pass, user)) {
    return;
  }

  activeController_->deps_.networkManager->set_credentials(ssid.c_str(), pass.c_str(), user.c_str());
  activeController_->server_.send(200, "application/json", "{\"ok\":true}");
}

void DeviceController::http_device_info_handler() {
  if (!activeController_) {
    return;
  }

  char response[128];
  snprintf(response, sizeof(response), "{\"deviceId\":\"%s\"}", activeController_->runtimeConfig_.deviceId);
  activeController_->server_.send(200, "application/json", response);
}

void DeviceController::http_heartbeat_handler() {
  if (!activeController_) {
    return;
  }
  activeController_->server_.send(200, "application/json", "{\"ok\":true}");
}

void DeviceController::update_ui_state() {
  const UiState previousState = renderModel_.uiState;
  char prevStatus[kMaxStatusLen];
  char prevDetail[kMaxDestinationLen];
  copy_str(prevStatus, sizeof(prevStatus), renderModel_.statusLine);
  copy_str(prevDetail, sizeof(prevDetail), renderModel_.statusDetail);

  const bool wifiUp = deps_.networkManager->is_connected();
  const bool mqttUp = deps_.mqttClient->connected();

  if (renderModel_.hasData && wifiUp && mqttUp) {
    renderModel_.uiState = UiState::kTransit;
    copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "TRANSIT");
    copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "Live arrivals");
  } else if (deps_.networkManager->setup_mode_active() && !wifiUp) {
    renderModel_.uiState = UiState::kSetupMode;
    copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "SETUP MODE");
    copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "Connect to device Wi-Fi");
  } else if (!wifiUp) {
    renderModel_.uiState = UiState::kNoWifi;
    copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "NO WIFI");
    copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "Trying reconnect");
  } else if (!mqttUp) {
    renderModel_.uiState = UiState::kWifiOkNoMqtt;
    copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "WIFI OK");
    copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "MQTT offline");
  } else {
    renderModel_.uiState = UiState::kConnectedWaitingData;
    copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "CONNECTED");
    copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "Waiting transit data");
  }

  if (previousState != renderModel_.uiState ||
      strcmp(prevStatus, renderModel_.statusLine) != 0 ||
      strcmp(prevDetail, renderModel_.statusDetail) != 0) {
    renderDirty_ = true;
  }
}

void DeviceController::render_frame(uint32_t nowMs) {
  if (!renderDirty_) {
    return;
  }
  if (nowMs - lastRenderAtMs_ < kMinRenderGapMs) {
    return;
  }
  lastRenderAtMs_ = nowMs;

  if (!deps_.displayEngine->begin_frame()) {
    return;
  }

  deps_.layoutEngine->build_transit_layout(renderModel_, drawList_);
  for (size_t i = 0; i < drawList_.count; ++i) {
    const DrawCommand &cmd = drawList_.commands[i];
    switch (cmd.type) {
      case DrawCommandType::kFillRect:
        deps_.displayEngine->fill_rect(cmd.x, cmd.y, cmd.w, cmd.h, cmd.color);
        break;
      case DrawCommandType::kText:
        deps_.displayEngine->draw_text(cmd.x, cmd.y, cmd.text, cmd.color, cmd.size, cmd.bg);
        break;
      case DrawCommandType::kBadge:
        gBadgeRenderer.draw_badge(*deps_.displayEngine, cmd.x, cmd.y, cmd.w, cmd.text);
        break;
      default:
        break;
    }
  }

  deps_.displayEngine->present();
  renderDirty_ = false;
}

void DeviceController::publish_display_state() {
  if (!deps_.mqttClient->connected()) {
    return;
  }

  char r1Provider[80];
  char r1Line[80];
  char r1Label[128];
  char r1Eta[32];
  char r2Provider[80];
  char r2Line[80];
  char r2Label[128];
  char r2Eta[32];
  char r3Provider[80];
  char r3Line[80];
  char r3Label[128];
  char r3Eta[32];

  json_escape(renderModel_.rows[0].providerId, r1Provider, sizeof(r1Provider));
  json_escape(renderModel_.rows[0].routeId, r1Line, sizeof(r1Line));
  json_escape(renderModel_.rows[0].destination, r1Label, sizeof(r1Label));
  json_escape(renderModel_.rows[0].eta, r1Eta, sizeof(r1Eta));
  json_escape(renderModel_.rows[1].providerId, r2Provider, sizeof(r2Provider));
  json_escape(renderModel_.rows[1].routeId, r2Line, sizeof(r2Line));
  json_escape(renderModel_.rows[1].destination, r2Label, sizeof(r2Label));
  json_escape(renderModel_.rows[1].eta, r2Eta, sizeof(r2Eta));
  json_escape(renderModel_.rows[2].providerId, r3Provider, sizeof(r3Provider));
  json_escape(renderModel_.rows[2].routeId, r3Line, sizeof(r3Line));
  json_escape(renderModel_.rows[2].destination, r3Label, sizeof(r3Label));
  json_escape(renderModel_.rows[2].eta, r3Eta, sizeof(r3Eta));

  char payload[768];
  snprintf(payload,
           sizeof(payload),
           "{\"deviceId\":\"%s\",\"activeRows\":%u,\"row1\":{\"provider\":\"%s\",\"line\":\"%s\",\"label\":\"%s\",\"eta\":\"%s\"},\"row2\":{\"provider\":\"%s\",\"line\":\"%s\",\"label\":\"%s\",\"eta\":\"%s\"},\"row3\":{\"provider\":\"%s\",\"line\":\"%s\",\"label\":\"%s\",\"eta\":\"%s\"}}",
           runtimeConfig_.deviceId,
           static_cast<unsigned>(renderModel_.activeRows),
           r1Provider,
           r1Line,
           r1Label,
           r1Eta,
           r2Provider,
           r2Line,
           r2Label,
           r2Eta,
           r3Provider,
           r3Line,
           r3Label,
           r3Eta);

  deps_.mqttClient->publish_state(payload, false);
}

bool DeviceController::perform_ota_update(const String& url) {
  Serial.printf("[OTA] Starting update from %s\n", url.c_str());
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] HTTP GET failed: %d\n", code);
    http.end();
    return false;
  }
  int contentLen = http.getSize();
  bool canBegin = Update.begin(contentLen > 0 ? contentLen : UPDATE_SIZE_UNKNOWN);
  if (!canBegin) {
    Serial.println("[OTA] Update.begin() failed — not enough space?");
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  if (!Update.end(true) || Update.hasError()) {
    Serial.printf("[OTA] Update failed: %s\n", Update.errorString());
    http.end();
    return false;
  }
  Serial.printf("[OTA] Written %u bytes. Restarting...\n", written);
  http.end();
  delay(200);
  ESP.restart();
  return true;
}

}  // namespace core
