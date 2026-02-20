#include "core/vertical_layout_engine.h"

namespace core {

namespace {

constexpr float kPaddingRatio = 0.12f;

int16_t distribute_extra(uint8_t index, uint8_t total, int16_t remainder) {
  if (remainder <= 0 || total == 0) {
    return 0;
  }
  const int32_t prev = (static_cast<int32_t>(remainder) * index) / total;
  const int32_t next = (static_cast<int32_t>(remainder) * (index + 1)) / total;
  return static_cast<int16_t>(next - prev);
}

}  // namespace

VerticalLayoutResult VerticalLayoutEngine::compute(uint16_t totalHeight, uint8_t rowCount) const {
  VerticalLayoutResult out{};
  out.rowCount = rowCount < 1 ? 1 : (rowCount > kMaxTransitRows ? kMaxTransitRows : rowCount);

  if (totalHeight == 0) {
    out.rows[0] = {0, 0};
    out.topMargin = 0;
    out.bottomMargin = 0;
    out.gap = 0;
    return out;
  }

  int16_t totalPadding = static_cast<int16_t>(static_cast<float>(totalHeight) * kPaddingRatio);
  if (totalPadding < 0) totalPadding = 0;
  if (totalPadding > static_cast<int16_t>(totalHeight - 1)) {
    totalPadding = static_cast<int16_t>(totalHeight - 1);
  }

  int16_t usableHeight = static_cast<int16_t>(totalHeight) - totalPadding;
  if (usableHeight < static_cast<int16_t>(out.rowCount)) {
    usableHeight = static_cast<int16_t>(out.rowCount);
    totalPadding = static_cast<int16_t>(totalHeight) - usableHeight;
  }

  const uint8_t gapCount = static_cast<uint8_t>(out.rowCount - 1);
  const uint8_t padSlots = static_cast<uint8_t>(gapCount + 2);  // top + gaps + bottom
  const int16_t padUnit = padSlots > 0 ? static_cast<int16_t>(totalPadding / padSlots) : 0;
  const int16_t padRemainder = padSlots > 0 ? static_cast<int16_t>(totalPadding % padSlots) : 0;

  const int16_t baseRowHeight = static_cast<int16_t>(usableHeight / out.rowCount);
  const int16_t rowRemainder = static_cast<int16_t>(usableHeight % out.rowCount);

  out.topMargin = static_cast<int16_t>(padUnit + distribute_extra(0, padSlots, padRemainder));
  out.gap = static_cast<int16_t>(padUnit + (gapCount > 0 ? distribute_extra(1, padSlots, padRemainder) : 0));
  out.bottomMargin = static_cast<int16_t>(padUnit + distribute_extra(static_cast<uint8_t>(padSlots - 1), padSlots, padRemainder));

  int16_t y = out.topMargin;
  for (uint8_t i = 0; i < out.rowCount; ++i) {
    const int16_t h = static_cast<int16_t>(baseRowHeight + distribute_extra(i, out.rowCount, rowRemainder));
    out.rows[i] = {y, h};
    if (i + 1 < out.rowCount) {
      int16_t gap = padUnit + distribute_extra(static_cast<uint8_t>(i + 1), padSlots, padRemainder);
      y = static_cast<int16_t>(y + h + gap);
    }
  }

  return out;
}

}  // namespace core
