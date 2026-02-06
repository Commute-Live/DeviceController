#pragma once

#include <Arduino.h>

void draw_transit_logo(int center_x,
                       int center_y,
                       char letter,
                       const String &color,
                       int radius,
                       int brightness,
                       bool clear);
