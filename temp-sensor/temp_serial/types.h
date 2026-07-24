// Types live in a header so they are declared before arduino-builder's
// auto-generated function prototypes, which land just after the includes.
#pragma once

#include <stdint.h>

enum Band {
  B_FAIL,    // the sensor didn't answer
  B_COLD,
  B_COOL,
  B_COMFY,
  B_WARM,
  B_HOT
};

struct Config {
  uint16_t magic;
  int16_t  comfyLow;    // degrees C; below this is COOL
  int16_t  comfyHigh;   // degrees C; above this is WARM
};
