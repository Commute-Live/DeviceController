#include "transit/mta_color_map.h"

#include <ctype.h>
#include <string.h>

namespace transit {

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) |
                               ((g & 0xFC) << 3) |
                               (b >> 3));
}

// Official MTA route family colors encoded as RGB565.
static constexpr RouteColorEntry kRouteColorTable[] = {
    {"ACE", rgb565(0x00, 0x39, 0xA6)},
    {"BDFM", rgb565(0xFF, 0x63, 0x19)},
    {"G", rgb565(0x6C, 0xBE, 0x45)},
    {"JZ", rgb565(0x99, 0x66, 0x33)},
    {"L", rgb565(0xA7, 0xA9, 0xAC)},
    {"NQRW", rgb565(0xFC, 0xCC, 0x0A)},
    {"123", rgb565(0xEE, 0x35, 0x2E)},
    {"456", rgb565(0x00, 0x93, 0x3C)},
    {"7", rgb565(0xB9, 0x33, 0xAD)},
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
