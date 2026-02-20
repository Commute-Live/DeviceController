#include "display/LayoutEngine.h"

namespace display {

RowLayout LayoutEngine::compute_row_layout(int16_t totalWidth,
                                           const RowFrame &frame,
                                           uint8_t textSize,
                                           uint8_t etaChars) const {
  RowLayout out{};
  if (textSize < 1) textSize = 1;
  if (etaChars < 1) etaChars = 1;

  // Strict geometry derived from row height.
  int16_t badgeSize = static_cast<int16_t>((frame.height * 3) / 4);  // 0.75 * rowHeight
  if (badgeSize < 5) badgeSize = 5;
  // Force odd diameter so circle center maps to an exact pixel and stays symmetrical.
  if ((badgeSize & 1) == 0) {
    badgeSize -= 1;
  }

  const int16_t etaWidth = static_cast<int16_t>(etaChars * 6 * textSize);
  const int16_t badgeX = kOuterMargin;
  const int16_t badgeY = static_cast<int16_t>(frame.yStart + ((frame.height - badgeSize) / 2));
  const int16_t leftZoneWidth = static_cast<int16_t>(badgeSize + kInnerGap);
  const int16_t etaX = static_cast<int16_t>(totalWidth - kOuterMargin - etaWidth);
  const int16_t destinationX = static_cast<int16_t>(badgeX + leftZoneWidth);
  int16_t destinationWidth = static_cast<int16_t>(etaX - destinationX - kInnerGap);
  if (destinationWidth < 0) destinationWidth = 0;

  const int16_t textHeight = static_cast<int16_t>(8 * textSize);
  // Keep destination + ETA vertically centered to the badge (logo), not just the row box.
  const int16_t textY = static_cast<int16_t>(badgeY + ((badgeSize - textHeight) / 2));

  out.badgeX = badgeX;
  out.badgeY = badgeY;
  out.badgeSize = badgeSize;
  out.destinationX = destinationX;
  out.destinationWidth = destinationWidth;
  out.etaX = etaX;
  out.etaWidth = etaWidth;
  out.textY = textY;
  return out;
}

}  // namespace display
