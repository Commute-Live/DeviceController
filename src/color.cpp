#include "color.h"

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

uint16_t color_from_hex(const String &hex, int brightness) {
  if (brightness > 255) brightness = 255;
  if (brightness < 0) brightness = 0;

  String s = hex;
  s.trim();

  if (s.length() == 7 && s[0] == '#') {
    int r1 = hex_digit(s[1]), r2 = hex_digit(s[2]);
    int g1 = hex_digit(s[3]), g2 = hex_digit(s[4]);
    int b1 = hex_digit(s[5]), b2 = hex_digit(s[6]);

    if (r1 >= 0 && r2 >= 0 && g1 >= 0 && g2 >= 0 && b1 >= 0 && b2 >= 0) {
      uint8_t r = static_cast<uint8_t>(((r1 << 4) | r2) * brightness / 255);
      uint8_t g = static_cast<uint8_t>(((g1 << 4) | g2) * brightness / 255);
      uint8_t b = static_cast<uint8_t>(((b1 << 4) | b2) * brightness / 255);
      return rgb565(r, g, b);
    }
  }

  return rgb565(brightness, brightness, brightness);
}

// WIP need to verify these colors or provide custom colors
uint16_t color_from_name(const String &name, int brightness) {
  String n = name;
  n.toLowerCase();
  
  // Constrain brightness to valid 0-255 range
  if (brightness > 255) brightness = 255;
  if (brightness < 0) brightness = 0;

  if (name.length() > 0 && name[0] == '#') {
    return color_from_hex(name, brightness);
  }

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
  if (n == "purple")  return rgb565(brightness, 10, brightness);
  
  // Violet: ~93% Red, ~50% Green, 100% Blue
  if (n == "violet")  return rgb565((brightness * 238) >> 8, (brightness * 130) >> 8, brightness);
  
  // Gray: 50% Red, 50% Green, 50% Blue
  if (n == "gray" || n == "grey") {
    int g = brightness >> 1; // Half brightness
    return rgb565(g, g, g);
  }

  return rgb565(brightness, brightness, brightness);
}
