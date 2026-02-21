#include "calibration.h"

#include <Preferences.h>
#include <ctype.h>

namespace calibration {

namespace {

constexpr const char *kPrefsNs = "disp";
constexpr const char *kKeyChain = "chain";
constexpr const char *kKeyXOff = "xoff";
constexpr const char *kKeyYOff = "yoff";
constexpr const char *kKeyCal = "calok";

int16_t apply_x(int16_t x, const State &state) { return static_cast<int16_t>(x + state.xOffset); }
int16_t apply_y(int16_t y, const State &state) { return static_cast<int16_t>(y + state.yOffset); }

void draw_hline_offset(VirtualMatrixPanel *vm, int16_t x, int16_t y, int16_t w, uint16_t color, const State &state) {
  if (!vm) return;
  vm->drawFastHLine(apply_x(x, state), apply_y(y, state), w, color);
}

void draw_vline_offset(VirtualMatrixPanel *vm, int16_t x, int16_t y, int16_t h, uint16_t color, const State &state) {
  if (!vm) return;
  vm->drawFastVLine(apply_x(x, state), apply_y(y, state), h, color);
}

void draw_rect_offset(VirtualMatrixPanel *vm, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color,
                      const State &state) {
  if (!vm) return;
  vm->drawRect(apply_x(x, state), apply_y(y, state), w, h, color);
}

void fill_rect_offset(VirtualMatrixPanel *vm, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color,
                      const State &state) {
  if (!vm) return;
  vm->fillRect(apply_x(x, state), apply_y(y, state), w, h, color);
}

void set_cursor_offset(VirtualMatrixPanel *vm, int16_t x, int16_t y, const State &state) {
  if (!vm) return;
  vm->setCursor(apply_x(x, state), apply_y(y, state));
}

}  // namespace

void print_help() {
  Serial.println("[CAL] Commands: n/p map, l/r/t/b fix missing side, i/j/k/m fine XY, z reset XY, s save, x exit");
}

State load_state(uint8_t chainOptionCount) {
  State state{0, 0, 0, false};
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, true)) {
    return state;
  }

  const uint8_t chain = prefs.getUChar(kKeyChain, 0);
  state.chainIndex = chain < chainOptionCount ? chain : 0;
  state.xOffset = prefs.getChar(kKeyXOff, 0);
  state.yOffset = prefs.getChar(kKeyYOff, 0);
  state.calibrated = prefs.getBool(kKeyCal, false);
  prefs.end();
  return state;
}

void save_state(const State &state) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNs, false)) {
    return;
  }
  prefs.putUChar(kKeyChain, state.chainIndex);
  prefs.putChar(kKeyXOff, state.xOffset);
  prefs.putChar(kKeyYOff, state.yOffset);
  prefs.putBool(kKeyCal, true);
  prefs.end();
}

bool apply_mapping(const Context &ctx, const State &state) {
  if (!ctx.matrix || !ctx.virtualMatrix || state.chainIndex >= ctx.chainOptionCount) {
    return false;
  }

  if (*ctx.virtualMatrix) {
    delete *ctx.virtualMatrix;
    *ctx.virtualMatrix = nullptr;
  }

  *ctx.virtualMatrix =
      new VirtualMatrixPanel(*ctx.matrix, ctx.panelRows, ctx.panelCols, ctx.panelW, ctx.panelH,
                             ctx.chainOptions[state.chainIndex].type);
  return *ctx.virtualMatrix != nullptr;
}

void draw_test_pattern(const Context &ctx, const State &state) {
  if (!ctx.matrix || !ctx.virtualMatrix || !*ctx.virtualMatrix) return;

  auto *vm = *ctx.virtualMatrix;
  vm->fillScreen(0);

  const uint16_t white = ctx.matrix->color565(255, 255, 255);
  const uint16_t red = ctx.matrix->color565(255, 0, 0);
  const uint16_t green = ctx.matrix->color565(0, 255, 0);
  const uint16_t blue = ctx.matrix->color565(0, 0, 255);
  const uint16_t yellow = ctx.matrix->color565(255, 255, 0);

  draw_rect_offset(vm, 0, 0, ctx.totalW, ctx.totalH, white, state);
  draw_vline_offset(vm, static_cast<int16_t>(ctx.totalW / 2), 0, static_cast<int16_t>(ctx.totalH), white, state);
  draw_hline_offset(vm, 0, static_cast<int16_t>(ctx.totalH / 2), static_cast<int16_t>(ctx.totalW), white, state);

  fill_rect_offset(vm, 1, 1, 6, 6, red, state);
  fill_rect_offset(vm, static_cast<int16_t>(ctx.totalW - 7), 1, 6, 6, green, state);
  fill_rect_offset(vm, 1, static_cast<int16_t>(ctx.totalH - 7), 6, 6, blue, state);
  fill_rect_offset(vm, static_cast<int16_t>(ctx.totalW - 7), static_cast<int16_t>(ctx.totalH - 7), 6, 6, yellow,
                   state);

  vm->setTextWrap(false);
  vm->setTextSize(1);
  vm->setTextColor(white);
  set_cursor_offset(vm, 10, 2, state);
  vm->print("CAL ");
  vm->print(state.chainIndex);
  vm->print(":");
  vm->print(ctx.chainOptions[state.chainIndex].name);

  set_cursor_offset(vm, 10, static_cast<int16_t>(ctx.totalH - 9), state);
  vm->print("n p lrtb s");

  Serial.print("[CAL] Viewing ");
  Serial.print(state.chainIndex);
  Serial.print(" -> ");
  Serial.print(ctx.chainOptions[state.chainIndex].name);
  Serial.print(" | xOff=");
  Serial.print(state.xOffset);
  Serial.print(" yOff=");
  Serial.println(state.yOffset);
}

void step_mapping(State &state, int direction, uint8_t chainOptionCount) {
  int next = static_cast<int>(state.chainIndex) + direction;
  if (next < 0) next = chainOptionCount - 1;
  if (next >= chainOptionCount) next = 0;
  state.chainIndex = static_cast<uint8_t>(next);
}

void adjust_missing_side(State &state, char side) {
  if (side == 'l') state.xOffset += 1;
  if (side == 'r') state.xOffset -= 1;
  if (side == 't') state.yOffset += 1;
  if (side == 'b') state.yOffset -= 1;
}

CommandResult handle_command(char ch, State &state, uint8_t chainOptionCount) {
  const char cmd = static_cast<char>(tolower(ch));

  if (cmd == 'n') {
    step_mapping(state, +1, chainOptionCount);
    return CommandResult::kRedrawCalibration;
  }
  if (cmd == 'p') {
    step_mapping(state, -1, chainOptionCount);
    return CommandResult::kRedrawCalibration;
  }
  if (cmd == 'l' || cmd == 'r' || cmd == 't' || cmd == 'b') {
    adjust_missing_side(state, cmd);
    return CommandResult::kRedrawCalibration;
  }
  if (cmd == 'i') {
    state.yOffset -= 1;
    return CommandResult::kRedrawCalibration;
  }
  if (cmd == 'k') {
    state.yOffset += 1;
    return CommandResult::kRedrawCalibration;
  }
  if (cmd == 'j') {
    state.xOffset -= 1;
    return CommandResult::kRedrawCalibration;
  }
  if (cmd == 'm') {
    state.xOffset += 1;
    return CommandResult::kRedrawCalibration;
  }
  if (cmd == 'z') {
    state.xOffset = 0;
    state.yOffset = 0;
    return CommandResult::kRedrawCalibration;
  }
  if (cmd == 's') {
    return CommandResult::kExitSave;
  }
  if (cmd == 'x') {
    return CommandResult::kExitNoSave;
  }
  return CommandResult::kNoChange;
}

}  // namespace calibration

