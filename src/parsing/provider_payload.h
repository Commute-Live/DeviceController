#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace parsing {

struct ProviderRow {
  String label;
  String eta;
  uint8_t badgeShape = 1;    // 0=circle, 1=pill
  uint16_t badgeColor = 0x8410;  // gray RGB565 fallback
  char badgeText[5] = {};
  bool scrollEnabled = false;
  bool delayed = false;
};

struct ProviderPayload {
  ProviderRow row1;
  ProviderRow row2;
  bool hasRow1 = false;
  bool hasRow2 = false;
  String row1EtaExtra;
  String row2EtaExtra;
};

}  // namespace parsing
