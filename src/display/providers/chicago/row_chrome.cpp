#include "display/providers/chicago/row_chrome.h"

#include "display/layout_constants.h"
#include "transit/providers/chicago/subway/style.h"
#include "transit/providers/nyc/subway/colors.h"

namespace display {
namespace providers {
namespace chicago {

RowChromeResult draw_row_chrome(MatrixPanel_I2S_DMA *matrix,
                                const String &routeId,
                                int centerY,
                                bool redrawFixed) {
  const display::layout::RowLayoutProfile rowLayout =
      display::layout::row_layout_profile_for_width(matrix->width());
  const int kBadgeX = (rowLayout.leftFixedWidth - display::layout::kBadgeWidth) / 2;

  if (redrawFixed) {
    const String badgeText = transit::providers::chicago::subway::route_label(routeId);
    const uint16_t badgeColor = transit::providers::nyc::subway::color_from_hex(
        transit::providers::chicago::subway::route_color_hex(routeId), 40);

    const int badgeY = centerY - (display::layout::kBadgeHeight / 2);
    matrix->fillRoundRect(
        kBadgeX,
        badgeY,
        display::layout::kBadgeWidth,
        display::layout::kBadgeHeight,
        display::layout::kBadgeCornerRadius,
        badgeColor);

    int16_t x1, y1;
    uint16_t w, h;
    matrix->getTextBounds(badgeText.c_str(), 0, 0, &x1, &y1, &w, &h);
    int textX = kBadgeX + ((display::layout::kBadgeWidth - (int)w) / 2);
    if (textX < kBadgeX + 1) textX = kBadgeX + 1;
    matrix->setTextColor(
        transit::providers::nyc::subway::color_from_name(
            transit::providers::chicago::subway::route_text_color(routeId), 80),
        badgeColor);
    matrix->setCursor(textX, centerY - 3);
    matrix->print(badgeText);
  }

  RowChromeResult result;
  result.labelStartX = rowLayout.leftFixedWidth + rowLayout.middleGap;
  result.clearLeftColumn = false;
  result.prefixRouteInLabel = false;
  return result;
}

}  // namespace chicago
}  // namespace providers
}  // namespace display
