#pragma once

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "display/providers/row_chrome.h"
#include "transit/providers/nyc/subway/catalog.h"

namespace display {
namespace providers {
namespace nyc {

RowChromeResult draw_row_chrome(MatrixPanel_I2S_DMA *matrix,
                                const String &providerId,
                                const String &routeId,
                                const transit::LineDefinition *line,
                                int centerY,
                                bool redrawFixed);

}  // namespace nyc
}  // namespace providers
}  // namespace display
