#pragma once

#include <stdint.h>

namespace display {

constexpr int16_t kHomeTrainIconWidth = 12;
constexpr int16_t kHomeTrainIconHeight = 10;
constexpr int16_t kHomeBusIconWidth = 11;
constexpr int16_t kHomeBusIconHeight = 11;

// Monochrome train glyph sized for the 128x32 home screen chip row.
inline constexpr uint8_t kHomeTrainIcon[kHomeTrainIconWidth * kHomeTrainIconHeight] = {
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
    0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0,
    0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0,
    0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
};

// Monochrome bus glyph sized for CTA bus badges on the 128x32 matrix.
inline constexpr uint8_t kHomeBusIcon[kHomeBusIconWidth * kHomeBusIconHeight] = {
    0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0,
    0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0,
    0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0,
};

}  // namespace display
