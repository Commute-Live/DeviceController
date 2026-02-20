#pragma once

#include <stdint.h>

namespace display {

struct RowFrame {
  int16_t yStart;
  int16_t height;
};

struct RowLayout {
  int16_t badgeX;
  int16_t badgeY;
  int16_t badgeSize;
  int16_t destinationX;
  int16_t destinationWidth;
  int16_t etaX;
  int16_t etaWidth;
  int16_t textY;
};

class LayoutEngine final {
 public:
  static constexpr int16_t kOuterMargin = 2;
  static constexpr int16_t kInnerGap = 2;

  RowLayout compute_row_layout(int16_t totalWidth, const RowFrame &frame, uint8_t textSize, uint8_t etaChars) const;
};

}  // namespace display
