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
    DrawCommand title{};
    title.type = DrawCommandType::kText;
    title.x = 2;
    title.y = 2;
    title.color = kColorCyan;
    title.bg = kColorBlack;
    title.size = height_ >= 64 ? 2 : 1;
    title.text = out.copy_text("Commute Live");
    out.push(title);

    DrawCommand status{};
    status.type = DrawCommandType::kText;
    status.x = 2;
    status.y = static_cast<int16_t>(height_ / 2U - 4);
    status.color = kColorWhite;
    status.bg = kColorBlack;
    status.size = 1;
    status.text = out.copy_text(model.statusLine[0] ? model.statusLine : "BOOTING");
    out.push(status);

    DrawCommand detail{};
    detail.type = DrawCommandType::kText;
    detail.x = 2;
    detail.y = static_cast<int16_t>(height_ - 10);
    detail.color = kColorGray;
    detail.bg = kColorBlack;
    detail.size = 1;
    detail.text = out.copy_text(model.statusDetail[0] ? model.statusDetail : "");
    out.push(detail);
    return;
  }

  uint8_t rowCount = model.activeRows;
  if (rowCount < 1) rowCount = 1;
  if (rowCount > kMaxTransitRows) rowCount = kMaxTransitRows;

  const VerticalLayoutResult layout = verticalLayout_.compute(height_, rowCount);
  const int16_t baseRowHeight = layout.rows[0].height > 0 ? layout.rows[0].height : static_cast<int16_t>(height_);
  int16_t fixedBadgeSize =
      static_cast<int16_t>(baseRowHeight - (2 * display::LayoutEngine::kOuterMargin));
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
