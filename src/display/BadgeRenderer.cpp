#include "display/BadgeRenderer.h"

#include <ctype.h>
#include <string.h>

#include "transit/MtaColorMap.h"

namespace display {

namespace {

constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kBlack = 0x0000;

const char *route_text(const char *routeId, char *out, size_t outLen) {
  if (!out || outLen < 2) return "";
  out[0] = '\0';
  if (!routeId) return out;

  size_t j = 0;
  char first = '\0';
  for (size_t i = 0; routeId[i] != '\0' && j + 1 < outLen; ++i) {
    char c = routeId[i];
    if (c == ' ' || c == '-' || c == '_') continue;
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (j == 0) {
      out[j++] = c;
      first = c;
      continue;
    }
    // Preserve express suffix style from logo branch (e.g. 6X, 7X, FX) only.
    if (c == 'X' && first != '\0') {
      out[j++] = c;
    }
    break;
  }
  out[j] = '\0';
  return out;
}

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

void BadgeRenderer::draw_badge(DisplayEngine &display,
                               int16_t x,
                               int16_t y,
                               int16_t size,
                               const char *routeId) const {
  if (size <= 0) return;

  int16_t badgeSize = size;
  if (badgeSize < 5) badgeSize = 5;

  int16_t r = static_cast<int16_t>(badgeSize / 2 - kStrokePaddingPx);
  if (r < 1) r = 1;
  const int16_t cx = static_cast<int16_t>(x + (badgeSize / 2));
  const int16_t cy = static_cast<int16_t>(y + (badgeSize / 2));
  const uint16_t fill = transit::MtaColorMap::color_for_route(routeId);

  fill_circle_midpoint(display, cx, cy, r, fill);

  const int16_t targetFontHeight = static_cast<int16_t>((badgeSize * 3) / 5);  // 0.6 * badgeSize
  uint8_t textSize = static_cast<uint8_t>((targetFontHeight + 4) / 8);
  if (textSize < 1) textSize = 1;
  if (textSize < 3) ++textSize;  // Requested: one size bigger by default.

  char textBuf[4];
  route_text(routeId, textBuf, sizeof(textBuf));
  if (textBuf[0] == '\0') return;

  TextMetrics tm = display.measure_text(textBuf, textSize);
  const int16_t maxGlyph = static_cast<int16_t>((badgeSize * 3) / 5);
  while (textSize > 1 && (tm.width > maxGlyph || tm.height > maxGlyph)) {
    --textSize;
    tm = display.measure_text(textBuf, textSize);
  }

  // Center using measured glyph bounds relative to cursor origin.
  const int16_t tx = static_cast<int16_t>(cx - (tm.width / 2) - tm.xOffset + 1);
  const int16_t ty = static_cast<int16_t>(cy - (tm.height / 2) - tm.yOffset + 1);
  display.draw_text_transparent(tx, ty, textBuf, badge_text_color(fill), textSize);
}

}  // namespace display
