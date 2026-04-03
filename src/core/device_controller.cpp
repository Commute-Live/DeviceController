#include "core/device_controller.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

#include "parsing/payload_parser.h"
#include "parsing/provider_parser_router.h"
#include "core/logging.h"
#include "display/badge_renderer.h"
#include "network/wifi_manager.h"

#ifndef JACK_LEI
#define JACK_LEI true
#endif

#ifndef COMMUTELIVE_FIRMWARE_VERSION
#define COMMUTELIVE_FIRMWARE_VERSION "dev"
#endif

#ifndef COMMUTELIVE_DEVICE_ENV
#define COMMUTELIVE_DEVICE_ENV "unknown"
#endif

namespace core {

DeviceController *DeviceController::activeController_ = nullptr;

namespace {

constexpr uint32_t kHeartbeatEveryMs = 15000;
constexpr uint32_t kTelemetryEveryMs = 30000;
constexpr uint32_t kDeviceLogHeartbeatEveryMs = 300000;
constexpr uint32_t kLowHeapWarningEveryMs = 60000;
constexpr uint32_t kCrashBreadcrumbPersistEveryMs = 30000;
constexpr uint32_t kLowHeapWarningThresholdBytes = 32768;
constexpr uint32_t kMinRenderGapMs = 40;
constexpr uint32_t kScrollStepMs = 65;       // advance 1px every 65ms (~15px/sec)
constexpr uint32_t kScrollStartPauseMs = 1500; // pause before first scroll
constexpr uint32_t kScrollLoopPauseMs = 2500;  // pause at the end before text jumps back
constexpr int16_t kScrollEtaSafetyGapPx = 4;   // keep scrolled text clear of the ETA column
constexpr int16_t kScrollGapPx = 16;          // gap between end and restart of text
constexpr uint8_t kMinDisplayType = 1;
constexpr uint8_t kMaxDisplayType = 5;
constexpr uint8_t kBrightnessFallbackPercent = JACK_LEI ? 80 : 60;
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorAmber = 0xFD20;
display::BadgeRenderer gBadgeRenderer;
Preferences gDevicePrefs;

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

bool extract_json_bool_field(const String &json, const char *field, bool fallbackValue) {
  String value = extract_json_string_field(json, field);
  value.trim();
  value.toLowerCase();

  if (value == "true" || value == "1") {
    return true;
  }
  if (value == "false" || value == "0") {
    return false;
  }
  return fallbackValue;
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
  model.rows[0].displayType = kMinDisplayType;
  copy_str(model.rows[0].direction, sizeof(model.rows[0].direction), "");
  copy_str(model.rows[0].destination, sizeof(model.rows[0].destination), "Waiting data");
  copy_str(model.rows[0].eta, sizeof(model.rows[0].eta), "--");
  copy_str(model.rows[0].etaExtra, sizeof(model.rows[0].etaExtra), "");

  for (uint8_t i = 1; i < kMaxTransitRows; ++i) {
    copy_str(model.rows[i].providerId, sizeof(model.rows[i].providerId), "");
    copy_str(model.rows[i].routeId, sizeof(model.rows[i].routeId), "");
    model.rows[i].displayType = kMinDisplayType;
    copy_str(model.rows[i].direction, sizeof(model.rows[i].direction), "");
    copy_str(model.rows[i].destination, sizeof(model.rows[i].destination), "");
    copy_str(model.rows[i].eta, sizeof(model.rows[i].eta), "");
    copy_str(model.rows[i].etaExtra, sizeof(model.rows[i].etaExtra), "");
  }
}

int clamp_rows_to_display(int value) {
  if (value < 1) return 1;
  if (value > static_cast<int>(kMaxVisibleTransitRows)) return static_cast<int>(kMaxVisibleTransitRows);
  return value;
}

uint8_t clamp_display_type(int value) {
  if (value < static_cast<int>(kMinDisplayType)) return kMinDisplayType;
  if (value > static_cast<int>(kMaxDisplayType)) return kMaxDisplayType;
  return static_cast<uint8_t>(value);
}

uint8_t parse_brightness_percent(const String &message, uint8_t fallbackPercent) {
  const int raw = extract_json_int_field(message, "brightness", static_cast<int>(fallbackPercent));
  if (raw < 1) return 1;
  if (raw > 100) return 100;
  return static_cast<uint8_t>(raw);
}

uint8_t brightness_percent_to_panel(uint8_t percent) {
  const uint32_t numerator = static_cast<uint32_t>(percent) * static_cast<uint32_t>(percent) * 255U;
  const uint16_t scaled = static_cast<uint16_t>((numerator + 5000U) / 10000U);
  if (scaled < 1U) return 1;
  if (scaled > 255U) return 255;
  return static_cast<uint8_t>(scaled);
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

uint8_t extract_row_display_type(const String &message, uint8_t rowIndex, uint8_t fallbackDisplayType) {
  String item;
  if (!extract_lines_object_at(message, rowIndex, item)) {
    return fallbackDisplayType;
  }
  return clamp_display_type(extract_json_int_field(item, "displayType", fallbackDisplayType));
}

bool extract_row_scrolling(const String &message, uint8_t rowIndex, bool fallback) {
  String item;
  if (!extract_lines_object_at(message, rowIndex, item)) {
    return fallback;
  }
  return extract_json_bool_field(item, "scrolling", fallback);
}

String extract_row_display_label(const String &message, uint8_t rowIndex) {
  String item;
  if (extract_lines_object_at(message, rowIndex, item)) {
    String label = extract_json_string_field(item, "label");
    if (label.length() > 0) return label;
    label = extract_json_string_field(item, "topText");
    if (label.length() > 0) return label;
    label = extract_json_string_field(item, "destination");
    if (label.length() > 0) return label;
    label = extract_json_string_field(item, "directionLabel");
    if (label.length() > 0) return label;
    label = extract_json_string_field(item, "stop");
    if (label.length() > 0) return label;
  }
  return "";
}

int extract_eta_values_from_line_object(const String &lineObject, String outEtas[], int maxCount) {
  if (maxCount <= 0) return 0;
  const int compactEtaCount = extract_json_string_array_field(lineObject, "etas", outEtas, maxCount);
  if (compactEtaCount > 0) return compactEtaCount;
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
  row.displayType = kMinDisplayType;
  copy_str(row.direction, sizeof(row.direction), "");
  copy_str(row.destination, sizeof(row.destination), "");
  copy_str(row.eta, sizeof(row.eta), "");
  copy_str(row.etaExtra, sizeof(row.etaExtra), "");
}

void trim_text_for_chars(const char *src, uint8_t charLimit, char *dst, size_t dstLen) {
  if (!dst || dstLen == 0) {
    return;
  }
  if (!src || charLimit == 0) {
    dst[0] = '\0';
    return;
  }

  size_t n = strnlen(src, dstLen - 1);
  if (n > static_cast<size_t>(charLimit)) {
    n = charLimit;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool strings_equal(const char *lhs, const char *rhs) {
  return strcmp(lhs ? lhs : "", rhs ? rhs : "") == 0;
}

bool rows_equal(const TransitRowModel &lhs, const TransitRowModel &rhs) {
  return strings_equal(lhs.providerId, rhs.providerId) &&
         strings_equal(lhs.routeId, rhs.routeId) &&
         lhs.displayType == rhs.displayType &&
         strings_equal(lhs.direction, rhs.direction) &&
         strings_equal(lhs.destination, rhs.destination) &&
         strings_equal(lhs.eta, rhs.eta) &&
         strings_equal(lhs.etaExtra, rhs.etaExtra);
}

bool row_layout_fields_equal(const TransitRowModel &lhs, const TransitRowModel &rhs) {
  return strings_equal(lhs.providerId, rhs.providerId) &&
         strings_equal(lhs.routeId, rhs.routeId) &&
         lhs.displayType == rhs.displayType &&
         strings_equal(lhs.direction, rhs.direction) &&
         strings_equal(lhs.destination, rhs.destination);
}

bool row_eta_fields_equal(const TransitRowModel &lhs, const TransitRowModel &rhs) {
  return strings_equal(lhs.eta, rhs.eta) &&
         strings_equal(lhs.etaExtra, rhs.etaExtra);
}

bool render_models_equal_for_display(const RenderModel &lhs, const RenderModel &rhs) {
  if (lhs.uiState != rhs.uiState ||
      lhs.hasData != rhs.hasData ||
      lhs.displayType != rhs.displayType ||
      lhs.activeRows != rhs.activeRows ||
      !strings_equal(lhs.statusLine, rhs.statusLine) ||
      !strings_equal(lhs.statusDetail, rhs.statusDetail) ||
      !strings_equal(lhs.apSsid, rhs.apSsid) ||
      !strings_equal(lhs.apPin, rhs.apPin)) {
    return false;
  }

  for (uint8_t i = 0; i < kMaxTransitRows; ++i) {
    if (!rows_equal(lhs.rows[i], rhs.rows[i])) {
      return false;
    }
  }
  return true;
}

DeviceController::RenderMode classify_render_mode(const RenderModel &current,
                                                  const RenderModel &next,
                                                  bool doubleBuffered,
                                                  uint8_t &etaDirtyRows) {
  etaDirtyRows = 0;
  if (render_models_equal_for_display(current, next)) {
    return DeviceController::RenderMode::kNone;
  }

  if (doubleBuffered ||
      current.uiState != UiState::kTransit ||
      next.uiState != UiState::kTransit ||
      !current.hasData ||
      !next.hasData ||
      current.displayType != next.displayType ||
      current.activeRows != next.activeRows ||
      !strings_equal(current.statusLine, next.statusLine) ||
      !strings_equal(current.statusDetail, next.statusDetail) ||
      !strings_equal(current.apSsid, next.apSsid) ||
      !strings_equal(current.apPin, next.apPin)) {
    return DeviceController::RenderMode::kFull;
  }

  for (uint8_t i = 0; i < current.activeRows; ++i) {
    if (!row_layout_fields_equal(current.rows[i], next.rows[i])) {
      return DeviceController::RenderMode::kFull;
    }
    if (!row_eta_fields_equal(current.rows[i], next.rows[i])) {
      etaDirtyRows |= static_cast<uint8_t>(1U << i);
    }
  }

  for (uint8_t i = current.activeRows; i < kMaxTransitRows; ++i) {
    if (!rows_equal(current.rows[i], next.rows[i])) {
      return DeviceController::RenderMode::kFull;
    }
  }

  return etaDirtyRows == 0 ? DeviceController::RenderMode::kNone : DeviceController::RenderMode::kEtaOnly;
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

const char *ui_state_name(UiState state) {
  switch (state) {
    case UiState::kBooting:
      return "kBooting";
    case UiState::kSetupMode:
      return "kSetupMode";
    case UiState::kNoWifi:
      return "kNoWifi";
    case UiState::kWifiOkNoMqtt:
      return "kWifiOkNoMqtt";
    case UiState::kConnectedWaitingData:
      return "kConnectedWaitingData";
    case UiState::kBlank:
      return "kBlank";
    case UiState::kTransit:
      return "kTransit";
    default:
      return "kUnknown";
  }
}

const char *network_state_name(NetworkState state) {
  switch (state) {
    case NetworkState::kDisconnected:
      return "kDisconnected";
    case NetworkState::kConnecting:
      return "kConnecting";
    case NetworkState::kConnected:
      return "kConnected";
    case NetworkState::kApMode:
      return "kApMode";
    default:
      return "kUnknown";
  }
}

const char *reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:
      return "ESP_RST_UNKNOWN";
    case ESP_RST_POWERON:
      return "ESP_RST_POWERON";
    case ESP_RST_EXT:
      return "ESP_RST_EXT";
    case ESP_RST_SW:
      return "ESP_RST_SW";
    case ESP_RST_PANIC:
      return "ESP_RST_PANIC";
    case ESP_RST_INT_WDT:
      return "ESP_RST_INT_WDT";
    case ESP_RST_TASK_WDT:
      return "ESP_RST_TASK_WDT";
    case ESP_RST_WDT:
      return "ESP_RST_WDT";
    case ESP_RST_DEEPSLEEP:
      return "ESP_RST_DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "ESP_RST_BROWNOUT";
    case ESP_RST_SDIO:
      return "ESP_RST_SDIO";
    default:
      return "ESP_RST_OTHER";
  }
}

bool is_crash_reset_reason(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
      return true;
    default:
      return false;
  }
}

}  // namespace

DeviceController::DeviceController(const Dependencies &deps)
    : deps_(deps),
      runtimeConfig_{},
      renderModel_{},
      drawList_{},
      server_(80),
      bootCount_(0),
      lastBreadcrumbPersistAtMs_(0),
      lastHeartbeatAtMs_(0),
      lastTelemetryAtMs_(0),
      lastDeviceLogHeartbeatAtMs_(0),
      lastLowMemoryWarningAtMs_(0),
      lastRenderAtMs_(0),
      lastWifiDisconnectAtMs_(0),
      lastMqttDisconnectAtMs_(0),
      renderDirty_(true),
      lastMqttConnected_(false),
      bootLogPublished_(false),
      pendingCrashReport_(false),
      pendingWifiConnectedLog_(false),
      pendingWifiDisconnectLog_(false),
      pendingMqttDisconnectLog_(false),
      pendingRenderMode_(RenderMode::kFull),
      etaDirtyRowMask_(0),
      scrollState_{} {
  memset(&renderModel_, 0, sizeof(renderModel_));
  memset(scrollState_, 0, sizeof(scrollState_));
  memset(pendingProvisionToken_, 0, sizeof(pendingProvisionToken_));
  memset(pendingProvisionServerUrl_, 0, sizeof(pendingProvisionServerUrl_));
  memset(pendingCrashReportMetadata_, 0, sizeof(pendingCrashReportMetadata_));
  renderModel_.uiState = UiState::kBooting;
  copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "BOOTING");
  copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "Starting device");
  set_default_rows(renderModel_);
  copy_str(renderModel_.bleName, sizeof(renderModel_.bleName), "");
}

bool DeviceController::begin() {
  if (!deps_.configStore || !deps_.networkManager || !deps_.mqttClient || !deps_.displayEngine ||
      !deps_.layoutEngine) {
    DCTRL_LOGE("CORE", "Device controller begin failed because dependencies were missing");
    return false;
  }

  if (!deps_.configStore->begin() || !deps_.configStore->load(runtimeConfig_)) {
    DCTRL_LOGE("CORE", "Device controller begin failed while loading runtime config");
    return false;
  }
  DCTRL_LOGI("CORE",
             "Runtime config loaded deviceId=%s mqtt=%s:%u apSsid=%s display=%ux%u panels=%ux%u",
             runtimeConfig_.deviceId,
             runtimeConfig_.mqtt.host,
             static_cast<unsigned>(runtimeConfig_.mqtt.port),
             runtimeConfig_.network.apSsid,
             runtimeConfig_.display.panelWidth,
             runtimeConfig_.display.panelHeight,
             runtimeConfig_.display.panelCols,
             runtimeConfig_.display.panelRows);

  const esp_reset_reason_t resetReason = esp_reset_reason();
  if (gDevicePrefs.begin("device", false)) {
    const uint32_t lastUptimeMs = gDevicePrefs.getUInt("last_uptime_ms", 0);
    const uint32_t lastFreeHeap = gDevicePrefs.getUInt("last_free_heap", 0);
    const int32_t lastWifiRssi = gDevicePrefs.getInt("last_wifi_rssi", 0);
    const bool lastWifiConnected = gDevicePrefs.getBool("last_wifi_up", false);
    const bool lastMqttConnected = gDevicePrefs.getBool("last_mqtt_up", false);
    bootCount_ = gDevicePrefs.getUInt("boot_count", 0) + 1;
    gDevicePrefs.putUInt("boot_count", bootCount_);
    if (is_crash_reset_reason(resetReason)) {
      snprintf(pendingCrashReportMetadata_,
               sizeof(pendingCrashReportMetadata_),
               "{\"boot_count\":%lu,\"reset_reason\":\"%s\",\"last_uptime_ms\":%lu,\"last_free_heap\":%lu,\"last_wifi_connected\":%s,\"last_mqtt_connected\":%s,\"last_wifi_rssi\":%ld}",
               static_cast<unsigned long>(bootCount_),
               reset_reason_name(resetReason),
               static_cast<unsigned long>(lastUptimeMs),
               static_cast<unsigned long>(lastFreeHeap),
               lastWifiConnected ? "true" : "false",
               lastMqttConnected ? "true" : "false",
               static_cast<long>(lastWifiRssi));
      pendingCrashReport_ = true;
    }
    gDevicePrefs.end();
  } else {
    bootCount_ = 1;
  }
  DCTRL_LOGI("CORE", "Boot metadata firmware=%s bootCount=%lu resetReason=%s",
             COMMUTELIVE_FIRMWARE_VERSION,
             static_cast<unsigned long>(bootCount_),
             reset_reason_name(resetReason));

  MqttTopics topics{};
  if (!MqttClient::build_default_topics(runtimeConfig_.deviceId, topics)) {
    DCTRL_LOGE("CORE", "Failed to build MQTT topics for deviceId=%s", runtimeConfig_.deviceId);
    return false;
  }

  if (!deps_.mqttClient->begin(runtimeConfig_.mqtt, topics)) {
    DCTRL_LOGE("CORE", "MQTT client begin failed for deviceId=%s", runtimeConfig_.deviceId);
    return false;
  }

  deps_.networkManager->set_state_callback(&DeviceController::on_network_state_change, this);
  deps_.mqttClient->set_command_callback(&DeviceController::on_mqtt_command, this);
  activeController_ = this;
  setup_http_routes();
  // Always start BLE so the user can re-provision even if stale credentials exist.
  // Advertising stops automatically once WiFi connects successfully.
  // Use deviceId as the BLE name so the app can get the ID directly from the scan
  // without needing to read the STATUS characteristic (which is unreliable on iOS).
  bleProvisioner_.begin(runtimeConfig_.deviceId, runtimeConfig_.deviceId);
  bleScanPending_ = false;
  bleProvisioner_.set_scan_callback([](void *ctx) {
    // Do NOT run the scan here — this runs in the NimBLE host task (tiny stack).
    // Set a flag and run from tick() on the main Arduino task instead.
    static_cast<DeviceController *>(ctx)->bleScanPending_ = true;
  }, this);
  deps_.networkManager->begin(runtimeConfig_.network);
  server_.begin();
  DCTRL_LOGI("HTTP", "Core API ready routes=/connect,/device-info,/heartbeat,/status");

  if (!deps_.displayEngine->begin(runtimeConfig_.display)) {
    DCTRL_LOGE("DISPLAY", "Display engine begin failed");
    return false;
  }

  deps_.layoutEngine->set_viewport(deps_.displayEngine->geometry().totalWidth,
                                   deps_.displayEngine->geometry().totalHeight);

  update_ui_state();
  persist_runtime_breadcrumbs(millis(), true);
  schedule_full_render();
  render_frame(millis());
  DCTRL_LOGI("CORE", "Device controller begin complete deviceId=%s", runtimeConfig_.deviceId);
  return true;
}

void DeviceController::tick(uint32_t nowMs) {
  if (bleScanPending_) {
    bleScanPending_ = false;
    wifi_manager::scan_and_emit([](const char *json, void *ctx2) {
      static_cast<DeviceController *>(ctx2)->bleProvisioner_.notify_scan_results(json);
    }, this);
  }

  if (bleProvisioner_.credentials_pending()) {
    const ble::BleCredentials creds = bleProvisioner_.take_credentials();
    // Store token + serverUrl so we can call /device/provision once WiFi connects.
    strncpy(pendingProvisionToken_,     creds.token,     sizeof(pendingProvisionToken_)     - 1);
    strncpy(pendingProvisionServerUrl_, creds.serverUrl, sizeof(pendingProvisionServerUrl_) - 1);
    pendingProvisionToken_[sizeof(pendingProvisionToken_) - 1]         = '\0';
    pendingProvisionServerUrl_[sizeof(pendingProvisionServerUrl_) - 1] = '\0';
    DCTRL_LOGI("BLE", "Applying provisioned credentials ssid=%s passwordLen=%u enterprise=%s token=%s",
               creds.ssid,
               static_cast<unsigned>(strlen(creds.password)),
               core::logging::bool_str(creds.username[0] != '\0'),
               creds.token[0] != '\0' ? "yes" : "no");
    bleProvisioner_.notify_status("{\"status\":\"connecting\"}");
    deps_.mqttClient->disconnect(true);
    deps_.networkManager->set_credentials(creds.ssid, creds.password, creds.username);
    // BLE provisioner is no longer needed after credentials arrive.
    bleProvisioner_.stop();
  }
  deps_.networkManager->tick(nowMs);
  deps_.mqttClient->tick(nowMs);
  const bool mqttConnected = deps_.mqttClient->connected();
  server_.handleClient();
  update_ui_state();

  if (!mqttConnected && lastMqttConnected_) {
    lastMqttDisconnectAtMs_ = nowMs;
    pendingMqttDisconnectLog_ = true;
  }

  if (mqttConnected && !lastMqttConnected_) {
    if (pendingWifiConnectedLog_) {
      publish_device_log("info", "wifi_connected", "WiFi connected");
      pendingWifiConnectedLog_ = false;
    }

    if (pendingMqttDisconnectLog_) {
      char metadata[128];
      snprintf(metadata, sizeof(metadata),
               "{\"disconnect_duration_ms\":%lu}",
               static_cast<unsigned long>(nowMs - lastMqttDisconnectAtMs_));
      publish_device_log("warn", "mqtt_disconnected", "MQTT connection recovered after disconnect", metadata);
      pendingMqttDisconnectLog_ = false;
    }

    publish_device_log("info", "mqtt_connected", "MQTT connected");

    if (!bootLogPublished_) {
      if (pendingCrashReport_) {
        publish_device_log("error",
                           "crash_reboot",
                           "Device restarted after crash",
                           pendingCrashReportMetadata_[0] ? pendingCrashReportMetadata_ : "{}");
        pendingCrashReport_ = false;
      }
      publish_device_log("info", "boot", "Device boot completed");
      bootLogPublished_ = true;
    }
  }
  lastMqttConnected_ = mqttConnected;

  if (mqttConnected) {
    if (nowMs - lastHeartbeatAtMs_ >= kHeartbeatEveryMs) {
      lastHeartbeatAtMs_ = nowMs;

      char payload[128];
      snprintf(payload, sizeof(payload),
               "{\"deviceId\":\"%s\",\"uptimeMs\":%lu,\"freeHeap\":%lu}",
               runtimeConfig_.deviceId,
               static_cast<unsigned long>(nowMs),
               static_cast<unsigned long>(ESP.getFreeHeap()));
      if (!deps_.mqttClient->publish_heartbeat(payload)) {
        publish_device_log("error", "mqtt_publish_failed", "Failed to publish heartbeat", "{\"topic\":\"heartbeat\"}");
      }
    }

    if (nowMs - lastTelemetryAtMs_ >= kTelemetryEveryMs) {
      lastTelemetryAtMs_ = nowMs;
      char payload[160];
      snprintf(payload, sizeof(payload),
               "{\"freeHeap\":%lu,\"maxAlloc\":%lu,\"wifiRssi\":%d}",
               static_cast<unsigned long>(ESP.getFreeHeap()),
               static_cast<unsigned long>(ESP.getMaxAllocHeap()),
               WiFi.RSSI());
      if (!deps_.mqttClient->publish_telemetry(payload)) {
        publish_device_log("error", "mqtt_publish_failed", "Failed to publish telemetry", "{\"topic\":\"telemetry\"}");
      }
    }

    if (nowMs - lastDeviceLogHeartbeatAtMs_ >= kDeviceLogHeartbeatEveryMs) {
      lastDeviceLogHeartbeatAtMs_ = nowMs;
      publish_device_log("info", "heartbeat", "Device heartbeat");
    }

    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap <= kLowHeapWarningThresholdBytes &&
        nowMs - lastLowMemoryWarningAtMs_ >= kLowHeapWarningEveryMs) {
      lastLowMemoryWarningAtMs_ = nowMs;
      publish_device_log("warn", "low_memory", "Device free heap is low");
    }
  }

  persist_runtime_breadcrumbs(nowMs);

  tick_scroll(nowMs);
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
  DCTRL_LOGI("CORE", "Handling network callback state=%s wifi=%s mqtt=%s setupMode=%s",
             network_state_name(state),
             core::logging::wifi_status_name(WiFi.status()),
             core::logging::bool_str(deps_.mqttClient->connected()),
             core::logging::bool_str(deps_.networkManager->setup_mode_active()));
  if (state == NetworkState::kConnected) {
    pendingWifiConnectedLog_ = true;
    if (pendingWifiDisconnectLog_) {
      char metadata[160];
      snprintf(metadata, sizeof(metadata),
               "{\"disconnect_duration_ms\":%lu}",
               static_cast<unsigned long>(millis() - lastWifiDisconnectAtMs_));
      publish_device_log("warn", "wifi_disconnected", "WiFi connection recovered after disconnect", metadata);
      pendingWifiDisconnectLog_ = false;
    }
  } else if (state == NetworkState::kDisconnected || state == NetworkState::kApMode) {
    lastWifiDisconnectAtMs_ = millis();
    pendingWifiDisconnectLog_ = true;
  }

  // Only interact with BLE provisioner when it is active (no prior credentials).
  // Once credentials arrive, tick() stops the provisioner so these are no-ops.
  if (state == NetworkState::kConnected) {
    // If we have a pending provision token, call the server to register this device.
    if (pendingProvisionToken_[0] != '\0') {
      const bool ok = call_provision_api(pendingProvisionServerUrl_, runtimeConfig_.deviceId, pendingProvisionToken_);
      memset(pendingProvisionToken_,     0, sizeof(pendingProvisionToken_));
      memset(pendingProvisionServerUrl_, 0, sizeof(pendingProvisionServerUrl_));
      if (!ok) {
        Serial.println("[PROVISION] Failed — notifying BLE");
        bleProvisioner_.notify_status("{\"status\":\"failed\"}");
        update_ui_state();
        return;
      }
      Serial.println("[PROVISION] Success");
    }
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"connected\",\"deviceId\":\"%s\"}",
             runtimeConfig_.deviceId);
    DCTRL_LOGI("BLE", "Notifying BLE status connected for deviceId=%s", runtimeConfig_.deviceId);
    bleProvisioner_.notify_status(buf);
  } else if (state == NetworkState::kDisconnected) {
    DCTRL_LOGW("BLE", "Notifying BLE status failed because WiFi disconnected");
    bleProvisioner_.notify_status("{\"status\":\"failed\"}");
  }
  update_ui_state();
}

bool DeviceController::call_provision_api(const char *serverUrl, const char *deviceId, const char *token) {
  if (!serverUrl || serverUrl[0] == '\0' || !token || token[0] == '\0') {
    return false;
  }

  char url[256];
  snprintf(url, sizeof(url), "%s/device/provision", serverUrl);

  char body[256];
  snprintf(body, sizeof(body), "{\"deviceId\":\"%s\",\"token\":\"%s\"}", deviceId, token);

  Serial.printf("[PROVISION] POST %s\n", url);

  WiFiClientSecure client;
  client.setInsecure();  // skip cert verification — token provides auth
  HTTPClient http;

  if (!http.begin(client, url)) {
    Serial.println("[PROVISION] http.begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  const int code = http.POST(body);
  http.end();

  Serial.printf("[PROVISION] Response code: %d\n", code);
  return code == 200;
}

void DeviceController::handle_command(const char *topic, const uint8_t *payload, size_t len) {
  if (!payload || len == 0 || len > kMaxPayloadLen) {
    DCTRL_LOGW("MQTT", "Ignoring incoming command topic=%s payloadLen=%u maxPayloadLen=%u",
               core::logging::safe_str(topic),
               static_cast<unsigned>(len),
               static_cast<unsigned>(kMaxPayloadLen));
    return;
  }

  char messageBuf[kMaxPayloadLen + 1];
  memcpy(messageBuf, payload, len);
  messageBuf[len] = '\0';
  const String message(messageBuf);
  const int arrivalsToDisplay = clamp_rows_to_display(extract_json_int_field(message, "arrivalsToDisplay", 1));
  const uint8_t displayType = clamp_display_type(extract_json_int_field(message, "displayType", 1));
  const uint8_t brightnessPercent = parse_brightness_percent(message, kBrightnessFallbackPercent);
  const bool scrolling = extract_json_bool_field(message, "scrolling", false);
  const uint8_t panelBrightness = brightness_percent_to_panel(brightnessPercent);
  DCTRL_LOGI("MQTT", "Incoming command topic=%s len=%u", core::logging::safe_str(topic), static_cast<unsigned>(len));
  DCTRL_LOGI("MQTT", "Incoming payload=%s", message.c_str());

  String cmdType = extract_json_string_field(message, "type");
  DCTRL_LOGI("MQTT", "Parsed command type=%s provider=%s displayType=%u arrivalsToDisplay=%d brightness=%u%% panel=%u",
             cmdType.length() ? cmdType.c_str() : "(data)",
             extract_json_string_field(message, "provider").c_str(),
             static_cast<unsigned>(displayType),
             arrivalsToDisplay,
             static_cast<unsigned>(brightnessPercent),
             static_cast<unsigned>(panelBrightness));
  if (cmdType == "ota_update") {
    String url = extract_json_string_field(message, "url");
    if (url.length() > 0) {
      DCTRL_LOGI("OTA", "Received OTA update request url=%s", url.c_str());
      perform_ota_update(url);
    }
    return;
  }

  if (cmdType == "factory_reset") {
    DCTRL_LOGW("CMD", "Factory reset requested; clearing credentials and restarting");
    deps_.mqttClient->disconnect(true);
    wifi_manager::clear_credentials();
    delay(500);
    ESP.restart();
    return;
  }

  if (cmdType == "disconnect_wifi") {
    handle_disconnect_wifi_command(message);
    return;
  }

  if (cmdType == "display_blank") {
    handle_display_blank_command(message, brightnessPercent, panelBrightness);
    return;
  }

  String provider = extract_json_string_field(message, "provider");
  if (provider.length() == 0 || !parsing::is_supported_provider_id(provider)) {
    DCTRL_LOGW("MQTT", "Ignoring payload because provider is unsupported provider=%s", provider.c_str());
    renderModel_.hasData = false;
    schedule_full_render();
    return;
  }

  parsing::ProviderPayload parsed{};
  if (!parsing::parse_provider_payload(provider, message, parsed) || !parsed.hasRow1) {
    DCTRL_LOGW("MQTT", "Ignoring payload because parser returned no row data provider=%s", provider.c_str());
    return;
  }

  const String row1Direction = extract_row_direction_label(message, 0);
  const String row2Direction = extract_row_direction_label(message, 1);
  const String row1DisplayLabel = extract_row_display_label(message, 0);
  const String row2DisplayLabel = extract_row_display_label(message, 1);
  const uint8_t row1DisplayType = extract_row_display_type(message, 0, displayType);
  const uint8_t row2DisplayType = extract_row_display_type(message, 1, displayType);
  char row1EtaExtra[kMaxDestinationLen];
  char row2EtaExtra[kMaxDestinationLen];
  row1EtaExtra[0] = '\0';
  row2EtaExtra[0] = '\0';
  const bool row1CompactExtraEtaPreset = (row1DisplayType == 4 || row1DisplayType == 5);
  const bool row2CompactExtraEtaPreset = (row2DisplayType == 4 || row2DisplayType == 5);
  if (row1CompactExtraEtaPreset || row2CompactExtraEtaPreset) {
    extract_row_eta_extra(message, 0, row1EtaExtra, sizeof(row1EtaExtra));
    extract_row_eta_extra(message, 1, row2EtaExtra, sizeof(row2EtaExtra));
  }

  const String row1Provider = parsed.row1.provider.length() ? parsed.row1.provider : provider;
  if (!parsing::is_supported_provider_id(row1Provider)) {
    DCTRL_LOGW("MQTT", "Ignoring payload because row1 provider is unsupported provider=%s", row1Provider.c_str());
    return;
  }

  if (parsed.hasRow2) {
    const String row2Provider = parsed.row2.provider.length() ? parsed.row2.provider : provider;
    if (!parsing::is_supported_provider_id(row2Provider)) {
      DCTRL_LOGW("MQTT", "Ignoring payload because row2 provider is unsupported provider=%s", row2Provider.c_str());
      return;
    }
  }

  RenderModel nextModel = renderModel_;
  copy_str(nextModel.rows[0].providerId, sizeof(nextModel.rows[0].providerId),
           row1Provider.c_str());
  copy_str(nextModel.rows[0].routeId, sizeof(nextModel.rows[0].routeId),
           parsed.row1.line.length() ? parsed.row1.line.c_str() : "--");
  nextModel.rows[0].displayType = row1DisplayType;
  nextModel.rows[0].scrollEnabled = extract_row_scrolling(message, 0, scrolling);
  copy_str(nextModel.rows[0].direction, sizeof(nextModel.rows[0].direction),
           row1Direction.c_str());
  copy_str(nextModel.rows[0].destination, sizeof(nextModel.rows[0].destination),
           row1DisplayLabel.length() ? row1DisplayLabel.c_str() :
           (parsed.row1.label.length() ? parsed.row1.label.c_str() : nextModel.rows[0].routeId));
  normalize_eta(parsed.row1.eta, nextModel.rows[0].eta, sizeof(nextModel.rows[0].eta));
  copy_str(nextModel.rows[0].etaExtra, sizeof(nextModel.rows[0].etaExtra),
           row1CompactExtraEtaPreset ? row1EtaExtra : "");

  if (parsed.hasRow2) {
    const String row2Provider = parsed.row2.provider.length() ? parsed.row2.provider : provider;
    copy_str(nextModel.rows[1].providerId, sizeof(nextModel.rows[1].providerId),
             row2Provider.c_str());
    copy_str(nextModel.rows[1].routeId, sizeof(nextModel.rows[1].routeId),
             parsed.row2.line.length() ? parsed.row2.line.c_str() : "--");
    nextModel.rows[1].displayType = row2DisplayType;
    nextModel.rows[1].scrollEnabled = extract_row_scrolling(message, 1, scrolling);
    copy_str(nextModel.rows[1].direction, sizeof(nextModel.rows[1].direction),
             row2Direction.c_str());
    copy_str(nextModel.rows[1].destination, sizeof(nextModel.rows[1].destination),
             row2DisplayLabel.length() ? row2DisplayLabel.c_str() :
             (parsed.row2.label.length() ? parsed.row2.label.c_str() : nextModel.rows[1].routeId));
    normalize_eta(parsed.row2.eta, nextModel.rows[1].eta, sizeof(nextModel.rows[1].eta));
    copy_str(nextModel.rows[1].etaExtra, sizeof(nextModel.rows[1].etaExtra),
             row2CompactExtraEtaPreset ? row2EtaExtra : "");

    nextModel.activeRows = 2;
    for (uint8_t i = 2; i < kMaxTransitRows; ++i) {
      clear_row(nextModel.rows[i]);
    }
  } else {
    String row1Item;
    const bool hasExplicitRow1Eta =
        extract_lines_object_at(message, 0, row1Item) && extract_json_string_field(row1Item, "eta").length() > 0;
    char etaParts[kMaxTransitRows][kMaxEtaLen];
    const int etaCount = hasExplicitRow1Eta ? 0 : split_eta_tokens(nextModel.rows[0].eta, etaParts);
    int rowsToRender = arrivalsToDisplay;
    if (etaCount > 0 && rowsToRender > etaCount) {
      rowsToRender = etaCount;
    }
    if (row1CompactExtraEtaPreset) {
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
      copy_str(nextModel.rows[0].eta, sizeof(nextModel.rows[0].eta), etaParts[0]);
    }
    if (row1CompactExtraEtaPreset && row1EtaExtra[0] != '\0') {
      copy_str(nextModel.rows[0].etaExtra, sizeof(nextModel.rows[0].etaExtra), row1EtaExtra);
    } else if (row1CompactExtraEtaPreset && etaCount > 1) {
      char extraBuf[kMaxDestinationLen];
      extraBuf[0] = '\0';
      for (int i = 1; i < etaCount; ++i) {
        if (etaParts[i][0] == '\0') continue;
        if (extraBuf[0] != '\0') {
          strncat(extraBuf, ",", sizeof(extraBuf) - strlen(extraBuf) - 1);
        }
        strncat(extraBuf, etaParts[i], sizeof(extraBuf) - strlen(extraBuf) - 1);
      }
      copy_str(nextModel.rows[0].etaExtra, sizeof(nextModel.rows[0].etaExtra), extraBuf);
    } else {
      copy_str(nextModel.rows[0].etaExtra, sizeof(nextModel.rows[0].etaExtra), "");
    }

    for (int i = 1; i < rowsToRender; ++i) {
      copy_str(nextModel.rows[i].providerId, sizeof(nextModel.rows[i].providerId),
               nextModel.rows[0].providerId);
      copy_str(nextModel.rows[i].routeId, sizeof(nextModel.rows[i].routeId),
               nextModel.rows[0].routeId);
      nextModel.rows[i].displayType = nextModel.rows[0].displayType;
      nextModel.rows[i].scrollEnabled = nextModel.rows[0].scrollEnabled;
      copy_str(nextModel.rows[i].direction, sizeof(nextModel.rows[i].direction),
               nextModel.rows[0].direction);
      copy_str(nextModel.rows[i].destination, sizeof(nextModel.rows[i].destination),
               nextModel.rows[0].destination);
      copy_str(nextModel.rows[i].etaExtra, sizeof(nextModel.rows[i].etaExtra), "");
      if (i < etaCount) {
        copy_str(nextModel.rows[i].eta, sizeof(nextModel.rows[i].eta), etaParts[i]);
      } else {
        copy_str(nextModel.rows[i].eta, sizeof(nextModel.rows[i].eta), "--");
      }
    }

    nextModel.activeRows = static_cast<uint8_t>(rowsToRender);
    for (int i = rowsToRender; i < static_cast<int>(kMaxTransitRows); ++i) {
      clear_row(nextModel.rows[i]);
    }
  }

  nextModel.hasData = true;
  nextModel.displayType = displayType;
  nextModel.uiState = UiState::kTransit;
  nextModel.updatedAtMs = millis();

  uint8_t etaDirtyRows = 0;
  const RenderMode nextRenderMode =
      classify_render_mode(renderModel_, nextModel, runtimeConfig_.display.doubleBuffered, etaDirtyRows);

  // Reset scroll state when destination text or per-row scroll setting changes
  bool anyScrollReset = false;
  for (uint8_t i = 0; i < kMaxTransitRows; ++i) {
    if (renderModel_.rows[i].scrollEnabled != nextModel.rows[i].scrollEnabled ||
        strcmp(renderModel_.rows[i].destination, nextModel.rows[i].destination) != 0 ||
        strcmp(renderModel_.rows[i].direction,   nextModel.rows[i].direction)   != 0) {
      reset_scroll_state(i);
      anyScrollReset = true;
    }
  }

  renderModel_ = nextModel;

  // If scroll state was reset (e.g. scroll toggled off), force full redraw so text
  // renders at offset 0 (truncated/cutoff) instead of frozen at mid-scroll position
  if (anyScrollReset) {
    schedule_full_render();
  }

  runtimeConfig_.display.brightness = panelBrightness;
  deps_.displayEngine->set_brightness(panelBrightness);

  switch (nextRenderMode) {
    case RenderMode::kNone:
      schedule_no_render();
      DCTRL_LOGI("MQTT", "Applied payload without render change activeRows=%u displayType=%u brightness=%u%% panel=%u",
                 static_cast<unsigned>(renderModel_.activeRows),
                 static_cast<unsigned>(renderModel_.displayType),
                 static_cast<unsigned>(brightnessPercent),
                 static_cast<unsigned>(panelBrightness));
      break;
    case RenderMode::kEtaOnly:
      schedule_eta_render(etaDirtyRows);
      DCTRL_LOGI("MQTT",
                 "Applied payload with ETA-only render rowsMask=0x%02x activeRows=%u displayType=%u brightness=%u%% panel=%u",
                 static_cast<unsigned>(etaDirtyRows),
                 static_cast<unsigned>(renderModel_.activeRows),
                 static_cast<unsigned>(renderModel_.displayType),
                 static_cast<unsigned>(brightnessPercent),
                 static_cast<unsigned>(panelBrightness));
      break;
    case RenderMode::kFull:
    default:
      schedule_full_render();
      DCTRL_LOGI("MQTT", "Applied payload with full render activeRows=%u displayType=%u brightness=%u%% panel=%u",
                 static_cast<unsigned>(renderModel_.activeRows),
                 static_cast<unsigned>(renderModel_.displayType),
                 static_cast<unsigned>(brightnessPercent),
                 static_cast<unsigned>(panelBrightness));
      break;
  }

  DCTRL_LOGI("MQTT",
             "Applied payload displayType=%u brightness=%u%% panel=%u activeRows=%u row1={provider:%s line:%s eta:%s extra:%s} row2={provider:%s line:%s eta:%s extra:%s}",
             static_cast<unsigned>(renderModel_.displayType),
             static_cast<unsigned>(brightnessPercent),
             static_cast<unsigned>(panelBrightness),
             static_cast<unsigned>(renderModel_.activeRows),
             renderModel_.rows[0].providerId,
             renderModel_.rows[0].routeId,
             renderModel_.rows[0].eta,
             renderModel_.rows[0].etaExtra,
             renderModel_.rows[1].providerId,
             renderModel_.rows[1].routeId,
             renderModel_.rows[1].eta,
             renderModel_.rows[1].etaExtra);
  publish_display_state();
}

void DeviceController::handle_display_blank_command(const String &message,
                                                    uint8_t brightnessPercent,
                                                    uint8_t panelBrightness) {
  const String reason = extract_json_string_field(message, "reason");

  renderModel_.hasData = false;
  renderModel_.uiState = UiState::kBlank;
  renderModel_.displayType = kMinDisplayType;
  renderModel_.activeRows = 0;
  renderModel_.updatedAtMs = millis();
  copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "");
  copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "");
  for (uint8_t i = 0; i < kMaxTransitRows; ++i) {
    clear_row(renderModel_.rows[i]);
    reset_scroll_state(i);
  }

  runtimeConfig_.display.brightness = panelBrightness;
  deps_.displayEngine->set_brightness(panelBrightness);

  schedule_full_render();
  DCTRL_LOGI("MQTT",
             "Applied display blank reason=%s brightness=%u%% panel=%u",
             reason.length() ? reason.c_str() : "(none)",
             static_cast<unsigned>(brightnessPercent),
             static_cast<unsigned>(panelBrightness));
  publish_display_state();
}

void DeviceController::handle_disconnect_wifi_command(const String &message) {
  const String reason = extract_json_string_field(message, "reason");
  const bool clearCredentials = extract_json_bool_field(message, "clearCredentials", false);
  const bool restartProvisioning = extract_json_bool_field(message, "restartProvisioning", false);

  Serial.printf("[CMD] Disconnect WiFi requested reason=%s clearCredentials=%s restartProvisioning=%s\n",
                reason.length() ? reason.c_str() : "(none)",
                clearCredentials ? "true" : "false",
                restartProvisioning ? "true" : "false");

  deps_.mqttClient->disconnect(true);
  deps_.networkManager->disconnect(clearCredentials, restartProvisioning);

  if (restartProvisioning) {
    bleProvisioner_.begin(runtimeConfig_.deviceId, runtimeConfig_.deviceId);
  } else {
    bleProvisioner_.stop();
  }

  renderModel_.hasData = false;
  set_default_rows(renderModel_);
  update_ui_state();
  schedule_full_render();
}

void DeviceController::setup_http_routes() {
  server_.on("/connect", HTTP_POST, &DeviceController::http_connect_handler);
  server_.on("/device-info", HTTP_GET, &DeviceController::http_device_info_handler);
  server_.on("/heartbeat", HTTP_GET, &DeviceController::http_heartbeat_handler);
  server_.on("/status", HTTP_GET, &DeviceController::http_status_handler);
  DCTRL_LOGI("HTTP", "Registered HTTP routes");
}

void DeviceController::http_connect_handler() {
  if (!activeController_) {
    return;
  }

  String ssid;
  String pass;
  String user;
  DCTRL_LOGI("HTTP", "Received /connect request");
  if (!wifi_manager::handle_connect_request(activeController_->server_, ssid, pass, user)) {
    return;
  }

  // Intentional credential switch: explicitly mark device offline before reconnecting Wi-Fi.
  DCTRL_LOGI("HTTP", "Switching credentials from /connect ssid=%s enterprise=%s",
             ssid.c_str(),
             core::logging::bool_str(user.length() > 0));
  activeController_->deps_.mqttClient->disconnect(true);
  activeController_->deps_.networkManager->set_credentials(ssid.c_str(), pass.c_str(), user.c_str());
  activeController_->server_.send(200, "application/json", "{\"ok\":true}");
}

void DeviceController::http_device_info_handler() {
  if (!activeController_) {
    return;
  }

  DCTRL_LOGI("HTTP", "Serving /device-info");
  char response[128];
  snprintf(response, sizeof(response), "{\"deviceId\":\"%s\"}", activeController_->runtimeConfig_.deviceId);
  activeController_->server_.send(200, "application/json", response);
}

void DeviceController::http_heartbeat_handler() {
  if (!activeController_) {
    return;
  }
  DCTRL_LOGD("HTTP", "Serving /heartbeat");
  activeController_->server_.send(200, "application/json", "{\"ok\":true}");
}

void DeviceController::http_status_handler() {
  if (!activeController_) {
    return;
  }
  const bool wifiConnected = activeController_->deps_.networkManager->is_connected();
  DCTRL_LOGI("HTTP", "Serving /status wifiConnected=%s mqttConnected=%s uiState=%s",
             core::logging::bool_str(wifiConnected),
             core::logging::bool_str(activeController_->deps_.mqttClient->connected()),
             ui_state_name(activeController_->renderModel_.uiState));
  char response[192];
  snprintf(response, sizeof(response),
           "{\"deviceId\":\"%s\",\"wifiConnected\":%s,\"firmwareVersion\":\"%s\"}",
           activeController_->runtimeConfig_.deviceId,
           wifiConnected ? "true" : "false",
           COMMUTELIVE_FIRMWARE_VERSION);
  activeController_->server_.send(200, "application/json", response);
}

void DeviceController::schedule_full_render() {
  renderDirty_ = true;
  pendingRenderMode_ = RenderMode::kFull;
  etaDirtyRowMask_ = 0;
}

void DeviceController::schedule_eta_render(uint8_t rowMask) {
  if (rowMask == 0 || pendingRenderMode_ == RenderMode::kFull) {
    return;
  }
  renderDirty_ = true;
  pendingRenderMode_ = RenderMode::kEtaOnly;
  etaDirtyRowMask_ = static_cast<uint8_t>(etaDirtyRowMask_ | rowMask);
}

void DeviceController::schedule_scroll_render() {
  // Only upgrade to scroll render if no higher-priority render is pending
  if (pendingRenderMode_ == RenderMode::kFull || pendingRenderMode_ == RenderMode::kEtaOnly) {
    return;
  }
  renderDirty_ = true;
  pendingRenderMode_ = RenderMode::kScrollOnly;
}

void DeviceController::reset_scroll_state(uint8_t rowIndex) {
  if (rowIndex >= kMaxTransitRows) return;
  scrollState_[rowIndex].offset = 0;
  scrollState_[rowIndex].textPixelWidth = 0;
  scrollState_[rowIndex].budgetWidth = 0;
  scrollState_[rowIndex].pauseUntilMs = 0;
  scrollState_[rowIndex].resetPending = false;
  scrollState_[rowIndex].active = false;
}

void DeviceController::tick_scroll(uint32_t nowMs) {
  if (renderModel_.uiState != UiState::kTransit) return;

  bool anyActive = false;
  bool scrollActivationChanged = false;
  uint8_t activeScrollRows[kMaxTransitRows];
  uint8_t activeScrollCount = 0;
  int8_t masterRowIndex = -1;
  int16_t maxOverflowPx = -1;

  for (uint8_t i = 0; i < renderModel_.activeRows; ++i) {
    if (!renderModel_.rows[i].scrollEnabled) continue;
    RowScrollState &s = scrollState_[i];

    // Measure text width on first tick for this row
    if (s.textPixelWidth == 0) {
      TransitRowGeometry geom{};
      if (!deps_.layoutEngine->compute_transit_row_geometry(renderModel_, i, geom)) continue;
      const char *text = renderModel_.rows[i].destination[0]
          ? renderModel_.rows[i].destination
          : renderModel_.rows[i].direction;
      const display::TextMetrics tm = deps_.displayEngine->measure_text(text, geom.destinationFont);
      s.textPixelWidth = static_cast<int16_t>(tm.width);
      s.budgetWidth = geom.effectiveDestinationWidth > kScrollEtaSafetyGapPx
          ? static_cast<int16_t>(geom.effectiveDestinationWidth - kScrollEtaSafetyGapPx)
          : geom.effectiveDestinationWidth;
      // Only activate scroll if text overflows
      s.active = s.textPixelWidth > s.budgetWidth;
      if (s.active) {
        scrollActivationChanged = true;
        s.pauseUntilMs = nowMs + kScrollStartPauseMs;
      }
    }

    if (!s.active) continue;

    anyActive = true;
    activeScrollRows[activeScrollCount++] = i;
    const int16_t overflowPx = static_cast<int16_t>(s.textPixelWidth - s.budgetWidth);
    if (overflowPx > maxOverflowPx) {
      maxOverflowPx = overflowPx;
      masterRowIndex = static_cast<int8_t>(i);
    }
  }

  if (!anyActive || masterRowIndex < 0) {
    return;
  }

  auto advance_scroll_state = [&](RowScrollState &s) {
    if (nowMs < s.pauseUntilMs) {
      return;
    }

    if (s.resetPending) {
      s.offset = 0;
      s.resetPending = false;
      s.pauseUntilMs = nowMs + kScrollLoopPauseMs;
      return;
    }

    s.offset = static_cast<int16_t>(s.offset - 1);
    if (-s.offset >= s.textPixelWidth - s.budgetWidth) {
      s.offset = static_cast<int16_t>(s.budgetWidth - s.textPixelWidth);
      s.resetPending = true;
      s.pauseUntilMs = nowMs + kScrollLoopPauseMs;
    }
  };

  if (activeScrollCount > 1 && scrollActivationChanged) {
    for (uint8_t idx = 0; idx < activeScrollCount; ++idx) {
      RowScrollState &s = scrollState_[activeScrollRows[idx]];
      s.offset = 0;
      s.pauseUntilMs = nowMs + kScrollStartPauseMs;
      s.resetPending = false;
    }
    schedule_scroll_render();
    return;
  }

  RowScrollState &master = scrollState_[masterRowIndex];
  advance_scroll_state(master);

  if (activeScrollCount > 1) {
    for (uint8_t idx = 0; idx < activeScrollCount; ++idx) {
      const uint8_t rowIndex = activeScrollRows[idx];
      if (rowIndex == static_cast<uint8_t>(masterRowIndex)) continue;
      RowScrollState &s = scrollState_[rowIndex];
      const int16_t followerMinOffset = static_cast<int16_t>(s.budgetWidth - s.textPixelWidth);
      int16_t syncedOffset = master.offset;
      if (syncedOffset < followerMinOffset) {
        syncedOffset = followerMinOffset;
      }
      if (syncedOffset > 0) {
        syncedOffset = 0;
      }
      s.offset = syncedOffset;
      s.pauseUntilMs = master.pauseUntilMs;
      s.resetPending = master.resetPending;
    }
  }

  schedule_scroll_render();
}

void DeviceController::schedule_no_render() {
  if (renderDirty_) {
    return;
  }
  pendingRenderMode_ = RenderMode::kNone;
  etaDirtyRowMask_ = 0;
}

void DeviceController::update_ui_state() {
  const UiState previousState = renderModel_.uiState;
  char prevStatus[kMaxStatusLen];
  char prevDetail[kMaxDestinationLen];
  copy_str(prevStatus, sizeof(prevStatus), renderModel_.statusLine);
  copy_str(prevDetail, sizeof(prevDetail), renderModel_.statusDetail);

  const bool wifiUp = deps_.networkManager->is_connected();
  const bool mqttUp = deps_.mqttClient->connected();

  if (renderModel_.uiState == UiState::kBlank && !renderModel_.hasData) {
  } else if (renderModel_.hasData && wifiUp && mqttUp) {
    renderModel_.uiState = UiState::kTransit;
    copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "TRANSIT");
    copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "Live arrivals");
  } else if (deps_.networkManager->setup_mode_active() && !wifiUp) {
    renderModel_.uiState = UiState::kSetupMode;
    copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "SET UP");
    copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "Scan QR and use the app");
    copy_str(renderModel_.apSsid, sizeof(renderModel_.apSsid), runtimeConfig_.network.apSsid);
    copy_str(renderModel_.apPin, sizeof(renderModel_.apPin), runtimeConfig_.network.apPassword);
    copy_str(renderModel_.bleName, sizeof(renderModel_.bleName), runtimeConfig_.deviceId);
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
    copy_str(renderModel_.statusLine, sizeof(renderModel_.statusLine), "ADD A LINE");
    copy_str(renderModel_.statusDetail, sizeof(renderModel_.statusDetail), "Open the app to get started");
  }

  if (previousState != renderModel_.uiState ||
      strcmp(prevStatus, renderModel_.statusLine) != 0 ||
      strcmp(prevDetail, renderModel_.statusDetail) != 0) {
    schedule_full_render();
    DCTRL_LOGI("UI",
               "UI state changed %s -> %s status='%s' detail='%s' wifiUp=%s mqttUp=%s hasData=%s networkState=%s",
               ui_state_name(previousState),
               ui_state_name(renderModel_.uiState),
               renderModel_.statusLine,
               renderModel_.statusDetail,
               core::logging::bool_str(wifiUp),
               core::logging::bool_str(mqttUp),
               core::logging::bool_str(renderModel_.hasData),
               network_state_name(deps_.networkManager->state()));
  }
}

void DeviceController::render_eta_updates() {
  DCTRL_LOGI("DISPLAY", "Rendering ETA-only update rowsMask=0x%02x activeRows=%u displayType=%u",
             static_cast<unsigned>(etaDirtyRowMask_),
             static_cast<unsigned>(renderModel_.activeRows),
             static_cast<unsigned>(renderModel_.displayType));

  char etaText[kMaxEtaLen];
  char etaExtraText[kMaxDestinationLen];
  for (uint8_t i = 0; i < renderModel_.activeRows && i < kMaxTransitRows; ++i) {
    if ((etaDirtyRowMask_ & static_cast<uint8_t>(1U << i)) == 0) {
      continue;
    }

    TransitRowGeometry geometry{};
    if (!deps_.layoutEngine->compute_transit_row_geometry(renderModel_, i, geometry)) {
      DCTRL_LOGW("DISPLAY", "Falling back to full redraw because ETA geometry lookup failed row=%u",
                 static_cast<unsigned>(i));
      schedule_full_render();
      return;
    }

    const TransitRowModel &row = renderModel_.rows[i];
    deps_.displayEngine->fill_rect(geometry.etaClearX, geometry.etaClearY, geometry.etaClearW, geometry.etaClearH, kColorBlack);
    trim_text_for_chars(row.eta[0] ? row.eta : "--", 3, etaText, sizeof(etaText));
    deps_.displayEngine->draw_text(geometry.etaTextX,
                                   geometry.etaTextY,
                                   etaText,
                                   LayoutEngine::eta_color_for_text(row.eta),
                                   geometry.etaFont,
                                   kColorBlack);

    if (geometry.hasEtaExtra) {
      deps_.displayEngine->fill_rect(geometry.etaExtraClearX,
                                     geometry.etaExtraClearY,
                                     geometry.etaExtraClearW,
                                     geometry.etaExtraClearH,
                                     kColorBlack);
      if (row.etaExtra[0] != '\0' && geometry.etaExtraCharLimit > 0) {
        trim_text_for_chars(row.etaExtra, geometry.etaExtraCharLimit, etaExtraText, sizeof(etaExtraText));
        deps_.displayEngine->draw_text(geometry.etaExtraTextX,
                                       geometry.etaExtraTextY,
                                       etaExtraText,
                                       kColorAmber,
                                       geometry.etaExtraFont,
                                       kColorBlack);
      }
    }
  }

  deps_.displayEngine->present();
  renderDirty_ = false;
  pendingRenderMode_ = RenderMode::kNone;
  etaDirtyRowMask_ = 0;
}

void DeviceController::render_scroll_updates() {
  for (uint8_t i = 0; i < renderModel_.activeRows && i < kMaxTransitRows; ++i) {
    const RowScrollState &s = scrollState_[i];
    if (!s.active) continue;

    TransitRowGeometry geom{};
    if (!deps_.layoutEngine->compute_transit_row_geometry(renderModel_, i, geom)) {
      schedule_full_render();
      return;
    }

    const TransitRowModel &row = renderModel_.rows[i];
    const char *text = row.destination[0] ? row.destination : row.direction;

    // Clear only the destination zone (badge and ETA are untouched)
    deps_.displayEngine->fill_rect(
        geom.destinationX,
        geom.destinationY,
        geom.effectiveDestinationWidth,
        static_cast<int16_t>(8 * geom.destinationFont),
        kColorBlack);

    // Draw characters one-by-one, strictly clipped to the destination zone.
    // Only characters fully within [destinationX, destinationX + effectiveDestinationWidth)
    // are rendered — no pixels ever bleed into the badge or ETA areas.
    const int16_t charW = static_cast<int16_t>(6 * geom.destinationFont);
    const int16_t clipLeft = geom.destinationX;
    const int16_t clipWidth = geom.effectiveDestinationWidth > kScrollEtaSafetyGapPx
        ? static_cast<int16_t>(geom.effectiveDestinationWidth - kScrollEtaSafetyGapPx)
        : geom.effectiveDestinationWidth;
    const int16_t clipRight = static_cast<int16_t>(geom.destinationX + clipWidth);
    int16_t cx = static_cast<int16_t>(geom.destinationX + s.offset);
    char buf[2] = {0, 0};
    for (const char *p = text; *p; ++p, cx = static_cast<int16_t>(cx + charW)) {
      if (cx < clipLeft) continue;           // starts left of zone — skip
      if (cx + charW > clipRight) break;     // extends past right of zone — done
      buf[0] = *p;
      deps_.displayEngine->draw_text(cx, geom.destinationY, buf, 0xFFFF, geom.destinationFont, kColorBlack);
    }
  }

  deps_.displayEngine->present();
  renderDirty_ = false;
  pendingRenderMode_ = RenderMode::kNone;
}

bool DeviceController::publish_device_log(const char *status,
                                          const char *eventType,
                                          const char *message,
                                          const char *metadataJson) {
  if (!deps_.mqttClient->connected()) {
    DCTRL_LOGW("MQTT", "Skipping device log publish because MQTT is offline eventType=%s",
               core::logging::safe_str(eventType));
    return false;
  }

  char safeMessage[160];
  char safeStatus[16];
  char safeEventType[64];
  char safeFirmware[32];
  char safeEnv[24];
  char safeResetReason[32];
  json_escape(core::logging::safe_str(message), safeMessage, sizeof(safeMessage));
  json_escape(core::logging::safe_str(status), safeStatus, sizeof(safeStatus));
  json_escape(core::logging::safe_str(eventType), safeEventType, sizeof(safeEventType));
  json_escape(COMMUTELIVE_FIRMWARE_VERSION, safeFirmware, sizeof(safeFirmware));
  json_escape(COMMUTELIVE_DEVICE_ENV, safeEnv, sizeof(safeEnv));
  json_escape(reset_reason_name(esp_reset_reason()), safeResetReason, sizeof(safeResetReason));

  char payload[kMaxPayloadLen + 1];
  const int written = snprintf(
      payload,
      sizeof(payload),
      "{\"message\":\"%s\",\"service\":\"device-controller\",\"source\":\"esp32\",\"status\":\"%s\",\"device_id\":\"%s\",\"firmware_version\":\"%s\",\"env\":\"%s\",\"reset_reason\":\"%s\",\"uptime_ms\":%lu,\"free_heap\":%lu,\"wifi_connected\":%s,\"mqtt_connected\":%s,\"boot_count\":%lu,\"event_type\":\"%s\",\"metadata\":%s}",
      safeMessage,
      safeStatus,
      runtimeConfig_.deviceId,
      safeFirmware,
      safeEnv,
      safeResetReason,
      static_cast<unsigned long>(millis()),
      static_cast<unsigned long>(ESP.getFreeHeap()),
      deps_.networkManager->is_connected() ? "true" : "false",
      deps_.mqttClient->connected() ? "true" : "false",
      static_cast<unsigned long>(bootCount_),
      safeEventType,
      metadataJson ? metadataJson : "{}");

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    DCTRL_LOGE("MQTT", "Device log payload exceeded buffer eventType=%s", core::logging::safe_str(eventType));
    return false;
  }

  if (!deps_.mqttClient->publish_log(payload)) {
    DCTRL_LOGE("MQTT", "Failed to publish device log eventType=%s", core::logging::safe_str(eventType));
    return false;
  }

  DCTRL_LOGI("MQTT", "Published device log eventType=%s status=%s message=%s",
             core::logging::safe_str(eventType),
             core::logging::safe_str(status),
             core::logging::safe_str(message));
  return true;
}

void DeviceController::persist_runtime_breadcrumbs(uint32_t nowMs, bool force) {
  if (!force && nowMs - lastBreadcrumbPersistAtMs_ < kCrashBreadcrumbPersistEveryMs) {
    return;
  }

  if (!gDevicePrefs.begin("device", false)) {
    return;
  }

  gDevicePrefs.putUInt("last_uptime_ms", nowMs);
  gDevicePrefs.putUInt("last_free_heap", ESP.getFreeHeap());
  gDevicePrefs.putInt("last_wifi_rssi", WiFi.RSSI());
  gDevicePrefs.putBool("last_wifi_up", deps_.networkManager->is_connected());
  gDevicePrefs.putBool("last_mqtt_up", deps_.mqttClient->connected());
  gDevicePrefs.end();
  lastBreadcrumbPersistAtMs_ = nowMs;
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
    DCTRL_LOGW("DISPLAY", "Skipping render because display frame could not begin");
    return;
  }

  if (pendingRenderMode_ == RenderMode::kEtaOnly) {
    render_eta_updates();
    return;
  }

  if (pendingRenderMode_ == RenderMode::kScrollOnly) {
    render_scroll_updates();
    return;
  }

  DCTRL_LOGI("DISPLAY", "Rendering frame uiState=%s activeRows=%u displayType=%u status='%s'",
             ui_state_name(renderModel_.uiState),
             static_cast<unsigned>(renderModel_.activeRows),
             static_cast<unsigned>(renderModel_.displayType),
             renderModel_.statusLine);
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
        gBadgeRenderer.draw_badge(*deps_.displayEngine, cmd.x, cmd.y, cmd.w, cmd.text, cmd.color);
        break;
      case DrawCommandType::kRectBadge:
        gBadgeRenderer.draw_rect_badge(*deps_.displayEngine, cmd.x, cmd.y, cmd.w, cmd.h, cmd.text, cmd.color);
        break;
      case DrawCommandType::kMonoBitmap:
        if (!cmd.bitmap || cmd.w <= 0 || cmd.h <= 0) {
          break;
        }
        for (int16_t y = 0; y < cmd.h; ++y) {
          for (int16_t x = 0; x < cmd.w; ++x) {
            const uint8_t pixel = cmd.bitmap[static_cast<size_t>(y) * static_cast<size_t>(cmd.w) + static_cast<size_t>(x)];
            deps_.displayEngine->draw_pixel(static_cast<int16_t>(cmd.x + x),
                                            static_cast<int16_t>(cmd.y + y),
                                            pixel ? cmd.color : cmd.bg);
          }
        }
        break;
      default:
        break;
    }
  }

  deps_.displayEngine->present();
  renderDirty_ = false;
  pendingRenderMode_ = RenderMode::kNone;
  etaDirtyRowMask_ = 0;
}

void DeviceController::publish_display_state() {
  if (!deps_.mqttClient->connected()) {
    DCTRL_LOGW("MQTT", "Skipping display state publish because MQTT is offline");
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

  char payload[768];
  const uint8_t reportedRows = renderModel_.activeRows > kMaxVisibleTransitRows ? kMaxVisibleTransitRows : renderModel_.activeRows;
  snprintf(payload,
           sizeof(payload),
           "{\"deviceId\":\"%s\",\"activeRows\":%u,\"row1\":{\"provider\":\"%s\",\"line\":\"%s\",\"label\":\"%s\",\"eta\":\"%s\"},\"row2\":{\"provider\":\"%s\",\"line\":\"%s\",\"label\":\"%s\",\"eta\":\"%s\"}}",
           runtimeConfig_.deviceId,
           static_cast<unsigned>(reportedRows),
           r1Provider,
           r1Line,
           r1Label,
           r1Eta,
           r2Provider,
           r2Line,
           r2Label,
           r2Eta);

  DCTRL_LOGI("MQTT", "Publishing display state payload=%s", payload);
  deps_.mqttClient->publish_state(payload, false);
}

bool DeviceController::perform_ota_update(const String& url) {
  DCTRL_LOGI("OTA", "Starting update from %s", url.c_str());
  HTTPClient http;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    secureClient.setTimeout(10000);
    http.begin(secureClient, url);
  } else {
    plainClient.setTimeout(10000);
    http.begin(plainClient, url);
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    DCTRL_LOGE("OTA", "HTTP GET failed code=%d", code);
    http.end();
    return false;
  }
  int contentLen = http.getSize();
  DCTRL_LOGI("OTA", "HTTP GET succeeded contentLen=%d transport=%s", contentLen,
             url.startsWith("https://") ? "https" : "http");
  bool canBegin = Update.begin(contentLen > 0 ? contentLen : UPDATE_SIZE_UNKNOWN);
  if (!canBegin) {
    DCTRL_LOGE("OTA", "Update.begin failed contentLen=%d", contentLen);
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[512];
  size_t written = 0;
  if (contentLen == -1) {
    DCTRL_LOGI("OTA", "Using chunked OTA download");
    // Chunked transfer: manually decode chunk size headers.
    while (http.connected()) {
      String sizeLine = stream->readStringUntil('\n');
      sizeLine.trim();
      if (sizeLine.length() == 0) continue;
      int chunkSize = static_cast<int>(strtol(sizeLine.c_str(), nullptr, 16));
      if (chunkSize == 0) break;
      int remaining = chunkSize;
      while (remaining > 0) {
        int toRead = remaining < static_cast<int>(sizeof(buf)) ? remaining : static_cast<int>(sizeof(buf));
        int got = stream->readBytes(buf, toRead);
        if (got <= 0) break;
        Update.write(buf, got);
        written += got;
        remaining -= got;
      }
      stream->readStringUntil('\n');  // trailing \r\n after chunk data
    }
  } else {
    written = Update.writeStream(*stream);
  }
  if (!Update.end(true) || Update.hasError()) {
    DCTRL_LOGE("OTA", "Update failed error=%s bytesWritten=%u", Update.errorString(), static_cast<unsigned>(written));
    http.end();
    return false;
  }
  DCTRL_LOGI("OTA", "OTA update written=%u bytes; restarting", static_cast<unsigned>(written));
  http.end();
  delay(200);
  ESP.restart();
  return true;
}

}  // namespace core
