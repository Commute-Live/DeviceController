#include "transit/providers/boston/subway/style.h"

namespace transit {
namespace providers {
namespace boston {
namespace subway {

bool is_mbta_provider_id(const String &provider_id) {
  String p = provider_id;
  p.trim();
  p.toLowerCase();
  return p == "mbta" || p == "mbta-subway" || p == "mbta-bus";
}

bool is_subway_line(const String &line_raw) {
  String line = line_raw;
  line.trim();
  line.toUpperCase();
  if (line == "RED" || line == "ORANGE" || line == "BLUE" || line == "MATTAPAN") {
    return true;
  }
  return line.startsWith("GREEN");
}

const char *subway_color_hex(const String &line_raw) {
  String line = line_raw;
  line.trim();
  line.toUpperCase();
  if (line == "RED" || line == "MATTAPAN") return "#DA291C";
  if (line == "ORANGE") return "#ED8B00";
  if (line == "BLUE") return "#003DA5";
  if (line.startsWith("GREEN")) return "#00843D";
  return "#7C858C";
}

String subway_badge_text(const String &line_raw) {
  String line = line_raw;
  line.trim();
  line.toUpperCase();
  if (line == "RED") return "RL";
  if (line == "ORANGE") return "OL";
  if (line == "BLUE") return "BL";
  if (line.startsWith("GREEN")) return "GL";
  if (line == "MATTAPAN") return "ML";
  return "MB";
}

}  // namespace subway
}  // namespace boston
}  // namespace providers
}  // namespace transit
