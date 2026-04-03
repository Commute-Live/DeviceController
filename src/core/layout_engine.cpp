#include "core/layout_engine.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "transit/mta_color_map.h"

namespace core {

namespace {

constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorWhite = 0xFFFF;
constexpr uint16_t kColorCyan = 0x5F1A;
constexpr uint16_t kColorGray = 0x7BEF;
constexpr uint16_t kColorAmber = 0xFD20;
constexpr uint16_t kColorRed = 0xF800;
constexpr uint16_t kColorGreen = 0x07E0;
constexpr uint16_t kColorBlue = 0x2C9F;
constexpr uint8_t kMinDisplayType = 1;
constexpr uint8_t kMaxDisplayType = 5;
constexpr uint8_t kTextSizeTiny = 0;
constexpr uint8_t kTextSizeTinyPlus = 255;
constexpr uint8_t kEtaChars = 3;


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

const char *row_label_for_display_type(const TransitRowModel &row) {
  const uint8_t normalizedDisplayType = normalize_display_type(row.displayType);
  if (normalizedDisplayType == 2 && row.direction[0] != '\0') {
    return row.direction;
  }
  return row.destination[0] ? row.destination : "-";
}

bool is_septa_provider(const char *providerId) {
  return providerId &&
         (strcmp(providerId, "septa-rail") == 0 || strcmp(providerId, "septa-bus") == 0 ||
          strcmp(providerId, "septa-trolley") == 0);
}

bool is_nyc_rail_bar_provider(const char *providerId) {
  return providerId &&
         (strcmp(providerId, "mta-lirr") == 0 || strcmp(providerId, "mta-mnr") == 0);
}

const char *copy_upper_trimmed(const char *src, char *out, size_t outLen) {
  if (!out || outLen == 0) return "";
  out[0] = '\0';
  if (!src) return out;

  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < outLen; ++i) {
    const char c = src[i];
    if (c == ' ' || c == '-' || c == '_') continue;
    out[j++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  out[j] = '\0';
  return out;
}

const char *septa_badge_text(const TransitRowModel &row, char *out, size_t outLen) {
  const char *routeId = row.routeId;
  if (!routeId || routeId[0] == '\0') {
    return copy_upper_trimmed("--", out, outLen);
  }

  char compact[kMaxRouteIdLen];
  copy_upper_trimmed(routeId, compact, sizeof(compact));

  if (strcmp(row.providerId, "septa-rail") == 0) {
    strncpy(out, compact, outLen - 1);
    out[outLen - 1] = '\0';
    return out;
  }

  if (strcmp(compact, "G1") == 0) {
    return copy_upper_trimmed("G", out, outLen);
  }
  if (strcmp(compact, "TBUS") == 0) {
    return copy_upper_trimmed("T", out, outLen);
  }
  if (strcmp(compact, "T5BUS") == 0) {
    return copy_upper_trimmed("T5", out, outLen);
  }
  if (strcmp(compact, "D1BUS") == 0 || strcmp(compact, "D1") == 0) {
    return copy_upper_trimmed("D1", out, outLen);
  }
  if (strcmp(compact, "D2BUS") == 0 || strcmp(compact, "D2") == 0) {
    return copy_upper_trimmed("D2", out, outLen);
  }
  if (strcmp(compact, "M1") == 0 || strcmp(compact, "M1BUS") == 0) {
    return copy_upper_trimmed("M", out, outLen);
  }
  if (strcmp(compact, "L1") == 0 || strcmp(compact, "L1OWL") == 0) {
    return copy_upper_trimmed("L", out, outLen);
  }
  if (strcmp(compact, "B1") == 0 || strcmp(compact, "B2") == 0 || strcmp(compact, "B3") == 0 ||
      strcmp(compact, "B1OWL") == 0) {
    return copy_upper_trimmed("B", out, outLen);
  }
  if (compact[0] == 'T' && isdigit(static_cast<unsigned char>(compact[1])) && compact[2] == '\0') {
    strncpy(out, compact, outLen - 1);
    out[outLen - 1] = '\0';
    return out;
  }

  strncpy(out, compact, outLen - 1);
  out[outLen - 1] = '\0';
  return out;
}

int16_t septa_badge_width(const TransitRowModel &row, int16_t badgeHeight) {
  char badgeLabel[8];
  const char *label = septa_badge_text(row, badgeLabel, sizeof(badgeLabel));
  const size_t len = strnlen(label, sizeof(badgeLabel));

  int16_t width = badgeHeight;
  if (len >= 2) width = static_cast<int16_t>(width + 4);
  if (len >= 3) width = static_cast<int16_t>(width + 4);
  if (len >= 4) width = static_cast<int16_t>(width + 4);
  if (width < badgeHeight) width = badgeHeight;
  if (width > 24) width = 24;
  return width;
}

int16_t max_septa_badge_width(const RenderModel &model, int16_t badgeHeight) {
  int16_t width = badgeHeight;
  uint8_t rowCount = model.activeRows;
  if (rowCount < 1) rowCount = 1;
  if (rowCount > kMaxTransitRows) rowCount = kMaxTransitRows;

  for (uint8_t i = 0; i < rowCount; ++i) {
    const TransitRowModel &row = model.rows[i];
    if (!is_septa_provider(row.providerId)) continue;
    const int16_t candidate = septa_badge_width(row, badgeHeight);
    if (candidate > width) width = candidate;
  }

  return width;
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
  return kColorGreen;
}

void compute_transit_row_frames(const RenderModel &model,
                                uint16_t height,
                                RowFrame out[kMaxTransitRows],
                                uint8_t &rowCount,
                                const TransitPresetConfig *&preset,
                                int16_t &fixedBadgeSize,
                                uint8_t &rowFont) {
  rowCount = model.activeRows;
  if (rowCount < 1) rowCount = 1;
  if (rowCount > kMaxTransitRows) rowCount = kMaxTransitRows;

  preset = &transit_preset_config(model.displayType);
  const int16_t topMargin = (rowCount == 3) ? preset->topMarginThreeRow : preset->topMarginTwoRow;
  const int16_t betweenMargin = (rowCount == 3) ? preset->betweenMarginThreeRow : preset->betweenMarginTwoRow;
  const int16_t bottomMargin = (rowCount == 3) ? preset->bottomMarginThreeRow : preset->bottomMarginTwoRow;
  const int16_t totalHeight = static_cast<int16_t>(height);
  const int16_t totalGap = static_cast<int16_t>(topMargin + bottomMargin + (rowCount - 1) * betweenMargin);
  const int16_t drawable = static_cast<int16_t>(totalHeight - totalGap);
  int16_t blockH = rowCount > 0 ? static_cast<int16_t>(drawable / rowCount) : 0;

  if (blockH <= 0) {
    VerticalLayoutEngine verticalLayout;
    const VerticalLayoutResult layout = verticalLayout.compute(height, rowCount);
    for (uint8_t i = 0; i < rowCount; ++i) {
      out[i] = layout.rows[i];
    }
    blockH = out[0].height > 0 ? out[0].height : 1;
  } else {
    for (uint8_t i = 0; i < rowCount; ++i) {
      out[i] = {
          static_cast<int16_t>(topMargin + i * (blockH + betweenMargin)),
          blockH,
      };
    }
  }

  int16_t targetRadius = static_cast<int16_t>((blockH - 1) / 2);
  if (rowCount == 3) {
    targetRadius = 4;
  }
  if (targetRadius < 1) targetRadius = 1;

  fixedBadgeSize = static_cast<int16_t>(2 * (targetRadius + 1));
  if (fixedBadgeSize < 5) fixedBadgeSize = 5;

  const int16_t targetTextHeight = static_cast<int16_t>((fixedBadgeSize * 3) / 5);
  rowFont = static_cast<uint8_t>((targetTextHeight + 4) / 8);
  if (rowFont < 1) rowFont = 1;
  if (rowFont > 2) rowFont = 2;
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

uint16_t LayoutEngine::eta_color_for_text(const char *eta) { return eta_color(eta); }

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

bool LayoutEngine::compute_transit_row_geometry(const RenderModel &model,
                                                uint8_t rowIndex,
                                                TransitRowGeometry &out) const {
  memset(&out, 0, sizeof(out));

  const bool transitView = model.hasData && model.uiState == UiState::kTransit;
  if (!transitView || rowIndex >= kMaxTransitRows) {
    return false;
  }

  RowFrame rowFrames[kMaxTransitRows]{};
  uint8_t rowCount = 0;
  const TransitPresetConfig *preset = nullptr;
  int16_t fixedBadgeSize = 0;
  uint8_t rowFont = 1;
  compute_transit_row_frames(model, height_, rowFrames, rowCount, preset, fixedBadgeSize, rowFont);
  if (rowIndex >= rowCount || !preset) {
    return false;
  }

  const TransitRowModel &row = model.rows[rowIndex];
  out.valid = true;
  out.normalizedDisplayType = normalize_display_type(row.displayType);
  out.frame = rowFrames[rowIndex];

  const int16_t rowY =
      out.frame.yStart > 0 ? static_cast<int16_t>(out.frame.yStart + preset->rowYNudge) : out.frame.yStart;
  display::RowFrame rowFrame{rowY, out.frame.height};
  const int16_t badgeWidth =
      is_septa_provider(row.providerId) ? max_septa_badge_width(model, fixedBadgeSize) : fixedBadgeSize;
  out.layout =
      rowLayout_.compute_row_layout(static_cast<int16_t>(width_), rowFrame, fixedBadgeSize, badgeWidth, rowFont, kEtaChars);

  out.badgeX = out.layout.badgeX > 0 ? static_cast<int16_t>(out.layout.badgeX + preset->rowXShift) : out.layout.badgeX;
  out.destinationX =
      out.layout.destinationX > 0 ? static_cast<int16_t>(out.layout.destinationX + preset->rowXShift)
                                  : out.layout.destinationX;
  const int16_t etaX =
      out.layout.etaX > 0 ? static_cast<int16_t>(out.layout.etaX + preset->rowXShift) : out.layout.etaX;

  out.etaFont =
      (out.normalizedDisplayType == 4 || out.normalizedDisplayType == 5)
          ? (rowFont > 1 ? static_cast<uint8_t>(rowFont - 1) : 1)
          : rowFont;
  const int16_t etaCharW = static_cast<int16_t>(6 * out.etaFont);
  const uint8_t etaLen = static_cast<uint8_t>(strnlen(row.eta[0] ? row.eta : "--", kMaxEtaLen - 1));
  const int16_t etaDrawW = static_cast<int16_t>(etaLen * etaCharW);
  out.etaTextX = static_cast<int16_t>(etaX + out.layout.etaWidth - etaDrawW + preset->etaRightNudgePx);
  out.etaTextY = static_cast<int16_t>(out.layout.textY + preset->etaYNudge);
  out.etaClearX = etaX;
  out.etaClearY = out.frame.yStart;
  out.etaClearW = static_cast<int16_t>(out.layout.etaWidth + (preset->etaRightNudgePx > 0 ? preset->etaRightNudgePx : 0));
  out.etaClearH = out.frame.height;

  out.destinationFont = (out.normalizedDisplayType == 3)
                            ? kTextSizeTiny
                            : (rowFont > 1 ? static_cast<uint8_t>(rowFont - 1) : 1);
  out.effectiveDestinationWidth =
      static_cast<int16_t>(out.layout.destinationWidth + preset->etaRightNudgePx);
  out.destinationY = static_cast<int16_t>(out.layout.textY + preset->destinationYNudge);

  if (out.normalizedDisplayType == 4 || out.normalizedDisplayType == 5) {
    const int16_t preset45Y = static_cast<int16_t>(out.destinationY - 3);
    const int16_t extraY = static_cast<int16_t>(preset45Y + 13);
    constexpr int16_t kTinyFontHeight = 6;
    if (extraY + kTinyFontHeight <= static_cast<int16_t>(height_)) {
      const int16_t lineCharW = 4;
      const int16_t lineBudgetW = (out.effectiveDestinationWidth > static_cast<int16_t>(3 * lineCharW))
                                      ? static_cast<int16_t>(out.effectiveDestinationWidth - static_cast<int16_t>(3 * lineCharW))
                                      : 0;
      const uint8_t lineChars =
          (lineBudgetW > 0 && lineCharW > 0) ? static_cast<uint8_t>(lineBudgetW / lineCharW) : 0;
      out.hasEtaExtra = true;
      out.etaExtraFont = kTextSizeTiny;
      out.etaExtraCharLimit = (lineChars > 2) ? static_cast<uint8_t>(lineChars - 2) : lineChars;
      out.etaExtraTextX = out.destinationX;
      out.etaExtraTextY = extraY;
      out.etaExtraClearX = out.destinationX;
      out.etaExtraClearY = static_cast<int16_t>(extraY > out.frame.yStart ? extraY - 1 : out.frame.yStart);
      out.etaExtraClearW = static_cast<int16_t>(width_ - out.destinationX);
      out.etaExtraClearH =
          static_cast<int16_t>((out.frame.yStart + out.frame.height) - out.etaExtraClearY);
      if (out.etaExtraClearH < 1) {
        out.etaExtraClearH = 1;
      }
    }
  }

  return true;
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
  bg.bitmap = nullptr;
  out.push(bg);

  const bool transitView = model.hasData && model.uiState == UiState::kTransit;
  if (!transitView) {
    const VerticalLayoutResult home = verticalLayout_.compute(height_, 3);
    const uint8_t homeFont = height_ >= 64 ? 2 : 1;
    const int16_t charW = static_cast<int16_t>(6 * homeFont);
    const int16_t textH = static_cast<int16_t>(8 * homeFont);

    if (model.uiState == UiState::kSetupMode) {
      const uint8_t titleFont = 1;
      const uint8_t tinyFont = kTextSizeTiny;
      const uint8_t titleChars = static_cast<uint8_t>((static_cast<int16_t>(width_) - 4) / 6);
      const uint8_t tinyChars = static_cast<uint8_t>((static_cast<int16_t>(width_) - 4) / 4);
      constexpr int16_t kTitleHeight = 8;
      constexpr int16_t kTinyHeight = 6;
      constexpr int16_t kLineGap = 2;
      constexpr int16_t kTitleY = 1;
      constexpr int16_t kUrlY = kTitleY + kTitleHeight + kLineGap + 4;
      constexpr int16_t kActionY = kUrlY + kTinyHeight + (kLineGap - 1);
      constexpr int16_t kBleY = kActionY + kTinyHeight + kLineGap;

      auto centered_x = [&](const char *text, uint8_t fontSize) -> int16_t {
        if (!text) return 2;
        const int16_t charW =
            (fontSize == kTextSizeTiny || fontSize == kTextSizeTinyPlus) ? 4 : static_cast<int16_t>(6 * fontSize);
        const int16_t textW = static_cast<int16_t>(strnlen(text, kMaxDestinationLen) * charW);
        int16_t x = static_cast<int16_t>((static_cast<int16_t>(width_) - textW) / 2);
        if (x < 2) x = 2;
        return x;
      };

      const char *titleText = trim_for_width("GET THE APP", titleChars, out);

      DrawCommand title{};
      title.type = DrawCommandType::kText;
      title.x = centered_x(titleText, titleFont);
      title.y = kTitleY;
      title.color = kColorWhite;
      title.bg = kColorBlack;
      title.size = titleFont;
      title.text = titleText;
      title.bitmap = nullptr;
      out.push(title);

      const char *urlText = trim_for_width("Go to commutelive.com/app", tinyChars, out);
      DrawCommand url{};
      url.type = DrawCommandType::kText;
      url.x = centered_x(urlText, tinyFont);
      url.y = kUrlY;
      url.color = kColorCyan;
      url.bg = kColorBlack;
      url.size = tinyFont;
      url.text = urlText;
      url.bitmap = nullptr;
      out.push(url);

      const char *actionText = trim_for_width("Open app, then connect", tinyChars, out);
      DrawCommand action{};
      action.type = DrawCommandType::kText;
      action.x = centered_x(actionText, tinyFont);
      action.y = kActionY;
      action.color = kColorWhite;
      action.bg = kColorBlack;
      action.size = tinyFont;
      action.text = actionText;
      action.bitmap = nullptr;
      out.push(action);

      char bleBuf[80];
      snprintf(bleBuf, sizeof(bleBuf), "BT: %s", model.bleName[0] ? model.bleName : "CommuteLive");
      const char *bleText = trim_for_width(bleBuf, tinyChars, out);

      DrawCommand bleLine{};
      bleLine.type = DrawCommandType::kText;
      bleLine.x = centered_x(bleText, tinyFont);
      bleLine.y = kBleY;
      bleLine.color = kColorBlue;
      bleLine.bg = kColorBlack;
      bleLine.size = tinyFont;
      bleLine.text = bleText;
      bleLine.bitmap = nullptr;
      out.push(bleLine);
    } else {
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
        title.bitmap = nullptr;
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
          chipBg.bitmap = nullptr;
          out.push(chipBg);

          DrawCommand chipText{};
          chipText.type = DrawCommandType::kText;
          chipText.x = static_cast<int16_t>(x + 2);
          chipText.y = chipY;
          chipText.color = kColorWhite;
          chipText.bg = chips[i].bg;
          chipText.size = chipFont;
          chipText.text = out.copy_text(chips[i].text);
          chipText.bitmap = nullptr;
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
          status.bitmap = nullptr;
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
          detail.bitmap = nullptr;
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
          compactLine.bitmap = nullptr;
          out.push(compactLine);
        }
      }
    }

    return;
  }

  uint8_t rowCount = model.activeRows;
  if (rowCount < 1) rowCount = 1;
  if (rowCount > kMaxTransitRows) rowCount = kMaxTransitRows;

  for (uint8_t i = 0; i < rowCount; ++i) {
    const TransitRowModel &row = model.rows[i];
    TransitRowGeometry rowGeometry{};
    if (!compute_transit_row_geometry(model, i, rowGeometry)) {
      continue;
    }

    const uint8_t normalizedDisplayType = rowGeometry.normalizedDisplayType;
    const bool hasRoute = row.routeId[0] != '\0' && strcmp(row.routeId, "--") != 0;
    const bool isNycRailBar = is_nyc_rail_bar_provider(row.providerId);
    const bool isSeptaBadge = is_septa_provider(row.providerId);

    if (isNycRailBar) {
      const int16_t badgeSize = rowGeometry.layout.badgeSize;
      const int16_t barWidth = badgeSize >= 10 ? 8 : 6;
      const int16_t barHeight = badgeSize > 2 ? static_cast<int16_t>(badgeSize - 2) : badgeSize;
      const int16_t barX = static_cast<int16_t>(rowGeometry.badgeX + ((badgeSize - barWidth) / 2));
      const int16_t barY = static_cast<int16_t>(rowGeometry.layout.badgeY + ((badgeSize - barHeight) / 2));

      DrawCommand railBar{};
      railBar.type = DrawCommandType::kFillRect;
      railBar.x = barX;
      railBar.y = barY;
      railBar.w = barWidth;
      railBar.h = barHeight;
      railBar.color = transit::MtaColorMap::color_for_provider_route(
          row.providerId,
          hasRoute ? row.routeId : "");
      railBar.bg = kColorBlack;
      railBar.size = 1;
      railBar.text = nullptr;
      railBar.bitmap = nullptr;
      out.push(railBar);
    } else if (isSeptaBadge) {
      char badgeLabel[8];
      const char *resolvedBadgeLabel = septa_badge_text(row, badgeLabel, sizeof(badgeLabel));
      const int16_t badgeY = static_cast<int16_t>(rowGeometry.layout.badgeY + 1);
      const int16_t badgeH =
          rowGeometry.layout.badgeSize > 2 ? static_cast<int16_t>(rowGeometry.layout.badgeSize - 2)
                                           : rowGeometry.layout.badgeSize;

      DrawCommand badge{};
      badge.type = DrawCommandType::kRectBadge;
      badge.x = rowGeometry.badgeX;
      badge.y = badgeY;
      badge.w = rowGeometry.layout.badgeWidth;
      badge.h = badgeH;
      badge.color = transit::MtaColorMap::color_for_provider_route(
          row.providerId,
          hasRoute ? row.routeId : "");
      badge.bg = kColorBlack;
      badge.size = rowGeometry.etaFont;
      badge.text = out.copy_text(resolvedBadgeLabel);
      badge.bitmap = nullptr;
      out.push(badge);
    } else {
      DrawCommand badge{};
      badge.type = DrawCommandType::kBadge;
      badge.x = rowGeometry.badgeX;
      badge.y = rowGeometry.layout.badgeY;
      badge.w = rowGeometry.layout.badgeSize;
      badge.h = rowGeometry.layout.badgeSize;
      badge.color = transit::MtaColorMap::color_for_provider_route(
          row.providerId,
          hasRoute ? row.routeId : "");
      badge.bg = kColorBlack;
      badge.size = rowGeometry.etaFont;
      badge.text = trim_for_width(hasRoute ? row.routeId : "--", 2, out);
      badge.bitmap = nullptr;
      out.push(badge);
    }

    DrawCommand eta{};
    eta.type = DrawCommandType::kText;
    eta.x = rowGeometry.etaTextX;
    eta.y = rowGeometry.etaTextY;
    eta.color = eta_color(row.eta);
    eta.bg = kColorBlack;
    eta.size = rowGeometry.etaFont;
    eta.text = trim_for_width(row.eta[0] ? row.eta : "--", kEtaChars, out);
    eta.bitmap = nullptr;
    out.push(eta);

    const uint8_t destinationFont = rowGeometry.destinationFont;
    const int16_t effectiveDestinationWidth = rowGeometry.effectiveDestinationWidth;
    const int16_t destinationY = rowGeometry.destinationY;

    auto draw_compact_line = [&](const char *text,
                                 int16_t y,
                                 uint8_t fontSize,
                                 uint8_t reserveRightChars = 0,
                                 uint16_t color = kColorWhite) {
      const int16_t lineCharW =
          (fontSize == kTextSizeTiny || fontSize == kTextSizeTinyPlus) ? 4 : static_cast<int16_t>(6 * fontSize);
      const int16_t lineBudgetW =
          (effectiveDestinationWidth > static_cast<int16_t>(reserveRightChars * lineCharW))
              ? static_cast<int16_t>(effectiveDestinationWidth - static_cast<int16_t>(reserveRightChars * lineCharW))
              : 0;
      const uint8_t lineChars =
          (lineBudgetW > 0 && lineCharW > 0) ? static_cast<uint8_t>(lineBudgetW / lineCharW) : 0;
      const uint8_t lineRenderChars =
          ((normalizedDisplayType == 3 || normalizedDisplayType == 4 || normalizedDisplayType == 5) && lineChars > 2)
              ? static_cast<uint8_t>(lineChars - 2)
              : lineChars;
      const int16_t lineSpaceAdvance =
          (normalizedDisplayType == 3) ? lineCharW : (lineCharW > 2 ? static_cast<int16_t>(lineCharW - 2) : 1);

      const char *trimmed = trim_for_width(text, lineRenderChars, out);
      char destinationBuf[kMaxDestinationLen];
      const char *src = trimmed ? trimmed : "";
      strncpy(destinationBuf, src, sizeof(destinationBuf) - 1);
      destinationBuf[sizeof(destinationBuf) - 1] = '\0';

      int16_t cursorX = rowGeometry.destinationX;
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
        destinationPart.size = fontSize;
        destinationPart.text = out.copy_text(token);
        destinationPart.bitmap = nullptr;
        out.push(destinationPart);
        cursorX = static_cast<int16_t>(cursorX + static_cast<int16_t>(tokenLen) * lineCharW);
        tokenLen = 0;
      };

      for (size_t c = 0; destinationBuf[c] != '\0'; ++c) {
        if (destinationBuf[c] == ' ') {
          flush_token();
          cursorX = static_cast<int16_t>(cursorX + lineSpaceAdvance);
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
      draw_compact_line(line1, preset3Y, destinationFont);
      if (line2[0] != '\0') {
        draw_compact_line(line2, static_cast<int16_t>(preset3Y + 7), destinationFont);
      }
    } else if (normalizedDisplayType == 4 || normalizedDisplayType == 5) {
      const int16_t preset45Y = static_cast<int16_t>(destinationY - 3);
      const char *line1 = (normalizedDisplayType == 5)
                              ? (row.direction[0] ? row.direction : (row.destination[0] ? row.destination : "-"))
                              : (row.destination[0] ? row.destination : "-");
      draw_compact_line(line1, preset45Y, destinationFont);
      if (rowGeometry.hasEtaExtra && row.etaExtra[0] != '\0') {
        // Reserve extra space on the right so bottom ETAs never overlap the main ETA column.
        draw_compact_line(row.etaExtra, rowGeometry.etaExtraTextY, rowGeometry.etaExtraFont, 3, kColorAmber);
      }
    } else {
      draw_compact_line(row_label_for_display_type(row), destinationY, destinationFont);
    }
  }
}

}  // namespace core
