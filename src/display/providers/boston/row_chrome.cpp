#include "display/providers/boston/row_chrome.h"

#include "display/layout_constants.h"
#include "transit/providers/boston/subway/style.h"
#include "transit/providers/nyc/subway/colors.h"

namespace display {
namespace providers {
namespace boston {

RowChromeResult draw_row_chrome(MatrixPanel_I2S_DMA *matrix,
                                const String &routeId,
                                int centerY,
                                bool redrawFixed) {
  const display::layout::RowLayoutProfile rowLayout =
      display::layout::row_layout_profile_for_width(matrix->width());
  const int kBusBadgeX = (rowLayout.leftFixedWidth - display::layout::kBadgeWidth) / 2;
  constexpr int kBusBadgeW = display::layout::kBadgeWidth;
  constexpr int kBusBadgeH = display::layout::kBadgeHeight;

  const int kTrainBadgeX = (rowLayout.leftFixedWidth - 16) / 2;
  constexpr int kTrainBadgeW = 16;
  constexpr int kTrainBadgeH = 11;
  constexpr int kTrainBadgeRadius = 4;

  const bool isSubway = transit::providers::boston::subway::is_subway_line(routeId);

  if (redrawFixed) {
    if (isSubway) {
      String badgeText = transit::providers::boston::subway::subway_badge_text(routeId);
      const uint16_t badgeColor = transit::providers::nyc::subway::color_from_hex(
          transit::providers::boston::subway::subway_color_hex(routeId), 40);
      const int badgeY = centerY - (kTrainBadgeH / 2);
      matrix->fillRoundRect(
          kTrainBadgeX,
          badgeY,
          kTrainBadgeW,
          kTrainBadgeH,
          kTrainBadgeRadius,
          badgeColor);

      int16_t x1, y1;
      uint16_t w, h;
      matrix->setTextSize(1);
      matrix->getTextBounds(badgeText.c_str(), 0, 0, &x1, &y1, &w, &h);
      const int16_t tx = kTrainBadgeX + ((kTrainBadgeW - (int16_t)w) / 2) - x1 + 1;
      const int16_t ty = badgeY + ((kTrainBadgeH - (int16_t)h) / 2) - y1 + 1;
      matrix->setTextColor(transit::providers::nyc::subway::color_from_name("white", 80), badgeColor);
      matrix->setCursor(tx, ty);
      matrix->print(badgeText);
    } else {
      String badgeText = routeId.length() ? routeId : String("BUS");
      const uint16_t badgeColor = transit::providers::nyc::subway::color_from_hex("#2A2F35", 40);
      const int badgeY = centerY - (kBusBadgeH / 2);
      matrix->fillRoundRect(
          kBusBadgeX,
          badgeY,
          kBusBadgeW,
          kBusBadgeH,
          display::layout::kBadgeCornerRadius,
          badgeColor);

      int16_t x1, y1;
      uint16_t w, h;
      matrix->getTextBounds(badgeText.c_str(), 0, 0, &x1, &y1, &w, &h);
      int textX = kBusBadgeX + ((kBusBadgeW - (int)w) / 2);
      if (textX < kBusBadgeX + 1) textX = kBusBadgeX + 1;
      matrix->setTextColor(transit::providers::nyc::subway::color_from_name("white", 80), badgeColor);
      matrix->setCursor(textX, centerY - 3);
      matrix->print(badgeText);
    }
  }

  RowChromeResult result;
  result.labelStartX = rowLayout.leftFixedWidth + rowLayout.middleGap;
  result.clearLeftColumn = false;
  result.prefixRouteInLabel = false;
  return result;
}

}  // namespace boston
}  // namespace providers
}  // namespace display
