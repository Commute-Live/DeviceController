#include "transit/MtaColorMap.h"

#include <ctype.h>
#include <string.h>

namespace transit {

namespace {

// Official MTA route family colors encoded as RGB565.
static constexpr RouteColorEntry kRouteColorTable[] = {
    {"ACE", 0x01B4},   // #0039A6
    {"BDFM", 0xFB23},  // #FF6319
    {"G", 0x65E8},     // #6CBE45
    {"JZ", 0x9B26},    // #996633
    {"L", 0xAD55},     // #A7A9AC
    {"NQRW", 0xFE61},  // #FCCC0A
    {"123", 0xE986},   // #EE352E
    {"456", 0x04A7},   // #00933C
    {"7", 0xB2B5},     // #B933AD
};

char normalize_route_char(const char *routeId) {
  if (!routeId) return '\0';
  for (size_t i = 0; routeId[i] != '\0'; ++i) {
    const char c = routeId[i];
    if (c == ' ' || c == '-' || c == '_') {
      continue;
    }
    return static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  return '\0';
}

}  // namespace

uint16_t MtaColorMap::color_for_route(const char *routeId) {
  const char needle = normalize_route_char(routeId);
  if (needle == '\0') {
    return kFallbackColor;
  }

  for (size_t i = 0; i < (sizeof(kRouteColorTable) / sizeof(kRouteColorTable[0])); ++i) {
    if (strchr(kRouteColorTable[i].routes, needle) != nullptr) {
      return kRouteColorTable[i].color565;
    }
  }

  return kFallbackColor;
}

}  // namespace transit
