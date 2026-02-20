#pragma once

#include <stdint.h>

#include "display/DisplayEngine.h"

namespace display {

class BadgeRenderer final {
 public:
  void draw_badge(DisplayEngine &display, int16_t x, int16_t y, int16_t size, const char *routeId) const;

 private:
  void fill_circle_midpoint(DisplayEngine &display, int16_t cx, int16_t cy, int16_t r, uint16_t color) const;
};

}  // namespace display
