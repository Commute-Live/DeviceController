#pragma once

#include <Arduino.h>

namespace transit {
namespace providers {
namespace boston {
namespace subway {

bool is_mbta_provider_id(const String &provider_id);
bool is_subway_line(const String &line);
const char *subway_color_hex(const String &line);
String subway_badge_text(const String &line);

}  // namespace subway
}  // namespace boston
}  // namespace providers
}  // namespace transit
