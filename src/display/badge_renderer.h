#pragma once

#include <stdint.h>

#include "display/display_engine.h"

namespace display {

enum class RoundedBadgeStyle : uint8_t {
  kPill = 1,
  kRail = 2,
};

class BadgeRenderer final {
 public:
  static constexpr uint8_t kStrokePaddingPx = 1;
  static constexpr uint16_t kAspectXQ8_8 = 256;  // 1.0
  static constexpr uint16_t kAspectYQ8_8 = 256;  // 1.0

  void draw_badge(DisplayEngine &display,
                  int16_t x,
                  int16_t y,
                  int16_t size,
                  const char *routeId,
                  uint16_t fillColor) const;
  void draw_rect_badge(DisplayEngine &display,
                       int16_t x,
                       int16_t y,
                       int16_t w,
                       int16_t h,
                       const char *label,
                       uint16_t fill,
                       RoundedBadgeStyle style) const;

 private:
  int16_t corrected_radius(int16_t value, uint16_t aspectXQ8_8, uint16_t aspectYQ8_8) const;
  void fill_circle_midpoint(DisplayEngine &display, int16_t cx, int16_t cy, int16_t r, uint16_t color) const;
  void fill_rounded_rect(DisplayEngine &display,
                         int16_t x,
                         int16_t y,
                         int16_t w,
                         int16_t h,
                         int16_t radius,
                         uint16_t color) const;
};

}  // namespace display
