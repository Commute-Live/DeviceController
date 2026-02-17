#pragma once

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "display/providers/row_chrome.h"

namespace display {
namespace providers {
namespace boston {

RowChromeResult draw_row_chrome(MatrixPanel_I2S_DMA *matrix,
                                const String &routeId,
                                int centerY,
                                bool redrawFixed);

}  // namespace boston
}  // namespace providers
}  // namespace display
