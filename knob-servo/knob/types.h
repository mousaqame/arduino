// Types live in a header so they are declared before arduino-builder's
// auto-generated function prototypes, which land just after the includes.
#pragma once

#include <stdint.h>

enum Mode {
  M_KNOB,   // the potentiometer drives the servo
  M_WEB     // the dashboard drives it instead
};

struct Config {
  uint16_t magic;
  int16_t  rawMin;      // reading that should mean the lowest angle
  int16_t  rawMax;      // reading that should mean the highest angle
  int16_t  angleLow;    // lowest angle to send the servo
  int16_t  angleHigh;   // highest angle to send the servo
  uint8_t  invert;      // flip which way the knob turns it
};
