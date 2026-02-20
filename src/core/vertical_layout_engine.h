#pragma once

#include <stdint.h>

#include "core/models.h"

namespace core {

struct RowFrame {
  int16_t yStart;
  int16_t height;
};

struct VerticalLayoutResult {
  RowFrame rows[kMaxTransitRows];
  uint8_t rowCount;
  int16_t topMargin;
  int16_t bottomMargin;
  int16_t gap;
};

class VerticalLayoutEngine final {
 public:
  VerticalLayoutResult compute(uint16_t totalHeight, uint8_t rowCount) const;
};

}  // namespace core
