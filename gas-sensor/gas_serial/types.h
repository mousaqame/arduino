// Types live in a header so they are declared before arduino-builder's
// auto-generated function prototypes, which land just after the includes.
#pragma once

#include <stdint.h>

enum Level {
  L_WARMUP,   // heater still settling; readings mean nothing yet
  L_CLEAR,    // at or near the clean-air baseline
  L_RISING,   // noticeably above baseline, not yet an alarm
  L_LEAK      // well above baseline
};

struct Config {
  uint16_t magic;
  int16_t  baseline;     // clean-air reading, learned during calibration
  int16_t  warnDelta;    // baseline + this  -> RISING
  int16_t  alarmDelta;   // baseline + this  -> LEAK
  uint8_t  muted;
};
