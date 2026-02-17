#pragma once

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "transit/providers/nyc/subway/catalog.h"

namespace display {
namespace providers {

struct RowChromeResult {
  int labelStartX;
  bool clearLeftColumn;
  bool prefixRouteInLabel;
};

RowChromeResult draw_row_chrome(MatrixPanel_I2S_DMA *matrix,
                                const String &providerId,
                                const String &routeId,
                                const transit::LineDefinition *nycLine,
                                int centerY,
                                bool redrawFixed);

}  // namespace providers
}  // namespace display
