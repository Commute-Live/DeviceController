#include "display/providers/row_chrome.h"

#include "display/providers/boston/row_chrome.h"
#include "display/providers/chicago/row_chrome.h"
#include "display/providers/nyc/row_chrome.h"
#include "display/providers/philadelphia/row_chrome.h"
#include "transit/providers/boston/subway/style.h"

namespace display {
namespace providers {

RowChromeResult draw_row_chrome(MatrixPanel_I2S_DMA *matrix,
                                const String &providerId,
                                const String &routeId,
                                const transit::LineDefinition *nycLine,
                                int centerY,
                                bool redrawFixed) {
  String p = providerId;
  p.trim();
  p.toLowerCase();

  if (p == "cta-subway" || p == "cta") {
    return chicago::draw_row_chrome(matrix, routeId, centerY, redrawFixed);
  }

  if (transit::providers::boston::subway::is_mbta_provider_id(p)) {
    return boston::draw_row_chrome(matrix, routeId, centerY, redrawFixed);
  }

  if (p == "septa-bus" || p == "septa-rail" || p == "philly-bus" || p == "philly-rail") {
    return philadelphia::draw_row_chrome(matrix, p, routeId, centerY, redrawFixed);
  }

  return nyc::draw_row_chrome(matrix, p, routeId, nycLine, centerY, redrawFixed);
}

}  // namespace providers
}  // namespace display
