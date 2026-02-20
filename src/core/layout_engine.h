#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/models.h"

namespace core {

enum class DrawCommandType : uint8_t {
  kFillRect,
  kText,
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

class LayoutEngine final {
 public:
  LayoutEngine();

  void set_viewport(uint16_t width, uint16_t height);
  void build_transit_layout(const RenderModel &model, DrawList &out);

 private:
  uint16_t width_;
  uint16_t height_;

  const char *trim_for_width(const char *src, uint8_t charLimit, DrawList &out);
};

}  // namespace core
