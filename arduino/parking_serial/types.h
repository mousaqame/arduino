// Types live in a header so they are declared before arduino-builder's
// auto-generated function prototypes, which land just after the includes.
#pragma once

#include <stdint.h>

enum Zone { Z_FAR, Z_CLOSE, Z_GOOD, Z_VCLOSE, Z_STOP };

struct Config {
  uint16_t magic;
  int16_t  farCm;     // above this -> FAR
  int16_t  closeCm;   // above this -> CLOSE
  int16_t  goodCm;    // above this -> GOOD
  int16_t  vcloseCm;  // above this -> V.CLOSE, at or below -> STOP!
  uint8_t  muted;
};
