#pragma once

#include <Arduino.h>
#include "transit/providers/nyc/subway/catalog.h"

namespace transit {
namespace registry {

enum class System {
  NycSubway,
};

void set_active_system(System system);
System get_active_system();

const LineDefinition *find_line(const String &route_id);
const LineDefinition *find_line(const String &route_id, System system);

const LineDefinition *parse_line_from_message(const String &message);
const LineDefinition *parse_line_from_message(const String &message, System system);

const LineDefinition &default_line();
const LineDefinition &default_line(System system);

}  // namespace registry
}  // namespace transit
