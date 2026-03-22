#pragma once

#include <stdint.h>

namespace transit {

struct RouteColorEntry {
  const char *routes;
  uint16_t color565;
};

class MtaColorMap final {
 public:
  static constexpr uint16_t kFallbackColor = 0x7BEF;  // neutral gray
  static uint16_t color_for_route(const char *routeId);
};

}  // namespace transit
