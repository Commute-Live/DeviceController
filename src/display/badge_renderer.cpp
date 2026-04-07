#include "display/badge_renderer.h"

#include <ctype.h>
#include <string.h>

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

void BadgeRenderer::fill_rounded_rect(DisplayEngine &display,
                                      int16_t x,
                                      int16_t y,
                                      int16_t w,
                                      int16_t h,
                                      uint16_t color) const {
  if (w <= 0 || h <= 0) {
    return;
  }

  // On a low-res matrix, a 1px radius reads best: keep the body solid and
  // clip only the four corners.
  if (w < 3 || h < 3) {
    display.fill_rect(x, y, w, h, color);
    return;
  }

  display.fill_rect(static_cast<int16_t>(x + 1), y, static_cast<int16_t>(w - 2), h, color);
  display.fill_rect(x, static_cast<int16_t>(y + 1), 1, static_cast<int16_t>(h - 2), color);
  display.fill_rect(static_cast<int16_t>(x + w - 1),
                    static_cast<int16_t>(y + 1),
                    1,
                    static_cast<int16_t>(h - 2),
                    color);
}

void BadgeRenderer::draw_badge(DisplayEngine &display,
                               int16_t x,
                               int16_t y,
                               int16_t size,
                               const char *routeId,
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

void BadgeRenderer::draw_rect_badge(DisplayEngine &display,
                                    int16_t x,
                                    int16_t y,
                                    int16_t w,
                                    int16_t h,
                                    const char *label,
                                    uint16_t fill) const {
  if (w <= 0 || h <= 0 || !label || label[0] == '\0') return;

  fill_rounded_rect(display, x, y, w, h, fill);

  uint8_t textSize = 1;
  const size_t labelLen = strnlen(label, 8);
  if (labelLen <= 1 && h >= 10) {
    textSize = 2;
  }

  TextMetrics tm = display.measure_text(label, textSize);
  while (textSize > 1 && (tm.width > static_cast<int16_t>(w - 4) || tm.height > static_cast<int16_t>(h - 4))) {
    --textSize;
    tm = display.measure_text(label, textSize);
  }
  // If still too wide at size 1, drop to tiny font so longer labels shrink to
  // fit the same box as shorter ones.
  if (textSize == 1 && (tm.width > static_cast<int16_t>(w - 2) || tm.height > static_cast<int16_t>(h - 2))) {
    textSize = kTextSizeTiny;
    tm = display.measure_text(label, textSize);
  }

  const int16_t cx = static_cast<int16_t>(x + (w / 2));
  const int16_t cy = static_cast<int16_t>(y + (h / 2));
  const int16_t ty = static_cast<int16_t>(cy - (tm.height / 2) - tm.yOffset);
  const uint16_t textColor = badge_text_color(fill);

  // For fixed 3-letter rail labels like BAB/HEM/RON, slot each glyph into a
  // fixed 21x11 grid: 2px side padding, 2px top/bottom padding, 5px glyph
  // columns, and 1px gaps between glyphs.
  if (textSize == kTextSizeTiny && labelLen == 3 && w >= 19) {
    constexpr int16_t kGlyphSlotW = 5;
    constexpr int16_t kGlyphGapW = 1;
    constexpr int16_t kGlyphContentH = 7;
    const int16_t totalGlyphW = static_cast<int16_t>(3 * kGlyphSlotW + 2 * kGlyphGapW);
    const int16_t startX = static_cast<int16_t>(x + ((w - totalGlyphW) / 2));
    const int16_t topY = static_cast<int16_t>(y + ((h - kGlyphContentH) / 2));
    for (int16_t i = 0; i < 3; ++i) {
      char glyph[2]{label[i], '\0'};
      TextMetrics glyphMetrics = display.measure_text(glyph, textSize);
      const int16_t slotX = static_cast<int16_t>(startX + i * (kGlyphSlotW + kGlyphGapW));
      const int16_t slotCenterX = static_cast<int16_t>(slotX + (kGlyphSlotW / 2));
      const int16_t glyphX =
          static_cast<int16_t>(slotCenterX - (glyphMetrics.width / 2) - glyphMetrics.xOffset + 1);
      const int16_t glyphY = static_cast<int16_t>(topY - glyphMetrics.yOffset + 1);
      display.draw_text_transparent(glyphX, glyphY, glyph, textColor, textSize);
    }
    return;
  }

  const int16_t tx = static_cast<int16_t>(cx - (tm.width / 2) - tm.xOffset);
  display.draw_text_transparent(tx, ty, label, textColor, textSize);
}

}  // namespace display
