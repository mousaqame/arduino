// Four-servo robot controlled from a phone or browser.
//
// The NodeMCU hosts its own web page, so nothing else is needed once it is
// powered — no PC, no app. It joins your WiFi if secrets.h has credentials,
// and otherwise starts its own hotspot so it always comes up reachable.
//
//   on your WiFi   ->  http://robot.local   (or the IP shown on serial)
//   hotspot mode   ->  join "RobotBot", then http://192.168.4.1
//
// Servos never jump: each joint eases toward its target at a set speed. That
// looks better and, more importantly, avoids the current spike four servos
// slamming at once would cause.

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Servo.h>

#include "types.h"
#include "page.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#endif
#ifndef WIFI_SSID
  #define WIFI_SSID ""      // no secrets.h yet -> straight to hotspot mode
  #define WIFI_PASS ""
#endif

// Hotspot used when your WiFi isn't available. Not a secret — change it in
// secrets.h if you like.
#ifndef AP_SSID
  #define AP_SSID "RobotBot"
  #define AP_PASS "robot1234"
#endif

#define HOSTNAME "robot"

// GPIO numbers, not the D-labels, so this still builds on a generic ESP8266
// board. These four are the NodeMCU pins with no boot-time constraints.
//   D1 = GPIO5   D2 = GPIO4   D5 = GPIO14   D6 = GPIO12
Joint joints[JOINT_COUNT] = {
  { "Torso",     5, 10, 170, 90, 90, 90, false, Servo() },
  { "Head",      4, 30, 150, 90, 90, 90, false, Servo() },
  { "Left arm",  14, 10, 170, 90, 90, 90, false, Servo() },
  { "Right arm", 12, 10, 170, 90, 90, 90, false, Servo() },
};

// Joint order: 0 torso, 1 head, 2 left arm, 3 right arm.
static const Frame F_WAVE[] = {
  {{ -1, -1, -1, 165 }, 150},
  {{ -1, -1, -1, 115 },  60},
  {{ -1, -1, -1, 165 },  60},
  {{ -1, -1, -1, 115 },  60},
  {{ -1, -1, -1, 165 }, 150},
  {{ -1, -1, -1,  90 },   0},
};
static const Frame F_NOD[] = {
  {{ -1,  65, -1, -1 }, 90},
  {{ -1, 115, -1, -1 }, 90},
  {{ -1,  65, -1, -1 }, 90},
  {{ -1,  90, -1, -1 },  0},
};
static const Frame F_SHAKE[] = {
  {{ -1,  55, -1, -1 }, 70},
  {{ -1, 130, -1, -1 }, 70},
  {{ -1,  55, -1, -1 }, 70},
  {{ -1,  90, -1, -1 },  0},
};
static const Frame F_LOOKL[]  = {{{  55,  45, -1, -1 }, 0}};
static const Frame F_LOOKR[]  = {{{ 125, 140, -1, -1 }, 0}};
static const Frame F_HOME[]   = {{{  90,  90, 90, 90 }, 0}};

// Sway the body one way while the arms go the other, then a couple of big
// two-armed beats to break up the rhythm.
static const Frame F_DANCE[] = {
  {{  60, 105,  45, 140 },  70},
  {{ 120,  75, 140,  45 },  70},
  {{  60, 105,  45, 140 },  70},
  {{ 120,  75, 140,  45 },  70},
  {{  90,  90, 160, 160 }, 110},   // both arms up
  {{  90,  90,  30,  30 }, 110},   // both arms down
  {{  55, 115, 155,  40 },  90},
  {{ 125,  65,  40, 155 },  90},
  {{  90,  90,  90,  90 },   0},
};

// Deliberately slow — the whole point is that it looks like it is searching
// rather than twitching.
static const Frame F_SCAN[] = {
  {{  25,  45, -1, -1 }, 500},
  {{  90,  90, -1, -1 }, 250},
  {{ 155, 140, -1, -1 }, 500},
  {{  90,  90, -1, -1 },   0},
};

static const Frame F_CLAP[] = {
  {{ -1, -1, 130,  50 }, 60},
  {{ -1, -1,  75, 105 }, 60},
  {{ -1, -1, 130,  50 }, 60},
  {{ -1, -1,  75, 105 }, 60},
  {{ -1, -1, 130,  50 }, 60},
  {{ -1, -1,  90,  90 },  0},
};

// Arms out, head tipped one way then the other: "no idea".
static const Frame F_SHRUG[] = {
  {{ -1, 115, 145, 35 }, 350},
  {{ -1,  70, 145, 35 }, 350},
  {{ -1,  90,  90, 90 },   0},
};

static const Frame F_SLEEP[] = {
  {{ 90, 40, 25, 25 }, 0},
};

static const Pose POSES[] = {
  { "wave",      "Wave",       F_WAVE,  sizeof(F_WAVE)  / sizeof(Frame), 180 },
  { "dance",     "Dance",      F_DANCE, sizeof(F_DANCE) / sizeof(Frame), 220 },
  { "clap",      "Clap",       F_CLAP,  sizeof(F_CLAP)  / sizeof(Frame), 260 },
  { "nod",       "Nod",        F_NOD,   sizeof(F_NOD)   / sizeof(Frame), 130 },
  { "shake",     "Shake head", F_SHAKE, sizeof(F_SHAKE) / sizeof(Frame), 150 },
  { "shrug",     "Shrug",      F_SHRUG, sizeof(F_SHRUG) / sizeof(Frame), 110 },
  { "scan",      "Look around",F_SCAN,  sizeof(F_SCAN)  / sizeof(Frame),  45 },
  { "lookleft",  "Look left",  F_LOOKL, 1,  90 },
  { "lookright", "Look right", F_LOOKR, 1,  90 },
  { "sleep",     "Sleep",      F_SLEEP, 1,  30 },
  { "home",      "Stand",      F_HOME,  1,   0 },
};
static const uint8_t POSE_COUNT = sizeof(POSES) / sizeof(Pose);

ESP8266WebServer server(80);

float    speedDps = 120.0f;      // degrees per second
String   where    = "starting";

// Gentle mode, for a supply that can't feed four servos at once. Only the
// joint furthest from its target moves; the rest hold. Speed is capped, and
// once everything settles the servos are released so nothing is being held
// against gravity for no reason.
bool          gentle     = false;
unsigned long idleSince  = 0;
const float   GENTLE_MAX_DPS = 90.0f;
const unsigned long RELAX_AFTER = 4000;

// running pose
const Frame *seq      = nullptr;
uint8_t      seqLen   = 0;
uint8_t      seqIdx   = 0;
bool         seqApplied = false;
unsigned long arrivedAt = 0;
float        poseSpeed = 0.0f;   // this move's own speed; 0 = use speedDps

// ---- servos --------------------------------------------------------------

void attachAll() {
  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    if (!joints[i].attached) {
      joints[i].servo.attach(joints[i].pin, 500, 2400);   // SG90-friendly range
      joints[i].attached = true;
    }
  }
}

void detachAll() {
  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    if (joints[i].attached) {
      joints[i].servo.detach();
      joints[i].attached = false;
    }
  }
}

int16_t clampToJoint(uint8_t i, int16_t a) {
  if (a < joints[i].minA) return joints[i].minA;
  if (a > joints[i].maxA) return joints[i].maxA;
  return a;
}

void setTarget(uint8_t i, int16_t angle) {
  joints[i].target = clampToJoint(i, angle);
  attachAll();
}

bool allArrived() {
  for (uint8_t i = 0; i < JOINT_COUNT; i++)
    if (fabsf(joints[i].cur - joints[i].target) > 0.8f) return false;
  return true;
}

void updateJoints(unsigned long now) {
  static unsigned long last = 0;
  float dt = (now - last) / 1000.0f;
  last = now;
  if (dt <= 0.0f || dt > 0.25f) dt = 0.02f;   // first pass, or a long stall

  // A move can carry its own speed — a dance and a slow sweep should not run
  // at the same rate. Falls back to whatever the dashboard slider says.
  float sp = (seq != nullptr && poseSpeed > 0.0f) ? poseSpeed : speedDps;
  if (gentle && sp > GENTLE_MAX_DPS) sp = GENTLE_MAX_DPS;
  const float step = sp * dt;

  // In gentle mode pick the single joint furthest from where it should be;
  // everything else waits its turn, so only one motor ever draws moving current.
  int8_t only = -1;
  if (gentle) {
    float worst = 0.8f;
    for (uint8_t i = 0; i < JOINT_COUNT; i++) {
      const float d = fabsf(joints[i].target - joints[i].cur);
      if (d > worst) { worst = d; only = i; }
    }
  }

  bool moving = false;
  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    Joint &j = joints[i];
    if (!j.attached) continue;
    if (gentle && only >= 0 && i != only) { j.servo.write((int)lroundf(j.cur)); continue; }
    const float diff = j.target - j.cur;
    if (fabsf(diff) <= step) j.cur = j.target;
    else { j.cur += (diff > 0 ? step : -step); moving = true; }
    j.servo.write((int)lroundf(j.cur));
  }

  if (gentle && !moving && seq == nullptr) {
    if (idleSince == 0)                    idleSince = now;
    else if (now - idleSince > RELAX_AFTER) detachAll();
  } else {
    idleSince = 0;
  }
}

// ---- pose sequencer ------------------------------------------------------

void startPose(const Pose &p) {
  seq = p.frames; seqLen = p.count; seqIdx = 0;
  seqApplied = false; arrivedAt = 0;
  poseSpeed = p.dps;
  attachAll();
}

void stopPose() { seq = nullptr; poseSpeed = 0.0f; }

void updatePose(unsigned long now) {
  if (!seq) return;

  if (!seqApplied) {
    for (uint8_t i = 0; i < JOINT_COUNT; i++) {
      const int16_t a = seq[seqIdx].a[i];
      if (a >= 0) setTarget(i, a);
    }
    seqApplied = true;
    arrivedAt = 0;
    return;
  }

  if (!allArrived()) { arrivedAt = 0; return; }
  if (arrivedAt == 0) arrivedAt = now;
  if (now - arrivedAt < seq[seqIdx].hold) return;

  if (++seqIdx >= seqLen) stopPose();
  else                    seqApplied = false;
}

// ---- web -----------------------------------------------------------------

String stateJson() {
  String s = F("{\"where\":\"");
  s += where;
  s += F("\",\"speed\":");
  s += (int)speedDps;
  s += F(",\"gentle\":");
  s += gentle ? F("true") : F("false");
  s += F(",\"busy\":");
  s += seq ? F("true") : F("false");
  s += F(",\"joints\":[");
  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    if (i) s += ',';
    s += F("{\"name\":\"");  s += joints[i].name;
    s += F("\",\"angle\":");  s += (int)lroundf(joints[i].cur);
    s += F(",\"target\":");   s += joints[i].target;
    s += F(",\"min\":");      s += joints[i].minA;
    s += F(",\"max\":");      s += joints[i].maxA;
    s += '}';
  }
  s += F("]}");
  return s;
}

void sendState() { server.send(200, F("application/json"), stateJson()); }
void sendErr(const __FlashStringHelper *m) {
  server.send(400, F("application/json"), String(F("{\"error\":\"")) + m + F("\"}"));
}

void handleRoot()  { server.send_P(200, PSTR("text/html"), PAGE_INDEX); }

// The page builds its buttons from this, so adding a move means editing POSES
// and nothing else.
void handleMoves() {
  String s = F("[");
  for (uint8_t i = 0; i < POSE_COUNT; i++) {
    if (i) s += ',';
    s += F("{\"name\":\"");   s += POSES[i].name;
    s += F("\",\"label\":\""); s += POSES[i].label;
    s += F("\"}");
  }
  s += ']';
  server.send(200, F("application/json"), s);
}

void handleJoint() {
  if (!server.hasArg("name") || !server.hasArg("angle")) { sendErr(F("need name and angle")); return; }
  const String n = server.arg("name");
  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    if (n.equalsIgnoreCase(joints[i].name)) {
      stopPose();                       // a manual move overrides a running pose
      setTarget(i, server.arg("angle").toInt());
      sendState();
      return;
    }
  }
  sendErr(F("no such joint"));
}

void handlePose() {
  const String n = server.arg("name");
  for (uint8_t i = 0; i < POSE_COUNT; i++) {
    if (n.equalsIgnoreCase(POSES[i].name)) { startPose(POSES[i]); sendState(); return; }
  }
  sendErr(F("no such move"));
}

void handleStop() {
  stopPose();
  for (uint8_t i = 0; i < JOINT_COUNT; i++)     // freeze where they are
    joints[i].target = (int16_t)lroundf(joints[i].cur);
  sendState();
}

void handleRelax() { stopPose(); detachAll(); sendState(); }

void handleGentle() {
  const String m = server.arg("mode");
  if (m.equalsIgnoreCase("gentle"))      gentle = true;
  else if (m.equalsIgnoreCase("normal")) { gentle = false; attachAll(); }
  else { sendErr(F("use mode=gentle or mode=normal")); return; }
  idleSince = 0;
  sendState();
}

void handleSpeed() {
  const int dps = server.arg("dps").toInt();
  if (dps < 10 || dps > 400) { sendErr(F("speed out of range 10..400")); return; }
  speedDps = dps;
  sendState();
}

// ---- network -------------------------------------------------------------

void startNetwork() {
  WiFi.persistent(false);
  WiFi.hostname(HOSTNAME);

  if (strlen(WIFI_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print(F("joining "));
    Serial.print(WIFI_SSID);
    for (uint8_t i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    where = WiFi.localIP().toString();
    Serial.print(F("on your network: http://"));
    Serial.println(where);
    Serial.println(F("or try: http://" HOSTNAME ".local"));
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    where = String(F("hotspot ")) + WiFi.softAPIP().toString();
    Serial.println(F("no WiFi — started hotspot \"" AP_SSID "\""));
    Serial.print(F("join it, then open http://"));
    Serial.println(WiFi.softAPIP());
  }

  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

// ---- setup / loop --------------------------------------------------------

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n\nrobot starting"));

  for (uint8_t i = 0; i < JOINT_COUNT; i++) {
    joints[i].cur = joints[i].home;
    joints[i].target = joints[i].home;
  }
  attachAll();

  startNetwork();

  server.on("/",            HTTP_GET,  handleRoot);
  server.on("/api/state",   HTTP_GET,  sendState);
  server.on("/api/moves",   HTTP_GET,  handleMoves);
  server.on("/api/joint",   HTTP_POST, handleJoint);
  server.on("/api/pose",    HTTP_POST, handlePose);
  server.on("/api/stop",    HTTP_POST, handleStop);
  server.on("/api/relax",   HTTP_POST, handleRelax);
  server.on("/api/speed",   HTTP_POST, handleSpeed);
  server.on("/api/power",   HTTP_POST, handleGentle);
  server.onNotFound([]() { server.send(404, F("text/plain"), F("not found")); });
  server.begin();

  Serial.println(F("ready"));
}

void loop() {
  const unsigned long now = millis();
  server.handleClient();
  MDNS.update();
  updatePose(now);
  updateJoints(now);
}
