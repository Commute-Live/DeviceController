#include "core/layout_engine.h"

namespace core {

LayoutEngine::LayoutEngine() : width_(128), height_(32) {}

void LayoutEngine::set_viewport(uint16_t width, uint16_t height) {
  width_ = width;
  height_ = height;
}

void LayoutEngine::build_transit_layout(const RenderModel &model, DrawList &out) const {
  out.reset();

  const int16_t rowHeight = static_cast<int16_t>(height_ / 2U);
  const uint8_t fontSize = height_ >= 64 ? 2 : 1;

  for (uint8_t i = 0; i < 2; ++i) {
    const int16_t rowTop = static_cast<int16_t>(i * rowHeight);

    DrawCommand bg{};
    bg.type = DrawCommandType::kFillRect;
    bg.x = 0;
    bg.y = rowTop;
    bg.w = static_cast<int16_t>(width_);
    bg.h = rowHeight;
    bg.color = 0x0000;
    bg.size = 1;
    bg.text = nullptr;
    out.push(bg);

    DrawCommand text{};
    text.type = DrawCommandType::kText;
    text.x = 2;
    text.y = static_cast<int16_t>(rowTop + (fontSize == 2 ? 3 : 2));
    text.w = 0;
    text.h = 0;
    text.color = 0xFFFF;
    text.size = fontSize;
    text.text = model.hasData ? model.rows[i].routeId : "--";
    out.push(text);
  }
}

}  // namespace core
