#pragma once

#include <Arduino.h>

namespace parsing {

struct ProviderRow {
  String provider;
  String line;
  String label;
  String eta;
};

struct ProviderPayload {
  ProviderRow row1;
  ProviderRow row2;
  bool hasRow1 = false;
  bool hasRow2 = false;
};

}  // namespace parsing
