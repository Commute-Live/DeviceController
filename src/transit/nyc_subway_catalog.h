#pragma once

#include <Arduino.h>

namespace transit {

struct LineDefinition {
  const char *id;
  char symbol;
  const char *color_name;
  const char *color_hex;
};

namespace nyc_subway {

const LineDefinition *find_line(const String &route_id);
const LineDefinition *parse_line_from_message(const String &message);
const LineDefinition &default_line();
size_t line_count();

}  // namespace nyc_subway
}  // namespace transit

