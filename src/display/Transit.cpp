#include "Transit.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "transit/providers/nyc/subway/colors.h"

// Matrix instance lives in main.cpp; we render into it here.
extern MatrixPanel_I2S_DMA *matrix;

/*
GUIDE LINES FOR THIS FUNCTION:

Single line display
r = 14
Double line display
r = 5
*/

// Draws a solid pixel-circle badge with a centered letter.
// size = radius in pixels (minimum 2 recommended).
void draw_transit_logo(int center_x,
                       int center_y,
                       char letter,
                       const String &color,
                       int radius,
                       int brightness,
                       int text_size,
                       bool clear,
                       const String &text_color,
                       int text_brightness) {
  // Clamp radius so the badge is always visible.
  int r = max(2, radius);

  uint16_t circleColor = transit::providers::nyc::subway::color_from_name(color, brightness);
  int tb = (text_brightness < 0) ? brightness : text_brightness;
  uint16_t letterColor = transit::providers::nyc::subway::color_from_name(text_color, tb);

  if (clear) {
    matrix->fillScreen(0);
  }
  matrix->setRotation(0);

  int16_t cx = center_x;
  int16_t cy = center_y;

  // Keep the full circle within bounds.
  cx = constrain(cx, r, matrix->width() - r);
  cy = constrain(cy, r, matrix->height() - r);

  matrix->fillCircle(cx, cy, r, circleColor);

  matrix->setTextWrap(false);
  matrix->setTextColor(letterColor);
  matrix->setTextSize(text_size);

  char s[2] = {letter, '\0'};
  int16_t x1, y1;
  uint16_t w, h;
  matrix->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);

  // Center glyph bbox on the circle center.
  int16_t tx = cx - (x1 + (int16_t)w / 2);
  int16_t ty = cy - (y1 + (int16_t)h / 2);

  // Apply a small visual nudge for this font size on HUB75.
  tx += 1;
  ty += 1;

  matrix->setCursor(tx, ty);
  matrix->print(s);
}

void draw_transit_logo_large(char letter, const String &color) {
  draw_transit_logo(
      16,
      matrix->height() / 2,
      letter,
      color,
      14,
      20,
      3,
      true,
      "white",
      40);
}
