// lcddash8266.ino - 16x2 character LCD dashboard on its own NodeMCU / ESP8266.
//
// A standalone sibling to racedash8266. Same serial protocol, same telemetry
// frames, so forza_bridge.py feeds both boards the identical stream and you can
// run either one alone or both together.
//
//   LCD (D2/D1) -> 16x2 character LCD on a PCF8574 I2C backpack
//
//        0123456789012345
//   row0 G5 |||||||||||
//   row1 214KMH  6820RPM
//
// Having the LCD on a board of its own means nothing else shares its bus, so
// it can be powered from 5V (VIN) if 3.3V leaves it too faint to read. See
// WIRING.md before doing that -- it is a real trade-off, not a free upgrade.
//
// Serial in (115200 baud, newline terminated, case-insensitive):
//   D <kmh> <rpm> <maxrpm> <gear> <thr> <brk> <race>   telemetry frame
//   PING              -> OK PONG lcddash8266 <ver>
//   GET               -> OK current state
//   DEMO ON|OFF       -> fake sweep, so the screen can be tested with no PC
//   LCD               -> re-scan, then hold a solid test pattern for 8s
//   TEST              -> redraw the boot self-test

#include <Wire.h>
#include <ESP8266WiFi.h>

#define VERSION "1.0"

// NodeMCU silkscreen -> GPIO. D1 = GPIO5, D2 = GPIO4.
// D3, D4 and D8 are boot strapping pins and are avoided on purpose.
#define SDA_PIN 4    // D2
#define SCL_PIN 5    // D1

// ---- 16x2 character LCD on a PCF8574 backpack -----------------------------
//
// Driven directly rather than through a library. There are several mutually
// incompatible forks of LiquidCrystal_I2C in the wild and picking the wrong
// one is a long afternoon; this is about sixty lines and cannot drift.
//
// Standard backpack pin mapping (LCM1602 / YwRobot and every clone of it):
//   P0 = RS   P1 = RW   P2 = EN   P3 = backlight   P4..P7 = D4..D7

uint8_t lcdAddr = 0;              // 0 = nothing found
const uint8_t LCD_BACKLIGHT = 0x08;
const uint8_t LCD_EN        = 0x04;

void lcdExpanderWrite(uint8_t data) {
  Wire.beginTransmission(lcdAddr);
  Wire.write(data | LCD_BACKLIGHT);
  Wire.endTransmission();
}

// The HD44780 latches on the falling edge of EN, so every nibble is
// write / EN high / EN low.
void lcdWrite4(uint8_t nibbleAndFlags) {
  lcdExpanderWrite(nibbleAndFlags);
  lcdExpanderWrite(nibbleAndFlags | LCD_EN);
  delayMicroseconds(1);
  lcdExpanderWrite(nibbleAndFlags & ~LCD_EN);
  delayMicroseconds(50);
}

void lcdSend(uint8_t value, uint8_t rs) {
  lcdWrite4((value & 0xF0) | rs);
  lcdWrite4((uint8_t)(value << 4) | rs);
}

void lcdCommand(uint8_t c) { lcdSend(c, 0); }
void lcdData(uint8_t c)    { lcdSend(c, 1); }

void lcdSetCursor(uint8_t col, uint8_t row) {
  lcdCommand(0x80 | (col + (row ? 0x40 : 0x00)));
}

void lcdCreateChar(uint8_t loc, const uint8_t *rows) {
  lcdCommand(0x40 | ((loc & 0x07) << 3));
  for (uint8_t i = 0; i < 8; i++) lcdData(rows[i]);
}

// Rev-bar building blocks: 1 to 4 lit columns out of the cell's 5. A fully lit
// cell uses 0xFF, which is already in the HD44780's character ROM.
static const uint8_t LCD_BAR[4][8] = {
  {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
  {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
  {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C},
  {0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E},
};

// A PCF8574 lands somewhere in 0x20-0x27, a PCF8574A in 0x38-0x3F, depending on
// its solder jumpers. 0x27 and 0x3F are only the most common, not the only
// ones, so scan both ranges. This board has no OLED, but 0x3C and 0x3D are
// still skipped so a screen borrowed from the other board cannot be mistaken
// for a backpack and driven as one.
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

bool lcdBegin() {
  if (!lcdProbe()) return false;

  delay(50);                       // HD44780 power-on settling
  lcdExpanderWrite(0x00);
  delay(50);

  lcdWrite4(0x30); delay(5);       // the documented "wake up in 8-bit" triple
  lcdWrite4(0x30); delayMicroseconds(150);
  lcdWrite4(0x30); delayMicroseconds(150);
  lcdWrite4(0x20);                 // and only now switch to 4-bit

  lcdCommand(0x28);                // 4-bit, 2 lines, 5x8 font
  lcdCommand(0x08);                // display off
  lcdCommand(0x01); delay(2);      // clear (slow, needs the wait)
  lcdCommand(0x06);                // entry mode: advance right, no shift
  lcdCommand(0x0C);                // display on, cursor off, blink off

  for (uint8_t i = 0; i < 4; i++) lcdCreateChar(i + 1, LCD_BAR[i]);
  return true;
}

// A shadow copy of what is on the glass. Only changed cells get rewritten,
// which keeps a refresh down to a handful of I2C writes instead of ~200.
char lcdFrame[2][16];
char lcdShadow[2][16];
bool lcdShadowValid = false;

void lcdFlush() {
  for (uint8_t r = 0; r < 2; r++) {
    uint8_t c = 0;
    while (c < 16) {
      if (lcdShadowValid && lcdFrame[r][c] == lcdShadow[r][c]) { c++; continue; }
      lcdSetCursor(c, r);
      // Write the whole run of changed cells; the cursor auto-advances.
      while (c < 16 && (!lcdShadowValid || lcdFrame[r][c] != lcdShadow[r][c])) {
        lcdData((uint8_t)lcdFrame[r][c]);
        lcdShadow[r][c] = lcdFrame[r][c];
        c++;
      }
    }
  }
  lcdShadowValid = true;
}

// ---- telemetry state -----------------------------------------------------

int  kmh    = 0;
int  rpm    = 0;
int  maxRpm = 8000;   // never 0, it is a divisor
int  gear   = 0;
int  thr    = 0;
int  brk    = 0;
bool raceOn = false;

unsigned long lastFrameMs = 0;
unsigned long frames      = 0;
unsigned long lastDrawMs  = 0;
unsigned long lastBeatMs  = 0;
unsigned long lcdBlocksUntil = 0;

bool demoMode = false;

const unsigned long DRAW_MS  = 120;    // ~8 Hz is plenty for characters
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

// ---- layout --------------------------------------------------------------
//
// Columns 0-2 are the gear, 3-15 a thirteen-cell rev bar. Each cell resolves
// to a fifth of a character, so the bar has 65 steps rather than 13 and moves
// smoothly instead of jumping. In the shift zone the bar is replaced by a
// flashing SHIFT UP!, which is far easier to catch at the edge of vision.

void lcdBuildFrame(bool lcdBlink) {
  memset(lcdFrame, ' ', sizeof lcdFrame);

  if (dataStale(millis())) {
    memcpy(lcdFrame[0], "== RACE DASH ==", 15);
    memcpy(lcdFrame[1], "WAITING FOR DATA", 16);
    return;
  }

  char g[4];
  if      (!raceOn)   strcpy(g, "-  ");
  else if (gear == 0) strcpy(g, "R  ");
  else                snprintf(g, sizeof g, "G%-2d", gear);
  memcpy(lcdFrame[0], g, 3);

  bool shift = rpmFraction() >= SHIFT_AT && raceOn;
  if (shift && lcdBlink) {
    memcpy(&lcdFrame[0][3], "  SHIFT UP!  ", 13);
  } else {
    int sub  = (int)(rpmFraction() * 65.0f + 0.5f);   // 13 cells x 5 columns
    int full = sub / 5, rem = sub % 5;
    for (int i = 0; i < 13; i++) {
      if      (i < full)         lcdFrame[0][3 + i] = (char)0xFF;   // solid block
      else if (i == full && rem) lcdFrame[0][3 + i] = (char)rem;    // custom 1..4
    }
  }

  int rpmShown = rpm > 9999 ? 9999 : rpm;
  char buf[20];
  snprintf(buf, sizeof buf, "%3dKMH  %4dRPM", kmh, rpmShown);
  memcpy(lcdFrame[1], buf, 15);
}

void selfTest() {
  int  sKmh = kmh, sRpm = rpm, sGear = gear;
  bool sRace = raceOn;
  unsigned long sFrame = lastFrameMs;

  kmh = 214; rpm = 6820; maxRpm = 8000; gear = 5; raceOn = true;
  lastFrameMs = millis();          // so it shows the test frame, not "WAITING"

  lcdBuildFrame(false);
  lcdFlush();
  delay(2500);

  kmh = sKmh; rpm = sRpm; gear = sGear; raceOn = sRace;
  lastFrameMs = sFrame;
}

// ---- serial --------------------------------------------------------------

void printState() {
  Serial.print(F("OK STATE kmh="));   Serial.print(kmh);
  Serial.print(F(" rpm="));           Serial.print(rpm);
  Serial.print(F(" maxrpm="));        Serial.print(maxRpm);
  Serial.print(F(" gear="));          Serial.print(gear);
  Serial.print(F(" race="));          Serial.print(raceOn ? 1 : 0);
  Serial.print(F(" demo="));          Serial.print(demoMode ? 1 : 0);
  Serial.print(F(" frames="));        Serial.print(frames);
  Serial.print(F(" lcd=0x"));         Serial.print(lcdAddr, HEX);
  Serial.print(F(" freeheap="));      Serial.println(ESP.getFreeHeap());
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
  gear   = clampInt(f[3], 0, 99);
  thr    = clampInt(f[4], 0, 255);
  brk    = clampInt(f[5], 0, 255);
  raceOn = f[6] != 0;

  frames++;
  lastFrameMs = millis();
  demoMode = false;              // real data always wins
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;

  if (!strcasecmp(cmd, "D")) { handleFrame(); return; }

  if (!strcasecmp(cmd, "PING")) {
    Serial.println(F("OK PONG lcddash8266 " VERSION));
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

  if (!strcasecmp(cmd, "TEST")) {
    selfTest();
    Serial.println(F("OK TEST"));
    return;
  }

  // Re-scan and hold every cell solid for 8 seconds. With the contrast wrong
  // the glass looks blank whatever it is told to show, so a full field of
  // blocks is the only honest way to find the trimpot's working range.
  if (!strcasecmp(cmd, "LCD")) {
    if (lcdBegin()) {
      Serial.print(F("OK LCD at 0x")); Serial.println(lcdAddr, HEX);
      Serial.println(F("# both rows now solid blocks for 8s"));
      Serial.println(F("# turn the blue trimpot on the backpack until you see them"));
      memset(lcdFrame, 0xFF, sizeof lcdFrame);
      lcdShadowValid = false;
      lcdFlush();
      lcdBlocksUntil = millis() + 8000;
    } else {
      Serial.println(F("ERR no backpack found on D2/D1"));
      Serial.println(F("# scanned 0x20-0x27 and 0x38-0x3F. check GND/VCC/SDA/SCL"));
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
  kmh    = (int)(t * 285);
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

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  if (lcdBegin()) {
    Serial.print(F("# 16x2 LCD found at 0x")); Serial.println(lcdAddr, HEX);
    Serial.println(F("# lcddash8266 " VERSION " ready"));
    selfTest();
  } else {
    Serial.println(F("ERR no LCD on D2/D1 (scanned 0x20-0x27 and 0x38-0x3F)"));
    Serial.println(F("# check GND/VCC/SDA/SCL, then send LCD to re-scan"));
  }
}

void loop() {
  unsigned long now = millis();

  serviceSerial();

  if (demoMode) serviceDemo(now);

  // Retry the probe every few seconds so plugging the LCD in while running
  // picks it up, instead of needing a reset.
  if (!lcdAddr && now - lastDrawMs >= 3000) {
    lastDrawMs = now;
    if (lcdBegin()) {
      Serial.print(F("# LCD appeared at 0x")); Serial.println(lcdAddr, HEX);
    }
    return;
  }

  if (lcdAddr && now >= lcdBlocksUntil && now - lastDrawMs >= DRAW_MS) {
    lastDrawMs = now;
    lcdBuildFrame((now / 250) % 2);   // 2 Hz flash reads as deliberate
    lcdFlush();
  }

  if (now - lastBeatMs >= BEAT_MS) {
    lastBeatMs = now;
    Serial.print(F("# alive frames=")); Serial.print(frames);
    Serial.print(F(" stale="));         Serial.print(dataStale(now) ? 1 : 0);
    Serial.print(F(" lcd=0x"));         Serial.println(lcdAddr, HEX);
  }

  yield();
}
