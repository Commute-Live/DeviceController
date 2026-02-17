#pragma once

namespace display {
namespace layout {

constexpr int kBadgeWidth = 24;
constexpr int kBadgeHeight = 12;
constexpr int kBadgeCornerRadius = 2;
constexpr int kLeftMarginPx = 2;

struct RowLayoutProfile {
  int leftFixedWidth;
  int rightFixedWidth;
  int middleGap;
};

inline RowLayoutProfile row_layout_profile_for_width(int panelWidthPx) {
  // 64x32 panel chain (legacy layout)
  if (panelWidthPx < 128) {
    return RowLayoutProfile{26, 20, 1};
  }

  // 128x32 and wider layouts
  return RowLayoutProfile{30, 22, 1};
}

}  // namespace layout
}  // namespace display
