// DHT11 temperature and humidity sensor with a serial link to a PC dashboard.
//
// Wiring:  VCC -> 5V,  DATA -> D2,  GND -> GND
//
// A DHT11 is slow and low-resolution by design: whole degrees, roughly +-2 C,
// and it refuses to be read more than about once a second. So this reads every
// two and a half seconds and reports what it got, rather than pretending to
// more precision or speed than the part has.
//
// Failed reads are normal and occasional. They are reported rather than hidden,
// because a rising failure count is how you notice a loose DATA wire before it
// becomes a mystery.
//
// Telemetry out (every 2.5s):  T <millis> <tempC> <humidity> <band>
// Replies:                     OK ... / ERR ... / # <info>
//
// Commands in (newline terminated, case-insensitive):
//   PING                  -> OK PONG
//   GET                   -> dump current config, min and max
//   SET LOW <c>           below this is COOL
//   SET HIGH <c>          above this is WARM
//   PEAKS                 -> reset the min and max
//   SAVE                  persist settings to EEPROM
//   RESET                 restore built-in defaults (does not auto-save)

#include <DHT.h>
#include <EEPROM.h>

#include "types.h"

#define DHTPIN  2
#define DHTTYPE DHT11

#define EEPROM_ADDR  0
#define EEPROM_MAGIC 0x7E11

// A DHT11 will not answer faster than about once a second. Asking more often
// just returns stale values or errors.
const unsigned long READ_MS  = 2500;
const unsigned long TELEM_MS = 2500;

DHT dht(DHTPIN, DHTTYPE);
Config cfg;

void loadDefaults() {
  cfg.magic     = EEPROM_MAGIC;
  cfg.comfyLow  = 20;
  cfg.comfyHigh = 26;
}

// ---- runtime state -------------------------------------------------------

float    tempC   = 0.0f;
float    hum     = 0.0f;
bool     haveOne = false;      // have we ever had a good read?
Band     band    = B_FAIL;

float    tMin =  999.0f, tMax = -999.0f;
float    hMin =  999.0f, hMax = -999.0f;

uint16_t failStreak = 0;       // consecutive failures right now
uint32_t failTotal  = 0;       // since boot, for spotting a flaky wire

unsigned long lastRead  = 0;
unsigned long lastTelem = 0;

char    lineBuf[40];
uint8_t lineLen = 0;

const char *bandName(Band b) {
  switch (b) {
    case B_COLD:  return "COLD";
    case B_COOL:  return "COOL";
    case B_COMFY: return "COMFY";
    case B_WARM:  return "WARM";
    case B_HOT:   return "HOT";
    default:      return "FAIL";
  }
}

Band bandFor(float t) {
  if (t <  cfg.comfyLow  - 5) return B_COLD;
  if (t <  cfg.comfyLow)      return B_COOL;
  if (t <= cfg.comfyHigh)     return B_COMFY;
  if (t <= cfg.comfyHigh + 5) return B_WARM;
  return B_HOT;
}

// ---- setup ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  EEPROM.get(EEPROM_ADDR, cfg);
  if (cfg.magic != EEPROM_MAGIC) loadDefaults();

  dht.begin();

  Serial.println(F("\n# temp_serial ready"));
  Serial.println(F("# first reading takes a couple of seconds"));
  printConfig();
}

// ---- reading -------------------------------------------------------------

void readSensor() {
  const float h = dht.readHumidity();
  const float t = dht.readTemperature();     // celsius

  if (isnan(h) || isnan(t)) {
    failStreak++;
    failTotal++;
    band = B_FAIL;
    if (failStreak == 3) Serial.println(F("# sensor not answering, check the DATA wire on D2"));
    return;
  }

  if (failStreak >= 3) Serial.println(F("# sensor back"));
  failStreak = 0;

  tempC = t;
  hum   = h;
  haveOne = true;
  band = bandFor(t);

  if (t < tMin) tMin = t;
  if (t > tMax) tMax = t;
  if (h < hMin) hMin = h;
  if (h > hMax) hMax = h;
}

// ---- serial --------------------------------------------------------------

void printConfig() {
  Serial.print(F("OK CFG low="));   Serial.print(cfg.comfyLow);
  Serial.print(F(" high="));        Serial.print(cfg.comfyHigh);
  Serial.print(F(" tmin="));        Serial.print(haveOne ? tMin : 0.0f, 1);
  Serial.print(F(" tmax="));        Serial.print(haveOne ? tMax : 0.0f, 1);
  Serial.print(F(" hmin="));        Serial.print(haveOne ? hMin : 0.0f, 1);
  Serial.print(F(" hmax="));        Serial.print(haveOne ? hMax : 0.0f, 1);
  Serial.print(F(" fails="));       Serial.println(failTotal);
}

void handleCommand(char *line) {
  char *cmd = strtok(line, " ");
  if (!cmd) return;

  if (!strcasecmp(cmd, "PING")) { Serial.println(F("OK PONG")); return; }
  if (!strcasecmp(cmd, "GET"))  { printConfig(); return; }

  if (!strcasecmp(cmd, "SET")) {
    char *what = strtok(NULL, " ");
    char *val  = strtok(NULL, " ");
    if (!what || !val) { Serial.println(F("ERR usage SET <LOW|HIGH> <degreesC>")); return; }
    const int v = atoi(val);
    if (v < -10 || v > 60) { Serial.println(F("ERR value out of range -10..60")); return; }
    if      (!strcasecmp(what, "LOW"))  cfg.comfyLow  = v;
    else if (!strcasecmp(what, "HIGH")) cfg.comfyHigh = v;
    else { Serial.println(F("ERR unknown setting")); return; }
    if (cfg.comfyHigh <= cfg.comfyLow)
      Serial.println(F("# warn high is not above low, COMFY will never be reached"));
    if (haveOne) band = bandFor(tempC);
    printConfig();
    return;
  }

  if (!strcasecmp(cmd, "PEAKS")) {
    tMin = hMin =  999.0f;
    tMax = hMax = -999.0f;
    if (haveOne) { tMin = tMax = tempC; hMin = hMax = hum; }
    Serial.println(F("OK PEAKS cleared"));
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
    loadDefaults();
    if (haveOne) band = bandFor(tempC);
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
  Serial.print(haveOne ? tempC : 0.0f, 1);
  Serial.print(' ');
  Serial.print(haveOne ? hum : 0.0f, 1);
  Serial.print(' ');
  Serial.println(bandName(band));
}

// ---- main loop -----------------------------------------------------------

void loop() {
  const unsigned long now = millis();

  serviceSerial();

  if (now - lastRead >= READ_MS) { lastRead = now; readSensor(); }
  if (now - lastTelem >= TELEM_MS) { lastTelem = now; sendTelemetry(now); }
}
