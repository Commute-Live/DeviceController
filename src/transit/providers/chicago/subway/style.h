#pragma once

#include <Arduino.h>

namespace transit {
namespace providers {
namespace chicago {
namespace subway {

String route_label(const String &route_id);
const char *route_color_hex(const String &route_id);
const char *route_text_color(const String &route_id);

}  // namespace subway
}  // namespace chicago
}  // namespace providers
}  // namespace transit
