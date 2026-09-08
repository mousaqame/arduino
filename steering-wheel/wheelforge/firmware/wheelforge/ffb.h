#pragma once

// The PID force feedback engine.
//
// DirectInput does not send forces. It creates an effect, is told which block
// the device allocated, fills in that block's parameters over several output
// reports, and then says "play". This holds those blocks and turns whichever
// are running into a single torque figure, every millisecond.
//
// Torque here is a signed number in the range +/-10000. The device gain, the
// per-effect gain and the configured ceiling are all applied on the way out;
// nothing in this file talks to a motor.

#include "hid_descriptor.h"

// Effect types, in the order the descriptor declares them.
#define ET_CONSTANT   1
#define ET_RAMP       2
#define ET_SQUARE     3
#define ET_SINE       4
#define ET_TRIANGLE   5
#define ET_SAWTOOTH_U 6
#define ET_SAWTOOTH_D 7
#define ET_SPRING     8
#define ET_DAMPER     9
#define ET_INERTIA    10
#define ET_FRICTION   11

#ifndef FFB_SCALE
  #define FFB_SCALE   10000L
#endif

struct FfbEffect {
  uint8_t  inUse;
  uint8_t  running;
  uint8_t  type;

  uint16_t duration;        // ms, 0 = infinite
  uint16_t startDelay;
  uint32_t startedAt;
  uint8_t  gain;            // 0..255
  uint8_t  loopCount;

  // Direction, as two 0..255 angles. Only the first matters on a one axis
  // wheel, but the host sends both.
  uint8_t  directionX;
  uint8_t  enableAxis;

  // Constant and ramp
  int16_t  magnitude;       // -255..255
  int8_t   rampStart;
  int8_t   rampEnd;

  // Periodic
  uint8_t  periodicMagnitude;
  int8_t   offset;
  uint8_t  phase;
  uint16_t period;          // ms

  // Envelope
  uint8_t  attackLevel;
  uint16_t attackTime;
  uint8_t  fadeLevel;
  uint16_t fadeTime;
  uint8_t  hasEnvelope;

  // Condition (spring, damper, inertia, friction)
  int8_t   cpOffset;
  int8_t   positiveCoefficient;
  int8_t   negativeCoefficient;
  uint8_t  positiveSaturation;
  uint8_t  negativeSaturation;
  uint8_t  deadBand;
};

static FfbEffect g_effects[WF_MAX_EFFECTS];

static uint8_t  g_deviceGain = 255;
static uint8_t  g_actuatorsEnabled = 0;   // the host must enable them
static uint8_t  g_devicePaused = 0;
static uint8_t  g_lastBlockIndex = 0;
static uint8_t  g_lastBlockStatus = 1;    // 1 success, 2 full, 3 error
static uint8_t  g_playingCount = 0;

static void ffbReset() {
  for (uint8_t i = 0; i < WF_MAX_EFFECTS; i++) {
    g_effects[i].inUse = 0;
    g_effects[i].running = 0;
  }
  g_deviceGain = 255;
  g_devicePaused = 0;
  g_playingCount = 0;
}

static void ffbStopAll() {
  for (uint8_t i = 0; i < WF_MAX_EFFECTS; i++) g_effects[i].running = 0;
  g_playingCount = 0;
}

// Returns a 1-based block index, or 0 when the pool is full.
static uint8_t ffbAllocate(uint8_t type) {
  for (uint8_t i = 0; i < WF_MAX_EFFECTS; i++) {
    if (g_effects[i].inUse) continue;

    FfbEffect* e = &g_effects[i];
    memset(e, 0, sizeof(FfbEffect));
    e->inUse = 1;
    e->type = type;
    e->gain = 255;
    e->period = 100;
    e->duration = 0;
    return (uint8_t)(i + 1);
  }
  return 0;
}

static FfbEffect* ffbBlock(uint8_t index) {
  if (index < 1 || index > WF_MAX_EFFECTS) return NULL;
  FfbEffect* e = &g_effects[index - 1];
  return e->inUse ? e : NULL;
}

// ---------------------------------------------------------------- output reports

static void ffbHandleOutput(const uint8_t* data, uint16_t len) {
  if (len < 2) return;

  uint8_t reportId = data[0];
  FfbEffect* e;

  switch (reportId) {

    // 14 bytes on the wire. The payload the descriptor declares is:
    //   [1]     effect block index
    //   [2]     effect type
    //   [3..4]  duration          (ms)
    //   [5..6]  trigger repeat interval
    //   [7..8]  sample period
    //   [9]     gain
    //   [10]    trigger button
    //   [11]    axes enable + direction enable, padded to a byte
    //   [12..13] direction, two angles
    // These offsets are checked against the descriptor by check_descriptor.py;
    // being one byte out here reads the trigger button as the effect gain and
    // every force comes out at the wrong strength.
    case WF_RID_SET_EFFECT:
      if (len < 14) return;
      e = ffbBlock(data[1]);
      if (!e) return;
      e->type       = data[2];
      e->duration   = (uint16_t)(data[3] | (data[4] << 8));
      e->gain       = data[9];
      e->enableAxis = data[11];
      e->directionX = data[12];
      break;

    case WF_RID_SET_ENVELOPE:
      if (len < 8) return;
      e = ffbBlock(data[1]);
      if (!e) return;
      e->attackLevel = data[2];
      e->fadeLevel   = data[3];
      e->attackTime  = (uint16_t)(data[4] | (data[5] << 8));
      e->fadeTime    = (uint16_t)(data[6] | (data[7] << 8));
      e->hasEnvelope = 1;
      break;

    case WF_RID_SET_CONDITION:
      if (len < 9) return;
      e = ffbBlock(data[1]);
      if (!e) return;
      e->cpOffset            = (int8_t)data[3];
      e->positiveCoefficient = (int8_t)data[4];
      e->negativeCoefficient = (int8_t)data[5];
      e->positiveSaturation  = data[6];
      e->negativeSaturation  = data[7];
      e->deadBand            = data[8];
      break;

    case WF_RID_SET_PERIODIC:
      if (len < 7) return;
      e = ffbBlock(data[1]);
      if (!e) return;
      e->periodicMagnitude = data[2];
      e->offset            = (int8_t)data[3];
      e->phase             = data[4];
      e->period            = (uint16_t)(data[5] | (data[6] << 8));
      if (e->period == 0) e->period = 1;
      break;

    case WF_RID_SET_CONSTANT:
      if (len < 4) return;
      e = ffbBlock(data[1]);
      if (!e) return;
      e->magnitude = (int16_t)(data[2] | (data[3] << 8));
      break;

    case WF_RID_SET_RAMP:
      if (len < 4) return;
      e = ffbBlock(data[1]);
      if (!e) return;
      e->rampStart = (int8_t)data[2];
      e->rampEnd   = (int8_t)data[3];
      break;

    case WF_RID_EFFECT_OPERATION:
      if (len < 4) return;
      e = ffbBlock(data[1]);
      if (!e) return;
      if (data[2] == 3) {                  // stop
        e->running = 0;
      } else {
        if (data[2] == 2) ffbStopAll();    // start solo
        e->running = 1;
        e->loopCount = data[3];
        e->startedAt = millis();
      }
      break;

    case WF_RID_BLOCK_FREE:
      e = ffbBlock(data[1]);
      if (!e) return;
      e->inUse = 0;
      e->running = 0;
      break;

    case WF_RID_DEVICE_CONTROL:
      switch (data[1]) {
        case 1: g_actuatorsEnabled = 1; break;   // enable actuators
        case 2: g_actuatorsEnabled = 0; break;   // disable actuators
        case 3: ffbStopAll(); break;             // stop all
        case 4: ffbReset(); break;               // reset
        case 5: g_devicePaused = 1; break;
        case 6: g_devicePaused = 0; break;
      }
      break;

    case WF_RID_DEVICE_GAIN:
      g_deviceGain = data[1];
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------- features

// The host writes Create New Effect, then immediately reads Block Load to find
// out which block it got. Answering that pair correctly is the whole handshake:
// get it wrong and every CreateEffect call in the game fails.
static void ffbHandleSetFeature(const uint8_t* data, uint16_t len) {
  if (len < 2) return;
  if (data[0] != WF_RID_CREATE_EFFECT) return;

  uint8_t index = ffbAllocate(data[1]);
  g_lastBlockIndex = index;
  g_lastBlockStatus = index ? 1 : 2;      // success, or pool full
}

static uint8_t ffbFreeBlocks() {
  uint8_t free = 0;
  for (uint8_t i = 0; i < WF_MAX_EFFECTS; i++)
    if (!g_effects[i].inUse) free++;
  return free;
}

// Returns the number of bytes written into dst, or 0 for an unknown report.
static uint8_t ffbHandleGetFeature(uint8_t reportId, uint8_t* dst, uint8_t maxLen) {
  if (reportId == WF_RID_BLOCK_LOAD && maxLen >= 5) {
    dst[0] = WF_RID_BLOCK_LOAD;
    dst[1] = g_lastBlockIndex;
    dst[2] = g_lastBlockStatus;
    uint16_t avail = (uint16_t)ffbFreeBlocks() * 16;
    dst[3] = (uint8_t)(avail & 0xff);
    dst[4] = (uint8_t)(avail >> 8);
    return 5;
  }

  if (reportId == WF_RID_POOL && maxLen >= 5) {
    dst[0] = WF_RID_POOL;
    uint16_t poolSize = WF_MAX_EFFECTS * 16;
    dst[1] = (uint8_t)(poolSize & 0xff);
    dst[2] = (uint8_t)(poolSize >> 8);
    dst[3] = WF_MAX_EFFECTS;
    dst[4] = 0x01;                        // device managed pool
    return 5;
  }

  return 0;
}

// ---------------------------------------------------------------- torque

// Attack and fade, as a 0..255 multiplier.
static int32_t ffbEnvelope(const FfbEffect* e, uint32_t elapsed) {
  if (!e->hasEnvelope) return 255;

  if (e->attackTime > 0 && elapsed < e->attackTime) {
    int32_t span = 255 - (int32_t)e->attackLevel;
    return (int32_t)e->attackLevel + (span * (int32_t)elapsed) / e->attackTime;
  }

  if (e->duration > 0 && e->fadeTime > 0) {
    uint32_t fadeStart = (e->duration > e->fadeTime) ? (e->duration - e->fadeTime) : 0;
    if (elapsed > fadeStart) {
      int32_t into = (int32_t)(elapsed - fadeStart);
      int32_t span = 255 - (int32_t)e->fadeLevel;
      int32_t v = 255 - (span * into) / e->fadeTime;
      return v < 0 ? 0 : v;
    }
  }

  return 255;
}

// A triangle wave stands in for sine. It costs no floating point and, through a
// gear and a belt, nobody can tell the difference.
static int32_t ffbWave(uint8_t type, uint32_t elapsed, const FfbEffect* e) {
  uint32_t period = e->period ? e->period : 1;
  uint32_t phaseOffset = ((uint32_t)e->phase * period) / 255;
  uint32_t t = (elapsed + phaseOffset) % period;
  int32_t half = (int32_t)(period / 2);
  if (half <= 0) half = 1;

  switch (type) {
    case ET_SQUARE:
      return (t < (uint32_t)half) ? 255 : -255;

    case ET_SINE:
    case ET_TRIANGLE: {
      int32_t up = ((int32_t)t * 1020) / (int32_t)period;   // 0..1020
      int32_t v = up - 255;                                  // -255..765
      if (v > 255) v = 510 - v;                              // fold to a triangle
      if (v < -255) v = -510 - v;
      return v;
    }

    case ET_SAWTOOTH_U:
      return (((int32_t)t * 510) / (int32_t)period) - 255;

    case ET_SAWTOOTH_D:
      return 255 - (((int32_t)t * 510) / (int32_t)period);
  }
  return 0;
}

// position and velocity are both scaled to +/-FFB_SCALE.
static int32_t ffbCondition(const FfbEffect* e, int32_t metric) {
  int32_t cp = ((int32_t)e->cpOffset * FFB_SCALE) / 128;
  int32_t db = ((int32_t)e->deadBand * FFB_SCALE) / 255;

  int32_t delta = metric - cp;
  if (delta > -db && delta < db) return 0;
  delta -= (delta > 0) ? db : -db;

  int32_t coef = (delta > 0) ? e->positiveCoefficient : e->negativeCoefficient;
  int32_t force = (delta * coef) / 128;

  int32_t sat = (delta > 0) ? e->positiveSaturation : e->negativeSaturation;
  if (sat > 0) {
    int32_t limit = (sat * FFB_SCALE) / 255;
    if (force > limit) force = limit;
    if (force < -limit) force = -limit;
  }

  // A condition effect opposes what it measures: a spring pushes back toward
  // its centre point, a damper resists the direction of travel.
  return -force;
}

// position, velocity, acceleration all scaled to +/-FFB_SCALE.
// Returns torque in the same range.
static int32_t ffbTorque(uint32_t now, int32_t position, int32_t velocity, int32_t acceleration) {
  if (!g_actuatorsEnabled || g_devicePaused) {
    g_playingCount = 0;
    return 0;
  }

  int32_t total = 0;
  uint8_t playing = 0;

  for (uint8_t i = 0; i < WF_MAX_EFFECTS; i++) {
    FfbEffect* e = &g_effects[i];
    if (!e->inUse || !e->running) continue;

    uint32_t elapsed = now - e->startedAt;

    if (e->duration > 0 && elapsed >= e->duration) {
      e->running = 0;
      continue;
    }

    playing++;
    int32_t force = 0;

    switch (e->type) {
      case ET_CONSTANT:
        force = ((int32_t)e->magnitude * FFB_SCALE) / 255;
        break;

      case ET_RAMP: {
        int32_t span = e->duration ? e->duration : 1000;
        int32_t p = (int32_t)elapsed;
        if (p > span) p = span;
        int32_t v = (int32_t)e->rampStart
                  + (((int32_t)e->rampEnd - (int32_t)e->rampStart) * p) / span;
        force = (v * FFB_SCALE) / 128;
        break;
      }

      case ET_SQUARE:
      case ET_SINE:
      case ET_TRIANGLE:
      case ET_SAWTOOTH_U:
      case ET_SAWTOOTH_D: {
        int32_t wave = ffbWave(e->type, elapsed, e);
        int32_t mag = ((int32_t)e->periodicMagnitude * wave) / 255;
        mag += ((int32_t)e->offset * 2);
        force = (mag * FFB_SCALE) / 255;
        break;
      }

      case ET_SPRING:   force = ffbCondition(e, position); break;
      case ET_DAMPER:   force = ffbCondition(e, velocity); break;
      case ET_INERTIA:  force = ffbCondition(e, acceleration); break;
      case ET_FRICTION: {
        int32_t sign = (velocity > 200) ? FFB_SCALE : ((velocity < -200) ? -FFB_SCALE : 0);
        force = ffbCondition(e, sign);
        break;
      }
    }

    int32_t env = ffbEnvelope(e, elapsed);
    force = (force * env) / 255;
    force = (force * (int32_t)e->gain) / 255;

    total += force;
  }

  g_playingCount = playing;

  total = (total * (int32_t)g_deviceGain) / 255;

  if (total > FFB_SCALE) total = FFB_SCALE;
  if (total < -FFB_SCALE) total = -FFB_SCALE;
  return total;
}
