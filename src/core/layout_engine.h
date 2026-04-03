#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/models.h"
#include "core/vertical_layout_engine.h"
#include "display/layout_engine.h"

namespace core {

enum class DrawCommandType : uint8_t {
  kFillRect,
  kText,
  kBadge,
  kRectBadge,
  kMonoBitmap,
};

struct DrawCommand {
  DrawCommandType type;
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  uint16_t color;
  uint16_t bg;
  uint8_t size;
  const char *text;
  const uint8_t *bitmap;
};

struct DrawList {
  static constexpr size_t kMaxCommands = 96;
  static constexpr size_t kTextPoolSize = 512;

  DrawCommand commands[kMaxCommands];
  size_t count;
  char textPool[kTextPoolSize];
  size_t textUsed;

  void reset() {
    count = 0;
    textUsed = 0;
  }

  bool push(const DrawCommand &command) {
    if (count >= kMaxCommands) {
      return false;
    }
    commands[count++] = command;
    return true;
  }

  const char *copy_text(const char *text);
};

struct TransitRowGeometry {
  bool valid;
  uint8_t normalizedDisplayType;
  int16_t badgeX;
  RowFrame frame;
  display::RowLayout layout;
  int16_t destinationX;
  int16_t destinationY;
  int16_t effectiveDestinationWidth;
  uint8_t destinationFont;
  int16_t etaTextX;
  int16_t etaTextY;
  uint8_t etaFont;
  int16_t etaClearX;
  int16_t etaClearY;
  int16_t etaClearW;
  int16_t etaClearH;
  bool hasEtaExtra;
  int16_t etaExtraTextX;
  int16_t etaExtraTextY;
  uint8_t etaExtraFont;
  uint8_t etaExtraCharLimit;
  int16_t etaExtraClearX;
  int16_t etaExtraClearY;
  int16_t etaExtraClearW;
  int16_t etaExtraClearH;
};

class LayoutEngine final {
 public:
  LayoutEngine();

  static uint16_t eta_color_for_text(const char *eta);

  void set_viewport(uint16_t width, uint16_t height);
  void build_transit_layout(const RenderModel &model, DrawList &out);
  bool compute_transit_row_geometry(const RenderModel &model, uint8_t rowIndex, TransitRowGeometry &out) const;

 private:
  uint16_t width_;
  uint16_t height_;
  VerticalLayoutEngine verticalLayout_;
  display::LayoutEngine rowLayout_;

  const char *trim_for_width(const char *src, uint8_t charLimit, DrawList &out);
};

}  // namespace core
