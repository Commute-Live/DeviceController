#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace device {

struct LedConfig {
  uint16_t panelWidth;
  uint16_t panelHeight;
  uint8_t chainLength;
  uint8_t brightness;
  bool serpentine;
};

LedConfig defaults();

void sanitize(LedConfig &config);

bool load(LedConfig &config);

void save(const LedConfig &config);

bool apply_update_from_request(WebServer &server, LedConfig &config, String &error);

String to_json(const LedConfig &config);

}  // namespace device
