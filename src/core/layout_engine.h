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
  uint8_t size;
  const char *text;
};

struct DrawList {
  static constexpr size_t kMaxCommands = 64;
  DrawCommand commands[kMaxCommands];
  size_t count;

  void reset() { count = 0; }
  bool push(const DrawCommand &command) {
    if (count >= kMaxCommands) {
      return false;
    }
    commands[count++] = command;
    return true;
  }
};

class LayoutEngine final {
 public:
  LayoutEngine();

  void set_viewport(uint16_t width, uint16_t height);
  void build_transit_layout(const RenderModel &model, DrawList &out) const;

 private:
  uint16_t width_;
  uint16_t height_;
};

}  // namespace core
