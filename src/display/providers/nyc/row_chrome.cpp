#include "display/providers/nyc/row_chrome.h"

#include "display/Transit.h"
#include "display/layout_constants.h"

namespace display {
namespace providers {
namespace nyc {

RowChromeResult draw_row_chrome(MatrixPanel_I2S_DMA *matrix,
                                const String &providerId,
                                const String &routeId,
                                const transit::LineDefinition *line,
                                int centerY,
                                bool redrawFixed) {
  (void)matrix;
  constexpr int kLogoRadius = 6;
  const display::layout::RowLayoutProfile rowLayout =
      display::layout::row_layout_profile_for_width(matrix->width());
  const int kLogoCenterX = rowLayout.leftFixedWidth / 2;

  String p = providerId;
  p.trim();
  p.toLowerCase();
  const bool isBus = p == "mta-bus";

  if (redrawFixed && !isBus) {
    if (line) {
      draw_transit_logo(
          kLogoCenterX,
          centerY,
          line->symbol,
          line->color_hex,
          kLogoRadius,
          20,
          1,
          false,
          "white",
          40);
    } else {
      char fallbackSymbol = 'B';
      if (routeId.length() > 0) {
        fallbackSymbol = routeId[0];
        if (fallbackSymbol >= 'a' && fallbackSymbol <= 'z') {
          fallbackSymbol = fallbackSymbol - 'a' + 'A';
        }
      } else if (p == "mta-subway" || p == "mta") {
        fallbackSymbol = 'M';
      }
      draw_transit_logo(
          kLogoCenterX,
          centerY,
          fallbackSymbol,
          "#7C858C",
          kLogoRadius,
          20,
          1,
          false,
          "white",
          40);
    }
  }

  RowChromeResult result;
  result.labelStartX = rowLayout.leftFixedWidth + rowLayout.middleGap;
  result.clearLeftColumn = false;
  result.prefixRouteInLabel = isBus;
  return result;
}

}  // namespace nyc
}  // namespace providers
}  // namespace display
