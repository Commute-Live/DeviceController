#include "display/badge_renderer.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

namespace display {

namespace {

constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kBlack = 0x0000;
constexpr uint8_t kTextSizeTinyPlus = 255;

uint16_t badge_text_color(uint16_t fill) {
  const uint8_t r = static_cast<uint8_t>(((fill >> 11) & 0x1F) * 255 / 31);
  const uint8_t g = static_cast<uint8_t>(((fill >> 5) & 0x3F) * 255 / 63);
  const uint8_t b = static_cast<uint8_t>((fill & 0x1F) * 255 / 31);
  const uint16_t luminance = static_cast<uint16_t>((r * 299U + g * 587U + b * 114U) / 1000U);
  return luminance > 160U ? kBlack : kWhite;
}

}  // namespace

int16_t BadgeRenderer::corrected_radius(int16_t value, uint16_t aspectXQ8_8, uint16_t aspectYQ8_8) const {
  if (value <= 0) return 0;
  if (aspectXQ8_8 == 0) return value;
  const int32_t num = static_cast<int32_t>(value) * static_cast<int32_t>(aspectYQ8_8) +
                      static_cast<int32_t>(aspectXQ8_8 / 2);
  const int32_t den = static_cast<int32_t>(aspectXQ8_8);
  const int32_t out = num / den;
  return static_cast<int16_t>(out > 0 ? out : 0);
}

void BadgeRenderer::fill_circle_midpoint(DisplayEngine &display,
                                         int16_t cx,
                                         int16_t cy,
                                         int16_t r,
                                         uint16_t color) const {
  if (r <= 0) {
    display.draw_pixel(cx, cy, color);
    return;
  }

  int16_t x = r;
  int16_t y = 0;
  int16_t d = 1 - r;

  while (x >= y) {
    const int16_t xCorr = corrected_radius(x, kAspectXQ8_8, kAspectYQ8_8);
    const int16_t yCorr = corrected_radius(y, kAspectXQ8_8, kAspectYQ8_8);
    display.draw_hline(cx - xCorr, cy + y, static_cast<int16_t>(2 * xCorr + 1), color);
    display.draw_hline(cx - xCorr, cy - y, static_cast<int16_t>(2 * xCorr + 1), color);
    display.draw_hline(cx - yCorr, cy + x, static_cast<int16_t>(2 * yCorr + 1), color);
    display.draw_hline(cx - yCorr, cy - x, static_cast<int16_t>(2 * yCorr + 1), color);

    ++y;
    if (d < 0) {
      d += static_cast<int16_t>(2 * y + 1);
    } else {
      --x;
      d += static_cast<int16_t>(2 * (y - x) + 1);
    }
  }
}

void BadgeRenderer::fill_rounded_rect(DisplayEngine &display,
                                      int16_t x,
                                      int16_t y,
                                      int16_t w,
                                      int16_t h,
                                      int16_t radius,
                                      uint16_t color) const {
  if (w <= 0 || h <= 0) return;
  if (radius < 1) {
    display.fill_rect(x, y, w, h, color);
    return;
  }

  int16_t clampedRadius = radius;
  if (clampedRadius > (h / 2)) clampedRadius = static_cast<int16_t>(h / 2);
  if (clampedRadius > (w / 2)) clampedRadius = static_cast<int16_t>(w / 2);
  if (clampedRadius < 1) {
    display.fill_rect(x, y, w, h, color);
    return;
  }

  const float centerY = (static_cast<float>(h) - 1.0f) * 0.5f;
  const float effectiveRadius = static_cast<float>(clampedRadius) - 0.25f;

  for (int16_t yy = 0; yy < h; ++yy) {
    int16_t inset = 0;
    if (yy < clampedRadius || yy >= static_cast<int16_t>(h - clampedRadius)) {
      const float dy = fabsf(static_cast<float>(yy) - centerY);
      float dx = 0.0f;
      const float inside = effectiveRadius * effectiveRadius - dy * dy;
      if (inside > 0.0f) {
        dx = sqrtf(inside);
      }
      inset = static_cast<int16_t>(clampedRadius - static_cast<int16_t>(dx + 0.999f));
      if (inset < 0) inset = 0;
    }

    const int16_t lineX = static_cast<int16_t>(x + inset);
    const int16_t lineW = static_cast<int16_t>(w - 2 * inset);
    if (lineW > 0) {
      display.draw_hline(lineX, static_cast<int16_t>(y + yy), lineW, color);
    }
  }
}

void BadgeRenderer::draw_badge(DisplayEngine &display,
                               int16_t x,
                               int16_t y,
                               int16_t size,
                               const char *label,
                               uint16_t fillColor) const {
  if (size <= 0) return;

  int16_t badgeSize = size;
  if (badgeSize < 5) badgeSize = 5;

  int16_t r = static_cast<int16_t>(badgeSize / 2 - kStrokePaddingPx);
  if (r < 1) r = 1;
  const int16_t cx = static_cast<int16_t>(x + (badgeSize / 2));
  const int16_t cy = static_cast<int16_t>(y + (badgeSize / 2));
  const uint16_t fill = fillColor;

  fill_circle_midpoint(display, cx, cy, r, fill);

  if (!label || label[0] == '\0') return;

  uint8_t textSize = 1;
  const size_t labelLen = strnlen(label, 8);
  if (labelLen <= 2 && badgeSize >= 10) {
    textSize = 2;
  }

  TextMetrics tm = display.measure_text(label, textSize);
  const int16_t maxGlyph = static_cast<int16_t>((badgeSize * 3) / 5);
  while (textSize > 1 && (tm.width > maxGlyph || tm.height > maxGlyph)) {
    --textSize;
    tm = display.measure_text(label, textSize);
  }
  if (tm.width > maxGlyph || tm.height > maxGlyph) {
    textSize = kTextSizeTinyPlus;
    tm = display.measure_text(label, textSize);
    if (tm.width > maxGlyph || tm.height > maxGlyph) {
      textSize = kTextSizeTiny;
      tm = display.measure_text(label, textSize);
    }
  }

  // Center using measured glyph bounds relative to cursor origin.
  const int16_t tx = static_cast<int16_t>(cx - (tm.width / 2) - tm.xOffset + 1);
  const int16_t ty = static_cast<int16_t>(cy - (tm.height / 2) - tm.yOffset + 1);
  display.draw_text_transparent(tx, ty, label, badge_text_color(fill), textSize);
}

void BadgeRenderer::draw_rect_badge(DisplayEngine &display,
                                    int16_t x,
                                    int16_t y,
                                    int16_t w,
                                    int16_t h,
                                    const char *label,
                                    uint16_t fill,
                                    RoundedBadgeStyle style) const {
  if (w <= 0 || h <= 0) return;

  (void)style;
  // Keep the shared 21x11 badge box, but only soften the corners slightly.
  int16_t radius = (w >= 10 && h >= 8) ? 1 : 0;
  fill_rounded_rect(display, x, y, w, h, radius, fill);

  if (!label || label[0] == '\0') return;

  // Label is always 1-3 chars; truncate defensively.
  char truncated[4];
  size_t labelLen = strnlen(label, 8);
  if (labelLen > 3) labelLen = 3;
  memcpy(truncated, label, labelLen);
  truncated[labelLen] = '\0';
  label = truncated;

  // Always use textSize=1 (5x7 font) — label is always 1-3 chars, fits in 21x11.
  const uint8_t textSize = 1;
  TextMetrics tm = display.measure_text(label, textSize);

  const uint16_t textColor = badge_text_color(fill);
  const int16_t cx = static_cast<int16_t>(x + (w / 2));
  const int16_t cy = static_cast<int16_t>(y + (h / 2));
  const int16_t tx = static_cast<int16_t>(cx - (tm.width / 2) - tm.xOffset + 1);
  const int16_t ty = static_cast<int16_t>(cy - (tm.height / 2) - tm.yOffset + 1);
  display.draw_text(tx, ty, label, textColor, textSize, fill);
}

}  // namespace display
