#include "device/led_config.h"

#include <Preferences.h>
#include "parsing/payload_parser.h"

namespace device {

namespace {

constexpr const char *kPrefsNamespace = "ledcfg";

bool parse_int(const String &value, int &out) {
  if (value.length() == 0) return false;
  char *end = nullptr;
  long parsed = strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') return false;
  out = static_cast<int>(parsed);
  return true;
}

bool parse_bool(const String &value, bool &out) {
  String v = value;
  v.trim();
  v.toLowerCase();
  if (v == "1" || v == "true" || v == "yes" || v == "on") {
    out = true;
    return true;
  }
  if (v == "0" || v == "false" || v == "no" || v == "off") {
    out = false;
    return true;
  }
  return false;
}

bool read_field(WebServer &server, const char *name, String &out) {
  if (server.hasArg(name)) {
    out = server.arg(name);
    return true;
  }

  if (!server.hasArg("plain")) return false;
  String plain = server.arg("plain");
  if (plain.length() == 0) return false;

  out = extract_json_string_field(plain, name);
  return out.length() > 0;
}

}  // namespace

LedConfig defaults() {
  return LedConfig{
      64,    // panelWidth
      32,    // panelHeight
      2,     // chainLength
      255,   // brightness
      false  // serpentine
  };
}

void sanitize(LedConfig &config) {
  if (config.panelWidth < 32) config.panelWidth = 32;
  if (config.panelWidth > 128) config.panelWidth = 128;

  if (config.panelHeight < 16) config.panelHeight = 16;
  if (config.panelHeight > 64) config.panelHeight = 64;

  if (config.chainLength < 1) config.chainLength = 1;
  if (config.chainLength > 8) config.chainLength = 8;

  if (config.brightness < 1) config.brightness = 1;
  if (config.brightness > 255) config.brightness = 255;
}

bool load(LedConfig &config) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return false;
  }

  LedConfig loaded = defaults();
  loaded.panelWidth = prefs.getUShort("width", loaded.panelWidth);
  loaded.panelHeight = prefs.getUShort("height", loaded.panelHeight);
  loaded.chainLength = prefs.getUChar("chain", loaded.chainLength);
  loaded.brightness = prefs.getUChar("bright", loaded.brightness);
  loaded.serpentine = prefs.getBool("serp", loaded.serpentine);
  prefs.end();

  sanitize(loaded);
  config = loaded;
  return true;
}

void save(const LedConfig &config) {
  LedConfig next = config;
  sanitize(next);

  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.putUShort("width", next.panelWidth);
  prefs.putUShort("height", next.panelHeight);
  prefs.putUChar("chain", next.chainLength);
  prefs.putUChar("bright", next.brightness);
  prefs.putBool("serp", next.serpentine);
  prefs.end();
}

bool apply_update_from_request(WebServer &server, LedConfig &config, String &error) {
  LedConfig next = config;
  bool touched = false;

  String value;
  int parsedInt = 0;
  bool parsedBool = false;

  if (read_field(server, "panelWidth", value)) {
    if (!parse_int(value, parsedInt)) {
      error = "Invalid panelWidth";
      return false;
    }
    next.panelWidth = static_cast<uint16_t>(parsedInt);
    touched = true;
  }

  if (read_field(server, "panelHeight", value)) {
    if (!parse_int(value, parsedInt)) {
      error = "Invalid panelHeight";
      return false;
    }
    next.panelHeight = static_cast<uint16_t>(parsedInt);
    touched = true;
  }

  if (read_field(server, "chainLength", value)) {
    if (!parse_int(value, parsedInt)) {
      error = "Invalid chainLength";
      return false;
    }
    next.chainLength = static_cast<uint8_t>(parsedInt);
    touched = true;
  }

  if (read_field(server, "brightness", value)) {
    if (!parse_int(value, parsedInt)) {
      error = "Invalid brightness";
      return false;
    }
    next.brightness = static_cast<uint8_t>(parsedInt);
    touched = true;
  }

  if (read_field(server, "serpentine", value)) {
    if (!parse_bool(value, parsedBool)) {
      error = "Invalid serpentine";
      return false;
    }
    next.serpentine = parsedBool;
    touched = true;
  }

  if (!touched) {
    error = "No updatable fields provided";
    return false;
  }

  sanitize(next);
  config = next;
  return true;
}

String to_json(const LedConfig &config) {
  String json = "{";
  json += "\"panelWidth\":";
  json += String(config.panelWidth);
  json += ",\"panelHeight\":";
  json += String(config.panelHeight);
  json += ",\"chainLength\":";
  json += String(config.chainLength);
  json += ",\"brightness\":";
  json += String(config.brightness);
  json += ",\"serpentine\":";
  json += (config.serpentine ? "true" : "false");
  json += "}";
  return json;
}

}  // namespace device
