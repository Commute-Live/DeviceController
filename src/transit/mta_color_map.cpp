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

struct AliasRouteColorEntry {
  const char *aliases;
  uint16_t color565;
};

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

// SEPTA Regional Rail — keyed by GTFS route_short_name
static constexpr RouteColorEntry kSeptaRailColorTable[] = {
    {"AIR", rgb565(0x45, 0x63, 0x7A)},  // Airport Line
    {"CHE", rgb565(0x45, 0x63, 0x7A)},  // Chestnut Hill East
    {"CHW", rgb565(0x45, 0x63, 0x7A)},  // Chestnut Hill West
    {"CYN", rgb565(0x45, 0x63, 0x7A)},  // Cynwyd
    {"FOX", rgb565(0x45, 0x63, 0x7A)},  // Fox Chase
    {"LAN", rgb565(0x45, 0x63, 0x7A)},  // Lansdale/Doylestown
    {"MED", rgb565(0x45, 0x63, 0x7A)},  // Media/Wawa
    {"NOR", rgb565(0x45, 0x63, 0x7A)},  // Manayunk/Norristown
    {"PAO", rgb565(0x45, 0x63, 0x7A)},  // Paoli/Thorndale
    {"TRE", rgb565(0x45, 0x63, 0x7A)},  // Trenton
    {"WAR", rgb565(0x45, 0x63, 0x7A)},  // Warminster
    {"WIL", rgb565(0x45, 0x63, 0x7A)},  // Wilmington/Newark
    {"WTR", rgb565(0x45, 0x63, 0x7A)},  // West Trenton
};

// SEPTA trolley lines — keyed by internal GTFS route ID
static constexpr RouteColorEntry kSeptaTrolleyColorTable[] = {
    {"G1", rgb565(0xFF, 0xD7, 0x00)},  // Route 15
    {"T1", rgb565(0x3B, 0x7B, 0x38)},  // Route 10
    {"T2", rgb565(0x3B, 0x7B, 0x38)},  // Route 34
    {"T3", rgb565(0x3B, 0x7B, 0x38)},  // Route 13
    {"T4", rgb565(0x3B, 0x7B, 0x38)},  // Route 11
    {"T5", rgb565(0x3B, 0x7B, 0x38)},  // Route 36
    {"D1", rgb565(0xDC, 0x2E, 0x6B)},  // Route 101
    {"D2", rgb565(0xDC, 0x2E, 0x6B)},  // Route 102
};
static constexpr uint16_t kMtaBusColor = rgb565(0x00, 0x39, 0xA6);   // MTA institutional blue
static constexpr uint16_t kSeptaBusColor = rgb565(0x00, 0x5D, 0xAA);
static constexpr uint16_t kCtaBusFallbackColor = rgb565(0x99, 0x99, 0x9C);

static constexpr AliasRouteColorEntry kCtaSubwayColorTable[] = {
    {"BLUE|BLUELINE", rgb565(0x00, 0xA1, 0xDE)},
    {"BRN|BROWN|BROWNLINE", rgb565(0x62, 0x36, 0x1B)},
    {"G|GREEN|GREENLINE", rgb565(0x00, 0x9B, 0x3A)},
    {"ORG|ORANGE|ORANGELINE", rgb565(0xF9, 0x46, 0x1C)},
    {"P|PURPLE|PURPLELINE", rgb565(0x52, 0x23, 0x98)},
    {"PINK|PINKLINE", rgb565(0xE2, 0x7E, 0xA6)},
    {"R|RED|REDLINE", rgb565(0xC6, 0x0C, 0x30)},
    {"Y|YELLOW|YELLOWLINE", rgb565(0xF9, 0xE3, 0x00)},
};

static constexpr AliasRouteColorEntry kCtaBusColorTable[] = {
    {"100|120|121|125|134|135|136|143|146|147|148|2|26|6|X4|X49|X9", rgb565(0xB7, 0x12, 0x34)},
    {"12|20|34|4|47|49|53|54|55|60|63|66|72|77|79|81|82|9|95", rgb565(0x41, 0x41, 0x45)},
    {"J14", rgb565(0x00, 0x65, 0xBD)},
    {"1|103|106|108|11|111|111A|112|115|119|124|126|15|151|152|155|156|157|165|169|171|172|18|192|201|206|21|22|24|28|29|3|30|31|35|36|37|39|43|44|48|49B|50|51|52|52A|53A|54A|54B|55A|55N|56|57|59|62|62H|63W|65|67|68|7|70|71|73|74|75|76|78|8|80|81W|84|85|85A|86|87|88|8A|90|91|92|93|94|96|97|N5",
     rgb565(0x99, 0x99, 0x9C)},
};
static constexpr AliasRouteColorEntry kMbtaSubwayColorTable[] = {
    {"RED|MATTAPAN", rgb565(0xDA, 0x29, 0x1C)},
    {"ORANGE", rgb565(0xED, 0x8B, 0x00)},
    {"BLUE", rgb565(0x00, 0x3D, 0xA5)},
    {"GREEN|GREENB|GREENC|GREEND|GREENE", rgb565(0x00, 0x84, 0x3D)},
};
static constexpr AliasRouteColorEntry kMbtaCommuterRailColorTable[] = {
    {"CRGREENBUSH|CRLOWELL|CAPEFLYER", rgb565(0x16, 0x47, 0xB7)},
    {"CRHAVERHILL|CRNEWBURYPORT", rgb565(0x1B, 0xA7, 0xE1)},
    {"CRKINGSTON|CRNEEDHAM", rgb565(0x8C, 0x25, 0x33)},
    {"CRPROVIDENCE|CRFRANKLIN|CRFOXBORO", rgb565(0x00, 0x9E, 0x5D)},
    {"CRFAIRMOUNT", rgb565(0xD9, 0x2D, 0x20)},
    {"CRFITCHBURG", rgb565(0xED, 0x8B, 0x00)},
    {"CRWORCESTER", rgb565(0x7C, 0x3A, 0xED)},
    {"CRNEWBEDFORD", rgb565(0xC2, 0x41, 0x0C)},
};
static constexpr AliasRouteColorEntry kMbtaFerryColorTable[] = {
    {"BOATEASTBOSTON|BOATLYNN", rgb565(0x16, 0x47, 0xB7)},
    {"BOATF1", rgb565(0xED, 0x8B, 0x00)},
    {"BOATF4", rgb565(0x0E, 0xA5, 0xE9)},
    {"BOATF6", rgb565(0x00, 0x84, 0x3D)},
    {"BOATF7", rgb565(0xDA, 0x29, 0x1C)},
    {"BOATF8", rgb565(0x7C, 0x3A, 0xED)},
};
static constexpr uint16_t kSeptaRailBadgeColor = rgb565(0x45, 0x63, 0x7A);
static constexpr uint16_t kSeptaBroadStreetColor = rgb565(0xF2, 0x61, 0x00);
static constexpr uint16_t kSeptaTrolleyGreen = rgb565(0x5A, 0x96, 0x0A);
static constexpr uint16_t kSeptaNhslPurple = rgb565(0x5F, 0x24, 0x9F);
static constexpr uint16_t kSeptaMediaSharonHillPink = rgb565(0xDC, 0x2E, 0x6B);
static constexpr uint16_t kMbtaBusColor = rgb565(0x0F, 0x4C, 0xBA);
static constexpr uint16_t kMbtaFerryFallbackColor = rgb565(0x0E, 0xA5, 0xE9);

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

void normalize_route_token(const char *routeId, char *out, size_t outLen) {
  if (!out || outLen == 0) return;
  out[0] = '\0';
  if (!routeId) return;

  size_t j = 0;
  for (size_t i = 0; routeId[i] != '\0' && j + 1 < outLen; ++i) {
    const unsigned char c = static_cast<unsigned char>(routeId[i]);
    if (!isalnum(c)) continue;
    out[j++] = static_cast<char>(toupper(c));
  }
  out[j] = '\0';
}

bool normalized_route_starts_with(const char *routeId, const char *prefix) {
  char normalized[32];
  normalize_route_token(routeId, normalized, sizeof(normalized));
  if (normalized[0] == '\0') return false;
  const size_t prefixLen = strlen(prefix);
  return strncmp(normalized, prefix, prefixLen) == 0;
}

bool is_mbta_bus_route(const char *routeId) {
  char normalized[32];
  normalize_route_token(routeId, normalized, sizeof(normalized));
  if (normalized[0] == '\0') return false;
  if (normalized[0] >= '0' && normalized[0] <= '9') return true;
  return strncmp(normalized, "SL", 2) == 0 || strncmp(normalized, "CT", 2) == 0;
}

bool token_matches_alias_list(const char *routeId, const char *aliases) {
  if (!aliases) return false;

  char normalized[32];
  normalize_route_token(routeId, normalized, sizeof(normalized));
  if (normalized[0] == '\0') {
    return false;
  }

  const char *cursor = aliases;
  while (*cursor != '\0') {
    const char *sep = strchr(cursor, '|');
    const size_t aliasLen = sep ? static_cast<size_t>(sep - cursor) : strlen(cursor);
    if (aliasLen == strlen(normalized) && strncmp(cursor, normalized, aliasLen) == 0) {
      return true;
    }
    if (!sep) break;
    cursor = sep + 1;
  }

  return false;
}

uint16_t color_from_alias_table(const AliasRouteColorEntry *table,
                                size_t count,
                                const char *routeId,
                                uint16_t fallbackColor) {
  for (size_t i = 0; i < count; ++i) {
    if (token_matches_alias_list(routeId, table[i].aliases)) {
      return table[i].color565;
    }
  }
  return fallbackColor;
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

    if (strcmp(providerId, "septa-rail") == 0) {
      for (size_t i = 0; i < (sizeof(kSeptaRailColorTable) / sizeof(kSeptaRailColorTable[0])); ++i) {
        if (strcmp(kSeptaRailColorTable[i].routes, routeId) == 0) {
          return kSeptaRailColorTable[i].color565;
        }
      }
      return kSeptaRailBadgeColor;
    }

    if (strcmp(providerId, "septa-trolley") == 0) {
      for (size_t i = 0; i < (sizeof(kSeptaTrolleyColorTable) / sizeof(kSeptaTrolleyColorTable[0])); ++i) {
        if (strcmp(kSeptaTrolleyColorTable[i].routes, routeId) == 0) {
          return kSeptaTrolleyColorTable[i].color565;
        }
      }
      return rgb565(0x3B, 0x7B, 0x38);  // SEPTA trolley green fallback
    }

    if (strcmp(providerId, "septa-bus") == 0) {
      if (strcmp(routeId, "L1") == 0 || strcmp(routeId, "L1 OWL") == 0) {
        return rgb565(0x00, 0x97, 0xD6);
      }
      if (strcmp(routeId, "B1") == 0 || strcmp(routeId, "B2") == 0 || strcmp(routeId, "B3") == 0 ||
          strcmp(routeId, "B1 OWL") == 0) {
        return kSeptaBroadStreetColor;
      }
      if (strcmp(routeId, "M1") == 0 || strcmp(routeId, "M1 BUS") == 0) {
        return kSeptaNhslPurple;
      }
      if (strcmp(routeId, "T BUS") == 0 || strcmp(routeId, "T5 BUS") == 0) {
        return kSeptaTrolleyGreen;
      }
      if (strcmp(routeId, "D1 BUS") == 0 || strcmp(routeId, "D2 BUS") == 0) {
        return kSeptaMediaSharonHillPink;
      }
      return kSeptaBusColor;
    }

    if (strcmp(providerId, "mta-bus") == 0) {
      return kMtaBusColor;
    }

    if (strcmp(providerId, "cta-subway") == 0) {
      return color_from_alias_table(kCtaSubwayColorTable,
                                    sizeof(kCtaSubwayColorTable) / sizeof(kCtaSubwayColorTable[0]),
                                    routeId,
                                    kFallbackColor);
    }

    if (strcmp(providerId, "cta-bus") == 0) {
      return color_from_alias_table(kCtaBusColorTable,
                                    sizeof(kCtaBusColorTable) / sizeof(kCtaBusColorTable[0]),
                                    routeId,
                                    kCtaBusFallbackColor);
    }

    if (strcmp(providerId, "mbta") == 0) {
      if (normalized_route_starts_with(routeId, "BOAT")) {
        return color_from_alias_table(kMbtaFerryColorTable,
                                      sizeof(kMbtaFerryColorTable) / sizeof(kMbtaFerryColorTable[0]),
                                      routeId,
                                      kMbtaFerryFallbackColor);
      }
      if (is_mbta_bus_route(routeId)) {
        return kMbtaBusColor;
      }
      if (normalized_route_starts_with(routeId, "CR") || token_matches_alias_list(routeId, "CAPEFLYER")) {
        return color_from_alias_table(kMbtaCommuterRailColorTable,
                                      sizeof(kMbtaCommuterRailColorTable) / sizeof(kMbtaCommuterRailColorTable[0]),
                                      routeId,
                                      kFallbackColor);
      }
      return color_from_alias_table(kMbtaSubwayColorTable,
                                    sizeof(kMbtaSubwayColorTable) / sizeof(kMbtaSubwayColorTable[0]),
                                    routeId,
                                    kFallbackColor);
    }
  }

  return color_for_route(routeId);
}

}  // namespace transit
