#include "transit/providers/chicago/subway/style.h"

namespace transit {
namespace providers {
namespace chicago {
namespace subway {

String route_label(const String &route_id) {
  String route = route_id;
  route.trim();
  if (route.length() == 0) return "CTA";

  String upper = route;
  upper.toUpperCase();

  if (upper == "RED") return "RED";
  if (upper == "BLUE") return "BLU";
  if (upper == "BRN") return "BRN";
  if (upper == "ORG") return "ORG";
  if (upper == "PINK") return "PNK";
  if (upper == "P") return "P";
  if (upper == "Y") return "YEL";
  if (upper == "G") return "GRN";
  return upper;
}

const char *route_color_hex(const String &route_id) {
  String upper = route_id;
  upper.trim();
  upper.toUpperCase();

  if (upper == "RED") return "#C60C30";
  if (upper == "BLUE") return "#00A1DE";
  if (upper == "BRN") return "#62361B";
  if (upper == "G") return "#009B3A";
  if (upper == "ORG") return "#F9461C";
  if (upper == "P") return "#522398";
  if (upper == "PINK") return "#E27EA6";
  if (upper == "Y") return "#F9E300";
  return "#7C858C";
}

const char *route_text_color(const String &route_id) {
  String upper = route_id;
  upper.trim();
  upper.toUpperCase();

  if (upper == "Y" || upper == "PINK") return "black";
  return "white";
}

}  // namespace subway
}  // namespace chicago
}  // namespace providers
}  // namespace transit
