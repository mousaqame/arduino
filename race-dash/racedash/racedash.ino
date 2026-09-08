// racedash.ino — two-OLED racing dashboard for a classic ESP32 (WROOM-32).
//
// Bus 0 (Wire , GPIO21 / GPIO22) -> HW-239, 128x64 : big speed readout
// Bus 1 (Wire1, GPIO25 / GPIO26) -> 0.91", 128x32  : gear + RPM + shift bar
//
// Both panels are SSD1306 at address 0x3C and neither can be changed without
// moving a resistor. They do not need changing: the ESP32 has two independent
// hardware I2C controllers, so each panel gets a bus to itself and the shared
// address never collides. That is the whole reason this project is not on an
// Uno — see README.md.
//
// The PC does the telemetry work. forza_bridge.py listens for Forza Horizon 5
// "Data Out" UDP packets, pulls out three numbers, and streams them here over
// USB serial. This sketch only draws.
//
// Serial in (115200 baud, newline terminated, case-insensitive):
//   D <kmh> <rpm> <maxrpm> <gear> <thr> <brk> <race>   telemetry frame
//   PING              -> OK PONG racedash <ver>
//   GET               -> OK current state
//   DEMO ON|OFF       -> fake sweep, so the screens can be tested with no PC
//   BRIGHT <0-255>    -> panel contrast
//   TEST              -> redraw the boot self-test
//
// Serial out: replies are "OK ..." / "ERR ...", and "# ..." for informational
// lines. Same convention as the parking sensor, so the same log readers work.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define VERSION "1.0"

// ---- pins ----------------------------------------------------------------
// Bus 0 is the ESP32's default I2C pair. Bus 1 has no default on this chip,
// so any free GPIO works; 25/26 are chosen because they are next to each other
// on the header and are not strapping pins.
#define SDA0 21
#define SCL0 22
#define SDA1 25
#define SCL1 26

#define OLED_ADDR 0x3C

// The 4th/5th constructor arguments are the I2C clock during and after a
// transfer. Both are pinned at 400 kHz — the default drops back to 100 kHz
// between transfers, which halves the frame rate for no benefit here.
Adafruit_SSD1306 big(128, 64, &Wire, -1, 400000UL, 400000UL);
Adafruit_SSD1306 wide(128, 32, &Wire1, -1, 400000UL, 400000UL);

// ---- telemetry state -----------------------------------------------------

int  kmh     = 0;
int  rpm     = 0;
int  maxRpm  = 8000;   // never 0, it is a divisor
int  gear    = 0;
int  thr     = 0;      // 0..255
int  brk     = 0;      // 0..255
bool raceOn  = false;
bool useMph  = true;   // set by the frame's optional 8th field

unsigned long lastFrameMs = 0;      // when a D line last arrived
unsigned long frames      = 0;      // D lines accepted
unsigned long lastDrawMs  = 0;
unsigned long lastBeatMs  = 0;

bool demoMode = false;

const unsigned long DRAW_MS  = 33;    // ~30 fps, about what two panels sustain
const unsigned long STALE_MS = 1500;  // no data for this long -> "waiting"
const unsigned long BEAT_MS  = 3000;  // heartbeat line to the PC

// Shift light comes on at this fraction of max RPM.
const float SHIFT_AT = 0.92f;

char    lineBuf[64];
uint8_t lineLen = 0;

// ---- seven-segment renderer ----------------------------------------------
//
// Bit order: a=0 b=1 c=2 d=3 e=4 f=5 g=6
//
//    aaaa
//   f    b
//   f    b
//    gggg
//   e    c
//   e    c
//    dddd
//
// A cell of height h with thickness t splits as t + half + t + half + t,
// so pick h = 3*t + 2*half to land exactly on the pixel grid.

static const uint8_t SEG_DIGIT[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static const uint8_t SEG_DASH = 0x40;  // just the middle bar
static const uint8_t SEG_P    = 0x73;  // the body of an R, before its leg
static const uint8_t SEG_N    = 0x37;  // a,b,c,e,f - the usual seven-seg N

void drawSeg(Adafruit_SSD1306 &d, int x, int y, int w, int h, int t, uint8_t m) {
  int half = (h - 3 * t) / 2;
  const uint16_t C = SSD1306_WHITE;
  if (m & 0x01) d.fillRect(x + t,     y,              w - 2 * t, t,    C);  // a
  if (m & 0x02) d.fillRect(x + w - t, y + t,          t,         half, C);  // b
  if (m & 0x04) d.fillRect(x + w - t, y + 2 * t + half, t,       half, C);  // c
  if (m & 0x08) d.fillRect(x + t,     y + h - t,      w - 2 * t, t,    C);  // d
  if (m & 0x10) d.fillRect(x,         y + 2 * t + half, t,       half, C);  // e
  if (m & 0x20) d.fillRect(x,         y + t,          t,         half, C);  // f
  if (m & 0x40) d.fillRect(x + t,     y + t + half,   w - 2 * t, t,    C);  // g
}

// R is the one glyph seven segments cannot spell. Draw a P and give it a leg.
void drawGlyphR(Adafruit_SSD1306 &d, int x, int y, int w, int h, int t) {
  drawSeg(d, x, y, w, h, t, SEG_P);
  for (int i = 0; i < t; i++)
    d.drawLine(x + w / 2 + i, y + h / 2, x + w - t + i, y + h - 1, SSD1306_WHITE);
}

// One gear position, sized to fit the cell it is given.
void drawGearGlyph(Adafruit_SSD1306 &d, int x, int y, int w, int h, int t) {
  if (!raceOn)            { drawSeg(d, x, y, w, h, t, SEG_DASH); return; }
  if (gear < 0)           { drawSeg(d, x, y, w, h, t, SEG_N);    return; }   // neutral
  if (gear == 0)          { drawGlyphR(d, x, y, w, h, t);        return; }
  if (gear >= 1 && gear <= 9) { drawSeg(d, x, y, w, h, t, SEG_DIGIT[gear]); return; }

  // 10th gear and up: two half-width digits sharing the same cell.
  int hw = (w - 2) / 2;
  drawSeg(d, x,          y, hw, h, t, SEG_DIGIT[(gear / 10) % 10]);
  drawSeg(d, x + hw + 2, y, hw, h, t, SEG_DIGIT[gear % 10]);
}

// ---- shared widgets ------------------------------------------------------

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

// Segmented rev bar. The top 12% is marked with ticks even when unlit, so the
// redline is visible at a glance instead of only once you have hit it.
void drawRpmBar(Adafruit_SSD1306 &d, int x, int y, int w, int h,
                float frac, bool shift, bool blinkOn) {
  d.drawRect(x, y, w, h, SSD1306_WHITE);

  const int segW = 3, gap = 1;
  int inner = w - 4;
  int n     = (inner + gap) / (segW + gap);
  int lit   = (int)(frac * n + 0.5f);
  int red   = (n * 88) / 100;

  for (int i = 0; i < n; i++) {
    int sx = x + 2 + i * (segW + gap);
    if (i < lit)        d.fillRect(sx, y + 2, segW, h - 4, SSD1306_WHITE);
    else if (i >= red)  d.drawFastVLine(sx + segW / 2, y + 2, h - 4, SSD1306_WHITE);
  }

  // Flash the whole bar in the shift zone. INVERSE flips what is already there,
  // so the lit segments go dark and the gaps light up — reads as a strobe.
  if (shift && blinkOn) d.fillRect(x, y, w, h, SSD1306_INVERSE);
}

void drawPedalBar(Adafruit_SSD1306 &d, int x, int y, int w, int h, int value) {
  d.drawRect(x, y, w, h, SSD1306_WHITE);
  int fill = ((w - 2) * value) / 255;
  if (fill > 0) d.fillRect(x + 1, y + 1, fill, h - 2, SSD1306_WHITE);
}

const char *gearLabel() {
  static char buf[4];
  if (!raceOn)   return "-";
  if (gear < 0)  return "N";
  if (gear == 0) return "R";
  snprintf(buf, sizeof buf, "%d", gear);
  return buf;
}

// ---- the two panels ------------------------------------------------------

// 128x64 — speed is the whole point of this screen, so it gets 37 of the 64 rows.
void drawBig(bool blinkOn) {
  bool shift = rpmFraction() >= SHIFT_AT && raceOn;

  big.clearDisplay();
  big.setTextColor(SSD1306_WHITE);

  big.setTextSize(1);
  big.setCursor(0, 0);
  big.print(F("SPEED"));
  big.setCursor(78, 0);
  big.print(F("GEAR "));
  big.print(gearLabel());
  big.drawFastHLine(0, 9, 128, SSD1306_WHITE);

  const int DY = 13, DH = 37, DW = 30, DT = 5;
  int v = kmh < 0 ? 0 : (kmh > 999 ? 999 : kmh);
  int d100 = v / 100, d10 = (v / 10) % 10, d1 = v % 10;

  if (d100)           drawSeg(big,  0, DY, DW, DH, DT, SEG_DIGIT[d100]);
  if (d100 || d10)    drawSeg(big, 33, DY, DW, DH, DT, SEG_DIGIT[d10]);
  drawSeg(big, 66, DY, DW, DH, DT, SEG_DIGIT[d1]);

  // Right-aligned against x=126 so the label stays put when MPH (3 characters)
  // swaps for KM/H (4).
  big.setTextSize(1);
  const char *unit = useMph ? "MPH" : "KM/H";
  big.setCursor(126 - (int)strlen(unit) * 6, 42);
  big.print(unit);

  drawRpmBar(big, 0, 51, 128, 8, rpmFraction(), shift, blinkOn);

  drawPedalBar(big,  0, 60, 62, 4, thr);
  drawPedalBar(big, 66, 60, 62, 4, brk);

  big.display();
}

// 128x32 — rev bar across the top, gear on the left, RPM digits on the right.
void drawWide(bool blinkOn) {
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

void drawWaiting() {
  big.clearDisplay();
  big.setTextColor(SSD1306_WHITE);
  big.setTextSize(2);
  big.setCursor(4, 6);
  big.print(F("WAITING"));
  big.setTextSize(1);
  big.setCursor(4, 30);
  big.print(F("no telemetry on USB"));
  big.setCursor(4, 42);
  big.print(F("run forza_bridge.py"));
  big.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  big.display();

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

// Every element on both panels at once, so a dead row or a swapped cable shows
// up before any of the rest of the chain exists.
void selfTest() {
  int savedKmh = kmh, savedRpm = rpm, savedGear = gear;
  int savedThr = thr, savedBrk = brk;
  bool savedRace = raceOn;

  kmh = 288; rpm = 7400; maxRpm = 8000; gear = 6;
  thr = 255; brk = 64;   raceOn = true;
  drawBig(true);
  drawWide(true);
  delay(2500);

  kmh = savedKmh; rpm = savedRpm; gear = savedGear;
  thr = savedThr; brk = savedBrk; raceOn = savedRace;
}

// ---- serial --------------------------------------------------------------

void printState() {
  Serial.print(F("OK STATE kmh="));   Serial.print(kmh);
  Serial.print(F(" rpm="));           Serial.print(rpm);
  Serial.print(F(" maxrpm="));        Serial.print(maxRpm);
  Serial.print(F(" gear="));          Serial.print(gear);
  Serial.print(F(" thr="));           Serial.print(thr);
  Serial.print(F(" brk="));           Serial.print(brk);
  Serial.print(F(" race="));          Serial.print(raceOn ? 1 : 0);
  Serial.print(F(" demo="));          Serial.print(demoMode ? 1 : 0);
  Serial.print(F(" frames="));        Serial.println(frames);
}

int clampInt(long v, long lo, long hi) {
  return (int)(v < lo ? lo : (v > hi ? hi : v));
}

// D <kmh> <rpm> <maxrpm> <gear> <thr> <brk> <race>
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
  demoMode = false;   // real data always wins
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;

  if (!strcasecmp(cmd, "D")) { handleFrame(); return; }

  if (!strcasecmp(cmd, "PING")) {
    Serial.println(F("OK PONG racedash " VERSION));
    return;
  }

  if (!strcasecmp(cmd, "GET")) { printState(); return; }

  if (!strcasecmp(cmd, "DEMO")) {
    char *s = strtok(NULL, " ");
    if (!s) { Serial.println(F("ERR usage DEMO ON|OFF")); return; }
    if      (!strcasecmp(s, "ON"))  demoMode = true;
    else if (!strcasecmp(s, "OFF")) demoMode = false;
    else { Serial.println(F("ERR usage DEMO ON|OFF")); return; }
    Serial.print(F("OK DEMO ")); Serial.println(demoMode ? F("ON") : F("OFF"));
    return;
  }

  if (!strcasecmp(cmd, "BRIGHT")) {
    char *s = strtok(NULL, " ");
    if (!s) { Serial.println(F("ERR usage BRIGHT <0-255>")); return; }
    int v = clampInt(atol(s), 0, 255);
    big.ssd1306_command(SSD1306_SETCONTRAST);  big.ssd1306_command(v);
    wide.ssd1306_command(SSD1306_SETCONTRAST); wide.ssd1306_command(v);
    Serial.print(F("OK BRIGHT ")); Serial.println(v);
    return;
  }

  if (!strcasecmp(cmd, "TEST")) {
    selfTest();
    Serial.println(F("OK TEST"));
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

// A plausible-looking pull through the gears, so the panels can be judged on a
// bench with nothing else plugged in.
void serviceDemo(unsigned long now) {
  float t = (now % 12000) / 12000.0f;          // 12 s loop
  int   g = 1 + (int)(t * 6);                  // 1..6
  float within = (t * 6) - (int)(t * 6);       // 0..1 inside the gear

  maxRpm = 8000;
  rpm    = (int)(2200 + within * 5800);
  gear   = g > 6 ? 6 : g;
  kmh    = (int)(t * 285);
  thr    = 210 + (int)(45 * within);
  brk    = 0;
  raceOn = true;
  lastFrameMs = now;
}

// ---- setup / loop --------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);

  // Bring both buses up on their own pins *before* the displays start.
  Wire.begin(SDA0, SCL0, 400000);
  Wire1.begin(SDA1, SCL1, 400000);

  // The last argument is periphBegin. It must be false: left at its default,
  // the library calls Wire.begin() with no pins and undoes the mapping above.
  bool okBig  = big.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false);
  bool okWide = wide.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false);

  if (!okBig)  Serial.println(F("ERR 128x64 panel not found on bus 0 (GPIO21/22)"));
  if (!okWide) Serial.println(F("ERR 128x32 panel not found on bus 1 (GPIO25/26)"));

  // One dead panel should not cost the other one. Only stop if both are gone.
  if (!okBig && !okWide) {
    Serial.println(F("ERR no panels — check wiring, then run i2c_scan"));
    while (1) delay(1000);
  }

  Serial.println(F("# racedash " VERSION " ready"));
  selfTest();
}

void loop() {
  unsigned long now = millis();

  serviceSerial();

  if (demoMode) serviceDemo(now);

  if (now - lastDrawMs >= DRAW_MS) {
    lastDrawMs = now;

    if (dataStale(now)) {
      drawWaiting();
    } else {
      bool blinkOn = (now / 60) % 2;    // ~8 Hz strobe in the shift zone
      drawBig(blinkOn);
      serviceSerial();                  // drain the UART between panels
      drawWide(blinkOn);
    }
  }

  if (now - lastBeatMs >= BEAT_MS) {
    lastBeatMs = now;
    Serial.print(F("# alive frames=")); Serial.print(frames);
    Serial.print(F(" stale="));         Serial.println(dataStale(now) ? 1 : 0);
  }
}
