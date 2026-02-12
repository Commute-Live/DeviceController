#pragma once

#include <Arduino.h>

uint16_t color_from_hex(const String &hex, int brightness);
uint16_t color_from_name(const String &name, int brightness);
