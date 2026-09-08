// racedash8266.ino - character LCD + 0.91" OLED racing dashboard, NodeMCU.
//
// The HW-239 128x64 OLED this sketch used to drive has been replaced by a
// 16-pin HD44780 character LCD, mounted above the 0.91" OLED.
//
//   LCD          -> gear, speed, RPM, rev bar, pedals   (the main readout)
//   0.91" OLED   -> gear, RPM, shift bar                (D6/D5, unchanged)
//
// ===========================================================================
//  CONFIGURE THESE FOUR LINES TO MATCH YOUR SCREEN, THEN FLASH
// ===========================================================================

#define LCD_COLS   16     // 16 or 20  - characters per row
#define LCD_ROWS    2     // 2  or 4   - number of rows
#define LCD_I2C     0     // 0 = wire the 16 pins directly (see below)
                          // 1 = an I2C backpack is soldered to the LCD

// Direct wiring only (LCD_I2C 0). NodeMCU label -> GPIO number.
//
//   LCD RS -> D1      LCD D4 -> D3        RW goes to GND (never read back)
//   LCD E  -> D2      LCD D5 -> D4
//                     LCD D6 -> D0
//                     LCD D7 -> D7
//
// D3 and D4 are boot strapping pins, which this project otherwise avoids.
// They are safe *here* and only here: with RW tied to GND the HD44780's data
// pins are permanently inputs, so they cannot hold a strapping pin at the
// wrong level while the NodeMCU boots. A screen module with pull-up resistors
// on those pins would, which is why the OLEDs must never go near them.
// D8 is left clear regardless - nothing can hold it low enough at boot.
#define PIN_RS  5    // D1
#define PIN_EN  4    // D2
#define PIN_D4  0    // D3
#define PIN_D5  2    // D4
#define PIN_D6 16    // D0
#define PIN_D7 13    // D7

// ===========================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>

#define VERSION "2.0"

// The 0.91" OLED keeps its own two pins and its own bus.
#define SDA_OLED 12   // D6
#define SCL_OLED 14   // D5
#define OLED_ADDR 0x3C

Adafruit_SSD1306 wide(128, 32, &Wire, -1);

// With an I2C backpack the LCD and the OLED are on different pin pairs, and
// the ESP8266's single bit-banged bus is re-pointed between them. Wired
// directly the LCD uses no I2C at all, so the bus stays parked on the OLED.
#if LCD_I2C
void useBusLcd()  { Wire.begin(4, 5);                 Wire.setClock(400000); }
void useBusOled() { Wire.begin(SDA_OLED, SCL_OLED);   Wire.setClock(400000); }
#else
void useBusLcd()  {}
void useBusOled() {}
#endif

// ---- HD44780 driver ------------------------------------------------------
//
// Written out rather than pulled from a library: there are several mutually
// incompatible forks of LiquidCrystal_I2C, and this way one file covers both
// the parallel and the backpack wiring with the same layout code on top.

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

// A PCF8574 sits somewhere in 0x20-0x27 and a PCF8574A in 0x38-0x3F depending
// on its solder jumpers; 0x27 and 0x3F are just the most common. 0x3C/0x3D are
// skipped because those are OLED addresses, not backpacks.
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

// Row 3 and 4 of a 4-line module are not a separate memory block: they are the
// continuation of rows 1 and 2, starting COLS further along.
void lcdSetCursor(uint8_t col, uint8_t row) {
  static const uint8_t base[4] = {0x00, 0x40, 0x00 + LCD_COLS, 0x40 + LCD_COLS};
  lcdCommand(0x80 | (base[row & 3] + col));
}

void lcdCreateChar(uint8_t loc, const uint8_t *rows) {
  lcdCommand(0x40 | ((loc & 0x07) << 3));
  for (uint8_t i = 0; i < 8; i++) lcdData(rows[i]);
}

// Rev-bar building blocks: 1 to 4 lit columns of the cell's 5. A fully lit
// cell uses 0xFF, which is already in the HD44780's character ROM.
static const uint8_t BAR_GLYPH[4][8] = {
  {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
  {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
  {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C},
  {0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E},
};

bool lcdBegin() {
#if LCD_I2C
  useBusLcd();
  if (!lcdProbe()) return false;
  delay(50);
  lcdExpanderWrite(0x00);
  delay(50);
#else
  const uint8_t outs[] = {PIN_RS, PIN_EN, PIN_D4, PIN_D5, PIN_D6, PIN_D7};
  for (uint8_t i = 0; i < 6; i++) { pinMode(outs[i], OUTPUT); digitalWrite(outs[i], LOW); }
  delay(50);                     // HD44780 needs >40ms after power comes up
#endif

  lcdWriteNibble(0x03); delay(5);            // the documented "wake up" triple,
  lcdWriteNibble(0x03); delayMicroseconds(150);  // sent in 8-bit mode
  lcdWriteNibble(0x03); delayMicroseconds(150);
  lcdWriteNibble(0x02);                      // and only now switch to 4-bit

  // 0x28 = 4-bit, two logical lines, 5x8 font. A 20x4 module is still "two
  // lines" as far as the controller is concerned.
  lcdCommand(LCD_ROWS > 1 ? 0x28 : 0x20);
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

void lcdCenter(uint8_t row, const char *s) {
  int len = (int)strlen(s);
  lcdPut(row, (LCD_COLS - len) / 2, s);
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

const unsigned long DRAW_MS  = 45;    // the OLED
const unsigned long LCD_MS   = 120;   // characters do not need more than ~8Hz
const unsigned long STALE_MS = 1500;
const unsigned long BEAT_MS  = 3000;

const float SHIFT_AT = 0.92f;

char    lineBuf[64];
uint8_t lineLen = 0;

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
// time. That is what makes it read like a rev counter and not a progress bar.

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
    lcdCenter(0, "==== RACE DASH ====");
    lcdCenter(1, "WAITING FOR DATA");
    lcdCenter(3, "run sim_bridge.py");
#else
    lcdCenter(0, "== RACE DASH ==");
    lcdCenter(1, "WAITING FOR DATA");
#endif
    return;
  }

  bool shift = rpmFraction() >= SHIFT_AT && raceOn;
  char buf[24];

#if LCD_ROWS >= 4
  // ---- 20x4: rev bar / gear + speed / revs / pedals ----
  //
  //   0  ################|||
  //   1  GEAR 5     214MPH
  //   2  RPM   6820 MAX  7800
  //   3  THR ###### BRK #
  if (shift && blink) lcdCenter(0, ">>>>> SHIFT UP <<<<<");
  else                barInto(0, 0, LCD_COLS, rpmFraction());

  snprintf(buf, sizeof buf, "GEAR %s", gearText());
  lcdPut(1, 0, buf);
  snprintf(buf, sizeof buf, "%3d%s", kmh, useMph ? "MPH" : "KMH");
  lcdPut(1, LCD_COLS - (int)strlen(buf), buf);

  snprintf(buf, sizeof buf, "RPM %5d", rpm > 99999 ? 99999 : rpm);
  lcdPut(2, 0, buf);
  snprintf(buf, sizeof buf, "MAX %5d", maxRpm > 99999 ? 99999 : maxRpm);
  lcdPut(2, LCD_COLS - (int)strlen(buf), buf);

  lcdPut(3, 0, "THR");
  barInto(3, 3, 6, thr / 255.0f);
  lcdPut(3, 10, "BRK");
  barInto(3, 13, 6, brk / 255.0f);

#else
  // ---- 16x2: gear + rev bar / speed + revs ----
  //
  //   0  G5 ###########
  //   1  214MPH  6820RPM
  char g[4];
  if      (!raceOn)   strcpy(g, "-  ");
  else if (gear < 0)  strcpy(g, "N  ");
  else if (gear == 0) strcpy(g, "R  ");
  else                snprintf(g, sizeof g, "G%-2d", gear);
  memcpy(lcdFrame[0], g, 3);

  if (shift && blink) lcdPut(0, 3, "  SHIFT UP!  ");
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

static const uint8_t SEG_DIGIT[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
static const uint8_t SEG_DASH = 0x40;  // just the middle bar
static const uint8_t SEG_P    = 0x73;  // the body of an R, before its leg
static const uint8_t SEG_N    = 0x37;  // a,b,c,e,f - the usual seven-seg N

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
  if (gear <= 9) { drawSeg(d, x, y, w, h, t, SEG_DIGIT[gear]); return; }

  int hw = (w - 2) / 2;
  drawSeg(d, x,          y, hw, h, t, SEG_DIGIT[(gear / 10) % 10]);
  drawSeg(d, x + hw + 2, y, hw, h, t, SEG_DIGIT[gear % 10]);
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

  useBusOled();
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
  useBusOled();
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

  if (lcdReady) { lcdBuildFrame(false); useBusLcd(); lcdFlush(); }
  drawOled(true);
  delay(2500);

  kmh = sKmh; rpm = sRpm; gear = sGear; thr = sThr; brk = sBrk;
  raceOn = sRace; lastFrameMs = sFrame;
}

// ---- serial --------------------------------------------------------------

void printState() {
  Serial.print(F("OK STATE kmh="));  Serial.print(kmh);
  Serial.print(F(" rpm="));          Serial.print(rpm);
  Serial.print(F(" maxrpm="));       Serial.print(maxRpm);
  Serial.print(F(" gear="));         Serial.print(gear);
  Serial.print(F(" thr="));          Serial.print(thr);
  Serial.print(F(" brk="));          Serial.print(brk);
  Serial.print(F(" race="));         Serial.print(raceOn ? 1 : 0);
  Serial.print(F(" unit="));         Serial.print(useMph ? F("mph") : F("kmh"));
  Serial.print(F(" lcd="));          Serial.print(lcdReady ? F("ok") : F("none"));
  Serial.print(F(" oled="));         Serial.print(hasOled ? F("ok") : F("none"));
  Serial.print(F(" frames="));       Serial.print(frames);
  Serial.print(F(" freeheap="));     Serial.println(ESP.getFreeHeap());
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

void serviceDemo(unsigned long now);

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;

  if (!strcasecmp(cmd, "D"))    { handleFrame(); return; }
  if (!strcasecmp(cmd, "PING")) { Serial.println(F("OK PONG racedash8266 " VERSION)); return; }
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

  // Re-init the LCD and hold every cell solid for 8 seconds. With the contrast
  // wrong the glass looks blank whatever it is told to show, so a full field of
  // blocks is the only honest way to find the trimpot's working range.
  if (!strcasecmp(cmd, "LCD")) {
    if (lcdBegin()) {
      Serial.println(F("OK LCD ready - both rows solid for 8s"));
      Serial.println(F("# turn the contrast pot until you see them"));
      memset(lcdFrame, 0xFF, sizeof lcdFrame);
      lcdShadowValid = false;
      useBusLcd();
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
  // Never used here, but the ESP8266 powers its radio at boot regardless, and
  // the background interrupts jitter both the bit-banged I2C and the UART.
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  Serial.setRxBufferSize(512);
  Serial.begin(115200);
  delay(200);
  Serial.println();

  Wire.begin(SDA_OLED, SCL_OLED);
  Wire.setClock(400000);

  // periphBegin must be false: left at its default the library calls
  // Wire.begin() with no pins and undoes the mapping above.
  hasOled = wide.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false);
  if (!hasOled) Serial.println(F("ERR 128x32 OLED not found on D6/D5"));

  if (lcdBegin()) {
    Serial.print(F("# LCD ready, "));
    Serial.print(LCD_COLS); Serial.print('x'); Serial.print(LCD_ROWS);
    Serial.println(LCD_I2C ? F(" over I2C backpack") : F(" wired directly"));
  } else {
    Serial.println(F("ERR LCD did not initialise"));
    Serial.println(F("# check wiring and the contrast pot, then send LCD"));
  }

  Serial.println(F("# racedash8266 " VERSION " ready"));
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
  }

  // The LCD runs on its own slower clock and its own blink rate: 2Hz reads as
  // a deliberate flash where the OLED's 8Hz strobe would just smear.
  if (lcdReady && (long)(now - lastLcdMs) >= (long)LCD_MS) {
    lastLcdMs = now;
    lcdBuildFrame((now / 250) % 2);
    useBusLcd();
    lcdFlush();
  }

  if (now - lastBeatMs >= BEAT_MS) {
    lastBeatMs = now;
    Serial.print(F("# alive frames=")); Serial.print(frames);
    Serial.print(F(" stale="));         Serial.println(dataStale(now) ? 1 : 0);
  }

  yield();
}
