// Car parking sensor with a serial control/telemetry link.
//
// Same behaviour as parking_sensor.ino, but the blocking delay() beep patterns
// are replaced with millis() timing so serial stays responsive, and thresholds
// can be retuned live from the PC.
//
// Telemetry out (every TELEMETRY_MS):  T <millis> distance=<cm> zone=<state>
// Wiring report:                       R oled=<0|1> sonic=<0|1>
// Replies:                             OK ... / ERR ... / # <info>
//
// See PROTOCOL.md - this sketch is the reference implementation of both lines.
//
// Commands in (newline terminated, case-insensitive):
//   PING                  -> OK PONG
//   CHECK                 -> re-send the R wiring report
//   GET                   -> dump current config
//   SET <ZONE> <cm>       ZONE = FAR|CLOSE|GOOD|VCLOSE
//   MODE AUTO|MANUAL      MANUAL freezes the parking logic's LED/buzzer control
//   LED ON|OFF|AUTO       manual override (MODE MANUAL only, except AUTO)
//   BUZZ <freq> <ms>      one-shot beep
//   MUTE ON|OFF           silence the buzzer, keep everything else
//   SAVE                  persist thresholds to EEPROM
//   RESET                 restore built-in defaults (does not auto-save)

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

#include "types.h"

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1

#define EEPROM_ADDR   0
#define EEPROM_MAGIC  0x50A2

const int trigPin   = 9;
const int echoPin   = 10;
const int ledPin    = 6;
const int buzzerPin = 7;

const unsigned long MEASURE_MS   = 50;
const unsigned long TELEMETRY_MS = 100;
const unsigned long DISPLAY_MS   = 100;
const float         NO_ECHO      = 999.0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Config cfg;

void loadDefaults() {
  cfg.magic    = EEPROM_MAGIC;
  cfg.farCm    = 100;
  cfg.closeCm  = 50;
  cfg.goodCm   = 20;
  cfg.vcloseCm = 10;
  cfg.muted    = 0;
}

// ---- runtime state -------------------------------------------------------

float         distance    = NO_ECHO;
Zone          zone        = Z_FAR;
Zone          lastZone    = Z_FAR;
bool          manualMode  = false;
bool          manualLed   = false;

// Wiring report. Only parts the board can actually sense are reported: the
// OLED answers on I2C, and the ultrasonic proves itself by returning an echo.
// The LED and buzzer are write-only pins with nothing to read back, so they
// are left out of the R line entirely - the dashboard shows them as "can't
// check", which is honest. Use the blink sketch to verify the LED.
bool          oledOk      = false;
bool          sonicSeen   = false;   // latches on the first real echo

// Defined further down, but called from setup() and measure(). Arduino's
// auto-prototyping would usually cover this; being explicit is cheaper than
// debugging why it didn't.
void sendReady();

unsigned long lastMeasure = 0;
unsigned long lastTelem   = 0;
unsigned long lastDisplay = 0;

// Non-blocking beep pattern.
unsigned int  beepFreq     = 0;      // 0 = silent
unsigned int  beepOnMs     = 0;
unsigned int  beepOffMs    = 0;      // 0 with freq > 0 = continuous tone
bool          beepAudible  = false;
unsigned long beepPhaseAt  = 0;

// One-shot beep from the BUZZ command.
unsigned long oneShotUntil = 0;

char    lineBuf[48];
uint8_t lineLen = 0;

const char *zoneName(Zone z) {
  switch (z) {
    case Z_FAR:    return "FAR";
    case Z_CLOSE:  return "CLOSE";
    case Z_GOOD:   return "GOOD";
    case Z_VCLOSE: return "V.CLOSE";
    default:       return "STOP!";
  }
}

// ---- setup ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  digitalWrite(ledPin, LOW);
  noTone(buzzerPin);

  EEPROM.get(EEPROM_ADDR, cfg);
  if (cfg.magic != EEPROM_MAGIC) loadDefaults();

  // A loose screen wire used to stop here forever (while(1)). From the PC that
  // looks exactly like "no code on the board", which sends you hunting the
  // wrong problem - and the sensor, LED and buzzer all work fine without a
  // screen. So carry on and report it in the R line instead.
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  if (oledOk) {
    Wire.setClock(400000);  // fast mode; the default 100kHz redraw starves the loop

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(15, 8);
    display.println(F("READY"));
    display.display();
    delay(2000);
  } else {
    Serial.println(F("# no screen found at 0x3C - running without it"));
  }

  Serial.println(F("# parking_serial ready"));
  printConfig();
  sendReady();
}

// ---- measurement ---------------------------------------------------------

void measure() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);

  // An echo is the only proof the sensor is really wired. It can't be checked
  // once in setup(): pointed at open space it legitimately reads nothing, and
  // reporting "not found" for a correctly wired sensor is worse than waiting.
  // So it latches on the first real echo and re-reports the moment it does.
  if (duration != 0 && !sonicSeen) { sonicSeen = true; sendReady(); }

  distance = (duration == 0) ? NO_ECHO : duration * 0.0343 / 2;

  if      (distance > cfg.farCm)    zone = Z_FAR;
  else if (distance > cfg.closeCm)  zone = Z_CLOSE;
  else if (distance > cfg.goodCm)   zone = Z_GOOD;
  else if (distance > cfg.vcloseCm) zone = Z_VCLOSE;
  else                              zone = Z_STOP;
}

// Map the current zone onto LED state and beep pattern.
void applyZone() {
  if (manualMode) {
    digitalWrite(ledPin, manualLed ? HIGH : LOW);
    beepFreq = 0;
    return;
  }

  switch (zone) {
    case Z_FAR:
    case Z_CLOSE:
      digitalWrite(ledPin, LOW);
      beepFreq = 0;
      break;
    case Z_GOOD:
      digitalWrite(ledPin, HIGH);
      beepFreq = 1000; beepOnMs = 100; beepOffMs = 400;
      break;
    case Z_VCLOSE:
      digitalWrite(ledPin, HIGH);
      beepFreq = 1500; beepOnMs = 100; beepOffMs = 150;
      break;
    case Z_STOP:
      digitalWrite(ledPin, HIGH);
      beepFreq = 2000; beepOnMs = 0; beepOffMs = 0;  // continuous
      break;
  }
}

// Drive the buzzer without blocking.
void serviceBuzzer(unsigned long now) {
  if (oneShotUntil) {
    if (now < oneShotUntil) { return; }
    oneShotUntil = 0;
    noTone(buzzerPin);
    beepPhaseAt = now;
    beepAudible = false;
  }

  if (cfg.muted || beepFreq == 0) {
    if (beepAudible) { noTone(buzzerPin); beepAudible = false; }
    return;
  }

  if (beepOnMs == 0 && beepOffMs == 0) {      // continuous
    if (!beepAudible) { tone(buzzerPin, beepFreq); beepAudible = true; }
    return;
  }

  unsigned int phaseLen = beepAudible ? beepOnMs : beepOffMs;
  if (now - beepPhaseAt >= phaseLen) {
    beepPhaseAt = now;
    beepAudible = !beepAudible;
    if (beepAudible) tone(buzzerPin, beepFreq);
    else             noTone(buzzerPin);
  }
}

void drawDisplay() {
  if (!oledOk) return;
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Distance: "));
  if (distance >= NO_ECHO) display.println(F("---"));
  else { display.print(distance, 0); display.println(F(" cm")); }

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print(zoneName(zone));

  if (manualMode) { display.setTextSize(1); display.setCursor(104, 0); display.print(F("MAN")); }
  else if (cfg.muted) { display.setTextSize(1); display.setCursor(110, 0); display.print(F("MU")); }

  display.display();
}

// ---- serial --------------------------------------------------------------

void printConfig() {
  Serial.print(F("OK CFG far=")); Serial.print(cfg.farCm);
  Serial.print(F(" close="));     Serial.print(cfg.closeCm);
  Serial.print(F(" good="));      Serial.print(cfg.goodCm);
  Serial.print(F(" vclose="));    Serial.print(cfg.vcloseCm);
  Serial.print(F(" muted="));     Serial.print(cfg.muted ? 1 : 0);
  Serial.print(F(" mode="));      Serial.println(manualMode ? F("MANUAL") : F("AUTO"));
}

// The dashboard flushes the serial buffer a few seconds after opening the port,
// which swallows whatever setup() printed - so it asks for this again with
// CHECK once it's listening. Also re-sent whenever a flag actually changes.
void sendReady() {
  Serial.print(F("R oled="));   Serial.print(oledOk ? 1 : 0);
  Serial.print(F(" sonic="));   Serial.println(sonicSeen ? 1 : 0);
}

void warnIfUnordered() {
  if (!(cfg.farCm > cfg.closeCm && cfg.closeCm > cfg.goodCm && cfg.goodCm > cfg.vcloseCm))
    Serial.println(F("# warn thresholds not descending, some zones unreachable"));
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

  if (!strcasecmp(cmd, "PING"))  { Serial.println(F("OK PONG")); return; }
  if (!strcasecmp(cmd, "CHECK")) { sendReady(); return; }
  if (!strcasecmp(cmd, "GET"))   { printConfig(); return; }

  if (!strcasecmp(cmd, "SET")) {
    char *what = strtok(NULL, " ");
    char *val  = strtok(NULL, " ");
    if (!what || !val) { Serial.println(F("ERR usage SET <FAR|CLOSE|GOOD|VCLOSE> <cm>")); return; }
    int v = atoi(val);
    if (v < 1 || v > 400) { Serial.println(F("ERR cm out of range 1..400")); return; }
    if      (!strcasecmp(what, "FAR"))    cfg.farCm    = v;
    else if (!strcasecmp(what, "CLOSE"))  cfg.closeCm  = v;
    else if (!strcasecmp(what, "GOOD"))   cfg.goodCm   = v;
    else if (!strcasecmp(what, "VCLOSE")) cfg.vcloseCm = v;
    else { Serial.println(F("ERR unknown zone")); return; }
    printConfig();
    warnIfUnordered();
    return;
  }

  if (!strcasecmp(cmd, "MODE")) {
    char *m = strtok(NULL, " ");
    if (!m) { Serial.println(F("ERR usage MODE AUTO|MANUAL")); return; }
    if      (!strcasecmp(m, "AUTO"))   { manualMode = false; }
    else if (!strcasecmp(m, "MANUAL")) { manualMode = true; }
    else { Serial.println(F("ERR usage MODE AUTO|MANUAL")); return; }
    printConfig();
    return;
  }

  if (!strcasecmp(cmd, "LED")) {
    char *s = strtok(NULL, " ");
    if (s && !strcasecmp(s, "AUTO")) { manualMode = false; Serial.println(F("OK LED AUTO")); return; }
    bool on;
    if (!onOff(s, &on)) { Serial.println(F("ERR usage LED ON|OFF|AUTO")); return; }
    if (!manualMode) { Serial.println(F("ERR set MODE MANUAL first")); return; }
    manualLed = on;
    digitalWrite(ledPin, on ? HIGH : LOW);
    Serial.print(F("OK LED ")); Serial.println(on ? F("ON") : F("OFF"));
    return;
  }

  if (!strcasecmp(cmd, "BUZZ")) {
    char *f  = strtok(NULL, " ");
    char *ms = strtok(NULL, " ");
    if (!f || !ms) { Serial.println(F("ERR usage BUZZ <freq> <ms>")); return; }
    int freq = atoi(f);
    int dur  = atoi(ms);
    if (freq < 31 || freq > 5000) { Serial.println(F("ERR freq out of range 31..5000")); return; }
    if (dur  < 1  || dur  > 5000) { Serial.println(F("ERR ms out of range 1..5000")); return; }
    oneShotUntil = millis() + dur;
    tone(buzzerPin, freq);
    Serial.println(F("OK BUZZ"));
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

  if (!strcasecmp(cmd, "SAVE")) {
    cfg.magic = EEPROM_MAGIC;
    EEPROM.put(EEPROM_ADDR, cfg);
    Serial.println(F("OK SAVED"));
    return;
  }

  if (!strcasecmp(cmd, "RESET")) {
    uint8_t wasMuted = cfg.muted;
    loadDefaults();
    cfg.muted = wasMuted;
    printConfig();
    return;
  }

  Serial.println(F("ERR unknown command"));
}

void serviceSerial() {
  while (Serial.available()) {
    char c = Serial.read();
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

// key=value, not positional - the dashboard reads the keys, so this is what
// lets one Live view serve every project. Copy this shape into new sketches.
void sendTelemetry(unsigned long now) {
  Serial.print(F("T "));
  Serial.print(now);
  Serial.print(F(" distance="));
  Serial.print(distance, 1);
  Serial.print(F(" zone="));
  Serial.println(zoneName(zone));
}

// ---- main loop -----------------------------------------------------------

void loop() {
  unsigned long now = millis();

  serviceSerial();

  if (now - lastMeasure >= MEASURE_MS) {
    lastMeasure = now;
    measure();
    if (zone != lastZone) { lastZone = zone; beepPhaseAt = now; beepAudible = false; noTone(buzzerPin); }
    applyZone();
  }

  serviceBuzzer(now);

  if (now - lastDisplay >= DISPLAY_MS) { lastDisplay = now; drawDisplay(); }
  if (now - lastTelem   >= TELEMETRY_MS) { lastTelem = now; sendTelemetry(now); }
}
