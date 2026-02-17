#include "display/providers/philadelphia/row_chrome.h"

#include "display/layout_constants.h"
#include "transit/providers/nyc/subway/colors.h"

namespace display {
namespace providers {
namespace philadelphia {

RowChromeResult draw_row_chrome(MatrixPanel_I2S_DMA *matrix,
                                const String &providerId,
                                const String &routeId,
                                int centerY,
                                bool redrawFixed) {
  const display::layout::RowLayoutProfile rowLayout =
      display::layout::row_layout_profile_for_width(matrix->width());
  const int kBusBadgeX = (rowLayout.leftFixedWidth - display::layout::kBadgeWidth) / 2;
  constexpr int kBusBadgeW = display::layout::kBadgeWidth;
  constexpr int kBusBadgeH = display::layout::kBadgeHeight;

  const int kRailBadgeX = (rowLayout.leftFixedWidth - 20) / 2;
  constexpr int kRailBadgeW = 20;
  constexpr int kRailBadgeH = 11;
  constexpr int kRailBadgeRadius = 3;

  String p = providerId;
  p.trim();
  p.toLowerCase();
  const bool isBus = p == "septa-bus" || p == "philly-bus";

  if (redrawFixed) {
    if (isBus) {
      String badgeText = routeId.length() ? routeId : String("BUS");
      const uint16_t badgeColor = transit::providers::nyc::subway::color_from_hex("#2A7F62", 40);
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
    } else {
      String badgeText = routeId.length() ? routeId : String("RR");
      badgeText.toUpperCase();
      if (badgeText.length() > 3) badgeText = badgeText.substring(0, 3);

      const uint16_t badgeColor = transit::providers::nyc::subway::color_from_hex("#1B4F9C", 40);
      const int badgeY = centerY - (kRailBadgeH / 2);
      matrix->fillRoundRect(
          kRailBadgeX,
          badgeY,
          kRailBadgeW,
          kRailBadgeH,
          kRailBadgeRadius,
          badgeColor);

      int16_t x1, y1;
      uint16_t w, h;
      matrix->setTextSize(1);
      matrix->getTextBounds(badgeText.c_str(), 0, 0, &x1, &y1, &w, &h);
      const int16_t tx = kRailBadgeX + ((kRailBadgeW - (int16_t)w) / 2) - x1 + 1;
      const int16_t ty = badgeY + ((kRailBadgeH - (int16_t)h) / 2) - y1 + 1;
      matrix->setTextColor(transit::providers::nyc::subway::color_from_name("white", 80), badgeColor);
      matrix->setCursor(tx, ty);
      matrix->print(badgeText);
    }
  }

  RowChromeResult result;
  result.labelStartX = rowLayout.leftFixedWidth + rowLayout.middleGap;
  result.clearLeftColumn = false;
  result.prefixRouteInLabel = false;
  return result;
}

}  // namespace philadelphia
}  // namespace providers
}  // namespace display
