#include "core/device_controller.h"

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "parsing/payload_parser.h"
#include "parsing/provider_parser_router.h"
#include "network/wifi_manager.h"

namespace core {

DeviceController *DeviceController::activeController_ = nullptr;

namespace {

constexpr uint32_t kHeartbeatEveryMs = 15000;
constexpr uint32_t kTelemetryEveryMs = 30000;
constexpr uint32_t kMinRenderGapMs = 40;

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
    copy_str(out, outLen, "DUE");
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
  copy_str(model.rows[0].providerId, sizeof(model.rows[0].providerId), "");
  copy_str(model.rows[0].routeId, sizeof(model.rows[0].routeId), "--");
  copy_str(model.rows[0].destination, sizeof(model.rows[0].destination), "Waiting data");
  copy_str(model.rows[0].eta, sizeof(model.rows[0].eta), "--");

  copy_str(model.rows[1].providerId, sizeof(model.rows[1].providerId), "");
  copy_str(model.rows[1].routeId, sizeof(model.rows[1].routeId), "--");
  copy_str(model.rows[1].destination, sizeof(model.rows[1].destination), "");
  copy_str(model.rows[1].eta, sizeof(model.rows[1].eta), "--");
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
  Serial.printf("[MQTT] Incoming topic=%s len=%u\n", topic ? topic : "(null)", static_cast<unsigned>(len));
  Serial.printf("[MQTT] Payload=%s\n", message.c_str());

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
  copy_str(renderModel_.rows[0].destination, sizeof(renderModel_.rows[0].destination),
           parsed.row1.label.length() ? parsed.row1.label.c_str() : renderModel_.rows[0].routeId);
  normalize_eta(parsed.row1.eta, renderModel_.rows[0].eta, sizeof(renderModel_.rows[0].eta));

  if (parsed.hasRow2) {
    copy_str(renderModel_.rows[1].providerId, sizeof(renderModel_.rows[1].providerId),
             parsed.row2.provider.c_str());
    copy_str(renderModel_.rows[1].routeId, sizeof(renderModel_.rows[1].routeId),
             parsed.row2.line.length() ? parsed.row2.line.c_str() : "--");
    copy_str(renderModel_.rows[1].destination, sizeof(renderModel_.rows[1].destination),
             parsed.row2.label.length() ? parsed.row2.label.c_str() : renderModel_.rows[1].routeId);
    normalize_eta(parsed.row2.eta, renderModel_.rows[1].eta, sizeof(renderModel_.rows[1].eta));

    const int row1Eta = parse_eta_minutes(renderModel_.rows[0].eta);
    const int row2Eta = parse_eta_minutes(renderModel_.rows[1].eta);
    if (row2Eta < row1Eta) {
      TransitRowModel tmp = renderModel_.rows[0];
      renderModel_.rows[0] = renderModel_.rows[1];
      renderModel_.rows[1] = tmp;
    }
  } else {
    copy_str(renderModel_.rows[1].providerId, sizeof(renderModel_.rows[1].providerId), "");
    copy_str(renderModel_.rows[1].routeId, sizeof(renderModel_.rows[1].routeId), "--");
    copy_str(renderModel_.rows[1].destination, sizeof(renderModel_.rows[1].destination), "");
    copy_str(renderModel_.rows[1].eta, sizeof(renderModel_.rows[1].eta), "--");
  }

  renderModel_.hasData = true;
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

  json_escape(renderModel_.rows[0].providerId, r1Provider, sizeof(r1Provider));
  json_escape(renderModel_.rows[0].routeId, r1Line, sizeof(r1Line));
  json_escape(renderModel_.rows[0].destination, r1Label, sizeof(r1Label));
  json_escape(renderModel_.rows[0].eta, r1Eta, sizeof(r1Eta));
  json_escape(renderModel_.rows[1].providerId, r2Provider, sizeof(r2Provider));
  json_escape(renderModel_.rows[1].routeId, r2Line, sizeof(r2Line));
  json_escape(renderModel_.rows[1].destination, r2Label, sizeof(r2Label));
  json_escape(renderModel_.rows[1].eta, r2Eta, sizeof(r2Eta));

  char payload[512];
  snprintf(payload,
           sizeof(payload),
           "{\"deviceId\":\"%s\",\"row1\":{\"provider\":\"%s\",\"line\":\"%s\",\"label\":\"%s\",\"eta\":\"%s\"},\"row2\":{\"provider\":\"%s\",\"line\":\"%s\",\"label\":\"%s\",\"eta\":\"%s\"}}",
           runtimeConfig_.deviceId,
           r1Provider,
           r1Line,
           r1Label,
           r1Eta,
           r2Provider,
           r2Line,
           r2Label,
           r2Eta);

  deps_.mqttClient->publish_state(payload, false);
}

}  // namespace core
