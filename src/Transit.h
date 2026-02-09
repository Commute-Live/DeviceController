#pragma once

#include <Arduino.h>

struct TransitLogoPreset {
  int center_x;
  int center_y;
  char letter;
  const char *color;
  int radius;
  int brightness;
  int text_size;
  bool clear;
  const char *text_color;
  int text_brightness;
};

void draw_transit_logo(int center_x,
                       int center_y,
                       char letter,
                       const String &color,
                       int radius,
                       int brightness,
                       int text_size,
                       bool clear,
                       const String &text_color = "white",
                       int text_brightness = -1);


void draw_transit_logo_preset(const TransitLogoPreset &preset);

extern const TransitLogoPreset LARGE_MTA_E;
extern const TransitLogoPreset MTA_E_ACCURATE;
extern const TransitLogoPreset MTA_7_ACCURATE;
extern const TransitLogoPreset MTA_G_ACCURATE;
