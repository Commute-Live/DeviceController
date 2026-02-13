#pragma once

#include <Arduino.h>

namespace transit {
namespace providers {
namespace nyc {
namespace subway {

uint16_t color_from_hex(const String &hex, int brightness);
uint16_t color_from_name(const String &name, int brightness);

}  // namespace subway
}  // namespace nyc
}  // namespace providers
}  // namespace transit
