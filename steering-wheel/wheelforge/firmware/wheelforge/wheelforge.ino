// WheelForge firmware -- one sketch, every board.
//
// What changes between boards is not the wheel logic but how it reaches the
// PC. Boards with USB device hardware enumerate as a real game controller.
// Boards without it stream over serial and the WheelForge app feeds a vJoy
// device on the PC side. boards.h picks which, and there is no third option:
// a chip with no USB peripheral cannot be talked into having one.
//
// Settings -- encoder PPR, rotation, invert, torque limit, driver type -- live
// in EEPROM and are set from the app over the serial link, so changing them
// never means recompiling.
//
// FORCE FEEDBACK: this build drives the motor only for the app's Wheel Test.
// It is not yet a HID force feedback device, so games cannot feel it. Every
// test effect expires on its own after WF_TEST_TIMEOUT_MS unless the app keeps
// refreshing it, so if the app closes or the cable is pulled the motor stops
// by itself. Outside a test the driver pins are left as inputs, not driven.
//
// UNTESTED ON HARDWARE.

#include <Arduino.h>
#include <EEPROM.h>
#include "boards.h"

#define WF_VERSION "0.3.0"

// An effect not refreshed within this long is dropped and the motor released.
// This is the difference between a failed test and a wheel fighting you with
// nobody driving it.
#define WF_TEST_TIMEOUT_MS 3000

#define REPORT_INTERVAL_US 2000     // 500 Hz
#define BUTTON_DEBOUNCE_MS 8
#define PEDAL_SMOOTH_SHIFT 3
#define BUTTON_BASE_INDEX  8        // matrix starts at HID button 9, like EMC Lite

// Position, velocity and torque all share this scale, so the PID engine and
// the local tests speak the same units.
#define FFB_SCALE          10000L

#if defined(WF_TRANSPORT_HID)
  #define WF_PLAYING_COUNT g_playingCount
#else
  #define WF_PLAYING_COUNT 0
#endif

#if defined(ARDUINO_ARCH_ESP32)
  #define WF_ISR IRAM_ATTR
#elif defined(ARDUINO_ARCH_ESP8266)
  #define WF_ISR ICACHE_RAM_ATTR
#else
  #define WF_ISR
#endif

// ===========================================================================
//  Report
// ===========================================================================

struct __attribute__((packed)) WheelReport {
  uint8_t  buttons[3];
  int16_t  steering;
  uint16_t throttle;
  uint16_t brake;
  uint16_t clutch;
};

static WheelReport g_report;
static WheelReport g_lastSent;

// These have to come after WheelReport and before anything that uses them.
// Bridge boards get none of it: with no USB device hardware there is nothing
// for a game to send forces to.
#if defined(WF_TRANSPORT_HID)
  #include "hid_descriptor.h"
  #include "ffb.h"
  #if defined(WF_USB_AVR)
    #include "usb_avr.h"
  #elif defined(WF_USB_TINYUSB)
    #include "usb_tinyusb.h"
  #elif defined(WF_USB_ESP32)
    #include "usb_esp32.h"
  #endif
#endif

// ===========================================================================
//  Stored settings
// ===========================================================================

// Bumped from WFC1 when the pin map moved into the config. An older struct
// fails the magic check and falls back to defaults rather than being read as
// nonsense.
#define WF_CONFIG_MAGIC 0x57464332UL   // "WFC2"

// Steering source.
#define ENC_QUADRATURE 0    // any incremental A/B encoder
#define ENC_ANALOG     1    // a potentiometer straight onto an analogue pin

// Motor driver wiring.
#define DRV_DUAL_PWM   0    // IBT-2, BTS7960: one PWM per direction
#define DRV_PWM_DIR    1    // L298N, MD10C, Cytron: PWM plus a direction line
#define DRV_PWM_DIR_EN 2    // as above with a separate enable pin

struct __attribute__((packed)) WfConfig {
  uint32_t magic;
  uint16_t ppr;         // encoder pulses per revolution, before quadrature
  uint16_t rotation;    // degrees lock to lock
  uint8_t  invert;      // flip steering direction
  uint8_t  maxTorque;   // 0..100, hard ceiling on every effect
  uint8_t  driver;      // DRV_*
  uint8_t  ffbInvert;   // flip the sign of every force
  uint8_t  ffbGain;     // 0..100, master scale on game forces

  // Pins live here rather than in the build, so a wheel wired to whatever was
  // convenient still works without recompiling anything.
  uint8_t  encType;     // ENC_*
  int8_t   encA;        // quadrature A, or the analogue pin for ENC_ANALOG
  int8_t   encB;        // quadrature B, ignored for ENC_ANALOG
  int8_t   motorA;      // PWM, or PWM forward for dual PWM
  int8_t   motorB;      // PWM reverse for dual PWM, else -1
  int8_t   motorDir;    // direction line, or -1
  int8_t   motorEn;     // enable line, or -1
  int8_t   pedalPin[3]; // throttle, brake, clutch; -1 for absent

  uint8_t  reserved;
  uint16_t checksum;
};

static WfConfig g_cfg;

static uint16_t configChecksum(const WfConfig& c) {
  const uint8_t* p = (const uint8_t*)&c;
  uint16_t sum = 0;
  for (size_t i = 0; i < sizeof(WfConfig) - sizeof(uint16_t); i++) sum += p[i];
  return sum ^ 0xA5A5;
}

static void configDefaults() {
  g_cfg.magic = WF_CONFIG_MAGIC;
  g_cfg.ppr = 600;
  g_cfg.rotation = 900;
  g_cfg.invert = 0;
  g_cfg.maxTorque = 30;      // deliberately low until someone has felt it
  g_cfg.driver = 0;
  g_cfg.ffbInvert = 0;
  g_cfg.ffbGain = 100;

  // The board's own pin map is only the starting point now.
  g_cfg.encType = ENC_QUADRATURE;
  g_cfg.encA = PIN_ENC_A;
  g_cfg.encB = PIN_ENC_B;
  g_cfg.motorA = PIN_MOTOR_PWM_A;
  g_cfg.motorB = PIN_MOTOR_PWM_B;
  g_cfg.motorDir = PIN_MOTOR_DIR;
  g_cfg.motorEn = -1;
  g_cfg.pedalPin[0] = PIN_THROTTLE;
  g_cfg.pedalPin[1] = PIN_BRAKE;
  g_cfg.pedalPin[2] = PIN_CLUTCH;

  g_cfg.reserved = 0;
  g_cfg.checksum = configChecksum(g_cfg);
}

static void configLoad() {
#if !defined(__AVR__)
  EEPROM.begin(sizeof(WfConfig) + 8);
#endif
  WfConfig tmp;
  EEPROM.get(0, tmp);

  if (tmp.magic == WF_CONFIG_MAGIC && tmp.checksum == configChecksum(tmp)) {
    g_cfg = tmp;
    if (g_cfg.ppr == 0) g_cfg.ppr = 600;
    if (g_cfg.rotation < 90) g_cfg.rotation = 900;
    if (g_cfg.maxTorque > 100) g_cfg.maxTorque = 100;
    if (g_cfg.ffbGain == 0 || g_cfg.ffbGain > 100) g_cfg.ffbGain = 100;
  } else {
    configDefaults();
  }
}

static void configSave() {
  g_cfg.magic = WF_CONFIG_MAGIC;
  g_cfg.checksum = configChecksum(g_cfg);
  EEPROM.put(0, g_cfg);
#if !defined(__AVR__)
  EEPROM.commit();
#endif
}

// ===========================================================================
//  Encoder
// ===========================================================================

volatile int32_t g_encoderCount = 0;
static volatile uint8_t g_encoderState = 0;

// Invalid transitions -- both channels changing at once -- map to zero, so
// electrical noise cannot inject phantom counts.
static const int8_t kQuadTable[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

// Reads whichever pins the config names. digitalRead costs more than a direct
// port fetch, but a fixed port fetch only works for one hard-coded pair, and
// being able to wire the encoder anywhere is worth more than the microseconds.
static inline uint8_t readEncoderPins() {
  return (uint8_t)((digitalRead(g_cfg.encA) ? 1 : 0) | (digitalRead(g_cfg.encB) ? 2 : 0));
}

static void WF_ISR encoderIsr() {
  uint8_t s = readEncoderPins();
  g_encoderState = (uint8_t)(((g_encoderState << 2) | s) & 0x0f);
  g_encoderCount += kQuadTable[g_encoderState];
}

static int32_t encoderCounts() {
  int32_t c;
  noInterrupts();
  c = g_encoderCount;
  interrupts();
  return c;
}

#if defined(__AVR__)
// Pin change interrupts fire for any pin on the chip, where the external
// interrupts only exist on two of them. Without this an encoder on, say, D7 of
// an Uno would simply never be counted -- which looks exactly like a dead
// encoder and is the single most confusing way for a build to fail.
static void avrAttachPinChange(uint8_t pin) {
  volatile uint8_t* pcmsk = digitalPinToPCMSK(pin);
  if (pcmsk == NULL) return;
  *pcmsk |= (uint8_t)(1 << digitalPinToPCMSKbit(pin));
  PCICR |= (uint8_t)(1 << digitalPinToPCICRbit(pin));
}

static void avrDetachAllPinChange() {
  PCICR = 0;
  #if defined(PCMSK0)
    PCMSK0 = 0;
  #endif
  #if defined(PCMSK1)
    PCMSK1 = 0;
  #endif
  #if defined(PCMSK2)
    PCMSK2 = 0;
  #endif
}

  #if defined(PCINT0_vect)
    ISR(PCINT0_vect) { encoderIsr(); }
  #endif
  #if defined(PCINT1_vect)
    ISR(PCINT1_vect) { encoderIsr(); }
  #endif
  #if defined(PCINT2_vect)
    ISR(PCINT2_vect) { encoderIsr(); }
  #endif
#endif

// Wires up whichever pins the config currently names. Safe to call again after
// the pins change.
static void encoderBegin() {
  if (g_cfg.encType == ENC_ANALOG) {
#if defined(__AVR__)
    avrDetachAllPinChange();
#endif
    return;                       // an analogue wheel needs no interrupts
  }

  pinMode(g_cfg.encA, INPUT_PULLUP);
  pinMode(g_cfg.encB, INPUT_PULLUP);

  noInterrupts();
  g_encoderState = readEncoderPins();
  interrupts();

  int ia = digitalPinToInterrupt(g_cfg.encA);
  int ib = digitalPinToInterrupt(g_cfg.encB);

#if defined(__AVR__)
  avrDetachAllPinChange();
  if (ia != NOT_AN_INTERRUPT) attachInterrupt(ia, encoderIsr, CHANGE);
  else avrAttachPinChange((uint8_t)g_cfg.encA);
  if (ib != NOT_AN_INTERRUPT) attachInterrupt(ib, encoderIsr, CHANGE);
  else avrAttachPinChange((uint8_t)g_cfg.encB);
#else
  // Every pin can raise an interrupt on RP2040 and the ESP32 families.
  if (ia != NOT_AN_INTERRUPT) attachInterrupt(ia, encoderIsr, CHANGE);
  if (ib != NOT_AN_INTERRUPT) attachInterrupt(ib, encoderIsr, CHANGE);
#endif
}

// Counts from centre to full lock. PPR is pre-quadrature, so 4x it.
static int32_t halfTravelCounts() {
  int32_t cpr = (int32_t)g_cfg.ppr * 4;
  int32_t half = (cpr * (int32_t)g_cfg.rotation) / 720L;
  return half > 0 ? half : 1;
}

static int16_t steeringValue() {
  if (g_cfg.encType == ENC_ANALOG) {
    if (g_cfg.encA < 0) return 0;
    int32_t raw = analogRead(g_cfg.encA);
    int32_t full = (1L << WF_ADC_BITS) - 1;
    int32_t v = ((raw * 65535L) / full) - 32768L;
    if (v > 32767L) v = 32767L;
    if (v < -32768L) v = -32768L;
    if (g_cfg.invert) v = -v;
    return (int16_t)v;
  }

  int32_t counts = encoderCounts();
  int32_t half = halfTravelCounts();

  if (counts > half) counts = half;
  if (counts < -half) counts = -half;

  int32_t v = (counts * 32767L) / half;
  if (g_cfg.invert) v = -v;
  return (int16_t)v;
}

static int16_t steeringDegrees() {
  int32_t counts = encoderCounts();
  int32_t cpr = (int32_t)g_cfg.ppr * 4;
  if (cpr <= 0) return 0;
  int32_t deg = (counts * 360L) / cpr;
  return (int16_t)(g_cfg.invert ? -deg : deg);
}

// ===========================================================================
//  Buttons
// ===========================================================================

static const int8_t kColPins[BUTTON_COLS] = BUTTON_COL_PINS;
static const int8_t kRowPins[BUTTON_ROWS] = BUTTON_ROW_PINS;

static uint8_t  g_btnStable[BUTTON_COLS][BUTTON_ROWS];
static uint8_t  g_btnPending[BUTTON_COLS][BUTTON_ROWS];
static uint32_t g_btnChangedAt[BUTTON_COLS][BUTTON_ROWS];

static void scanButtons(uint32_t now) {
  for (uint8_t c = 0; c < BUTTON_COLS; c++) {
    pinMode(kColPins[c], OUTPUT);
    digitalWrite(kColPins[c], LOW);
    delayMicroseconds(10);           // let the weak pullups settle

    for (uint8_t r = 0; r < BUTTON_ROWS; r++) {
      uint8_t pressed = (digitalRead(kRowPins[r]) == LOW) ? 1 : 0;

      if (pressed != g_btnPending[c][r]) {
        g_btnPending[c][r] = pressed;
        g_btnChangedAt[c][r] = now;
      } else if (g_btnStable[c][r] != pressed &&
                 (now - g_btnChangedAt[c][r]) >= BUTTON_DEBOUNCE_MS) {
        g_btnStable[c][r] = pressed;
      }
    }

    // Release rather than drive high: two buttons held at once would otherwise
    // short a driven high column into a driven low one.
    digitalWrite(kColPins[c], HIGH);
    pinMode(kColPins[c], INPUT);
  }
}

static void packButtons() {
  g_report.buttons[0] = 0;
  g_report.buttons[1] = 0;
  g_report.buttons[2] = 0;

  for (uint8_t c = 0; c < BUTTON_COLS; c++) {
    for (uint8_t r = 0; r < BUTTON_ROWS; r++) {
      if (!g_btnStable[c][r]) continue;
      uint8_t index = (uint8_t)(BUTTON_BASE_INDEX + (c * BUTTON_ROWS) + r);
      if (index >= 24) continue;
      g_report.buttons[index >> 3] |= (uint8_t)(1 << (index & 7));
    }
  }
}

// ===========================================================================
//  Pedals
// ===========================================================================

static uint16_t g_pedal[3];

// Everything is reported on a 14 bit scale regardless of the board's ADC.
static uint16_t readPedal(int8_t pin, uint8_t slot) {
  if (pin < 0) return 0;

  uint16_t raw = (uint16_t)analogRead(pin);
#if WF_ADC_BITS >= 14
  raw = raw >> (WF_ADC_BITS - 14);
#else
  raw = (uint16_t)(raw << (14 - WF_ADC_BITS));
#endif

  int32_t diff = (int32_t)raw - (int32_t)g_pedal[slot];
  g_pedal[slot] = (uint16_t)((int32_t)g_pedal[slot] + (diff >> PEDAL_SMOOTH_SHIFT));
  return g_pedal[slot];
}

// ===========================================================================
//  Motor -- test effects only
// ===========================================================================

enum WfEffect {
  EFFECT_NONE = 0,
  EFFECT_SPRING,
  EFFECT_CONSTANT,
  EFFECT_DAMPER,
  EFFECT_FRICTION,
  EFFECT_SINE
};

static uint8_t  g_effect = EFFECT_NONE;
static int16_t  g_effectArg = 0;      // -100..100 or 0..100 depending on effect
static uint16_t g_effectHz = 2;
static uint32_t g_effectUntil = 0;
static int16_t  g_lastTorque = 0;
static bool     g_motorEngaged = false;

static void motorRelease() {
  // Inputs, not driven low: the H-bridge sees nothing at all. This is the
  // resting state whenever nothing is driving the wheel.
  if (g_cfg.motorA >= 0) pinMode(g_cfg.motorA, INPUT);
  if (g_cfg.motorB >= 0) pinMode(g_cfg.motorB, INPUT);
  if (g_cfg.motorDir >= 0) pinMode(g_cfg.motorDir, INPUT);
  if (g_cfg.motorEn >= 0) { pinMode(g_cfg.motorEn, OUTPUT); digitalWrite(g_cfg.motorEn, LOW); }
  g_lastTorque = 0;
  g_motorEngaged = false;
}

static bool motorConfigured() {
  if (g_cfg.motorA < 0) return false;
  if (g_cfg.driver == DRV_DUAL_PWM) return g_cfg.motorB >= 0;
  return g_cfg.motorDir >= 0;
}

static void motorEngage() {
  if (g_cfg.motorA >= 0) pinMode(g_cfg.motorA, OUTPUT);
  if (g_cfg.motorB >= 0) pinMode(g_cfg.motorB, OUTPUT);
  if (g_cfg.motorDir >= 0) pinMode(g_cfg.motorDir, OUTPUT);
  if (g_cfg.motorEn >= 0) { pinMode(g_cfg.motorEn, OUTPUT); digitalWrite(g_cfg.motorEn, HIGH); }
  g_motorEngaged = true;
}

// torque is -1000..1000. The configured ceiling is applied here, once, so no
// caller can route around it.
static void motorWrite(int32_t torque) {
  int32_t ceiling = (int32_t)g_cfg.maxTorque * 10;      // 0..1000
  if (torque > ceiling) torque = ceiling;
  if (torque < -ceiling) torque = -ceiling;
  g_lastTorque = (int16_t)torque;

  int32_t mag = torque < 0 ? -torque : torque;
  int32_t duty = (mag * WF_PWM_MAX) / 1000;
  if (duty > WF_PWM_MAX) duty = WF_PWM_MAX;

  if (g_cfg.driver == DRV_DUAL_PWM) {
    // One PWM per direction, as an IBT-2 or BTS7960 expects: drive one side and
    // hold the other at zero. Never both, which would be a shoot-through.
    if (torque >= 0) {
      analogWrite(g_cfg.motorA, (int)duty);
      if (g_cfg.motorB >= 0) analogWrite(g_cfg.motorB, 0);
    } else {
      analogWrite(g_cfg.motorA, 0);
      if (g_cfg.motorB >= 0) analogWrite(g_cfg.motorB, (int)duty);
    }
  } else {
    // PWM on one pin, direction on another. An enable line, if the driver has
    // one, is already held high by motorEngage.
    if (g_cfg.motorDir >= 0) digitalWrite(g_cfg.motorDir, torque >= 0 ? HIGH : LOW);
    analogWrite(g_cfg.motorA, (int)duty);
  }
}

static void effectStop() {
  g_effect = EFFECT_NONE;
  g_effectArg = 0;
  g_effectUntil = 0;
  motorRelease();
}

static void effectStart(uint8_t effect, int16_t arg, uint16_t hz, uint32_t now) {
  if (!motorConfigured()) return;
  g_effect = effect;
  g_effectArg = arg;
  g_effectHz = hz ? hz : 2;
  g_effectUntil = now + WF_TEST_TIMEOUT_MS;
  motorEngage();
}

// Position, velocity and acceleration, all scaled to +/-FFB_SCALE. The PID
// condition effects (spring, damper, inertia, friction) are defined in terms
// of these, so they have to be measured whether or not a game is connected.
static void kinematics(uint32_t now, int32_t* pos, int32_t* vel, int32_t* acc) {
  static int32_t lastPos = 0;
  static int32_t lastVel = 0;
  static uint32_t lastAt = 0;

  int32_t half = halfTravelCounts();
  int32_t counts = encoderCounts();

  int32_t p = (counts * FFB_SCALE) / half;
  if (p > FFB_SCALE) p = FFB_SCALE;
  if (p < -FFB_SCALE) p = -FFB_SCALE;

  int32_t dt = (int32_t)(now - lastAt);
  if (dt <= 0) dt = 1;
  if (dt > 100) dt = 100;          // after a pause, do not invent a huge spike

  int32_t v = ((p - lastPos) * 1000L) / dt;
  if (v > FFB_SCALE) v = FFB_SCALE;
  if (v < -FFB_SCALE) v = -FFB_SCALE;

  int32_t a = ((v - lastVel) * 1000L) / dt;
  if (a > FFB_SCALE) a = FFB_SCALE;
  if (a < -FFB_SCALE) a = -FFB_SCALE;

  lastPos = p;
  lastVel = v;
  lastAt = now;

  *pos = p;
  *vel = v;
  *acc = a;
}

// Local test effects, driven by the app rather than by a game. Returns torque
// in +/-1000, or leaves it at zero when nothing is running.
static int32_t testEffectTorque(uint32_t now, int32_t pos, int32_t vel) {
  if (g_effect == EFFECT_NONE) return 0;

  // The deadline is the whole safety story: if the app stops talking, the
  // effect lapses and the motor is released without anyone having to ask.
  if ((int32_t)(now - g_effectUntil) >= 0) {
    g_effect = EFFECT_NONE;
    return 0;
  }

  int32_t position = (pos * 1000L) / FFB_SCALE;
  int32_t velocity = (vel * 1000L) / FFB_SCALE;

  switch (g_effect) {
    case EFFECT_SPRING:
      return -(position * g_effectArg) / 100;

    case EFFECT_CONSTANT:
      return (int32_t)g_effectArg * 10;

    case EFFECT_DAMPER:
      return -(velocity * g_effectArg) / 100;

    case EFFECT_FRICTION:
      if (velocity > 20) return -(int32_t)g_effectArg * 10;
      if (velocity < -20) return (int32_t)g_effectArg * 10;
      return 0;

    case EFFECT_SINE: {
      uint32_t period = 1000U / (g_effectHz ? g_effectHz : 1);
      if (period == 0) period = 1;
      uint32_t phase = now % period;
      int32_t tri = (int32_t)((phase * 4000U) / period) - 2000;
      if (tri > 1000) tri = 2000 - tri;
      if (tri < -1000) tri = -2000 - tri;
      return (tri * g_effectArg) / 100;
    }
  }
  return 0;
}

// One place decides what the motor does. Forces from a game win over the
// app's test effects, so a running sim is never fighting the test page.
static void motorTick(uint32_t now) {
  if (!motorConfigured()) return;

  int32_t pos, vel, acc;
  kinematics(now, &pos, &vel, &acc);

  int32_t torque = 0;
  bool wantMotor = false;

#if defined(WF_TRANSPORT_HID)
  int32_t game = ffbTorque(now, pos, vel, acc);
  if (game != 0 || g_actuatorsEnabled) {
    torque = (game * 1000L) / FFB_SCALE;          // to +/-1000
    torque = (torque * (int32_t)g_cfg.ffbGain) / 100;
    if (g_cfg.ffbInvert) torque = -torque;
    wantMotor = g_actuatorsEnabled;
  }
#endif

  if (!wantMotor) {
    torque = testEffectTorque(now, pos, vel);
    wantMotor = (g_effect != EFFECT_NONE);
  }

  if (!wantMotor) {
    if (g_lastTorque != 0 || g_motorEngaged) motorRelease();
    return;
  }

  if (!g_motorEngaged) motorEngage();
  motorWrite(torque);
}

// ===========================================================================
//  Transport
// ===========================================================================


// Framed replies, so the PC has one parser whether the board is streaming
// input or answering a command.
//   A5 5A <type> <len> <payload...> <xor>
//     type 1 = binary input report
//     type 2 = text line
static void sendFrame(uint8_t type, const uint8_t* payload, uint8_t len) {
  uint8_t xorsum = (uint8_t)(type ^ len);
  for (uint8_t i = 0; i < len; i++) xorsum ^= payload[i];

  Serial.write((uint8_t)0xA5);
  Serial.write((uint8_t)0x5A);
  Serial.write(type);
  Serial.write(len);
  Serial.write(payload, len);
  Serial.write(xorsum);
}

static void sendText(const char* text) {
  uint8_t len = 0;
  while (text[len] && len < 200) len++;
  sendFrame(2, (const uint8_t*)text, len);
}

// snprintf rather than String: the Arduino String class drags in the heap and
// costs a few hundred bytes of RAM, which on a 328P with 2 KB is the difference
// between comfortable and stack-corrupting.
static void sendKeyValue(const char* key, long value) {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s=%ld", key, value);
  sendText(buf);
}

// ===========================================================================
//  Serial command protocol
// ===========================================================================

static char g_line[64];
static uint8_t g_lineLen = 0;

// Pin scan mode. Every pin that is not the serial link is put on a pullup and
// simply read, so the app can watch which ones move while the wheel is turned.
// This is how you find an encoder that is wired somewhere unexpected without
// guessing, and guessing is otherwise the only tool available.
static bool g_scanMode = false;
static uint32_t g_scanUntil = 0;

// Scan mode suspends the button matrix, so leaving it on is not harmless. If
// the app goes away mid-scan nothing would ever turn it off again.
#define WF_SCAN_TIMEOUT_MS 45000

static bool isMotorPin(uint8_t pin) {
  if (g_cfg.motorA >= 0 && pin == (uint8_t)g_cfg.motorA) return true;
  if (g_cfg.motorB >= 0 && pin == (uint8_t)g_cfg.motorB) return true;
  if (g_cfg.motorDir >= 0 && pin == (uint8_t)g_cfg.motorDir) return true;
  if (g_cfg.motorEn >= 0 && pin == (uint8_t)g_cfg.motorEn) return true;
  return false;
}

static void scanModeBegin(uint32_t now) {
  motorRelease();

  for (uint8_t i = 0; i < NUM_DIGITAL_PINS; i++) {
    if (i == 0 || i == 1) continue;          // the serial link

    // Never pull a motor driver input high. On an IBT-2 or BTS7960 a PWM
    // input held high is a driver told to go, so sweeping every pin would
    // spin the wheel during what is meant to be a passive read.
    if (isMotorPin(i)) continue;

    pinMode(i, INPUT_PULLUP);
  }

  g_scanMode = true;
  g_scanUntil = now + WF_SCAN_TIMEOUT_MS;
}

static void scanModeEnd() {
  g_scanMode = false;

  // Put the matrix and the encoder back the way the firmware expects them.
  for (uint8_t c = 0; c < BUTTON_COLS; c++) pinMode(kColPins[c], INPUT);
  for (uint8_t r = 0; r < BUTTON_ROWS; r++) pinMode(kRowPins[r], INPUT_PULLUP);
  encoderBegin();
}

static void reportPins() {
  uint32_t mask = 0;
  uint8_t count = NUM_DIGITAL_PINS;
  if (count > 32) count = 32;

  for (uint8_t i = 0; i < count; i++) {
    // D0 and D1 are the serial link. They toggle constantly from the very
    // conversation asking for this reading, and reporting them makes the pin
    // finder point at the wrong wires.
    if (i == 0 || i == 1) continue;
    if (digitalRead(i)) mask |= (1UL << i);
  }

  char buf[48];
  snprintf(buf, sizeof(buf), "pins %u %lu", (unsigned)count, (unsigned long)mask);
  sendText(buf);
}

// On AVR a plain string literal is copied into RAM at startup, and this
// firmware has enough of them to matter on a 2 KB part. WF_SEND keeps the text
// in flash and pulls it into a small buffer only when it is actually sent.
#if defined(__AVR__)
  #define WF_SEND(lit) do { \
      char _wfb[96]; \
      strncpy_P(_wfb, PSTR(lit), sizeof(_wfb) - 1); \
      _wfb[sizeof(_wfb) - 1] = 0; \
      sendText(_wfb); \
    } while (0)
#else
  #define WF_SEND(lit) sendText(lit)
#endif

static void reportConfig() {
  WF_SEND("--- config ---");
  sendText("version=" WF_VERSION);
  sendText("board=" WF_BOARD_NAME);
#if defined(WF_TRANSPORT_HID)
  WF_SEND("transport=hid");
#else
  WF_SEND("transport=bridge");
#endif
  sendKeyValue("ppr", g_cfg.ppr);
  sendKeyValue("rotation", g_cfg.rotation);
  sendKeyValue("invert", g_cfg.invert);
  sendKeyValue("maxtorque", g_cfg.maxTorque);
  sendKeyValue("driver", g_cfg.driver);
  sendKeyValue("enctype", g_cfg.encType);
  sendKeyValue("enca", g_cfg.encA);
  sendKeyValue("encb", g_cfg.encB);
  sendKeyValue("motora", g_cfg.motorA);
  sendKeyValue("motorb", g_cfg.motorB);
  sendKeyValue("motordir", g_cfg.motorDir);
  sendKeyValue("motoren", g_cfg.motorEn);
  sendKeyValue("pedala", g_cfg.pedalPin[0]);
  sendKeyValue("pedalb", g_cfg.pedalPin[1]);
  sendKeyValue("pedalc", g_cfg.pedalPin[2]);
  sendKeyValue("ffbinvert", g_cfg.ffbInvert);
  sendKeyValue("ffbgain", g_cfg.ffbGain);
  sendKeyValue("motor", motorConfigured() ? 1 : 0);
#if defined(WF_TRANSPORT_HID)
  sendKeyValue("ffb", 1);
  sendKeyValue("ffbactuators", g_actuatorsEnabled);
  sendKeyValue("ffbeffects", WF_MAX_EFFECTS);
#else
  sendKeyValue("ffb", 0);
#endif
  sendKeyValue("buttons", (long)BUTTON_COLS * BUTTON_ROWS);
  sendKeyValue("pedals", (PIN_THROTTLE >= 0) + (PIN_BRAKE >= 0) + (PIN_CLUTCH >= 0));
  WF_SEND("--- end ---");
}

static void reportStatus() {
  char buf[120];
  snprintf(buf, sizeof(buf),
           "status angle=%d counts=%ld raw=%d thr=%u brk=%u clu=%u torque=%d effect=%u playing=%u",
           (int)steeringDegrees(), (long)encoderCounts(), (int)g_report.steering,
           (unsigned)g_report.throttle, (unsigned)g_report.brake,
           (unsigned)g_report.clutch, (int)g_lastTorque, (unsigned)g_effect,
           (unsigned)WF_PLAYING_COUNT);
  sendText(buf);
}

static bool matches(const char* line, const char* cmd, const char** rest) {
  size_t n = strlen(cmd);
  if (strncasecmp(line, cmd, n) != 0) return false;
  if (line[n] != ' ' && line[n] != '\0') return false;
  *rest = line[n] == ' ' ? line + n + 1 : line + n;
  return true;
}

static void handleCommand(const char* line, uint32_t now) {
  const char* arg;

  if (matches(line, "?", &arg) || matches(line, "CONFIG", &arg)) {
    reportConfig();
  } else if (matches(line, "STATUS", &arg)) {
    reportStatus();
  } else if (matches(line, "SCAN", &arg)) {
    if (atol(arg) != 0) { scanModeBegin(now); WF_SEND("ok scan on"); }
    else { scanModeEnd(); WF_SEND("ok scan off"); }
  } else if (matches(line, "PINS", &arg)) {
    reportPins();
  } else if (matches(line, "PPR", &arg)) {
    long v = atol(arg);
    if (v >= 1 && v <= 20000) { g_cfg.ppr = (uint16_t)v; WF_SEND("ok ppr"); }
    else WF_SEND("err ppr must be 1..20000");
  } else if (matches(line, "ROT", &arg)) {
    long v = atol(arg);
    if (v >= 90 && v <= 2000) { g_cfg.rotation = (uint16_t)v; WF_SEND("ok rot"); }
    else WF_SEND("err rot must be 90..2000");
  } else if (matches(line, "INV", &arg)) {
    g_cfg.invert = (atol(arg) != 0) ? 1 : 0;
    WF_SEND("ok inv");
  } else if (matches(line, "MAXTORQUE", &arg)) {
    long v = atol(arg);
    if (v >= 0 && v <= 100) { g_cfg.maxTorque = (uint8_t)v; WF_SEND("ok maxtorque"); }
    else WF_SEND("err maxtorque must be 0..100");
  } else if (matches(line, "DRIVER", &arg)) {
    long v = atol(arg);
    if (v >= 0 && v <= 2) {
      motorRelease();
      g_cfg.driver = (uint8_t)v;
      WF_SEND("ok driver");
    } else WF_SEND("err driver 0=dual PWM 1=PWM+dir 2=PWM+dir+enable");
  } else if (matches(line, "FFBINV", &arg)) {
    g_cfg.ffbInvert = (atol(arg) != 0) ? 1 : 0;
    WF_SEND("ok ffbinv");
  } else if (matches(line, "ENCTYPE", &arg)) {
    long v = atol(arg);
    if (v >= 0 && v <= 1) {
      g_cfg.encType = (uint8_t)v;
      encoderBegin();
      WF_SEND("ok enctype");
    } else WF_SEND("err enctype 0=quadrature 1=analog");
  } else if (matches(line, "ENCPINS", &arg)) {
    int a = -1, b = -1;
    if (sscanf(arg, "%d %d", &a, &b) >= 1) {
      g_cfg.encA = (int8_t)a;
      g_cfg.encB = (int8_t)b;
      noInterrupts(); g_encoderCount = 0; interrupts();
      encoderBegin();
      WF_SEND("ok encpins");
    } else WF_SEND("err usage: ENCPINS <a> <b>");
  } else if (matches(line, "MOTORPINS", &arg)) {
    int a = -1, b = -1, d = -1, e = -1;
    if (sscanf(arg, "%d %d %d %d", &a, &b, &d, &e) >= 1) {
      motorRelease();
      g_cfg.motorA = (int8_t)a;
      g_cfg.motorB = (int8_t)b;
      g_cfg.motorDir = (int8_t)d;
      g_cfg.motorEn = (int8_t)e;
      WF_SEND("ok motorpins");
    } else WF_SEND("err usage: MOTORPINS <pwmA> <pwmB> <dir> <enable>");
  } else if (matches(line, "PEDALPINS", &arg)) {
    int t = -1, b = -1, c = -1;
    if (sscanf(arg, "%d %d %d", &t, &b, &c) >= 1) {
      g_cfg.pedalPin[0] = (int8_t)t;
      g_cfg.pedalPin[1] = (int8_t)b;
      g_cfg.pedalPin[2] = (int8_t)c;
      WF_SEND("ok pedalpins");
    } else WF_SEND("err usage: PEDALPINS <throttle> <brake> <clutch>");
  } else if (matches(line, "FFBGAIN", &arg)) {
    long v = atol(arg);
    if (v >= 0 && v <= 100) { g_cfg.ffbGain = (uint8_t)v; WF_SEND("ok ffbgain"); }
    else WF_SEND("err ffbgain must be 0..100");
  } else if (matches(line, "CENTER", &arg) || matches(line, "CENTRE", &arg)) {
    noInterrupts();
    g_encoderCount = 0;
    interrupts();
    WF_SEND("ok centre");
  } else if (matches(line, "SAVE", &arg)) {
    configSave();
    WF_SEND("ok saved");
  } else if (matches(line, "LOAD", &arg)) {
    motorRelease();
    configLoad();
    encoderBegin();
    WF_SEND("ok loaded");
  } else if (matches(line, "DEFAULTS", &arg)) {
    motorRelease();
    configDefaults();
    encoderBegin();
    WF_SEND("ok defaults");
  } else if (matches(line, "STOP", &arg)) {
    effectStop();
    WF_SEND("ok stop");
  } else if (matches(line, "TEST", &arg)) {
    if (!motorConfigured()) { WF_SEND("err no motor pins configured"); return; }
    char what[16];
    int value = 0, hz = 2;
    if (sscanf(arg, "%15s %d %d", what, &value, &hz) >= 2) {
      if (value < -100) value = -100;
      if (value > 100) value = 100;

      if (strcasecmp(what, "SPRING") == 0)        effectStart(EFFECT_SPRING, value, hz, now);
      else if (strcasecmp(what, "CONST") == 0)    effectStart(EFFECT_CONSTANT, value, hz, now);
      else if (strcasecmp(what, "DAMPER") == 0)   effectStart(EFFECT_DAMPER, value, hz, now);
      else if (strcasecmp(what, "FRICTION") == 0) effectStart(EFFECT_FRICTION, value, hz, now);
      else if (strcasecmp(what, "SINE") == 0)     effectStart(EFFECT_SINE, value, hz, now);
      else { WF_SEND("err unknown effect"); return; }

      WF_SEND("ok test");
    } else {
      WF_SEND("err usage: TEST <SPRING|CONST|DAMPER|FRICTION|SINE> <-100..100> [hz]");
    }
  } else if (line[0] != '\0') {
    WF_SEND("err unknown command");
  }
}

static void pumpSerial(uint32_t now) {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;

    if (ch == '\n') {
      g_line[g_lineLen] = '\0';
      handleCommand(g_line, now);
      g_lineLen = 0;
    } else if (g_lineLen < sizeof(g_line) - 1) {
      g_line[g_lineLen++] = ch;
    } else {
      // Overlong line: drop it rather than silently truncating into a
      // command that means something else.
      g_lineLen = 0;
    }
  }
}

// ===========================================================================
//  Setup and loop
// ===========================================================================

void setup() {
  motorRelease();

  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  for (uint8_t c = 0; c < BUTTON_COLS; c++) pinMode(kColPins[c], INPUT);
  for (uint8_t r = 0; r < BUTTON_ROWS; r++) pinMode(kRowPins[r], INPUT_PULLUP);

  for (uint8_t c = 0; c < BUTTON_COLS; c++)
    for (uint8_t r = 0; r < BUTTON_ROWS; r++) {
      g_btnStable[c][r] = 0;
      g_btnPending[c][r] = 0;
      g_btnChangedAt[c][r] = 0;
    }

#if WF_ADC_BITS > 10 && !defined(__AVR__)
  analogReadResolution(WF_ADC_BITS);
#endif

  configLoad();

  encoderBegin();

  Serial.begin(115200);

#if defined(WF_TRANSPORT_HID)
  usbBegin();
#endif

  memset(&g_report, 0, sizeof(g_report));
  memset(&g_lastSent, 0xff, sizeof(g_lastSent));
}

void loop() {
  static uint32_t nextReportAt = 0;
  static uint32_t nextBlinkAt = 0;
  static uint32_t lastSendMs = 0;
  static bool ledOn = false;

  uint32_t nowMs = millis();
  uint32_t nowUs = micros();

  // A scan that outlives the app would leave the buttons dead for good.
  if (g_scanMode && (int32_t)(nowMs - g_scanUntil) >= 0) scanModeEnd();

  if (!g_scanMode) scanButtons(nowMs);
  pumpSerial(nowMs);

#if defined(WF_TRANSPORT_HID)
  // Force feedback arrives as output reports. On AVR they land on an interrupt
  // endpoint that nothing services unless it is drained here.
  usbPoll();
#endif

  if (!g_scanMode) motorTick(nowMs);

  if ((int32_t)(nowUs - nextReportAt) < 0) return;
  nextReportAt = nowUs + REPORT_INTERVAL_US;

  packButtons();
  g_report.steering = steeringValue();
  g_report.throttle = readPedal(g_cfg.pedalPin[0], 0);
  g_report.brake    = readPedal(g_cfg.pedalPin[1], 1);
  g_report.clutch   = readPedal(g_cfg.pedalPin[2], 2);

  bool changed = memcmp(&g_report, &g_lastSent, sizeof(g_report)) != 0;

  // The keepalive matters: without it a perfectly still wheel sends nothing
  // after enumeration, and anything waiting on a first report -- joy.cpl, a
  // game's setup screen, WheelForge's own monitor -- looks broken.
  if (changed || (nowMs - lastSendMs) >= 250) {
    lastSendMs = nowMs;
    memcpy(&g_lastSent, &g_report, sizeof(g_report));

#if defined(WF_TRANSPORT_HID)
    usbSend(g_report);
#else
    sendFrame(1, (const uint8_t*)&g_report, sizeof(g_report));
#endif
  }

  if (nowMs >= nextBlinkAt) {
    nextBlinkAt = nowMs + (ledOn ? 1900 : 100);
    ledOn = !ledOn;
    digitalWrite(PIN_STATUS_LED, ledOn ? HIGH : LOW);
  }
}
