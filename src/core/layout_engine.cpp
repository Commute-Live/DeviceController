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
constexpr uint8_t kMinDisplayType = 1;
constexpr uint8_t kMaxDisplayType = 5;
constexpr uint8_t kTextSizeTiny = 0;
constexpr uint8_t kTextSizeTinyPlus = 255;

struct TransitPresetConfig {
  int16_t topMarginTwoRow;
  int16_t betweenMarginTwoRow;
  int16_t bottomMarginTwoRow;
  int16_t topMarginThreeRow;
  int16_t betweenMarginThreeRow;
  int16_t bottomMarginThreeRow;
  int16_t rowXShift;
  int16_t rowYNudge;
  int16_t etaRightNudgePx;
  int16_t destinationYNudge;
  int16_t etaYNudge;
};

constexpr TransitPresetConfig kPreset1Config{
    2,   // topMarginTwoRow
    2,   // betweenMarginTwoRow
    2,   // bottomMarginTwoRow
    1,   // topMarginThreeRow
    1,   // betweenMarginThreeRow
    1,   // bottomMarginThreeRow
    -1,  // rowXShift
    -1,  // rowYNudge
    2,   // etaRightNudgePx
    1,   // destinationYNudge
    1,   // etaYNudge
};

uint8_t normalize_display_type(uint8_t value) {
  if (value < kMinDisplayType) return kMinDisplayType;
  if (value > kMaxDisplayType) return kMaxDisplayType;
  return value;
}

const TransitPresetConfig &transit_preset_config(uint8_t displayType) {
  // Presets 2-5 will get dedicated geometry later; keep preset 1 behavior for now.
  switch (normalize_display_type(displayType)) {
    case 2:
    case 3:
    case 4:
    case 5:
    case 1:
    default:
      return kPreset1Config;
  }
}

const char *row_label_for_display_type(const TransitRowModel &row, uint8_t displayType) {
  const uint8_t normalizedDisplayType = normalize_display_type(displayType);
  if (normalizedDisplayType == 2 && row.direction[0] != '\0') {
    return row.direction;
  }
  return row.destination[0] ? row.destination : "-";
}

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

    // Row 1: brand.
    {
      const RowFrame frame = home.rows[0];
      const char *brand = "COMMUTE LIVE";
      const int16_t brandW = static_cast<int16_t>(strnlen(brand, 32) * charW);
      int16_t brandX = static_cast<int16_t>((static_cast<int16_t>(width_) - brandW) / 2);
      if (brandX < 2) brandX = 2;
      int16_t brandY = static_cast<int16_t>(frame.yStart + ((frame.height - textH) / 2));
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
          {"SEPTA", 0x1A74},  // #1F4FA3
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
  const TransitPresetConfig &preset = transit_preset_config(model.displayType);

  RowFrame rowFrames[kMaxTransitRows]{};
  const int16_t kTopMargin = (rowCount == 3) ? preset.topMarginThreeRow : preset.topMarginTwoRow;
  const int16_t kBetweenMargin = (rowCount == 3) ? preset.betweenMarginThreeRow : preset.betweenMarginTwoRow;
  const int16_t kBottomMargin = (rowCount == 3) ? preset.bottomMarginThreeRow : preset.bottomMarginTwoRow;
  const int16_t totalHeight = static_cast<int16_t>(height_);
  const int16_t totalGap = static_cast<int16_t>(kTopMargin + kBottomMargin + (rowCount - 1) * kBetweenMargin);
  const int16_t drawable = static_cast<int16_t>(totalHeight - totalGap);
  int16_t blockH = rowCount > 0 ? static_cast<int16_t>(drawable / rowCount) : 0;

  // Fallback to legacy vertical layout only if constraints are invalid.
  if (blockH <= 0) {
    const VerticalLayoutResult layout = verticalLayout_.compute(height_, rowCount);
    for (uint8_t i = 0; i < rowCount; ++i) rowFrames[i] = layout.rows[i];
    blockH = rowFrames[0].height > 0 ? rowFrames[0].height : 1;
  } else {
    for (uint8_t i = 0; i < rowCount; ++i) {
      rowFrames[i] = {
          static_cast<int16_t>(kTopMargin + i * (blockH + kBetweenMargin)),
          blockH,
      };
    }
  }

  int16_t targetRadius = static_cast<int16_t>((blockH - 1) / 2);
  if (rowCount == 3) {
    targetRadius = 4;  // Logos branch rule.
  }
  if (targetRadius < 1) targetRadius = 1;

  // BadgeRenderer uses r = (size / 2) - 1, so invert to preserve target radius.
  int16_t fixedBadgeSize = static_cast<int16_t>(2 * (targetRadius + 1));
  if (fixedBadgeSize < 5) fixedBadgeSize = 5;
  const int16_t targetTextHeight = static_cast<int16_t>((fixedBadgeSize * 3) / 5);  // 0.6 * badge size
  uint8_t rowFont = static_cast<uint8_t>((targetTextHeight + 4) / 8);
  if (rowFont < 1) rowFont = 1;
  if (rowFont > 2) rowFont = 2;
  constexpr uint8_t kEtaChars = 3;
  const int16_t charW = static_cast<int16_t>(6 * rowFont);

  for (uint8_t i = 0; i < rowCount; ++i) {
    const TransitRowModel &row = model.rows[i];
    const uint8_t normalizedDisplayType = normalize_display_type(model.displayType);
    const RowFrame frame = rowFrames[i];
    const bool hasRoute = row.routeId[0] != '\0' && strcmp(row.routeId, "--") != 0;
    const int16_t rowY =
        frame.yStart > 0 ? static_cast<int16_t>(frame.yStart + preset.rowYNudge) : frame.yStart;
    display::RowFrame rowFrame{
        rowY,
        frame.height,
    };
    const display::RowLayout rowGeom =
        rowLayout_.compute_row_layout(static_cast<int16_t>(width_), rowFrame, fixedBadgeSize, rowFont, kEtaChars);
    const int16_t badgeX =
        rowGeom.badgeX > 0 ? static_cast<int16_t>(rowGeom.badgeX + preset.rowXShift) : rowGeom.badgeX;
    const int16_t destinationX =
        rowGeom.destinationX > 0 ? static_cast<int16_t>(rowGeom.destinationX + preset.rowXShift) : rowGeom.destinationX;
    const int16_t etaX = rowGeom.etaX > 0 ? static_cast<int16_t>(rowGeom.etaX + preset.rowXShift) : rowGeom.etaX;

    DrawCommand badge{};
    badge.type = DrawCommandType::kBadge;
    badge.x = badgeX;
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
    const uint8_t etaFont =
        (normalizedDisplayType == 4 || normalizedDisplayType == 5)
            ? (rowFont > 1 ? static_cast<uint8_t>(rowFont - 1) : 1)
            : rowFont;
    const int16_t etaCharW = static_cast<int16_t>(6 * etaFont);
    const uint8_t etaLen = static_cast<uint8_t>(strnlen(row.eta[0] ? row.eta : "--", kMaxEtaLen - 1));
    const int16_t etaDrawW = static_cast<int16_t>(etaLen * etaCharW);
    eta.x = static_cast<int16_t>(etaX + rowGeom.etaWidth - etaDrawW + preset.etaRightNudgePx);
    eta.y = static_cast<int16_t>(rowGeom.textY + preset.etaYNudge);
    eta.color = eta_color(row.eta);
    eta.bg = kColorBlack;
    eta.size = etaFont;
    eta.text = trim_for_width(row.eta[0] ? row.eta : "--", kEtaChars, out);
    out.push(eta);

    const uint8_t destinationFont = (normalizedDisplayType == 3)
                                        ? kTextSizeTiny
                                        : (rowFont > 1 ? static_cast<uint8_t>(rowFont - 1) : 1);
    const int16_t destinationCharW =
        (destinationFont == kTextSizeTiny || destinationFont == kTextSizeTinyPlus)
            ? 4
            : static_cast<int16_t>(6 * destinationFont);
    const int16_t effectiveDestinationWidth = static_cast<int16_t>(rowGeom.destinationWidth + preset.etaRightNudgePx);
    const uint8_t labelChars =
        (effectiveDestinationWidth > 0 && destinationCharW > 0)
            ? static_cast<uint8_t>(effectiveDestinationWidth / destinationCharW)
            : 0;
    const uint8_t renderLabelChars =
        ((normalizedDisplayType == 3 || normalizedDisplayType == 4 || normalizedDisplayType == 5) && labelChars > 2)
            ? static_cast<uint8_t>(labelChars - 2)
            : labelChars;

    const int16_t destinationY = static_cast<int16_t>(rowGeom.textY + preset.destinationYNudge);
    const int16_t spaceAdvance = (normalizedDisplayType == 3)
                                     ? destinationCharW
                                     : (destinationCharW > 2 ? static_cast<int16_t>(destinationCharW - 2) : 1);

    auto draw_compact_line = [&](const char *text, int16_t y, uint16_t color = kColorWhite) {
      const char *trimmed = trim_for_width(text, renderLabelChars, out);
      char destinationBuf[kMaxDestinationLen];
      const char *src = trimmed ? trimmed : "";
      strncpy(destinationBuf, src, sizeof(destinationBuf) - 1);
      destinationBuf[sizeof(destinationBuf) - 1] = '\0';

      int16_t cursorX = destinationX;
      char token[kMaxDestinationLen];
      size_t tokenLen = 0;

      auto flush_token = [&]() {
        if (tokenLen == 0) return;
        token[tokenLen] = '\0';
        DrawCommand destinationPart{};
        destinationPart.type = DrawCommandType::kText;
        destinationPart.x = cursorX;
        destinationPart.y = y;
        destinationPart.color = color;
        destinationPart.bg = kColorBlack;
        destinationPart.size = destinationFont;
        destinationPart.text = out.copy_text(token);
        out.push(destinationPart);
        cursorX = static_cast<int16_t>(cursorX + static_cast<int16_t>(tokenLen) * destinationCharW);
        tokenLen = 0;
      };

      for (size_t c = 0; destinationBuf[c] != '\0'; ++c) {
        if (destinationBuf[c] == ' ') {
          flush_token();
          cursorX = static_cast<int16_t>(cursorX + spaceAdvance);
        } else if (tokenLen + 1 < sizeof(token)) {
          token[tokenLen++] = destinationBuf[c];
        }
      }
      flush_token();
    };

    if (normalizedDisplayType == 3) {
      // Preset 3: two-line label, nudged down for visual balance.
      const int16_t preset3Y = static_cast<int16_t>(destinationY + 2);
      const char *line1 = row.direction[0] ? row.direction : (row.destination[0] ? row.destination : "-");
      const char *line2 = row.direction[0] ? (row.destination[0] ? row.destination : "") : "";
      draw_compact_line(line1, preset3Y);
      if (line2[0] != '\0') {
        draw_compact_line(line2, static_cast<int16_t>(preset3Y + 7));
      }
    } else if (normalizedDisplayType == 4 || normalizedDisplayType == 5) {
      const int16_t preset45Y = static_cast<int16_t>(destinationY - 2);
      const char *line1 = (normalizedDisplayType == 5)
                              ? (row.direction[0] ? row.direction : (row.destination[0] ? row.destination : "-"))
                              : (row.destination[0] ? row.destination : "-");
      draw_compact_line(line1, preset45Y);
      if (row.etaExtra[0] != '\0') {
        draw_compact_line(row.etaExtra, static_cast<int16_t>(preset45Y + 7), kColorAmber);
      }
    } else {
      draw_compact_line(row_label_for_display_type(row, model.displayType), destinationY);
    }
  }
}

}  // namespace core
