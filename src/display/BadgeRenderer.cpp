#include "display/BadgeRenderer.h"

#include <ctype.h>
#include <string.h>

#include "transit/MtaColorMap.h"

namespace display {

namespace {

constexpr uint16_t kWhite = 0xFFFF;

const char *route_text(const char *routeId, char *out, size_t outLen) {
  if (!out || outLen < 2) return "";
  out[0] = '\0';
  if (!routeId) return out;

  size_t j = 0;
  for (size_t i = 0; routeId[i] != '\0' && j + 1 < outLen; ++i) {
    char c = routeId[i];
    if (c == ' ' || c == '-' || c == '_') continue;
    out[j++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (j == 2) break;  // keep badge text compact and centered
  }
  out[j] = '\0';
  return out;
}

}  // namespace

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
    display.draw_hline(cx - x, cy + y, static_cast<int16_t>(2 * x + 1), color);
    display.draw_hline(cx - x, cy - y, static_cast<int16_t>(2 * x + 1), color);
    display.draw_hline(cx - y, cy + x, static_cast<int16_t>(2 * y + 1), color);
    display.draw_hline(cx - y, cy - x, static_cast<int16_t>(2 * y + 1), color);

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
  if ((badgeSize & 1) == 0) {
    badgeSize -= 1;
  }

  const int16_t r = static_cast<int16_t>((badgeSize - 1) / 2);
  const int16_t cx = static_cast<int16_t>(x + (badgeSize / 2));
  const int16_t cy = static_cast<int16_t>(y + (badgeSize / 2));
  const uint16_t fill = transit::MtaColorMap::color_for_route(routeId);

  fill_circle_midpoint(display, cx, cy, r, fill);

  const int16_t targetFontHeight = static_cast<int16_t>((badgeSize * 3) / 5);  // 0.6 * badgeSize
  uint8_t textSize = static_cast<uint8_t>((targetFontHeight + 4) / 8);
  if (textSize < 1) textSize = 1;

  char textBuf[3];
  route_text(routeId, textBuf, sizeof(textBuf));
  if (textBuf[0] == '\0') return;

  TextMetrics tm = display.measure_text(textBuf, textSize);
  const int16_t maxGlyph = static_cast<int16_t>((badgeSize * 7) / 10);  // maintain visual breathing room
  while (textSize > 1 && (tm.width > maxGlyph || tm.height > maxGlyph)) {
    --textSize;
    tm = display.measure_text(textBuf, textSize);
  }

  // Center using measured glyph bounds relative to cursor origin.
  const int16_t tx = static_cast<int16_t>(cx - (tm.width / 2) - tm.xOffset);
  const int16_t ty = static_cast<int16_t>(cy - (tm.height / 2) - tm.yOffset);
  display.draw_text(tx, ty, textBuf, kWhite, textSize, fill);
}

}  // namespace display
