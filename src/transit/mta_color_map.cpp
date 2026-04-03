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
    {"S", rgb565(0x80, 0x81, 0x83)},
};

static constexpr RouteColorEntry kLirrRouteColorTable[] = {
    {"1", rgb565(0x00, 0x98, 0x5F)},   // Babylon Branch
    {"2", rgb565(0xCE, 0x8E, 0x00)},   // Hempstead Branch
    {"3", rgb565(0x00, 0xAF, 0x3F)},   // Oyster Bay Branch
    {"4", rgb565(0xA6, 0x26, 0xAA)},   // Ronkonkoma Branch
    {"5", rgb565(0x00, 0x69, 0x83)},   // Montauk Branch
    {"6", rgb565(0xFF, 0x63, 0x19)},   // Long Beach Branch
    {"7", rgb565(0x6E, 0x32, 0x19)},   // Far Rockaway Branch
    {"8", rgb565(0x00, 0xA1, 0xDE)},   // West Hempstead Branch
    {"9", rgb565(0xC6, 0x0C, 0x30)},   // Port Washington Branch
    {"10", rgb565(0x00, 0x39, 0xA6)},  // Port Jefferson Branch
    {"12", rgb565(0x4D, 0x53, 0x57)},  // City Terminal Zone
    {"13", rgb565(0xA6, 0x26, 0xAA)},  // Greenport Service
};

static constexpr RouteColorEntry kMnrRouteColorTable[] = {
    {"1", rgb565(0x00, 0x9B, 0x3A)},  // Hudson
    {"2", rgb565(0x00, 0x39, 0xA6)},  // Harlem
    {"3", rgb565(0xEE, 0x00, 0x34)},  // New Haven
    {"4", rgb565(0xEE, 0x00, 0x34)},  // New Canaan
    {"5", rgb565(0xEE, 0x00, 0x34)},  // Danbury
    {"6", rgb565(0xEE, 0x00, 0x34)},  // Waterbury
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

uint16_t MtaColorMap::color_for_provider_route(const char *providerId, const char *routeId) {
  if (!routeId || routeId[0] == '\0') {
    return kFallbackColor;
  }

  if (providerId) {
    if (strcmp(providerId, "mta-lirr") == 0) {
      for (size_t i = 0; i < (sizeof(kLirrRouteColorTable) / sizeof(kLirrRouteColorTable[0])); ++i) {
        if (strcmp(kLirrRouteColorTable[i].routes, routeId) == 0) {
          return kLirrRouteColorTable[i].color565;
        }
      }
      return kFallbackColor;
    }

    if (strcmp(providerId, "mta-mnr") == 0) {
      for (size_t i = 0; i < (sizeof(kMnrRouteColorTable) / sizeof(kMnrRouteColorTable[0])); ++i) {
        if (strcmp(kMnrRouteColorTable[i].routes, routeId) == 0) {
          return kMnrRouteColorTable[i].color565;
        }
      }
      return kFallbackColor;
    }
  }

  return color_for_route(routeId);
}

}  // namespace transit
