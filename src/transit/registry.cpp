#include "transit/registry.h"

namespace transit {
namespace registry {

namespace {
System g_active_system = System::NycSubway;
}

void set_active_system(System system) {
  g_active_system = system;
}

System get_active_system() {
  return g_active_system;
}

const LineDefinition *find_line(const String &route_id) {
  return find_line(route_id, g_active_system);
}

const LineDefinition *find_line(const String &route_id, System system) {
  switch (system) {
    case System::NycSubway:
      return providers::nyc::subway::find_line(route_id);
  }
  return nullptr;
}

const LineDefinition *parse_line_from_message(const String &message) {
  return parse_line_from_message(message, g_active_system);
}

const LineDefinition *parse_line_from_message(const String &message, System system) {
  switch (system) {
    case System::NycSubway:
      return providers::nyc::subway::parse_line_from_message(message);
  }
  return nullptr;
}

const LineDefinition &default_line() {
  return default_line(g_active_system);
}

const LineDefinition &default_line(System system) {
  switch (system) {
    case System::NycSubway:
      return providers::nyc::subway::default_line();
  }
  return providers::nyc::subway::default_line();
}

}  // namespace registry
}  // namespace transit
