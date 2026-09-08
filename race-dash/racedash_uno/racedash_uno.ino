// racedash_uno.ino - character LCD + 0.91" OLED racing dashboard, Arduino Uno.
//
// The Uno build of racedash8266. Same serial protocol, same layouts, same
// behaviour - so forza_bridge.py / sim_bridge.py drive it without knowing or
// caring which board is plugged in.
//
//   LCD          -> gear, speed, RPM, rev bar, pedals   (the main readout)
//   0.91" OLED   -> gear, RPM, shift bar                (A4/A5)
//
// Differences from the ESP8266 build, all forced by the hardware:
//   * The Uno's I2C is fixed to A4/A5 and cannot be moved, so the OLED lives
//     there and the LCD is wired in parallel. (With an I2C backpack both share
//     A4/A5 quite happily - different addresses, no clash.)
//   * 2 KB of RAM against the ESP8266's 80 KB. The 128x32 frame buffer alone
//     is 512 bytes, so GET reports free memory; below ~200 bytes expect random
//     resets.
//   * No WiFi radio to switch off, and the serial buffer is fixed at 64 bytes.
//
// ===========================================================================
//  CONFIGURE THESE THREE LINES TO MATCH YOUR SCREEN, THEN FLASH
// ===========================================================================

#define LCD_COLS   16     // 16 or 20  - characters per row
#define LCD_ROWS    2     // 2  or 4   - number of rows
#define LCD_I2C     0     // 0 = wire the 16 pins directly (see below)
                          // 1 = an I2C backpack is soldered to the LCD

// Direct wiring only (LCD_I2C 0). These are the pin numbers printed on the
// Uno's header. RW goes to GND; D0 and D1 are left alone because they are the
// USB serial link this whole project runs on.
//
//   LCD RS -> 12      LCD D4 -> 5
//   LCD E  -> 11      LCD D5 -> 4
//                     LCD D6 -> 3
//                     LCD D7 -> 2
#define PIN_RS 12
#define PIN_EN 11
#define PIN_D4  5
#define PIN_D5  4
#define PIN_D6  3
#define PIN_D7  2

// ---- analogue gauges (optional) -------------------------------------------
//
// Servos swinging needles across printed dials. Set either ENABLED to 0 if you
// have not built that gauge; its pin is then left completely alone, and with
// both at 0 no servo code runs at all.
//
// Gauges are set in MICROSECONDS of servo pulse, not degrees.
//
// Servo.write(degrees) only ever produces pulses between 544 and 2400us, and
// on a cheap servo that is nowhere near its real travel - which is why the
// needles were landing about 87% of the way up the dial however wide the angle
// range was set. Driving the pulse directly reaches the rest of it.
//
// ZERO_US is where the needle rests at zero. FULL_US is the far end.
//   * needle stops short of full scale -> move FULL_US further from ZERO_US
//   * needle strains or buzzes at the end -> move FULL_US back toward ZERO_US
// Roughly 10us is one degree. Reversed gauges simply have ZERO_US larger than
// FULL_US, which is the case for both servos here.
// TRIM_US slides the whole scale, zero included. Set it by eye with
// hold.py and the SPEED/RPM TRIM command, then write the value you settled on
// here so it survives a reset - trim otherwise lives only in RAM.
#define TACHO_ENABLED   1
#define TACHO_PIN       9
#define TACHO_ZERO_US 2300
#define TACHO_FULL_US  650    // NOT lower: below this the needle reaches its
                              // mechanical stop and the servo stalls there,
                              // buzzing and refusing to come back until the
                              // revs drop far enough to unload it
#define TACHO_TRIM_US    0

#define SPEEDO_ENABLED   1
#define SPEEDO_PIN      10
#define SPEEDO_ZERO_US 2300
#define SPEEDO_FULL_US  400
#define SPEEDO_TRIM_US -87    // measured against the 100mph mark

// The widest pulses the library will pass through. attach() is given these so
// writeMicroseconds() is not silently clamped back to 544-2400. The floor is
// below 400 on purpose: with a negative trim the far end lands under 400, and
// clamping there would quietly cancel the trim exactly where the scale is
// already tightest.
#define SERVO_US_MIN 300
#define SERVO_US_MAX 2600
#define SPEEDO_FULL    350    // needle hits the end at this speed, in whatever
                              // unit the PC is sending (mph by default)

#define SERVO_ENABLED (TACHO_ENABLED || SPEEDO_ENABLED)

// ===========================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#if SERVO_ENABLED
#include <Servo.h>
#endif

#define VERSION "2.1"

#define OLED_ADDR 0x3C

Adafruit_SSD1306 wide(128, 32, &Wire, -1);

// ---- HD44780 driver ------------------------------------------------------
//
// Written out rather than pulled from a library, so one file covers both the
// parallel and the backpack wiring with the same layout code on top.

bool lcdReady = false;

#if LCD_I2C

// Standard backpack mapping (LCM1602 / YwRobot and every clone):
//   P0 = RS   P1 = RW   P2 = EN   P3 = backlight   P4..P7 = D4..D7
uint8_t lcdAddr = 0;
const uint8_t BL_BIT = 0x08;
const uint8_t EN_BIT = 0x04;

void lcdExpanderWrite(uint8_t data) {
  Wire.beginTransmission(lcdAddr);
  Wire.write(data | BL_BIT);
  Wire.endTransmission();
}

void lcdWriteNibble(uint8_t nibbleAndRs) {
  lcdExpanderWrite(nibbleAndRs);
  lcdExpanderWrite(nibbleAndRs | EN_BIT);
  delayMicroseconds(1);
  lcdExpanderWrite(nibbleAndRs & ~EN_BIT);
  delayMicroseconds(50);
}

void lcdSend(uint8_t value, uint8_t rs) {
  lcdWriteNibble((value & 0xF0) | rs);
  lcdWriteNibble((uint8_t)(value << 4) | rs);
}

// A PCF8574 sits in 0x20-0x27 and a PCF8574A in 0x38-0x3F depending on its
// solder jumpers. 0x3C/0x3D are skipped: those are the OLED, not a backpack.
bool lcdProbe() {
  for (uint8_t a = 0x20; a <= 0x3F; a++) {
    if (a > 0x27 && a < 0x38) continue;
    if (a == 0x3C || a == 0x3D) continue;
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { lcdAddr = a; return true; }
  }
  lcdAddr = 0;
  return false;
}

#else   // ---- direct 16-pin wiring ----

void lcdPulse() {
  digitalWrite(PIN_EN, LOW);
  delayMicroseconds(1);
  digitalWrite(PIN_EN, HIGH);
  delayMicroseconds(1);          // the enable pulse must be at least 450ns
  digitalWrite(PIN_EN, LOW);
  delayMicroseconds(50);         // most commands need ~37us to settle
}

void lcdWriteNibble(uint8_t nibble) {
  digitalWrite(PIN_D4, (nibble >> 0) & 1);
  digitalWrite(PIN_D5, (nibble >> 1) & 1);
  digitalWrite(PIN_D6, (nibble >> 2) & 1);
  digitalWrite(PIN_D7, (nibble >> 3) & 1);
  lcdPulse();
}

void lcdSend(uint8_t value, uint8_t rs) {
  digitalWrite(PIN_RS, rs);
  lcdWriteNibble(value >> 4);
  lcdWriteNibble(value & 0x0F);
}

#endif

void lcdCommand(uint8_t c) { lcdSend(c, 0); }
void lcdData(uint8_t c)    { lcdSend(c, 1); }

// Rows 3 and 4 of a 4-line module are not a separate memory block: they are
// the continuation of rows 1 and 2, starting COLS further along.
void lcdSetCursor(uint8_t col, uint8_t row) {
  static const uint8_t base[4] = {0x00, 0x40, 0x00 + LCD_COLS, 0x40 + LCD_COLS};
  lcdCommand(0x80 | (base[row & 3] + col));
}

void lcdCreateChar(uint8_t loc, const uint8_t *rows) {
  lcdCommand(0x40 | ((loc & 0x07) << 3));
  for (uint8_t i = 0; i < 8; i++) lcdData(pgm_read_byte(rows + i));
}

// Rev-bar building blocks: 1 to 4 lit columns of the cell's 5. A fully lit
// cell uses 0xFF, already in the HD44780's character ROM. Held in flash --
// on a 2 KB part, 32 bytes of constants is worth not spending.
static const uint8_t BAR_GLYPH[4][8] PROGMEM = {
  {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
  {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
  {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C},
  {0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E},
};

bool lcdBegin() {
#if LCD_I2C
  if (!lcdProbe()) return false;
  delay(50);
  lcdExpanderWrite(0x00);
  delay(50);
#else
  const uint8_t outs[] = {PIN_RS, PIN_EN, PIN_D4, PIN_D5, PIN_D6, PIN_D7};
  for (uint8_t i = 0; i < 6; i++) { pinMode(outs[i], OUTPUT); digitalWrite(outs[i], LOW); }
  delay(50);                     // HD44780 needs >40ms after power comes up
#endif

  lcdWriteNibble(0x03); delay(5);                // the documented "wake up"
  lcdWriteNibble(0x03); delayMicroseconds(150);  // triple, sent in 8-bit mode
  lcdWriteNibble(0x03); delayMicroseconds(150);
  lcdWriteNibble(0x02);                          // and only now switch to 4-bit

  lcdCommand(LCD_ROWS > 1 ? 0x28 : 0x20);   // 4-bit, two logical lines, 5x8
  lcdCommand(0x08);              // display off
  lcdCommand(0x01); delay(2);    // clear - slow, needs the wait
  lcdCommand(0x06);              // entry mode: advance right, no shift
  lcdCommand(0x0C);              // display on, cursor off, blink off

  for (uint8_t i = 0; i < 4; i++) lcdCreateChar(i + 1, BAR_GLYPH[i]);
  lcdReady = true;
  return true;
}

// A shadow copy of what is on the glass. Only changed cells are rewritten,
// which turns a refresh into a handful of writes instead of a full redraw.
char lcdFrame[LCD_ROWS][LCD_COLS];
char lcdShadow[LCD_ROWS][LCD_COLS];
bool lcdShadowValid = false;

void lcdFlush() {
  for (uint8_t r = 0; r < LCD_ROWS; r++) {
    uint8_t c = 0;
    while (c < LCD_COLS) {
      if (lcdShadowValid && lcdFrame[r][c] == lcdShadow[r][c]) { c++; continue; }
      lcdSetCursor(c, r);
      while (c < LCD_COLS && (!lcdShadowValid || lcdFrame[r][c] != lcdShadow[r][c])) {
        lcdData((uint8_t)lcdFrame[r][c]);
        lcdShadow[r][c] = lcdFrame[r][c];
        c++;
      }
    }
  }
  lcdShadowValid = true;
}

void lcdPut(uint8_t row, int col, const char *s) {
  if (row >= LCD_ROWS) return;
  for (int i = 0; s[i] && col + i < LCD_COLS; i++)
    if (col + i >= 0) lcdFrame[row][col + i] = s[i];
}

// Labels live in flash and are copied out a character at a time, so they never
// occupy RAM. On a 2 KB part that is the difference between fitting and not.
void lcdPut_P(uint8_t row, int col, const char *s) {
  if (row >= LCD_ROWS) return;
  for (int i = 0; ; i++) {
    char c = pgm_read_byte(s + i);
    if (!c) break;
    if (col + i >= 0 && col + i < LCD_COLS) lcdFrame[row][col + i] = c;
  }
}

void lcdCenter_P(uint8_t row, const char *s) {
  int len = strlen_P(s);
  lcdPut_P(row, (LCD_COLS - len) / 2, s);
}

// ---- telemetry state -----------------------------------------------------

int  kmh    = 0;
int  rpm    = 0;
int  maxRpm = 8000;   // never 0, it is a divisor
int  gear   = 0;
int  thr    = 0;      // 0..255
int  brk    = 0;      // 0..255
bool raceOn = false;
bool useMph = true;   // set by the frame's optional 8th field

unsigned long lastFrameMs = 0;
unsigned long frames      = 0;
unsigned long lastDrawMs  = 0;
unsigned long lastLcdMs   = 0;
unsigned long lastBeatMs  = 0;

bool demoMode = false;
bool hasOled  = false;

const unsigned long DRAW_MS  = 50;    // the OLED
const unsigned long LCD_MS   = 120;   // characters do not need more than ~8Hz
const unsigned long STALE_MS = 1500;
const unsigned long BEAT_MS  = 3000;

const float SHIFT_AT = 0.92f;

char    lineBuf[56];
uint8_t lineLen = 0;

// The 128x32 frame buffer is 512 bytes on the heap and the compiler's "global
// variables" figure does not include it, so the board reports what is actually
// left. Below roughly 200 bytes, expect random resets.
int freeRam() {
  extern int __heap_start, *__brkval;
  int here;
  return (int)&here - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// serviceSerial() can stamp lastFrameMs a millisecond or two AFTER loop() took
// its snapshot of millis(), which makes lastFrameMs briefly larger than `now`.
// On unsigned arithmetic (now - lastFrameMs) then wraps to ~4 billion and every
// staleness test fires at once -- the dash blinks WAITING mid-race. Comparing
// as signed makes a frame from the near future read as "0ms ago", not "forever".
bool dataStale(unsigned long now) {
  return (long)(now - lastFrameMs) > (long)STALE_MS;
}

float rpmFraction() {
  if (maxRpm <= 0) return 0.0f;
  float f = (float)rpm / (float)maxRpm;
  return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

#if SPEEDO_ENABLED
float speedFraction() {
  float f = (float)kmh / (float)SPEEDO_FULL;
  return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}
#endif

// ---- analogue rev counter -------------------------------------------------
//
// Telemetry arrives 30 times a second and is never perfectly steady, so
// writing the raw angle straight to the servo makes the needle buzz audibly
// and jitter visibly - the same problem the knob-servo project hit reading a
// noisy pot. Two things fix it:
//
//   * easing, so the needle chases the target instead of snapping to it, which
//     also looks like a real instrument's damped movement
//   * a deadband, so sub-degree wobble never reaches the servo at all
//
// Without the deadband a stationary needle still hunts by a degree either way,
// which is the noise you can hear across a room.

#if SERVO_ENABLED
unsigned long lastServoMs = 0;
uint8_t       servoTurn   = 0;   // which gauge moves on this tick - see loop()

const unsigned long SERVO_MS  = 20;     // a servo cannot use faster than this
const float SERVO_EASE        = 0.35f;  // 0..1, higher = snappier
const int   SERVO_DEADBAND    = 8;      // microseconds, roughly one degree

struct Gauge {
  Servo sv;
  int   zeroUs, fullUs;
  int   trim;       // microseconds added to everything - slides the whole scale
  float pos;        // where the needle actually is, in microseconds
  int   last;       // last pulse written, -1 = never
  int   hold;       // >=0 parks the needle for calibration

  void begin(uint8_t pin, int zUs, int fUs, int trimUs) {
    zeroUs = zUs; fullUs = fUs; trim = trimUs;
    pos = zUs; last = -1; hold = -1;
    // The min/max here are what allow writeMicroseconds() to go beyond the
    // library's usual 544-2400 window.
    sv.attach(pin, SERVO_US_MIN, SERVO_US_MAX);
    write(zUs);
  }

  void write(int us) {
    if (us < SERVO_US_MIN) us = SERVO_US_MIN;
    if (us > SERVO_US_MAX) us = SERVO_US_MAX;
    if (us != last) { sv.writeMicroseconds(us); last = us; }
  }

  // Full travel and back, so the end stops are obvious and you can see at a
  // glance whether the needle is fouling the dial face.
  //
  // Steps toward fullUs whichever side of zeroUs it is on. That matters
  // because a reversed gauge has ZERO greater than FULL, and a fixed
  // ascending loop would simply not run at all in that case.
  void sweep() {
    int step = (fullUs >= zeroUs) ? 25 : -25;
    for (int u = zeroUs; step > 0 ? u <= fullUs : u >= fullUs; u += step) {
      write(u); delay(6);
    }
    delay(120);
    for (int u = fullUs; step > 0 ? u >= zeroUs : u <= zeroUs; u -= step) {
      write(u); delay(6);
    }
    write(zeroUs);
    pos = zeroUs;
  }

  // frac is 0..1 of full scale. With no live data the needle falls to zero
  // rather than freezing wherever the last frame left it - a stuck needle
  // looks exactly like live data.
  void update(float frac, bool live) {
    if (hold >= 0) { write(hold); return; }
    float target = zeroUs + (live ? frac * (fullUs - zeroUs) : 0.0f);
    pos += (target - pos) * SERVO_EASE;
    int us = (int)(pos + 0.5f) + trim;
    if (last < 0 || abs(us - last) >= SERVO_DEADBAND) write(us);
  }

  // Magnitude of the travel, direction kept. Growing the span spreads the
  // scale out; shrinking it pulls the far end back toward zero.
  int span() const { return abs(fullUs - zeroUs); }
  void setSpan(int s) {
    fullUs = (fullUs >= zeroUs) ? zeroUs + s : zeroUs - s;
  }

  // Handles the RPM / SPEED calibration commands. Deliberately a member
  // rather than a free function taking Gauge&: the .ino preprocessor hoists
  // prototypes for free functions above the struct definition, and one taking
  // a Gauge& then fails to compile because the type is not declared yet.
  void command(const char *name) {
    char *s = strtok(NULL, " ");
    if (!s) {
      Serial.print(F("ERR usage ")); Serial.print(name);
      Serial.println(F(" ZERO|FULL|SWEEP|AUTO|TRIM <us>|SPAN <us>|<us>"));
      return;
    }
    // ZERO and FULL park at the configured ends. A bare number is a raw pulse
    // width in microseconds - useful for finding how far the servo will
    // actually go before it strains.
    if (!strcasecmp(s, "ZERO")) {
      hold = zeroUs + trim;
      write(hold);
      Serial.print(F("OK ")); Serial.print(name);
      Serial.print(F(" ZERO at ")); Serial.print(hold); Serial.println(F("us"));
    } else if (!strcasecmp(s, "FULL")) {
      hold = fullUs + trim;
      write(hold);
      Serial.print(F("OK ")); Serial.print(name);
      Serial.print(F(" FULL at ")); Serial.print(hold); Serial.println(F("us"));
    } else if (!strcasecmp(s, "TRIM")) {
      // Slides the WHOLE scale, zero included. Use when every reading is off
      // by the same amount.
      char *v = strtok(NULL, " ");
      if (v) trim = (int)atol(v);
      hold = -1;
      Serial.print(F("OK ")); Serial.print(name);
      Serial.print(F(" TRIM ")); Serial.print(trim); Serial.println(F("us"));
    } else if (!strcasecmp(s, "SPAN")) {
      // Stretches the scale away from zero. Use when zero is right but the
      // far end falls short.
      char *v = strtok(NULL, " ");
      if (v) setSpan((int)atol(v));
      hold = -1;
      Serial.print(F("OK ")); Serial.print(name);
      Serial.print(F(" SPAN ")); Serial.print(span());
      Serial.print(F("us, full now ")); Serial.print(fullUs);
      Serial.println(F("us"));
    } else if (!strcasecmp(s, "AUTO")) {
      hold = -1;
      Serial.print(F("OK ")); Serial.print(name); Serial.println(F(" AUTO"));
    } else if (!strcasecmp(s, "SWEEP")) {
      hold = -1;
      sweep();
      Serial.print(F("OK ")); Serial.print(name);
      Serial.print(F(" SWEEP ")); Serial.print(zeroUs);
      Serial.print(F("us -> ")); Serial.print(fullUs); Serial.println(F("us"));
    } else {
      long v = atol(s);
      if (v < SERVO_US_MIN) v = SERVO_US_MIN;
      if (v > SERVO_US_MAX) v = SERVO_US_MAX;
      hold = (int)v;
      write(hold);
      Serial.print(F("OK ")); Serial.print(name);
      Serial.print(F(" RAW ")); Serial.print(hold);
      Serial.print(F("us (zero ")); Serial.print(zeroUs);
      Serial.print(F(", full ")); Serial.print(fullUs);
      Serial.println(F(")"));
    }
  }
};

#if TACHO_ENABLED
Gauge tacho;
#endif
#if SPEEDO_ENABLED
Gauge speedo;
#endif
#endif

const char *gearText() {
  static char buf[4];
  if (!raceOn)   return "-";
  if (gear < 0)  return "N";
  if (gear == 0) return "R";
  snprintf(buf, sizeof buf, "%d", gear);
  return buf;
}

// ---- LCD layout ----------------------------------------------------------
//
// Each bar cell resolves to a fifth of a character, so a 20-cell bar has 100
// steps rather than 20 and sweeps smoothly instead of stepping a block at a
// time. That is what makes it read like a rev counter, not a progress bar.

void barInto(uint8_t row, int col, int cells, float frac) {
  int sub  = (int)(frac * (cells * 5) + 0.5f);
  int full = sub / 5, rem = sub % 5;
  for (int i = 0; i < cells; i++) {
    char ch = ' ';
    if      (i < full)         ch = (char)0xFF;   // solid block
    else if (i == full && rem) ch = (char)rem;    // custom glyph 1..4
    lcdFrame[row][col + i] = ch;
  }
}

void lcdBuildFrame(bool blink) {
  memset(lcdFrame, ' ', sizeof lcdFrame);

  if (dataStale(millis())) {
#if LCD_ROWS >= 4
    lcdCenter_P(0, PSTR("==== RACE DASH ===="));
    lcdCenter_P(1, PSTR("WAITING FOR DATA"));
    lcdCenter_P(3, PSTR("run sim_bridge.py"));
#else
    lcdCenter_P(0, PSTR("== RACE DASH =="));
    lcdCenter_P(1, PSTR("WAITING FOR DATA"));
#endif
    return;
  }

  bool shift = rpmFraction() >= SHIFT_AT && raceOn;
  char buf[24];

#if LCD_ROWS >= 4
  // ---- 20x4: rev bar / gear + speed / revs / pedals ----
  //
  //   0  ####################
  //   1  GEAR 5        133MPH
  //   2  RPM  6820  MAX  8000
  //   3  THR###### BRK#3
  if (shift && blink) lcdCenter_P(0, PSTR(">>>>> SHIFT UP <<<<<"));
  else                barInto(0, 0, LCD_COLS, rpmFraction());

  snprintf(buf, sizeof buf, "GEAR %s", gearText());
  lcdPut(1, 0, buf);
  snprintf(buf, sizeof buf, "%3d%s", kmh, useMph ? "MPH" : "KMH");
  lcdPut(1, LCD_COLS - (int)strlen(buf), buf);

  snprintf(buf, sizeof buf, "RPM %5d", rpm > 99999 ? 99999 : rpm);
  lcdPut(2, 0, buf);
  snprintf(buf, sizeof buf, "MAX %5d", maxRpm > 99999 ? 99999 : maxRpm);
  lcdPut(2, LCD_COLS - (int)strlen(buf), buf);

  lcdPut_P(3, 0, PSTR("THR"));
  barInto(3, 3, 6, thr / 255.0f);
  lcdPut_P(3, 10, PSTR("BRK"));
  barInto(3, 13, 6, brk / 255.0f);

#else
  // ---- 16x2: gear + rev bar / speed + revs ----
  //
  //   0  G5 ###########
  //   1  133MPH  6820RPM
  char g[4];
  if      (!raceOn)   strcpy(g, "-  ");
  else if (gear < 0)  strcpy(g, "N  ");
  else if (gear == 0) strcpy(g, "R  ");
  else                snprintf(g, sizeof g, "G%-2d", gear);
  memcpy(lcdFrame[0], g, 3);

  if (shift && blink) lcdPut_P(0, 3, PSTR("  SHIFT UP!  "));
  else                barInto(0, 3, LCD_COLS - 3, rpmFraction());

  // Both unit words are three characters, so the row stays a fixed width.
  snprintf(buf, sizeof buf, "%3d%s  %4dRPM",
           kmh, useMph ? "MPH" : "KMH", rpm > 9999 ? 9999 : rpm);
  lcdPut(1, 0, buf);
#endif
}

// ---- 0.91" OLED ----------------------------------------------------------
//
// Seven-segment bits: a=0 b=1 c=2 d=3 e=4 f=5 g=6. A cell of height h and
// thickness t splits as t + half + t + half + t, so h = 3t + 2*half lands
// exactly on the pixel grid.

static const uint8_t SEG_DIGIT[10] PROGMEM = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
static const uint8_t SEG_DASH = 0x40;  // just the middle bar
static const uint8_t SEG_P    = 0x73;  // the body of an R, before its leg
static const uint8_t SEG_N    = 0x37;  // a,b,c,e,f - the usual seven-seg N

uint8_t segDigit(int d) { return pgm_read_byte(&SEG_DIGIT[d]); }

void drawSeg(Adafruit_SSD1306 &d, int x, int y, int w, int h, int t, uint8_t m) {
  int half = (h - 3 * t) / 2;
  const uint16_t C = SSD1306_WHITE;
  if (m & 0x01) d.fillRect(x + t,     y,                w - 2 * t, t,    C);
  if (m & 0x02) d.fillRect(x + w - t, y + t,            t,         half, C);
  if (m & 0x04) d.fillRect(x + w - t, y + 2 * t + half, t,         half, C);
  if (m & 0x08) d.fillRect(x + t,     y + h - t,        w - 2 * t, t,    C);
  if (m & 0x10) d.fillRect(x,         y + 2 * t + half, t,         half, C);
  if (m & 0x20) d.fillRect(x,         y + t,            t,         half, C);
  if (m & 0x40) d.fillRect(x + t,     y + t + half,     w - 2 * t, t,    C);
}

// R is the one glyph seven segments cannot spell. Draw a P and give it a leg.
void drawGlyphR(Adafruit_SSD1306 &d, int x, int y, int w, int h, int t) {
  drawSeg(d, x, y, w, h, t, SEG_P);
  for (int i = 0; i < t; i++)
    d.drawLine(x + w / 2 + i, y + h / 2, x + w - t + i, y + h - 1, SSD1306_WHITE);
}

void drawGearGlyph(Adafruit_SSD1306 &d, int x, int y, int w, int h, int t) {
  if (!raceOn)   { drawSeg(d, x, y, w, h, t, SEG_DASH); return; }
  if (gear < 0)  { drawSeg(d, x, y, w, h, t, SEG_N);    return; }
  if (gear == 0) { drawGlyphR(d, x, y, w, h, t);        return; }
  if (gear <= 9) { drawSeg(d, x, y, w, h, t, segDigit(gear)); return; }

  int hw = (w - 2) / 2;
  drawSeg(d, x,          y, hw, h, t, segDigit((gear / 10) % 10));
  drawSeg(d, x + hw + 2, y, hw, h, t, segDigit(gear % 10));
}

void drawRpmBar(Adafruit_SSD1306 &d, int x, int y, int w, int h,
                float frac, bool shift, bool blinkOn) {
  d.drawRect(x, y, w, h, SSD1306_WHITE);
  const int segW = 3, gap = 1;
  int n   = ((w - 4) + gap) / (segW + gap);
  int lit = (int)(frac * n + 0.5f);
  int red = (n * 88) / 100;
  for (int i = 0; i < n; i++) {
    int sx = x + 2 + i * (segW + gap);
    if (i < lit)       d.fillRect(sx, y + 2, segW, h - 4, SSD1306_WHITE);
    else if (i >= red) d.drawFastVLine(sx + segW / 2, y + 2, h - 4, SSD1306_WHITE);
  }
  // INVERSE flips what is already there, so lit segments go dark and the gaps
  // light up - it reads as a strobe rather than a flicker.
  if (shift && blinkOn) d.fillRect(x, y, w, h, SSD1306_INVERSE);
}

void drawOled(bool blinkOn) {
  if (!hasOled) return;
  bool shift = rpmFraction() >= SHIFT_AT && raceOn;

  wide.clearDisplay();
  wide.setTextColor(SSD1306_WHITE);

  drawRpmBar(wide, 0, 0, 128, 8, rpmFraction(), shift, blinkOn);
  drawGearGlyph(wide, 2, 9, 22, 22, 4);
  wide.drawFastVLine(29, 9, 22, SSD1306_WHITE);

  wide.setTextSize(1);
  wide.setCursor(34, 10);
  wide.print(shift && blinkOn ? F("SHIFT") : F("RPM"));

  char buf[8];
  snprintf(buf, sizeof buf, "%d", rpm < 0 ? 0 : (rpm > 99999 ? 99999 : rpm));
  wide.setTextSize(2);
  wide.setCursor(126 - (int)strlen(buf) * 12, 16);
  wide.print(buf);

  wide.display();
}

void drawOledWaiting() {
  if (!hasOled) return;
  wide.clearDisplay();
  wide.setTextColor(SSD1306_WHITE);
  wide.setTextSize(2);
  wide.setCursor(2, 1);
  wide.print(F("NO DATA"));
  wide.setTextSize(1);
  wide.setCursor(2, 20);
  wide.print(F("serial idle"));
  wide.display();
}

// Every element on both screens at once, so a dead row, a bad wire or a
// mis-set LCD_COLS shows up before the rest of the chain exists.
void selfTest() {
  int  sKmh = kmh, sRpm = rpm, sGear = gear, sThr = thr, sBrk = brk;
  bool sRace = raceOn;
  unsigned long sFrame = lastFrameMs;

  kmh = 179; rpm = 7400; maxRpm = 8000; gear = 6;
  thr = 255; brk = 64;   raceOn = true;
  lastFrameMs = millis();

  if (lcdReady) { lcdBuildFrame(false); lcdFlush(); }
  drawOled(true);

  // Swept one after the other, never together: two servos accelerating at the
  // same instant is the biggest current spike this board will ever ask for.
  // It also doubles as the pause the screens need to be read.
#if TACHO_ENABLED
  tacho.sweep();
#endif
#if SPEEDO_ENABLED
  speedo.sweep();
#endif
#if !SERVO_ENABLED
  delay(2500);
#endif

  kmh = sKmh; rpm = sRpm; gear = sGear; thr = sThr; brk = sBrk;
  raceOn = sRace; lastFrameMs = sFrame;
}

// ---- serial --------------------------------------------------------------

void printState() {
  Serial.print(F("OK STATE kmh="));  Serial.print(kmh);
  Serial.print(F(" rpm="));          Serial.print(rpm);
  Serial.print(F(" maxrpm="));       Serial.print(maxRpm);
  Serial.print(F(" gear="));         Serial.print(gear);
  Serial.print(F(" race="));         Serial.print(raceOn ? 1 : 0);
  Serial.print(F(" unit="));         Serial.print(useMph ? F("mph") : F("kmh"));
  Serial.print(F(" lcd="));          Serial.print(lcdReady ? F("ok") : F("none"));
  Serial.print(F(" oled="));         Serial.print(hasOled ? F("ok") : F("none"));
#if TACHO_ENABLED
  Serial.print(F(" tacho="));        Serial.print(tacho.last);
  Serial.print(F("us zero="));       Serial.print(tacho.zeroUs);
  Serial.print(F(" full="));         Serial.print(tacho.fullUs);
  Serial.print(F(" trim="));         Serial.print(tacho.trim);
#endif
#if SPEEDO_ENABLED
  Serial.print(F(" speedo="));       Serial.print(speedo.last);
  Serial.print(F("us zero="));       Serial.print(speedo.zeroUs);
  Serial.print(F(" full="));         Serial.print(speedo.fullUs);
  Serial.print(F(" trim="));         Serial.print(speedo.trim);
#endif
  Serial.print(F(" frames="));       Serial.print(frames);
  Serial.print(F(" free="));         Serial.println(freeRam());
}

int clampInt(long v, long lo, long hi) {
  return (int)(v < lo ? lo : (v > hi ? hi : v));
}

// Missing or junk fields are rejected wholesale rather than half-applied, so a
// truncated line can never leave the dash showing a mix of two frames.
void handleFrame() {
  long f[7];
  for (int i = 0; i < 7; i++) {
    char *tok = strtok(NULL, " ");
    if (!tok) { Serial.println(F("ERR frame needs 7 fields")); return; }
    f[i] = atol(tok);
  }

  kmh    = clampInt(f[0], 0, 999);
  rpm    = clampInt(f[1], 0, 99999);
  maxRpm = clampInt(f[2], 1, 99999);
  gear   = clampInt(f[3], -1, 99);   // -1 = neutral (AC, truck sims)
  thr    = clampInt(f[4], 0, 255);
  brk    = clampInt(f[5], 0, 255);
  raceOn = f[6] != 0;

  // Optional 8th field: 1 = mph, 0 = km/h. Absent from older bridges, so its
  // absence must leave the current setting alone rather than resetting it.
  char *units = strtok(NULL, " ");
  if (units) useMph = atol(units) != 0;

  frames++;
  lastFrameMs = millis();
  demoMode = false;              // real data always wins
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;

  if (!strcasecmp(cmd, "D"))    { handleFrame(); return; }
  if (!strcasecmp(cmd, "PING")) { Serial.println(F("OK PONG racedash_uno " VERSION)); return; }
  if (!strcasecmp(cmd, "GET"))  { printState(); return; }

  if (!strcasecmp(cmd, "DEMO")) {
    char *s = strtok(NULL, " ");
    if      (s && !strcasecmp(s, "ON"))  demoMode = true;
    else if (s && !strcasecmp(s, "OFF")) demoMode = false;
    else { Serial.println(F("ERR usage DEMO ON|OFF")); return; }
    Serial.print(F("OK DEMO ")); Serial.println(demoMode ? F("ON") : F("OFF"));
    return;
  }

  if (!strcasecmp(cmd, "TEST")) { selfTest(); Serial.println(F("OK TEST")); return; }

#if TACHO_ENABLED
  if (!strcasecmp(cmd, "RPM"))   { tacho.command("RPM");   return; }
#endif
#if SPEEDO_ENABLED
  if (!strcasecmp(cmd, "SPEED")) { speedo.command("SPEED"); return; }
#endif

  // Re-init the LCD and hold every cell solid for 8 seconds. With the contrast
  // wrong the glass looks blank whatever it is told to show, so a full field of
  // blocks is the only honest way to find the trimpot's working range.
  if (!strcasecmp(cmd, "LCD")) {
    if (lcdBegin()) {
      Serial.println(F("OK LCD ready - all cells solid for 8s"));
      Serial.println(F("# turn the contrast pot until you see them"));
      memset(lcdFrame, 0xFF, sizeof lcdFrame);
      lcdShadowValid = false;
      lcdFlush();
      lastLcdMs = millis() + 8000;
    } else {
      Serial.println(F("ERR LCD did not initialise - check wiring and contrast"));
    }
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

// ---- demo ----------------------------------------------------------------

void serviceDemo(unsigned long now) {
  float t = (now % 12000) / 12000.0f;
  int   g = 1 + (int)(t * 6);
  float within = (t * 6) - (int)(t * 6);

  maxRpm = 8000;
  rpm    = (int)(2200 + within * 5800);
  gear   = g > 6 ? 6 : g;
  kmh    = (int)(t * 177);
  thr    = 210 + (int)(45 * within);
  brk    = 0;
  raceOn = true;
  lastFrameMs = now;
}

// ---- setup / loop --------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();

  // Attached before anything slow, so the needles are under control from the
  // start instead of being flicked about by floating pins.
#if TACHO_ENABLED
  tacho.begin(TACHO_PIN, TACHO_ZERO_US, TACHO_FULL_US, TACHO_TRIM_US);
#endif
#if SPEEDO_ENABLED
  speedo.begin(SPEEDO_PIN, SPEEDO_ZERO_US, SPEEDO_FULL_US, SPEEDO_TRIM_US);
#endif

  Wire.begin();                  // the Uno's I2C is fixed to A4/A5
  Wire.setClock(400000);

  hasOled = wide.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!hasOled) Serial.println(F("ERR 128x32 OLED not found on A4/A5"));

  if (lcdBegin()) {
    Serial.print(F("# LCD ready, "));
    Serial.print(LCD_COLS); Serial.print('x'); Serial.print(LCD_ROWS);
    Serial.println(LCD_I2C ? F(" over I2C backpack") : F(" wired directly"));
  } else {
    Serial.println(F("ERR LCD did not initialise"));
    Serial.println(F("# check wiring and the contrast pot, then send LCD"));
  }

  Serial.print(F("# racedash_uno " VERSION " ready, free="));
  Serial.println(freeRam());
  selfTest();
}

void loop() {
  unsigned long now = millis();

  serviceSerial();

  if (demoMode) serviceDemo(now);

  if (hasOled && now - lastDrawMs >= DRAW_MS) {
    lastDrawMs = now;
    if (dataStale(now)) drawOledWaiting();
    else                drawOled((now / 60) % 2);   // ~8Hz strobe
    // Pushing 512 bytes to the OLED blocks for a while; draining the UART
    // straight afterwards keeps the 64-byte receive buffer from overflowing.
    serviceSerial();
  }

  // The LCD runs on its own slower clock and its own blink rate: 2Hz reads as
  // a deliberate flash where the OLED's 8Hz strobe would just smear.
  if (lcdReady && (long)(now - lastLcdMs) >= (long)LCD_MS) {
    lastLcdMs = now;
    lcdBuildFrame((now / 250) % 2);
    lcdFlush();
  }

#if SERVO_ENABLED
  if (now - lastServoMs >= SERVO_MS) {
    lastServoMs = now;
    bool live = raceOn && !dataStale(now);

    // Only one needle is moved per tick, alternating. Each still updates 25
    // times a second, far smoother than a needle needs, but the two servos
    // never draw their step current in the same instant. On a supply with no
    // reservoir capacitor that halves the worst-case sag on the 5V rail,
    // which is what causes brown-out resets.
    servoTurn ^= 1;
#if TACHO_ENABLED && SPEEDO_ENABLED
    if (servoTurn) tacho.update(rpmFraction(), live);
    else           speedo.update(speedFraction(), live);
#elif TACHO_ENABLED
    tacho.update(rpmFraction(), live);
#elif SPEEDO_ENABLED
    speedo.update(speedFraction(), live);
#endif
  }
#endif

  if (now - lastBeatMs >= BEAT_MS) {
    lastBeatMs = now;
    Serial.print(F("# alive frames=")); Serial.print(frames);
    Serial.print(F(" stale="));         Serial.print(dataStale(now) ? 1 : 0);
    Serial.print(F(" rpm="));           Serial.print(rpm);
#if TACHO_ENABLED
    Serial.print(F(" tacho="));         Serial.print(tacho.last);
#endif
#if SPEEDO_ENABLED
    Serial.print(F(" speedo="));        Serial.print(speedo.last);
#endif
    Serial.print(F(" free="));          Serial.println(freeRam());
  }
}
