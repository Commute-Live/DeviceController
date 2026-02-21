#pragma once

#include <Arduino.h>
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>

namespace calibration {

struct ChainOption {
  PANEL_CHAIN_TYPE type;
  const char *name;
};

struct State {
  uint8_t chainIndex;
  int8_t xOffset;
  int8_t yOffset;
  bool calibrated;
};

struct Context {
  MatrixPanel_I2S_DMA *matrix;
  VirtualMatrixPanel **virtualMatrix;
  uint16_t panelRows;
  uint16_t panelCols;
  uint16_t panelW;
  uint16_t panelH;
  uint16_t totalW;
  uint16_t totalH;
  const ChainOption *chainOptions;
  uint8_t chainOptionCount;
};

enum class CommandResult {
  kNoChange,
  kRedrawCalibration,
  kExitSave,
  kExitNoSave,
};

void print_help();
State load_state(uint8_t chainOptionCount);
void save_state(const State &state);
bool apply_mapping(const Context &ctx, const State &state);
void draw_test_pattern(const Context &ctx, const State &state);
void step_mapping(State &state, int direction, uint8_t chainOptionCount);
void adjust_missing_side(State &state, char side);
CommandResult handle_command(char ch, State &state, uint8_t chainOptionCount);

}  // namespace calibration

