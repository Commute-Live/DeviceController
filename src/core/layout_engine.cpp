#include "core/layout_engine.h"

#include <stdio.h>
#include <string.h>

namespace core {

namespace {

constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorWhite = 0xFFFF;
constexpr uint16_t kColorCyan = 0x5F1A;
constexpr uint16_t kColorGray = 0x7BEF;
constexpr uint16_t kColorAmber = 0xFD20;
constexpr uint16_t kColorRed = 0xF800;
constexpr uint16_t kColorGreen = 0x07E0;

uint16_t eta_color(const char *eta) {
  if (!eta || eta[0] == '\0' || strcmp(eta, "--") == 0) {
    return kColorGray;
  }
  if (strncmp(eta, "DUE", 3) == 0 || strncmp(eta, "NOW", 3) == 0) {
    return kColorRed;
  }

  int minutes = 0;
  bool foundDigit = false;
  for (const char *p = eta; *p != '\0'; ++p) {
    if (*p >= '0' && *p <= '9') {
      foundDigit = true;
      minutes = (minutes * 10) + (*p - '0');
    } else if (foundDigit) {
      break;
    }
  }

  if (!foundDigit) {
    return kColorGray;
  }
  if (minutes <= 1) {
    return kColorRed;
  }
  if (minutes <= 5) {
    return kColorAmber;
  }
  if (minutes <= 12) {
    return kColorGreen;
  }
  return kColorWhite;
}

}  // namespace

const char *DrawList::copy_text(const char *text) {
  if (!text) {
    return nullptr;
  }
  const size_t len = strnlen(text, kTextPoolSize);
  if (len == 0 || len >= (kTextPoolSize - textUsed)) {
    return nullptr;
  }

  char *dst = &textPool[textUsed];
  memcpy(dst, text, len);
  dst[len] = '\0';
  textUsed += len + 1;
  return dst;
}

LayoutEngine::LayoutEngine() : width_(128), height_(32) {}

void LayoutEngine::set_viewport(uint16_t width, uint16_t height) {
  width_ = width;
  height_ = height;
}

const char *LayoutEngine::trim_for_width(const char *src, uint8_t charLimit, DrawList &out) {
  if (!src || charLimit == 0) {
    return out.copy_text("");
  }

  char buf[kMaxDestinationLen];
  size_t n = strnlen(src, sizeof(buf) - 1);
  if (n > static_cast<size_t>(charLimit)) {
    n = charLimit;
  }
  memcpy(buf, src, n);
  buf[n] = '\0';
  return out.copy_text(buf);
}

void LayoutEngine::build_transit_layout(const RenderModel &model, DrawList &out) {
  out.reset();

  DrawCommand bg{};
  bg.type = DrawCommandType::kFillRect;
  bg.x = 0;
  bg.y = 0;
  bg.w = static_cast<int16_t>(width_);
  bg.h = static_cast<int16_t>(height_);
  bg.color = kColorBlack;
  bg.bg = kColorBlack;
  bg.size = 1;
  bg.text = nullptr;
  out.push(bg);

  const bool transitView = model.hasData && model.uiState == UiState::kTransit;
  if (!transitView) {
    const VerticalLayoutResult home = verticalLayout_.compute(height_, 3);
    const uint8_t homeFont = height_ >= 64 ? 2 : 1;
    const int16_t charW = static_cast<int16_t>(6 * homeFont);
    const int16_t textH = static_cast<int16_t>(8 * homeFont);

    // Subtle top accent.
    DrawCommand accent{};
    accent.type = DrawCommandType::kFillRect;
    accent.x = 0;
    accent.y = 0;
    accent.w = static_cast<int16_t>(width_);
    accent.h = 1;
    accent.color = kColorCyan;
    accent.bg = kColorBlack;
    accent.size = 1;
    accent.text = nullptr;
    out.push(accent);

    // Row 1: brand.
    {
      const RowFrame frame = home.rows[0];
      const char *brand = "COMMUTE LIVE";
      const int16_t brandW = static_cast<int16_t>(strnlen(brand, 32) * charW);
      int16_t brandX = static_cast<int16_t>((static_cast<int16_t>(width_) - brandW) / 2);
      if (brandX < 2) brandX = 2;
      int16_t brandY = static_cast<int16_t>(frame.yStart + ((frame.height - textH) / 2));
      brandY = static_cast<int16_t>(brandY + 1);  // 1px top margin
      if (brandY < frame.yStart) brandY = frame.yStart;

      DrawCommand title{};
      title.type = DrawCommandType::kText;
      title.x = brandX;
      title.y = brandY;
      title.color = kColorWhite;
      title.bg = kColorBlack;
      title.size = homeFont;
      title.text = out.copy_text(brand);
      out.push(title);
    }

    // Row 2: supported cities with MTA/CTA style chips.
    {
      const RowFrame frame = home.rows[1];
      const uint8_t chipFont = 1;
      const int16_t chipH = 8;
      const int16_t chipY = static_cast<int16_t>(frame.yStart + ((frame.height - chipH) / 2));

      struct Chip {
        const char *text;
        uint16_t bg;
      };
      const Chip chips[] = {
          {"MTA", 0x01B4},  // NYC subway blue
          {"CTA", 0xF800},  // Chicago red accent
          {"MBTA", 0xFD20},
          {"SEPTA", 0x5F1A},
      };

      int16_t totalChipW = 0;
      for (size_t i = 0; i < sizeof(chips) / sizeof(chips[0]); ++i) {
        const int16_t textW = static_cast<int16_t>(strnlen(chips[i].text, 16) * 6);
        totalChipW = static_cast<int16_t>(totalChipW + textW + 4);  // 2px pad each side
        if (i + 1 < sizeof(chips) / sizeof(chips[0])) totalChipW = static_cast<int16_t>(totalChipW + 2);
      }

      int16_t x = static_cast<int16_t>((static_cast<int16_t>(width_) - totalChipW) / 2);
      if (x < 2) x = 2;

      for (size_t i = 0; i < sizeof(chips) / sizeof(chips[0]); ++i) {
        const int16_t textW = static_cast<int16_t>(strnlen(chips[i].text, 16) * 6);
        const int16_t chipW = static_cast<int16_t>(textW + 4);

        DrawCommand chipBg{};
        chipBg.type = DrawCommandType::kFillRect;
        chipBg.x = x;
        chipBg.y = chipY;
        chipBg.w = chipW;
        chipBg.h = chipH;
        chipBg.color = chips[i].bg;
        chipBg.bg = kColorBlack;
        chipBg.size = 1;
        chipBg.text = nullptr;
        out.push(chipBg);

        DrawCommand chipText{};
        chipText.type = DrawCommandType::kText;
        chipText.x = static_cast<int16_t>(x + 2);
        chipText.y = chipY;
        chipText.color = kColorWhite;
        chipText.bg = chips[i].bg;
        chipText.size = chipFont;
        chipText.text = out.copy_text(chips[i].text);
        out.push(chipText);

        x = static_cast<int16_t>(x + chipW + 2);
      }
    }

    // Row 3: status line + detail.
    {
      const RowFrame frame = home.rows[2];
      const uint8_t statusFont = 1;
      const int16_t statusTextH = 8;
      const int16_t y = static_cast<int16_t>(frame.yStart + ((frame.height - statusTextH) / 2));
      const int16_t rightChars = static_cast<int16_t>((static_cast<int16_t>(width_) - 2) / 6);

      if (frame.height >= 16) {
        DrawCommand status{};
        status.type = DrawCommandType::kText;
        status.x = 2;
        status.y = frame.yStart;
        status.color = kColorWhite;
        status.bg = kColorBlack;
        status.size = statusFont;
        status.text = trim_for_width(model.statusLine[0] ? model.statusLine : "BOOTING",
                                     static_cast<uint8_t>(rightChars), out);
        out.push(status);

        DrawCommand detail{};
        detail.type = DrawCommandType::kText;
        detail.x = 2;
        detail.y = static_cast<int16_t>(frame.yStart + 8);
        detail.color = kColorGray;
        detail.bg = kColorBlack;
        detail.size = 1;
        detail.text = trim_for_width(model.statusDetail[0] ? model.statusDetail : "",
                                     static_cast<uint8_t>(rightChars), out);
        out.push(detail);
      } else {
        char compact[kMaxDestinationLen];
        snprintf(compact, sizeof(compact), "%s %s",
                 model.statusLine[0] ? model.statusLine : "BOOTING",
                 model.statusDetail[0] ? model.statusDetail : "");

        DrawCommand compactLine{};
        compactLine.type = DrawCommandType::kText;
        compactLine.x = 2;
        compactLine.y = y < frame.yStart ? frame.yStart : y;
        compactLine.color = kColorWhite;
        compactLine.bg = kColorBlack;
        compactLine.size = 1;
        compactLine.text = trim_for_width(compact, static_cast<uint8_t>(rightChars), out);
        out.push(compactLine);
      }
    }

    return;
  }

  uint8_t rowCount = model.activeRows;
  if (rowCount < 1) rowCount = 1;
  if (rowCount > kMaxTransitRows) rowCount = kMaxTransitRows;

  const VerticalLayoutResult layout = verticalLayout_.compute(height_, rowCount);
  const int16_t baseRowHeight = layout.rows[0].height > 0 ? layout.rows[0].height : static_cast<int16_t>(height_);
  const int16_t candidateBadgeSize = static_cast<int16_t>((baseRowHeight * 3) / 4);
  const int16_t maxAllowed = static_cast<int16_t>(baseRowHeight - (2 * display::LayoutEngine::kOuterMargin));
  int16_t fixedBadgeSize = candidateBadgeSize < maxAllowed ? candidateBadgeSize : maxAllowed;
  if (fixedBadgeSize < 5) fixedBadgeSize = 5;
  const int16_t targetTextHeight = static_cast<int16_t>((fixedBadgeSize * 3) / 5);  // 0.6 * badge size
  uint8_t rowFont = static_cast<uint8_t>((targetTextHeight + 4) / 8);
  if (rowFont < 1) rowFont = 1;
  if (rowFont > 2) rowFont = 2;
  constexpr uint8_t kEtaChars = 3;
  const int16_t charW = static_cast<int16_t>(6 * rowFont);

  for (uint8_t i = 0; i < rowCount; ++i) {
    const TransitRowModel &row = model.rows[i];
    const RowFrame frame = layout.rows[i];
    const bool hasRoute = row.routeId[0] != '\0' && strcmp(row.routeId, "--") != 0;
    display::RowFrame rowFrame{
        frame.yStart,
        frame.height,
    };
    const display::RowLayout rowGeom =
        rowLayout_.compute_row_layout(static_cast<int16_t>(width_), rowFrame, fixedBadgeSize, rowFont, kEtaChars);

    DrawCommand badge{};
    badge.type = DrawCommandType::kBadge;
    badge.x = rowGeom.badgeX;
    badge.y = rowGeom.badgeY;
    badge.w = rowGeom.badgeSize;
    badge.h = rowGeom.badgeSize;
    badge.color = kColorWhite;
    badge.bg = kColorBlack;
    badge.size = rowFont;
    badge.text = trim_for_width(hasRoute ? row.routeId : "--", 2, out);
    out.push(badge);

    DrawCommand eta{};
    eta.type = DrawCommandType::kText;
    const uint8_t etaLen = static_cast<uint8_t>(strnlen(row.eta[0] ? row.eta : "--", kMaxEtaLen - 1));
    const int16_t etaDrawW = static_cast<int16_t>(etaLen * charW);
    eta.x = static_cast<int16_t>(rowGeom.etaX + rowGeom.etaWidth - etaDrawW);
    eta.y = rowGeom.textY;
    eta.color = eta_color(row.eta);
    eta.bg = kColorBlack;
    eta.size = rowFont;
    eta.text = trim_for_width(row.eta[0] ? row.eta : "--", kEtaChars, out);
    out.push(eta);

    const uint8_t labelChars =
        rowGeom.destinationWidth > 0 ? static_cast<uint8_t>(rowGeom.destinationWidth / charW) : 0;

    DrawCommand destination{};
    destination.type = DrawCommandType::kText;
    destination.x = rowGeom.destinationX;
    destination.y = rowGeom.textY;
    destination.color = kColorWhite;
    destination.bg = kColorBlack;
    destination.size = rowFont;
    destination.text = trim_for_width(row.destination[0] ? row.destination : "-", labelChars, out);
    out.push(destination);
  }
}

}  // namespace core
