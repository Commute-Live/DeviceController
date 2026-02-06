#include "color.h"

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t color_from_name(const String &name, int brightness) {
  String n = name;
  n.toLowerCase();
  
  // Constrain brightness to valid 0-255 range
  if (brightness > 255) brightness = 255;
  if (brightness < 0) brightness = 0;

  // Primary & Secondary Colors
  if (n == "white")   return rgb565(brightness, brightness, brightness);
  if (n == "black")   return rgb565(0, 0, 0);
  if (n == "red")     return rgb565(brightness, 0, 0);
  if (n == "green")   return rgb565(0, brightness, 0);
  if (n == "blue")    return rgb565(0, 0, brightness);
  if (n == "yellow")  return rgb565(brightness, brightness, 0);
  if (n == "cyan")    return rgb565(0, brightness, brightness);
  if (n == "magenta") return rgb565(brightness, 0, brightness);

  // Complex Colors (Scaled by brightness)
  // Orange: 100% Red, ~64% Green
  if (n == "orange")  return rgb565(brightness, (brightness * 165) >> 8, 0);
  
  // Purple: ~50% Red, 0% Green, ~50% Blue
  if (n == "purple")  return rgb565((brightness * 128) >> 8, 0, (brightness * 128) >> 8);
  
  // Violet: ~93% Red, ~50% Green, 100% Blue
  if (n == "violet")  return rgb565((brightness * 238) >> 8, (brightness * 130) >> 8, brightness);
  
  // Gray: 50% Red, 50% Green, 50% Blue
  if (n == "gray" || n == "grey") {
    int g = brightness >> 1; // Half brightness
    return rgb565(g, g, g);
  }

  return rgb565(brightness, brightness, brightness);
}