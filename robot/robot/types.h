// Types live in a header so they are declared before arduino-builder's
// auto-generated function prototypes, which land just after the includes.
#pragma once

#include <Arduino.h>
#include <Servo.h>

#define JOINT_COUNT 4

struct Joint {
  const char *name;
  uint8_t     pin;
  int16_t     minA;
  int16_t     maxA;
  int16_t     home;
  int16_t     target;   // where we want it
  float       cur;      // where it is right now; eased toward target
  bool        attached;
  Servo       servo;
};

// One step of a movement. -1 in a slot means "leave that joint alone".
// hold is how long to stay put after every joint has arrived.
struct Frame {
  int16_t  a[JOINT_COUNT];
  uint16_t hold;
};

struct Pose {
  const char  *name;    // what you send as the command
  const char  *label;   // what the button says
  const Frame *frames;
  uint8_t      count;
  uint16_t     dps;     // speed for this move; 0 = use the dashboard's setting
};
