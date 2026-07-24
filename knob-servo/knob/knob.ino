// A potentiometer turning a servo, on an Arduino Mega 2560.
//
// Wiring:
//   Potentiometer  outer leg -> 5V,  middle leg (wiper) -> A0,  other leg -> GND
//   Servo          signal -> pin 9,  power -> 5V,  ground -> GND
//
// Two things a naive version of this gets wrong, both handled here:
//
//  * Pot noise. The ADC's last couple of bits wobble even with the knob
//    perfectly still, and a servo told to move half a degree back and forth
//    forever buzzes, gets hot, and wears out. So the reading is smoothed and
//    the servo only moves once the target has shifted by more than a deadband.
//
//  * Cheap pots rarely reach the true ends of their travel, so mapping 0..1023
//    straight onto 0..180 means the last few degrees are unreachable. The input
//    range is configurable and saved.
//
// Telemetry out (20 Hz):  T <millis> <raw> <angle> <mode>
// Replies:                OK ... / ERR ... / # <info>
//
// Commands in (newline terminated, case-insensitive):
//   PING                  -> OK PONG
//   GET                   -> dump current config
//   MODE KNOB|WEB         who drives the servo: the potentiometer, or this link
//   ANGLE <deg>           set the angle directly (needs MODE WEB)
//   SET MIN <n>           raw reading that means 0 degrees
//   SET MAX <n>           raw reading that means 180 degrees
//   SET LOW <deg>         lowest angle to send the servo
//   SET HIGH <deg>        highest angle to send the servo
//   INVERT ON|OFF         flip which way the knob turns the servo
//   SAVE                  persist settings to EEPROM
//   RESET                 restore built-in defaults (does not auto-save)

#include <Servo.h>
#include <EEPROM.h>

#include "types.h"

const int potPin   = A0;
const int servoPin = 9;

#define EEPROM_ADDR  0
#define EEPROM_MAGIC 0x4B0B

const unsigned long MEASURE_MS   = 20;
const unsigned long TELEMETRY_MS = 50;

// Degrees of change needed before the servo is told to move. Below this the
// signal is pot noise, not intent.
const int DEADBAND = 2;

Servo servo;
Config cfg;

void loadDefaults() {
  cfg.magic    = EEPROM_MAGIC;
  cfg.rawMin   = 0;
  cfg.rawMax   = 1023;
  cfg.angleLow = 0;
  cfg.angleHigh = 180;
  cfg.invert   = 0;
}

// ---- runtime state -------------------------------------------------------

int    raw     = 0;
float  smooth  = 0.0f;
int    angle   = 90;        // what the servo was last told
int    wanted  = 90;        // what the knob (or the web) is asking for
Mode   mode    = M_KNOB;

unsigned long lastMeasure = 0;
unsigned long lastTelem   = 0;

char    lineBuf[40];
uint8_t lineLen = 0;

const char *modeName(Mode m) { return m == M_WEB ? "WEB" : "KNOB"; }

// ---- servo ---------------------------------------------------------------

int angleFromRaw(int r) {
  int lo = cfg.rawMin, hi = cfg.rawMax;
  if (hi <= lo) hi = lo + 1;                 // never divide by zero
  if (r < lo) r = lo;
  if (r > hi) r = hi;

  long a = (long)(r - lo) * (cfg.angleHigh - cfg.angleLow) / (hi - lo) + cfg.angleLow;
  if (cfg.invert) a = cfg.angleHigh - (a - cfg.angleLow);
  if (a < cfg.angleLow)  a = cfg.angleLow;
  if (a > cfg.angleHigh) a = cfg.angleHigh;
  return (int)a;
}

void driveServo(int a) {
  if (a < cfg.angleLow)  a = cfg.angleLow;
  if (a > cfg.angleHigh) a = cfg.angleHigh;
  if (abs(a - angle) < DEADBAND) return;     // ignore noise-sized changes
  angle = a;
  servo.write(angle);
}

// ---- setup ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(potPin, INPUT);
  servo.attach(servoPin, 500, 2400);

  EEPROM.get(EEPROM_ADDR, cfg);
  if (cfg.magic != EEPROM_MAGIC) loadDefaults();

  smooth = analogRead(potPin);
  angle  = angleFromRaw((int)smooth);
  wanted = angle;
  servo.write(angle);

  Serial.println(F("\n# knob ready"));
  printConfig();
}

// ---- serial --------------------------------------------------------------

void printConfig() {
  Serial.print(F("OK CFG rawmin=")); Serial.print(cfg.rawMin);
  Serial.print(F(" rawmax="));       Serial.print(cfg.rawMax);
  Serial.print(F(" low="));          Serial.print(cfg.angleLow);
  Serial.print(F(" high="));         Serial.print(cfg.angleHigh);
  Serial.print(F(" invert="));       Serial.print(cfg.invert ? 1 : 0);
  Serial.print(F(" mode="));         Serial.println(modeName(mode));
}

bool onOff(const char *s, bool *out) {
  if (!s) return false;
  if (!strcasecmp(s, "ON"))  { *out = true;  return true; }
  if (!strcasecmp(s, "OFF")) { *out = false; return true; }
  return false;
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;

  if (!strcasecmp(cmd, "PING")) { Serial.println(F("OK PONG")); return; }
  if (!strcasecmp(cmd, "GET"))  { printConfig(); return; }

  if (!strcasecmp(cmd, "MODE")) {
    char *m = strtok(NULL, " ");
    if (!m) { Serial.println(F("ERR usage MODE KNOB|WEB")); return; }
    if      (!strcasecmp(m, "KNOB")) mode = M_KNOB;
    else if (!strcasecmp(m, "WEB"))  { mode = M_WEB; wanted = angle; }
    else { Serial.println(F("ERR usage MODE KNOB|WEB")); return; }
    printConfig();
    return;
  }

  if (!strcasecmp(cmd, "ANGLE")) {
    char *v = strtok(NULL, " ");
    if (!v) { Serial.println(F("ERR usage ANGLE <degrees>")); return; }
    if (mode != M_WEB) { Serial.println(F("ERR switch to MODE WEB first")); return; }
    wanted = atoi(v);
    Serial.println(F("OK ANGLE"));
    return;
  }

  if (!strcasecmp(cmd, "SET")) {
    char *what = strtok(NULL, " ");
    char *val  = strtok(NULL, " ");
    if (!what || !val) { Serial.println(F("ERR usage SET <MIN|MAX|LOW|HIGH> <n>")); return; }
    const int v = atoi(val);
    if      (!strcasecmp(what, "MIN"))  { if (v < 0 || v > 1023) { Serial.println(F("ERR 0..1023")); return; } cfg.rawMin = v; }
    else if (!strcasecmp(what, "MAX"))  { if (v < 0 || v > 1023) { Serial.println(F("ERR 0..1023")); return; } cfg.rawMax = v; }
    else if (!strcasecmp(what, "LOW"))  { if (v < 0 || v > 180)  { Serial.println(F("ERR 0..180")); return; }  cfg.angleLow = v; }
    else if (!strcasecmp(what, "HIGH")) { if (v < 0 || v > 180)  { Serial.println(F("ERR 0..180")); return; }  cfg.angleHigh = v; }
    else { Serial.println(F("ERR unknown setting")); return; }
    if (cfg.rawMax <= cfg.rawMin)     Serial.println(F("# warn rawmax is not above rawmin"));
    if (cfg.angleHigh <= cfg.angleLow) Serial.println(F("# warn high is not above low"));
    printConfig();
    return;
  }

  if (!strcasecmp(cmd, "INVERT")) {
    bool on;
    if (!onOff(strtok(NULL, " "), &on)) { Serial.println(F("ERR usage INVERT ON|OFF")); return; }
    cfg.invert = on ? 1 : 0;
    printConfig();
    return;
  }

  if (!strcasecmp(cmd, "SAVE")) {
    cfg.magic = EEPROM_MAGIC;
    EEPROM.put(EEPROM_ADDR, cfg);
    Serial.println(F("OK SAVED"));
    return;
  }

  if (!strcasecmp(cmd, "RESET")) { loadDefaults(); printConfig(); return; }

  Serial.println(F("ERR unknown command"));
}

void serviceSerial() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      lineBuf[lineLen] = '\0';
      if (lineLen) handleCommand(lineBuf);
      lineLen = 0;
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}

void sendTelemetry(unsigned long now) {
  Serial.print(F("T "));
  Serial.print(now);
  Serial.print(' ');
  Serial.print(raw);
  Serial.print(' ');
  Serial.print(angle);
  Serial.print(' ');
  Serial.println(modeName(mode));
}

// ---- main loop -----------------------------------------------------------

void loop() {
  const unsigned long now = millis();

  serviceSerial();

  if (now - lastMeasure >= MEASURE_MS) {
    lastMeasure = now;
    raw = analogRead(potPin);
    smooth = smooth * 0.8f + raw * 0.2f;     // take the edge off the ADC wobble
    if (mode == M_KNOB) wanted = angleFromRaw((int)smooth);
    driveServo(wanted);
  }

  if (now - lastTelem >= TELEMETRY_MS) { lastTelem = now; sendTelemetry(now); }
}
