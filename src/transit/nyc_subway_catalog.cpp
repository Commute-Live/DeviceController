#include "transit/nyc_subway_catalog.h"
#include <ctype.h>

namespace transit {
namespace nyc_subway {

static const LineDefinition kLines[] = {
    {"1", '1', "red", "#D82233"},
    {"2", '2', "red", "#D82233"},
    {"3", '3', "red", "#D82233"},
    {"4", '4', "green", "#009952"},
    {"5", '5', "green", "#009952"},
    {"6", '6', "green", "#009952"},
    {"7", '7', "purple", "#9A38A1"},
    {"A", 'A', "blue", "#0062CF"},
    {"B", 'B', "orange", "#EB6800"},
    {"C", 'C', "blue", "#0062CF"},
    {"D", 'D', "orange", "#EB6800"},
    {"E", 'E', "blue", "#0062CF"},
    {"F", 'F', "orange", "#EB6800"},
    {"G", 'G', "light green", "#799534"},
    {"J", 'J', "brown", "#8E5C33"},
    {"L", 'L', "gray", "#7C858C"},
    {"M", 'M', "orange", "#EB6800"},
    {"N", 'N', "yellow", "#F6BC26"},
    {"Q", 'Q', "yellow", "#F6BC26"},
    {"R", 'R', "yellow", "#F6BC26"},
    {"W", 'W', "yellow", "#F6BC26"},
    {"Z", 'Z', "brown", "#8E5C33"},
    {"S_42", 'S', "gray", "#7C858C"},
    {"S_FRANKLIN", 'S', "gray", "#7C858C"},
    {"S_ROCKAWAY", 'S', "gray", "#7C858C"},
};

static String canonicalize_route_id(String route_id) {
  route_id.trim();
  route_id.toUpperCase();
  route_id.replace(" ", "");
  route_id.replace("-", "_");

  if (route_id == "S") return "S_42";
  if (route_id == "S42" || route_id == "42S") return "S_42";
  if (route_id == "SFRANKLIN" || route_id == "S_FRANKLINAVE") return "S_FRANKLIN";
  if (route_id == "SROCKAWAY" || route_id == "S_ROCKAWAYPARK") return "S_ROCKAWAY";

  return route_id;
}

static String extract_field_value(const String &message, const char *field_name) {
  String key = "\"";
  key += field_name;
  key += "\"";

  int key_index = message.indexOf(key);
  if (key_index < 0) return "";

  int colon = message.indexOf(':', key_index + key.length());
  if (colon < 0) return "";

  int i = colon + 1;
  while (i < (int)message.length() && isspace(static_cast<unsigned char>(message[i]))) i++;
  if (i >= (int)message.length()) return "";

  char quote = message[i];
  if (quote == '"' || quote == '\'') {
    int end = message.indexOf(quote, i + 1);
    if (end < 0) return "";
    return message.substring(i + 1, end);
  }

  int end = i;
  while (end < (int)message.length()) {
    char c = message[end];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) break;
    end++;
  }

  if (end == i) return "";
  return message.substring(i, end);
}

const LineDefinition *find_line(const String &route_id) {
  String canonical = canonicalize_route_id(route_id);
  for (size_t i = 0; i < line_count(); i++) {
    if (canonical == kLines[i].id) {
      return &kLines[i];
    }
  }
  return nullptr;
}

const LineDefinition *parse_line_from_message(const String &message) {
  if (const LineDefinition *line = find_line(message)) {
    return line;
  }

  String route_field = extract_field_value(message, "route");
  if (route_field.length() > 0) {
    if (const LineDefinition *line = find_line(route_field)) {
      return line;
    }
  }

  String line_field = extract_field_value(message, "line");
  if (line_field.length() > 0) {
    if (const LineDefinition *line = find_line(line_field)) {
      return line;
    }
  }

  return nullptr;
}

const LineDefinition &default_line() {
  return kLines[11];  // E
}

size_t line_count() {
  return sizeof(kLines) / sizeof(kLines[0]);
}

}  // namespace nyc_subway
}  // namespace transit
