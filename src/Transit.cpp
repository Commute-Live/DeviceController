#include "Transit.h"
#include <Adafruit_Protomatter.h>
#include "color.h"

// Matrix instance lives in main.cpp; we render into it here.
extern Adafruit_Protomatter matrix;

// Draws a solid pixel-circle badge with a centered letter.
// size = radius in pixels (minimum 2 recommended).
void draw_transit_logo(int center_x,
                       int center_y,
                       char letter,
                       const String &color,
                       int radius,
                       int brightness,
                       bool clear) {
  // Clamp radius so the badge is always visible.
  int r = max(2, radius);

  uint16_t circleColor = color_from_name(color, brightness);
  uint16_t letterColor = color_from_name("white", brightness);

  if (clear) {
    matrix.fillScreen(0);
  }
  matrix.setRotation(0);

  // Use caller-provided center coordinates.
  int16_t cx = center_x;
  int16_t cy = center_y;

  // Keep the full circle within bounds.
  cx = constrain(cx, r, matrix.width() - r);
  cy = constrain(cy, r, matrix.height() - r);

  // Draw the solid badge.
  matrix.fillCircle(cx, cy, r, circleColor);

  // Draw letter in the badge.
  matrix.setTextWrap(false);
  matrix.setTextColor(letterColor);
  matrix.setTextSize(2); // keep consistent for transit badge look

  char s[2] = {letter, '\0'};
  int16_t x1, y1;
  uint16_t w, h;
  matrix.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);

  // Center glyph bbox on circle center.
  int16_t tx = cx - (int16_t)w / 2 - x1;
  int16_t ty = cy - (int16_t)h / 2 - y1;

  // One global optical tweak (applies to all chars).
  ty += 1;
  tx += 1;

  matrix.setCursor(tx, ty);
  matrix.print(s);
  matrix.show();
}
