#include "Transit.h"
#include <Adafruit_Protomatter.h>
#include "color.h"

// Matrix instance lives in main.cpp; we render into it here.
extern Adafruit_Protomatter matrix;

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

  uint16_t circleColor = color_from_name(color, brightness);
  int tb = (text_brightness < 0) ? brightness : text_brightness;
  uint16_t letterColor = color_from_name(text_color, tb);

  if (clear) {
    matrix.fillScreen(0);
  }
  matrix.setRotation(0);

  int16_t cx = center_x;
  int16_t cy = center_y;

  // Keep the full circle within bounds.
  cx = constrain(cx, r, matrix.width() - r);
  cy = constrain(cy, r, matrix.height() - r);

  matrix.fillCircle(cx, cy, r, circleColor);

  matrix.setTextWrap(false);
  matrix.setTextColor(letterColor);
  matrix.setTextSize(text_size);

  char s[2] = {letter, '\0'};
  int16_t x1, y1;
  uint16_t w, h;
  matrix.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);

  // Center glyph bbox on circle center.
  int16_t tx = cx - (int16_t)w / 2 - x1;
  int16_t ty = cy - (int16_t)h / 2 - y1;

  if ((w & 1) == 0) tx += 1;
  if ((h & 1) == 0) ty += 1;
  tx += 1;
  ty += 1;

  matrix.setCursor(tx, ty);
  matrix.print(s);
  matrix.show();
}

void draw_transit_logo_preset(const TransitLogoPreset &preset) {
  int center_y = preset.center_y;
  if (center_y < 0) {
    center_y = matrix.height() / 2;
  }

  draw_transit_logo(preset.center_x,
                    center_y,
                    preset.letter,
                    preset.color,
                    preset.radius,
                    preset.brightness,
                    preset.text_size,
                    preset.clear,
                    preset.text_color,
                    preset.text_brightness);
}

const TransitLogoPreset LARGE_MTA_E = {
    15,
    -1,
    'E',
    "blue",
    14,
    20,
    3,
    true,
    "white",
    40,
};

const TransitLogoPreset LARGE_MTA_7 = {
    15,
    -1,
    '7',
    "purple",
    14,
    20,
    3,
    true,
    "white",
    40,
};

const TransitLogoPreset LARGE_MTA_G = {
    15,
    -1,
    'G',
    "green",
    14,
    20,
    3,
    true,
    "white",
    40,
};

const TransitLogoPreset SMALL_MTA_E = {
    12,
    -1,
    'E',
    "blue",
    10,
    20,
    1,
    true,
    "white",
    40,
};

const TransitLogoPreset SMALL_MTA_7 = {
    36,
    -1,
    '7',
    "purple",
    10,
    20,
    1,
    false,
    "white",
    40,
};

const TransitLogoPreset SMALL_MTA_G = {
    48,
    -1,
    'G',
    "green",
    12,
    20,
    1,
    false,
    "white",
    40,
};
