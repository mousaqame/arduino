// tacho8266.ino - analogue rev counter and speedometer, on a NodeMCU/ESP8266.
//
// Two servos swinging needles across printed dials, and nothing else. Same
// serial protocol as every other board in this project, so the PC bridges feed
// it the identical telemetry stream with no configuration - plug it in
// alongside the screens and it is simply another board they found.
//
//   rev counter servo signal -> D1
//   speedometer servo signal -> D2
//
// Print matching dials with:
//   python make_dial.py --kind rpm   --max 8000
//   python make_dial.py --kind speed --max 180
//
// Serial in (115200 baud, newline terminated, case-insensitive):
//   D <kmh> <rpm> <maxrpm> <gear> <thr> <brk> <race> [mph]
//   PING                      -> OK PONG tacho8266 <ver>
//   GET                       -> OK current state
//   DEMO ON|OFF               -> fake sweep, so it can be set up with no PC
//   RPM  <0-180>|SWEEP|AUTO   -> calibrate the rev counter
//   SPEED <0-180>|SWEEP|AUTO  -> calibrate the speedometer

#include <Servo.h>
#include <ESP8266WiFi.h>

#define VERSION "2.0"

// ===========================================================================
//  SET THESE TO MATCH YOUR GAUGES, THEN FLASH
// ===========================================================================
//
// MIN is where a needle sits at zero, MAX is the far end. Find yours with the
// RPM / SPEED commands rather than guessing - cheap servos rarely reach a true
// 0 or 180, and being driven into the mechanical stop makes them buzz and pull
// stall current indefinitely.
#define TACHO_ENABLED   1
#define TACHO_PIN       5     // D1
#define TACHO_MIN_DEG  10
#define TACHO_MAX_DEG 170

#define SPEEDO_ENABLED   1
#define SPEEDO_PIN       4    // D2
#define SPEEDO_MIN_DEG  10
#define SPEEDO_MAX_DEG 170
#define SPEEDO_FULL    350    // needle hits the end at this speed, in whatever
                              // unit the PC is sending (mph by default).
                              // Raise it and reprint the dial to match:
                              //   python make_dial.py --kind speed --max 350
// ===========================================================================

// Telemetry arrives 30 times a second and is never perfectly steady, so
// writing the raw angle straight to a servo makes the needle buzz audibly and
// jitter visibly. Easing makes it chase the target instead of snapping to it -
// which also looks like a real instrument's damped movement - and the deadband
// stops sub-degree wobble ever reaching the servo. Without the deadband a
// stationary needle still hunts by a degree either way.
const float SERVO_EASE     = 0.35f;   // 0..1, higher = snappier
const int   SERVO_DEADBAND = 1;       // degrees
const unsigned long SERVO_MS = 20;    // a servo cannot use faster than this

// ---- one gauge -----------------------------------------------------------

struct Gauge {
  Servo sv;
  int   minDeg, maxDeg;
  float pos;        // where the needle actually is
  int   last;       // last angle written, -1 = never
  int   hold;       // >=0 parks the needle for calibration

  void begin(uint8_t pin, int lo, int hi) {
    minDeg = lo; maxDeg = hi;
    pos = lo; last = -1; hold = -1;
    sv.attach(pin);
    write(lo);
  }

  void write(int deg) {
    if (deg < 0)   deg = 0;
    if (deg > 180) deg = 180;
    if (deg != last) { sv.write(deg); last = deg; }
  }

  // Full travel and back, so the end stops are obvious and you can see whether
  // the needle is fouling the dial face.
  //
  // Steps toward maxDeg whichever side of minDeg it is on. That matters
  // because reversing a gauge is done by setting MIN greater than MAX, and a
  // fixed ascending loop would simply not run at all in that case.
  void sweep() {
    int step = (maxDeg >= minDeg) ? 3 : -3;
    for (int d = minDeg; step > 0 ? d <= maxDeg : d >= maxDeg; d += step) {
      write(d); delay(5); yield();
    }
    delay(120);
    for (int d = maxDeg; step > 0 ? d >= minDeg : d <= minDeg; d -= step) {
      write(d); delay(5); yield();
    }
    write(minDeg);
    pos = minDeg;
  }

  // frac is 0..1 of full scale. When there is no live data the needle falls to
  // zero rather than freezing wherever the last frame left it - a stuck needle
  // looks exactly like live data.
  void update(float frac, bool live) {
    if (hold >= 0) { write(hold); return; }
    float target = minDeg + (live ? frac * (maxDeg - minDeg) : 0.0f);
    pos += (target - pos) * SERVO_EASE;
    int deg = (int)(pos + 0.5f);
    if (last < 0 || abs(deg - last) >= SERVO_DEADBAND) write(deg);
  }
};

#if TACHO_ENABLED
Gauge tacho;
#endif
#if SPEEDO_ENABLED
Gauge speedo;
#endif

// ---- telemetry state -----------------------------------------------------

int  rpm    = 0;
int  maxRpm = 8000;   // never 0, it is a divisor
int  gear   = 0;
int  kmh    = 0;      // speed in whatever unit the PC is sending
bool raceOn = false;
bool useMph = true;

unsigned long lastFrameMs = 0;
unsigned long frames      = 0;
unsigned long lastServoMs = 0;
unsigned long lastBeatMs  = 0;

bool    demoMode  = false;
uint8_t servoTurn = 0;    // which gauge moves on this tick - see loop()

char    lineBuf[64];
uint8_t lineLen = 0;

const unsigned long STALE_MS = 1500;
const unsigned long BEAT_MS  = 3000;

// serviceSerial() can stamp lastFrameMs a millisecond or two AFTER loop() took
// its snapshot of millis(), which makes lastFrameMs briefly larger than `now`.
// On unsigned arithmetic (now - lastFrameMs) then wraps to ~4 billion and the
// needles would drop to zero mid-race. Comparing as signed makes a frame from
// the near future read as "0ms ago", not "forever".
bool dataStale(unsigned long now) {
  return (long)(now - lastFrameMs) > (long)STALE_MS;
}

float clamp01(float f) { return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); }

float rpmFraction()   { return maxRpm <= 0 ? 0.0f : clamp01((float)rpm / maxRpm); }
float speedFraction() { return clamp01((float)kmh / (float)SPEEDO_FULL); }

// ---- serial --------------------------------------------------------------

void printState() {
  Serial.print(F("OK STATE rpm="));  Serial.print(rpm);
  Serial.print(F(" maxrpm="));       Serial.print(maxRpm);
  Serial.print(F(" speed="));        Serial.print(kmh);
  Serial.print(useMph ? F("mph") : F("kmh"));
  Serial.print(F(" gear="));         Serial.print(gear);
  Serial.print(F(" race="));         Serial.print(raceOn ? 1 : 0);
#if TACHO_ENABLED
  Serial.print(F(" tacho="));        Serial.print(tacho.last);
#endif
#if SPEEDO_ENABLED
  Serial.print(F(" speedo="));       Serial.print(speedo.last);
#endif
  Serial.print(F(" demo="));         Serial.print(demoMode ? 1 : 0);
  Serial.print(F(" frames="));       Serial.print(frames);
  Serial.print(F(" freeheap="));     Serial.println(ESP.getFreeHeap());
}

int clampInt(long v, long lo, long hi) {
  return (int)(v < lo ? lo : (v > hi ? hi : v));
}

// Missing or junk fields are rejected wholesale rather than half-applied, so a
// truncated line can never move a needle to a mix of two frames.
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
  gear   = clampInt(f[3], -1, 99);
  raceOn = f[6] != 0;

  char *units = strtok(NULL, " ");
  if (units) useMph = atol(units) != 0;

  frames++;
  lastFrameMs = millis();
  demoMode = false;              // real data always wins
}

// Shared by the RPM and SPEED commands.
void gaugeCommand(Gauge &g, const char *name) {
  char *s = strtok(NULL, " ");
  if (!s) {
    Serial.print(F("ERR usage ")); Serial.print(name);
    Serial.println(F(" <0-180>|SWEEP|AUTO"));
    return;
  }
  // ZERO and FULL are the ones to calibrate with. A bare number is a RAW servo
  // angle, which is not the same thing: on a reversed gauge the zero end is
  // angle 170, so asking for 10 sends the needle to the far side and looks
  // like the reversal never took effect.
  if (!strcasecmp(s, "ZERO")) {
    g.hold = g.minDeg;
    g.write(g.hold);
    Serial.print(F("OK ")); Serial.print(name);
    Serial.print(F(" ZERO at ")); Serial.println(g.hold);
  } else if (!strcasecmp(s, "FULL")) {
    g.hold = g.maxDeg;
    g.write(g.hold);
    Serial.print(F("OK ")); Serial.print(name);
    Serial.print(F(" FULL at ")); Serial.println(g.hold);
  } else if (!strcasecmp(s, "AUTO")) {
    g.hold = -1;
    Serial.print(F("OK ")); Serial.print(name); Serial.println(F(" AUTO"));
  } else if (!strcasecmp(s, "SWEEP")) {
    g.hold = -1;
    g.sweep();
    Serial.print(F("OK ")); Serial.print(name); Serial.println(F(" SWEEP"));
  } else {
    g.hold = clampInt(atol(s), 0, 180);
    g.write(g.hold);
    Serial.print(F("OK ")); Serial.print(name);
    Serial.print(F(" HOLD ")); Serial.println(g.hold);
  }
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;

  if (!strcasecmp(cmd, "D"))    { handleFrame(); return; }
  if (!strcasecmp(cmd, "PING")) { Serial.println(F("OK PONG tacho8266 " VERSION)); return; }
  if (!strcasecmp(cmd, "GET"))  { printState(); return; }

  if (!strcasecmp(cmd, "DEMO")) {
    char *s = strtok(NULL, " ");
    if      (s && !strcasecmp(s, "ON"))  demoMode = true;
    else if (s && !strcasecmp(s, "OFF")) demoMode = false;
    else { Serial.println(F("ERR usage DEMO ON|OFF")); return; }
    Serial.print(F("OK DEMO ")); Serial.println(demoMode ? F("ON") : F("OFF"));
    return;
  }

#if TACHO_ENABLED
  if (!strcasecmp(cmd, "RPM"))   { gaugeCommand(tacho,  "RPM");   return; }
#endif
#if SPEEDO_ENABLED
  if (!strcasecmp(cmd, "SPEED")) { gaugeCommand(speedo, "SPEED"); return; }
#endif

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

void serviceDemo(unsigned long now) {
  float t = (now % 12000) / 12000.0f;
  float within = (t * 6) - (int)(t * 6);
  maxRpm = 8000;
  rpm    = (int)(2200 + within * 5800);
  kmh    = (int)(t * 177);
  gear   = 1 + (int)(t * 6);
  raceOn = true;
  lastFrameMs = now;
}

// ---- setup / loop --------------------------------------------------------

void setup() {
  // Never used here, but the ESP8266 powers its radio at boot regardless, and
  // the background interrupts add jitter to the servo pulse train.
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  Serial.setRxBufferSize(512);
  Serial.begin(115200);
  delay(200);
  Serial.println();

  // Attached before anything slow, so the needles are under control from the
  // start rather than being flicked about by floating pins.
#if TACHO_ENABLED
  tacho.begin(TACHO_PIN, TACHO_MIN_DEG, TACHO_MAX_DEG);
#endif
#if SPEEDO_ENABLED
  speedo.begin(SPEEDO_PIN, SPEEDO_MIN_DEG, SPEEDO_MAX_DEG);
#endif

  Serial.println(F("# tacho8266 " VERSION " ready"));

  // Swept one after the other, never together: two servos accelerating at the
  // same instant is the biggest current spike this board will ever ask for.
#if TACHO_ENABLED
  tacho.sweep();
#endif
#if SPEEDO_ENABLED
  speedo.sweep();
#endif
}

void loop() {
  unsigned long now = millis();

  serviceSerial();

  if (demoMode) serviceDemo(now);

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

  if (now - lastBeatMs >= BEAT_MS) {
    lastBeatMs = now;
    Serial.print(F("# alive frames=")); Serial.print(frames);
    Serial.print(F(" stale="));         Serial.print(dataStale(now) ? 1 : 0);
#if TACHO_ENABLED
    Serial.print(F(" tacho="));         Serial.print(tacho.last);
#endif
#if SPEEDO_ENABLED
    Serial.print(F(" speedo="));        Serial.print(speedo.last);
#endif
    Serial.println();
  }

  yield();
}
