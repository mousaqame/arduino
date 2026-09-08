// segdash8266.ino - 4-digit LED speed readout on its own NodeMCU / ESP8266.
//
// For the common 4-digit module with CLK / DIO / VCC / GND pins: a TM1637.
// Replaces the 16x2 LCD, which was never bright enough even at full contrast.
//
//   TM1637 (CLK=D1, DIO=D2) -> speed in km/h, right-aligned
//
// Despite the two-wire look this is NOT I2C. The TM1637 has no addresses and
// no proper arbitration -- just a start/stop framing that resembles I2C enough
// to confuse people. So it gets bit-banged directly here, and it must not share
// pins with the OLEDs.
//
// Both lines idle high through the module's own pull-up resistors. The trick
// used throughout: the output registers are parked LOW once in setup(), so
// afterwards pinMode(OUTPUT) drives a pin low and pinMode(INPUT) releases it.
// That gives proper open-drain behaviour without ever driving a line high into
// another device.
//
// Serial in (115200 baud, newline terminated, case-insensitive) -- same
// protocol as the other boards, so forza_bridge.py feeds them all identically:
//   D <kmh> <rpm> <maxrpm> <gear> <thr> <brk> <race>   telemetry frame
//   PING            -> OK PONG segdash8266 <ver>
//   GET             -> OK current state
//   DEMO ON|OFF     -> fake sweep, so the display can be tested with no PC
//   BRIGHT <0-7>    -> LED brightness, 7 is brightest
//   TEST            -> show 8888 at full brightness

#include <ESP8266WiFi.h>

#define VERSION "1.0"

// NodeMCU silkscreen -> GPIO. D1 = GPIO5, D2 = GPIO4.
#define CLK_PIN 5    // D1
#define DIO_PIN 4    // D2

// Half a clock period. The TM1637 tolerates up to ~250 kHz; 50us is a relaxed
// ~10 kHz, which is plenty fast for a display refreshed a few times a second
// and leaves margin for long jumper wires.
const uint8_t TM_DELAY = 50;

uint8_t tmBright = 7;        // 0..7, 7 = brightest

// ---- TM1637 bit-banged driver --------------------------------------------

inline void tmLow(uint8_t pin)     { pinMode(pin, OUTPUT); }   // parked LOW
inline void tmRelease(uint8_t pin) { pinMode(pin, INPUT);  }   // pull-up takes it high
inline void tmWait()               { delayMicroseconds(TM_DELAY); }

void tmStart() {
  tmLow(DIO_PIN);
  tmWait();
}

void tmStop() {
  tmLow(DIO_PIN);
  tmWait();
  tmRelease(CLK_PIN);
  tmWait();
  tmRelease(DIO_PIN);
  tmWait();
}

// Bytes go out least-significant bit first. Returns the chip's ACK.
bool tmWriteByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    tmLow(CLK_PIN);
    tmWait();
    if (b & 0x01) tmRelease(DIO_PIN); else tmLow(DIO_PIN);
    tmWait();
    tmRelease(CLK_PIN);
    tmWait();
    b >>= 1;
  }

  // Ninth clock: the TM1637 pulls DIO low to acknowledge.
  tmLow(CLK_PIN);
  tmRelease(DIO_PIN);
  tmWait();
  tmRelease(CLK_PIN);
  tmWait();
  bool ack = (digitalRead(DIO_PIN) == 0);
  if (ack) tmLow(DIO_PIN);
  tmWait();
  tmLow(CLK_PIN);
  tmWait();
  return ack;
}

// Push all four digits, then re-send the brightness/on command. The display
// keeps its own state, so the brightness byte is what actually turns it on.
void tmWriteDigits(const uint8_t *seg) {
  tmStart(); tmWriteByte(0x40); tmStop();              // auto-increment address

  tmStart(); tmWriteByte(0xC0);                        // start at digit 0
  for (uint8_t i = 0; i < 4; i++) tmWriteByte(seg[i]);
  tmStop();

  tmStart(); tmWriteByte(0x88 | (tmBright & 0x07)); tmStop();
}

// Segment bits: 0=a 1=b 2=c 3=d 4=e 5=f 6=g. Same encoding the OLED
// seven-segment renderer uses, because it is the same idea in hardware.
static const uint8_t TM_DIGIT[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
static const uint8_t TM_BLANK = 0x00;
static const uint8_t TM_DASH  = 0x40;   // segment g alone

// ---- telemetry state -----------------------------------------------------

int  kmh    = 0;
int  rpm    = 0;
int  maxRpm = 8000;
int  gear   = 0;
bool raceOn = false;
bool useMph = true;   // set by the frame's optional 8th field

unsigned long lastFrameMs = 0;
unsigned long frames      = 0;
unsigned long lastDrawMs  = 0;
unsigned long lastBeatMs  = 0;

bool demoMode = false;

const unsigned long DRAW_MS  = 100;    // 10 Hz is smooth for a numeric readout
const unsigned long STALE_MS = 1500;
const unsigned long BEAT_MS  = 3000;

char    lineBuf[64];
uint8_t lineLen = 0;

uint8_t shadow[4] = {0xEE, 0xEE, 0xEE, 0xEE};   // impossible, forces first write

// serviceSerial() can stamp lastFrameMs a millisecond or two AFTER loop() took
// its snapshot of millis(), which makes lastFrameMs briefly larger than `now`.
// On unsigned arithmetic (now - lastFrameMs) then wraps to ~4 billion and the
// display would flick to dashes mid-race. Comparing as signed makes a frame
// from the near future read as "0ms ago", not "forever".
bool dataStale(unsigned long now) {
  return (long)(now - lastFrameMs) > (long)STALE_MS;
}

// Speed, right-aligned, leading zeros blanked. Dashes when there is no data.
void buildFrame(uint8_t *seg) {
  if (dataStale(millis()) || !raceOn) {
    seg[0] = seg[1] = seg[2] = seg[3] = TM_DASH;
    return;
  }

  int v = kmh < 0 ? 0 : (kmh > 9999 ? 9999 : kmh);
  seg[3] = TM_DIGIT[v % 10];
  seg[2] = (v >= 10)   ? TM_DIGIT[(v / 10)   % 10] : TM_BLANK;
  seg[1] = (v >= 100)  ? TM_DIGIT[(v / 100)  % 10] : TM_BLANK;
  seg[0] = (v >= 1000) ? TM_DIGIT[(v / 1000) % 10] : TM_BLANK;
}

// Only touch the display when something actually changed. At a steady speed
// that means no traffic at all.
void refresh() {
  uint8_t seg[4];
  buildFrame(seg);
  if (memcmp(seg, shadow, 4) == 0) return;
  memcpy(shadow, seg, 4);
  tmWriteDigits(seg);
}

// ---- serial --------------------------------------------------------------

void printState() {
  Serial.print(F("OK STATE kmh="));  Serial.print(kmh);
  Serial.print(F(" rpm="));          Serial.print(rpm);
  Serial.print(F(" gear="));         Serial.print(gear);
  Serial.print(F(" race="));         Serial.print(raceOn ? 1 : 0);
  Serial.print(F(" demo="));         Serial.print(demoMode ? 1 : 0);
  Serial.print(F(" unit="));         Serial.print(useMph ? F("mph") : F("kmh"));
  Serial.print(F(" bright="));       Serial.print(tmBright);
  Serial.print(F(" frames="));       Serial.print(frames);
  Serial.print(F(" freeheap="));     Serial.println(ESP.getFreeHeap());
}

int clampInt(long v, long lo, long hi) {
  return (int)(v < lo ? lo : (v > hi ? hi : v));
}

// Missing or junk fields are rejected wholesale rather than half-applied, so a
// truncated line can never leave the display showing a mix of two frames.
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
  raceOn = f[6] != 0;

  // Optional 8th field: 1 = mph, 0 = km/h. There is no unit label on a bare
  // 4-digit module, so this only shows up in GET -- but reading it keeps every
  // board speaking the same protocol.
  char *units = strtok(NULL, " ");
  if (units) useMph = atol(units) != 0;

  frames++;
  lastFrameMs = millis();
  demoMode = false;              // real data always wins
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;

  if (!strcasecmp(cmd, "D")) { handleFrame(); return; }

  if (!strcasecmp(cmd, "PING")) {
    Serial.println(F("OK PONG segdash8266 " VERSION));
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
    if (!s) { Serial.println(F("ERR usage BRIGHT <0-7>")); return; }
    tmBright = clampInt(atol(s), 0, 7);
    shadow[0] = 0xEE;                  // force a redraw at the new brightness
    refresh();
    Serial.print(F("OK BRIGHT ")); Serial.println(tmBright);
    return;
  }

  if (!strcasecmp(cmd, "TEST")) {
    uint8_t all8[4] = {0x7F, 0x7F, 0x7F, 0x7F};
    tmWriteDigits(all8);
    shadow[0] = 0xEE;
    Serial.println(F("OK TEST showing 8888"));
    delay(2000);
    refresh();
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

  maxRpm = 8000;
  rpm    = (int)(2200 + ((t * 6) - (int)(t * 6)) * 5800);
  gear   = g > 6 ? 6 : g;
  kmh    = (int)(t * 285);
  raceOn = true;
  lastFrameMs = now;
}

// ---- setup / loop --------------------------------------------------------

void setup() {
  // Never used here, but the ESP8266 powers its radio at boot regardless, and
  // the background interrupts jitter bit-banged timing and the UART.
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  Serial.setRxBufferSize(512);
  Serial.begin(115200);
  delay(200);
  Serial.println();

  // Park both output registers LOW once. From here on, pinMode alone decides
  // whether a line is pulled down or released -- neither is ever driven high.
  digitalWrite(CLK_PIN, LOW);
  digitalWrite(DIO_PIN, LOW);
  tmRelease(CLK_PIN);
  tmRelease(DIO_PIN);

  Serial.println(F("# segdash8266 " VERSION " ready"));

  uint8_t all8[4] = {0x7F, 0x7F, 0x7F, 0x7F};
  tmWriteDigits(all8);              // every segment lit, so a dead one shows up
  delay(2000);
  refresh();
}

void loop() {
  unsigned long now = millis();

  serviceSerial();

  if (demoMode) serviceDemo(now);

  if (now - lastDrawMs >= DRAW_MS) {
    lastDrawMs = now;
    refresh();
  }

  if (now - lastBeatMs >= BEAT_MS) {
    lastBeatMs = now;
    Serial.print(F("# alive frames=")); Serial.print(frames);
    Serial.print(F(" stale="));         Serial.print(dataStale(now) ? 1 : 0);
    Serial.print(F(" kmh="));           Serial.println(kmh);
  }

  yield();
}
