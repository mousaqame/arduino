// MQ-2 gas sensor with an LED, a buzzer, and a serial link to a PC dashboard.
//
// NOT A SAFETY DEVICE. An uncalibrated MQ-2 cannot measure a real gas
// concentration, and this has no certification of any kind. Treat it as a
// useful indicator and a good thing to build — never as the thing standing
// between you and a gas leak. Buy a certified alarm for that.
//
// Two things this does that a bare threshold sketch doesn't:
//
//  * Warm-up. An MQ-2's heater needs time. Readings in the first half minute
//    start high and drift down, so a fixed threshold fires spuriously on every
//    power-up. Nothing is judged until the warm-up finishes.
//
//  * A learned baseline. Clean-air readings vary a lot between individual
//    sensors, and with temperature and humidity, so one sensor's "250" is
//    another's "180". Alarms are raised relative to a measured baseline
//    instead of an absolute number.
//
// Telemetry out (10 Hz):  T <millis> <raw> <smoothed> <level>
// Replies:                OK ... / ERR ... / # <info>
//
// Commands in (newline terminated, case-insensitive):
//   PING                  -> OK PONG
//   GET                   -> dump current config
//   CAL                   -> re-learn the clean-air baseline (needs clean air!)
//   SET WARN <n>          how far above baseline counts as RISING
//   SET ALARM <n>         how far above baseline counts as LEAK
//   MUTE ON|OFF           silence the buzzer, keep the LED
//   TEST                  sound the alarm briefly to prove it works
//   SAVE                  persist settings to EEPROM
//   RESET                 restore built-in defaults (does not auto-save)

#include <EEPROM.h>

#include "types.h"

const int gasPin    = A5;   // MQ-2 analog out
const int buzzerPin = 8;    // piezo buzzer
const int ledPin    = 7;    // indicator LED  <-- change if yours is elsewhere

#define EEPROM_ADDR  0
#define EEPROM_MAGIC 0x6A51

const unsigned long WARMUP_MS    = 30000;  // heater settling time
// A 5s average landed on whatever the signal happened to be doing at the time.
// Measured clean-air swing on this sensor is roughly +-30 counts, so the
// baseline needs to average across several of those cycles to be meaningful.
const unsigned long CAL_MS       = 10000;
const unsigned long MEASURE_MS   = 20;
const unsigned long TELEMETRY_MS = 100;

Config cfg;

void loadDefaults() {
  cfg.magic      = EEPROM_MAGIC;
  cfg.baseline   = 0;       // 0 means "not calibrated yet"
  // Measured clean-air noise on this sensor spans about 60 counts peak to peak,
  // so a +60 warn threshold false-alarms on nothing at all. These leave room
  // for the noise while still catching a real rise, which is far larger.
  cfg.warnDelta  = 100;
  cfg.alarmDelta = 250;
  cfg.muted      = 0;
}

// ---- runtime state -------------------------------------------------------

int      raw      = 0;
float    smooth   = 0.0f;
Level    level    = L_WARMUP;
Level    lastLevel = L_WARMUP;
bool     warmed   = false;

// The MQ-2 signal spikes and wanders. A level has to persist before it is
// acted on, so a momentary wobble neither sets off the alarm nor clears a real
// one. Standing down is deliberately slower than escalating.
Level         candidate      = L_CLEAR;
unsigned long candidateSince = 0;
const unsigned long ESCALATE_MS = 3000;
const unsigned long RELAX_MS    = 8000;

unsigned long lastMeasure = 0;
unsigned long lastTelem   = 0;

// calibration run
bool          calibrating = false;
unsigned long calStart    = 0;
uint32_t      calSum      = 0;
uint16_t      calCount    = 0;

// non-blocking buzzer / LED patterns
unsigned int  beepFreq    = 0;
unsigned int  beepOnMs    = 0;
unsigned int  beepOffMs   = 0;
bool          beepAudible = false;
unsigned long beepPhaseAt = 0;
unsigned long testUntil   = 0;

bool          ledOn      = false;
unsigned int  ledOnMs    = 0;
unsigned int  ledOffMs   = 0;
unsigned long ledPhaseAt = 0;

const char *levelName(Level l) {
  switch (l) {
    case L_WARMUP: return "WARMUP";
    case L_CLEAR:  return "CLEAR";
    case L_RISING: return "RISING";
    default:       return "LEAK";
  }
}

// ---- setup ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(gasPin, INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  noTone(buzzerPin);

  EEPROM.get(EEPROM_ADDR, cfg);
  if (cfg.magic != EEPROM_MAGIC) loadDefaults();

  smooth = analogRead(gasPin);

  Serial.println(F("# gas_serial ready"));
  Serial.println(F("# warming up the sensor, about 30 seconds"));
  printConfig();
}

// ---- measurement ---------------------------------------------------------

void measure(unsigned long now) {
  raw = analogRead(gasPin);
  smooth = smooth * 0.96f + raw * 0.04f;      // the raw signal is very noisy

  if (!warmed) {
    if (now < WARMUP_MS) { level = L_WARMUP; return; }
    warmed = true;
    Serial.println(F("# warm-up finished"));
    if (cfg.baseline <= 0) {                  // never calibrated -> do it now
      startCal(now);
      Serial.println(F("# learning the clean-air baseline"));
    }
  }

  if (calibrating) {
    calSum += raw;
    calCount++;
    if (now - calStart >= CAL_MS) {
      cfg.baseline = calCount ? (int16_t)(calSum / calCount) : raw;
      calibrating = false;
      Serial.print(F("OK CAL baseline="));
      Serial.println(cfg.baseline);
      printConfig();
    }
    level = L_WARMUP;                         // hold off judging while learning
    return;
  }

  const int16_t over = (int16_t)smooth - cfg.baseline;
  Level want;
  if      (over >= cfg.alarmDelta) want = L_LEAK;
  else if (over >= cfg.warnDelta)  want = L_RISING;
  else                             want = L_CLEAR;

  if (want != candidate) { candidate = want; candidateSince = now; }

  if (level == L_WARMUP) {                 // first verdict after warm-up
    level = want;
  } else if (want != level) {
    const unsigned long need = (want > level) ? ESCALATE_MS : RELAX_MS;
    if (now - candidateSince >= need) level = want;
  }
}

void startCal(unsigned long now) {
  calibrating = true;
  calStart = now;
  calSum = 0;
  calCount = 0;
}

// Map the current level onto the LED and buzzer patterns.
void applyLevel() {
  switch (level) {
    case L_WARMUP:
      beepFreq = 0;
      ledOnMs = 120; ledOffMs = 1400;         // faint heartbeat while waiting
      break;
    case L_CLEAR:
      beepFreq = 0;
      ledOnMs = 0; ledOffMs = 0;              // off
      break;
    case L_RISING:
      beepFreq = 1800; beepOnMs = 80; beepOffMs = 1600;
      ledOnMs = 400; ledOffMs = 400;
      break;
    case L_LEAK:
      beepFreq = 2500; beepOnMs = 400; beepOffMs = 200;
      ledOnMs = 120; ledOffMs = 120;
      break;
  }
}

void serviceBuzzer(unsigned long now) {
  if (testUntil) {
    if (now < testUntil) return;
    testUntil = 0;
    noTone(buzzerPin);
    beepAudible = false;
    beepPhaseAt = now;
  }

  if (cfg.muted || beepFreq == 0) {
    if (beepAudible) { noTone(buzzerPin); beepAudible = false; }
    return;
  }

  const unsigned int phase = beepAudible ? beepOnMs : beepOffMs;
  if (now - beepPhaseAt >= phase) {
    beepPhaseAt = now;
    beepAudible = !beepAudible;
    if (beepAudible) tone(buzzerPin, beepFreq);
    else             noTone(buzzerPin);
  }
}

void serviceLed(unsigned long now) {
  if (ledOnMs == 0 && ledOffMs == 0) {
    if (ledOn) { digitalWrite(ledPin, LOW); ledOn = false; }
    return;
  }
  const unsigned int phase = ledOn ? ledOnMs : ledOffMs;
  if (now - ledPhaseAt >= phase) {
    ledPhaseAt = now;
    ledOn = !ledOn;
    digitalWrite(ledPin, ledOn ? HIGH : LOW);
  }
}

// ---- serial --------------------------------------------------------------

void printConfig() {
  Serial.print(F("OK CFG baseline=")); Serial.print(cfg.baseline);
  Serial.print(F(" warn="));           Serial.print(cfg.warnDelta);
  Serial.print(F(" alarm="));          Serial.print(cfg.alarmDelta);
  Serial.print(F(" muted="));          Serial.print(cfg.muted ? 1 : 0);
  Serial.print(F(" warmed="));         Serial.println(warmed ? 1 : 0);
}

bool onOff(const char *s, bool *out) {
  if (!s) return false;
  if (!strcasecmp(s, "ON"))  { *out = true;  return true; }
  if (!strcasecmp(s, "OFF")) { *out = false; return true; }
  return false;
}

char    lineBuf[40];
uint8_t lineLen = 0;

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;

  if (!strcasecmp(cmd, "PING")) { Serial.println(F("OK PONG")); return; }
  if (!strcasecmp(cmd, "GET"))  { printConfig(); return; }

  if (!strcasecmp(cmd, "CAL")) {
    if (!warmed) { Serial.println(F("ERR still warming up")); return; }
    startCal(millis());
    Serial.println(F("OK CAL started, keep the air clean for 5 seconds"));
    return;
  }

  if (!strcasecmp(cmd, "SET")) {
    char *what = strtok(NULL, " ");
    char *val  = strtok(NULL, " ");
    if (!what || !val) { Serial.println(F("ERR usage SET <WARN|ALARM> <n>")); return; }
    const int v = atoi(val);
    if (v < 5 || v > 900) { Serial.println(F("ERR value out of range 5..900")); return; }
    if      (!strcasecmp(what, "WARN"))  cfg.warnDelta  = v;
    else if (!strcasecmp(what, "ALARM")) cfg.alarmDelta = v;
    else { Serial.println(F("ERR unknown setting")); return; }
    if (cfg.alarmDelta <= cfg.warnDelta)
      Serial.println(F("# warn alarm is not above warn, RISING will be skipped"));
    printConfig();
    return;
  }

  if (!strcasecmp(cmd, "MUTE")) {
    bool on;
    if (!onOff(strtok(NULL, " "), &on)) { Serial.println(F("ERR usage MUTE ON|OFF")); return; }
    cfg.muted = on ? 1 : 0;
    if (on) { noTone(buzzerPin); beepAudible = false; }
    printConfig();
    return;
  }

  if (!strcasecmp(cmd, "TEST")) {
    testUntil = millis() + 600;
    tone(buzzerPin, 2500);
    Serial.println(F("OK TEST"));
    return;
  }

  if (!strcasecmp(cmd, "SAVE")) {
    cfg.magic = EEPROM_MAGIC;
    EEPROM.put(EEPROM_ADDR, cfg);
    Serial.println(F("OK SAVED"));
    return;
  }

  if (!strcasecmp(cmd, "RESET")) {
    const int16_t keep = cfg.baseline;
    loadDefaults();
    cfg.baseline = keep;                       // a re-learn is a separate action
    printConfig();
    return;
  }

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
  Serial.print(smooth, 1);
  Serial.print(' ');
  Serial.println(levelName(level));
}

// ---- main loop -----------------------------------------------------------

void loop() {
  const unsigned long now = millis();

  serviceSerial();

  if (now - lastMeasure >= MEASURE_MS) {
    lastMeasure = now;
    measure(now);
    if (level != lastLevel) {
      lastLevel = level;
      beepPhaseAt = now; beepAudible = false; noTone(buzzerPin);
      ledPhaseAt = now;
    }
    applyLevel();
  }

  serviceBuzzer(now);
  serviceLed(now);

  if (now - lastTelem >= TELEMETRY_MS) { lastTelem = now; sendTelemetry(now); }
}
