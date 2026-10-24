/*
  ============================================================================
   UNDERPASS FLOOD GUARD  -  live flood-depth warning for road underpasses
   Kreativity Show Jr. 2026 | Smart Infrastructure
   ESP32 + 5 home-made "touch" water probes + LED depth sign + siren + phone
  ============================================================================
   Board : "ESP32 Dev Module"  (your 38-pin ESP32-WROOM-32 board, USB-C)
   Core  : Arduino-ESP32 2.0.x or 3.x
           Arduino IDE > Tools > Board > esp32 > ESP32 Dev Module
   Libs  : nothing to install (WiFi, WebServer, DNSServer, Preferences are
           part of the ESP32 core)

   HOW IT SENSES WATER
   Ten ESP32 pins have a built-in capacitive TOUCH sensor. A bare jumper-wire
   tip on a touch pin reads a big number in air and a number near zero when
   it touches water (the water, which also touches the GND tip, soaks up its
   charge). So 5 jumper tips taped to a bamboo skewer = a 5-level water
   sensor with no sensor module. The touch signal puts almost no current
   through the water, so the tips corrode far less than DC water sensors.
   The two bolts are touch buttons too.

   WHAT IT DOES (THEME 2)
   Water depth at the underpass low point -> decision:
      5 cm   caution: slow down
     15 cm   ROAD CLOSED: stop light flashes, siren, phone alert
     30 cm   many cars can float
     45 cm   flowing water can sweep vehicles away
     60 cm   emergency: check for trapped vehicles, call 112
   Once closed, the road stays closed until the water is below 5 cm AND a
   person has inspected it and tapped bolt A (or the phone button).

   MODEL SCALE: 1 cm in the glass = 6 cm of water on the road. Tips sit
   0.8, 2.5, 5, 7.5 and 10 cm above the skewer's point; GND tip at the point.

   THEME - already set to your registered theme, Smart Infrastructure (2).
   The rules say the theme cannot be changed after Round 1.
     1 = Agriculture          : Smart Pani-Pipe for Alternate Wetting & Drying
     2 = Smart Infrastructure : Underpass Flood Guard
     3 = Health Care          : School Water Guard (toilet / hand-wash tank)
  ============================================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// ============================ 1. SETTINGS =================================
#define THEME 2

// 1 = buzzer beeps by itself on DC power (round black SFM-27 type)
// 0 = passive buzzer (only clicks on DC). Test: briefly touch red to 3V3 and
//     black to GND. Continuous beep = active (1). Single click = passive (0).
#define BUZZER_IS_ACTIVE 1

const uint8_t NUM_PROBES = 5;
// Touch pins for the depth-gauge tips: P1 = lowest tip ... P5 = highest tip
const uint8_t PROBE_PIN[NUM_PROBES] = {32, 33, 27, 14, 13};
// LED1..LED5 light when P1..P5 are wet; LED6 is the ACTION light
const uint8_t LED_PIN[6] = {21, 19, 18, 5, 17, 16};
const uint8_t BUZZER_PIN = 23;
// Two bolts used as touch buttons (touch pins)
const uint8_t PAD_PIN[2] = {4, 15};   // [0] = bolt A, [1] = bolt B
//   Bolt A  tap: silence / OK        hold 5 s: re-calibrate (gauge OUT of water)
//   Bolt B  tap: mode / test alarm   hold 5 s: LED + buzzer self-test

const char* AP_PASSWORD = "kreativity";      // phone Wi-Fi password (8+ characters)

// Sensitivity: fraction of the "dry" reading. A wet tip reads close to 0%;
// a dry tip whose wire runs next to wet wires can sag to 60-80%, so 40% sits
// safely between the two.
const float WET_RATIO   = 0.40f;   // below 40% of dry value -> WET
const float DRY_RATIO   = 0.55f;   // back above 55%         -> DRY
const float TOUCH_RATIO = 0.55f;   // bolt below 55%         -> touched
const uint8_t  CONFIRM_SAMPLES = 3;        // readings that must agree before a change
const uint32_t SAMPLE_MS  = 150;           // how often the tips are read
const uint32_t PAD_MS     = 50;            // how often the bolts are read (quick taps)
const uint32_t REALERT_MS = 20000;         // repeat the alarm every 20 s until "OK"
const uint32_t HISTORY_MS = 2000;          // one chart point every 2 s (4 min on phone)
const uint32_t HOLD_MS    = 5000;          // long-press time for the bolts

// ============================ 2. THEME TEXT ===============================
#if THEME == 1
const char* AP_SSID  = "PaniPipe-ESP32";
const char* TITLE    = "Smart Pani-Pipe";
const char* SUBTITLE = "AWD irrigation advisor for paddy fields";
const char* PROBE_LABEL[NUM_PROBES] = {"-15 cm", "-10 cm", "-5 cm", "0 cm soil", "+5 cm"};
const char* LEVEL_TEXT[NUM_PROBES + 1] = {
  "Below -15 cm", "-15 to -10 cm", "-10 to -5 cm", "-5 to 0 cm",
  "0 to +5 cm (above soil)", "+5 cm or more (target)"};
const char* COUNT_LABEL = "Irrigations";
const char* MODE_LABEL  = "Mode";
#elif THEME == 2
const char* AP_SSID  = "FloodGuard-ESP32";
const char* TITLE    = "Underpass Flood Guard";
const char* SUBTITLE = "Live water depth at the underpass low point";
const char* PROBE_LABEL[NUM_PROBES] = {"5 cm", "15 cm", "30 cm", "45 cm", "60 cm"};
const char* LEVEL_TEXT[NUM_PROBES + 1] = {
  "Under 5 cm (road clear)", "5-15 cm", "15-30 cm", "30-45 cm",
  "45-60 cm", "60 cm or more"};
const char* COUNT_LABEL = "Closures";
const char* MODE_LABEL  = "Road status";
#else
const char* AP_SSID  = "WaterGuard-ESP32";
const char* TITLE    = "School Water Guard";
const char* SUBTITLE = "Water for toilets & hand-wash, always";
const char* PROBE_LABEL[NUM_PROBES] = {"10%", "30%", "50%", "70%", "90%"};
const char* LEVEL_TEXT[NUM_PROBES + 1] = {
  "Below 10%", "10-30%", "30-50%", "50-70%", "70-90%", "90% or more"};
const char* COUNT_LABEL = "Refills";
const char* MODE_LABEL  = "Status";
#endif

// ============================ 3. STATE ====================================
WebServer   server(80);
DNSServer   dns;
Preferences prefs;

float    base[NUM_PROBES];        // "dry" reading of each tip
uint32_t raw[NUM_PROBES];         // latest filtered reading
bool     wet[NUM_PROBES];
uint8_t  agree[NUM_PROBES];
uint8_t  level = 0;               // how many tips are wet, counted from the bottom
int8_t   lastLevel = -1;          // level at the previous decision (-1 = not yet)
bool     fault = false;           // a higher tip is wet while a lower one is dry

float    padBase[2];
bool     padDown[2]     = {false, false};
bool     padLongDone[2] = {false, false};
uint32_t padSince[2]    = {0, 0};
uint8_t  padAgree[2]    = {0, 0};

// advice shown on the phone
enum Sev { SEV_OK = 0, SEV_INFO = 1, SEV_WARN = 2, SEV_ALERT = 3 };
Sev         sev = SEV_OK;
const char* headline = "Starting...";
const char* detail   = "";
enum Act { ACT_HEARTBEAT, ACT_SLOW, ACT_FAST, ACT_SOLID };
Act actionLed = ACT_HEARTBEAT;

// alarm
bool alarmOn = false, alarmAcked = false, alarmPlayNow = false;
uint32_t alarmLast = 0;
const uint16_t* alarmPattern = nullptr;

// theme logic
enum Phase { DRYING, NEED_WATER, FILLING };
Phase    phase = DRYING;
bool     flowering = false;       // theme 1: keep field flooded around flowering
uint16_t counter = 0;             // irrigations / closures / refills
uint32_t counterAt = 0;
uint32_t bannerUntil = 0;         // keeps "STOP THE PUMP" / "TANK FULL" on screen
uint32_t testUntil = 0;           // themes 2 and 3: test alarm on screen
enum Road { ROAD_OPEN, ROAD_CLOSED, ROAD_CHECK };
Road     road = ROAD_OPEN;        // theme 2: open / closed / waiting for inspection

// level-change memory (for the time estimate)
uint32_t chgT[2] = {0, 0};
int8_t   chgL[2] = {-1, -1};

// history + events for the phone
const uint8_t HIST_N = 120;
uint8_t  hist[HIST_N];
uint8_t  histCount = 0, histHead = 0;

struct LogEntry { uint32_t t; char msg[56]; };
const uint8_t EV_N = 12;
LogEntry events[EV_N];
uint8_t  evCount = 0, evHead = 0;

// ============================ 4. BUZZER ===================================
// Patterns: ON ms, OFF ms, ON ms, ... ended by 0
const uint16_t PAT_CHIRP[] = {60, 0};
const uint16_t PAT_TWO[]   = {60, 90, 60, 0};
const uint16_t PAT_THREE[] = {60, 90, 60, 90, 60, 0};
const uint16_t PAT_ALERT[] = {160, 110, 160, 110, 160, 0};
const uint16_t PAT_STOP[]  = {700, 250, 700, 0};
const uint16_t PAT_SIREN[] = {300, 150, 300, 150, 300, 150, 300, 150, 300, 0};

const uint16_t* pat = nullptr;
uint8_t  patIdx = 0;
uint32_t patStep = 0;

void buzz(bool on) {
#if BUZZER_IS_ACTIVE
  digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
#else
  if (on) tone(BUZZER_PIN, 2700); else noTone(BUZZER_PIN);
#endif
}
void play(const uint16_t* p) { pat = p; patIdx = 0; patStep = millis(); buzz(true); }
void buzzerTick() {
  if (!pat) return;
  if (millis() - patStep >= pat[patIdx]) {
    patIdx++;
    if (pat[patIdx] == 0) { pat = nullptr; buzz(false); return; }
    patStep = millis();
    buzz(patIdx % 2 == 0);
  }
}

// ============================ 5. LOG ======================================
void logEvent(const char* msg) {
  LogEntry& e = events[evHead];
  e.t = millis();
  strncpy(e.msg, msg, sizeof(e.msg) - 1);
  e.msg[sizeof(e.msg) - 1] = 0;
  evHead = (evHead + 1) % EV_N;
  if (evCount < EV_N) evCount++;
  Serial.print("# EVENT: "); Serial.println(msg);
}

void startAlarm(const uint16_t* p) { alarmOn = true; alarmAcked = false; alarmPlayNow = true; alarmPattern = p; }
void stopAlarm()                    { alarmOn = false; }

// ============================ 6. SENSING ==================================
uint32_t readMedian(uint8_t pin) {
  uint32_t v[5];
  for (uint8_t i = 0; i < 5; i++) v[i] = touchRead(pin);
  for (uint8_t i = 1; i < 5; i++) {            // sort 5 values, take the middle one
    uint32_t x = v[i]; int8_t j = i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
  return v[2];
}

float readAverage(uint8_t pin, uint8_t n) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) { sum += readMedian(pin); delay(5); }
  float a = (float)sum / n;
  return a < 1 ? 1 : a;
}

// keep a "dry"/"untouched" reference up to date while nothing is touching
void trackBase(float& b, uint32_t v, float lowLimit) {
  if (v > b * 1.3f)          b = v;                   // was calibrated while wet/touched: fix now
  else if (v > b)            b += (v - b) * 0.02f;    // slow drift up
  else if (v > b * lowLimit) b -= (b - v) * 0.002f;   // very slow drift down
}

void saveCalibration() {
  prefs.begin("pani", false);
  char key[4];
  for (uint8_t i = 0; i < NUM_PROBES; i++) { snprintf(key, sizeof(key), "b%u", (unsigned)i); prefs.putUInt(key, (uint32_t)base[i]); }
  prefs.putBool("ok", true);
  prefs.end();
}

bool loadCalibration() {
  prefs.begin("pani", true);
  bool ok = prefs.getBool("ok", false);
  char key[4];
  for (uint8_t i = 0; i < NUM_PROBES && ok; i++) {
    snprintf(key, sizeof(key), "b%u", (unsigned)i);
    base[i] = prefs.getUInt(key, 0);
    if (base[i] < 1) ok = false;
  }
  prefs.end();
  return ok;
}

void calibrateProbes() {   // the gauge must be OUT of the water and dry
  for (uint8_t i = 0; i < NUM_PROBES; i++) {
    base[i] = readAverage(PROBE_PIN[i], 16);
    wet[i] = false; agree[i] = 0;
  }
  saveCalibration();
  logEvent("Calibrated (gauge dry)");
  play(PAT_TWO);
}

void computeLevel() {
  uint8_t l = 0;
  while (l < NUM_PROBES && wet[l]) l++;
  bool f = false;
  for (uint8_t i = l; i < NUM_PROBES; i++) if (wet[i]) f = true;
  fault = f;
  if (l != level) {
    chgT[1] = chgT[0]; chgL[1] = chgL[0];
    chgT[0] = millis(); chgL[0] = l;
    level = l;
    char m[56];
    snprintf(m, sizeof(m), "Level now: %s", LEVEL_TEXT[level]);
    logEvent(m);
  }
}

void readProbes() {
  for (uint8_t i = 0; i < NUM_PROBES; i++) {
    uint32_t v = readMedian(PROBE_PIN[i]);
    raw[i] = v;
    bool candidate = wet[i] ? !(v > base[i] * DRY_RATIO) : (v < base[i] * WET_RATIO);
    if (candidate != wet[i]) {
      if (++agree[i] >= CONFIRM_SAMPLES) { wet[i] = candidate; agree[i] = 0; }
    } else {
      agree[i] = 0;
    }
    if (!wet[i] && !candidate) trackBase(base[i], v, DRY_RATIO);
  }
  computeLevel();
}

// speed in "tips per second" from the last two level changes (+ rising, - falling)
float levelRate() {
  if (chgL[1] < 0 || chgT[0] <= chgT[1]) return 0;
  return (float)(chgL[0] - chgL[1]) / ((chgT[0] - chgT[1]) / 1000.0f);
}
// seconds until the level reaches `target`, or -1 if it is not heading there
long etaTo(int target) {
  float r = levelRate();
  if (r == 0 || target == level) return -1;
  if ((target < level && r > 0) || (target > level && r < 0)) return -1;
  float secs = fabsf((float)(target - level)) / fabsf(r) - (millis() - chgT[0]) / 1000.0f;
  return secs < 0 ? 0 : (long)secs;
}

// ============================ 7. DECISIONS ================================
long        etaSec = -1;
const char* etaLabel = "Estimate";

void decide() {
  uint32_t now = millis();
  etaSec = -1;
  etaLabel = "Estimate";

#if THEME == 1
  // ---- AWD rule (ICAR-NRRI / IRRI "safe AWD"): irrigate when the water is
  //      15 cm below the soil, refill to 5 cm above it; keep it flooded
  //      around flowering.
  uint8_t trigger = flowering ? 3 : 0;          // need water when level <= trigger
  if (level <= trigger) {
    if (phase != NEED_WATER) {
      phase = NEED_WATER;
      startAlarm(PAT_ALERT);
      logEvent(flowering ? "Below soil during flowering: IRRIGATE" : "Below -15 cm: IRRIGATE NOW");
    }
  } else if (phase == NEED_WATER) {
    phase = FILLING; stopAlarm();
    logEvent("Water rising: pump is ON");
  }
  if (level >= NUM_PROBES && phase != DRYING) {
    phase = DRYING; stopAlarm(); counter++; counterAt = now;
    bannerUntil = now + 20000;
    play(PAT_STOP);
    logEvent("Reached +5 cm: STOP THE PUMP");
  }

  if (phase == NEED_WATER) {
    sev = SEV_ALERT; actionLed = ACT_FAST;
    headline = "IRRIGATE NOW";
    detail = flowering ? "Flowering stage: keep water above the soil. Start the pump."
                       : "Water is 15 cm below the soil. Start the pump and fill to +5 cm.";
  } else if (phase == FILLING) {
    sev = SEV_INFO; actionLed = ACT_SLOW;
    headline = "Filling - keep the pump ON";
    detail = "Stop when the +5 cm light comes on.";
    etaSec = etaTo(NUM_PROBES); etaLabel = "Pump can stop in";
  } else if (now < bannerUntil) {
    sev = SEV_INFO; actionLed = ACT_SOLID;
    headline = "STOP THE PUMP";
    detail = "Target depth (+5 cm) reached. Now let the field dry.";
  } else {
    sev = SEV_OK; actionLed = ACT_HEARTBEAT;
    headline = "No irrigation needed";
    detail = flowering ? "Flowering mode: alert as soon as water drops below the soil."
                       : "AWD: let the field dry down to -15 cm. Every skipped irrigation saves water and power.";
    etaSec = etaTo(trigger); etaLabel = "Next irrigation in";
  }

#elif THEME == 2
  // ---- Underpass Flood Guard. Water depth at the low point -> decision:
  //   level 1 =  5 cm : caution
  //   level 2 = 15 cm : ROAD CLOSED. Delhi PWD diverts traffic once water
  //                     reaches six inches, and six inches reaches the bottom
  //                     of most cars, causing loss of control and stalling.
  //   level 3 = 30 cm : twelve inches of water can float many cars
  //   level 4 = 45 cm : 18-24 inches of flowing water can carry most vehicles away
  //   level 5 = 60 cm : emergency
  //   (car figures: US National Weather Service flood-safety advice)
  const uint8_t CLOSE_LEVEL = 2;
  if (level >= CLOSE_LEVEL && road != ROAD_CLOSED) {
    road = ROAD_CLOSED;
    counter++; counterAt = now;
    startAlarm(PAT_SIREN);
    logEvent("ROAD CLOSED - divert traffic");
  } else if (road == ROAD_CLOSED && lastLevel >= 0 && level > lastLevel && level >= CLOSE_LEVEL) {
    startAlarm(PAT_SIREN);                     // rising again: sound even if it was silenced
    logEvent(level >= 5 ? "60 cm: EMERGENCY - call 112" : "Water still rising");
  }
  if (road == ROAD_CLOSED && level < CLOSE_LEVEL && alarmOn) stopAlarm();
  if (road == ROAD_CLOSED && level == 0) {
    road = ROAD_CHECK;
    play(PAT_TWO);
    logEvent("Water below 5 cm - inspect, then reopen");
  }
  if (road == ROAD_OPEN && level >= 1 && lastLevel == 0) {
    play(PAT_CHIRP);
    logEvent("Water on the road - caution");
  }

  if (road == ROAD_CLOSED) {
    sev = SEV_ALERT; actionLed = ACT_FAST;
    if (level >= 5)      { headline = "EMERGENCY - RESCUE ALERT";     detail = "60 cm or more: check for trapped vehicles and call 112."; }
    else if (level == 4) { headline = "ROAD CLOSED - SEVERE";         detail = "45-60 cm of flowing water can sweep vehicles away."; }
    else if (level == 3) { headline = "ROAD CLOSED - cars can float"; detail = "30 cm of water can float many cars. Keep everyone out."; }
    else if (level == 2) { headline = "ROAD CLOSED - divert traffic"; detail = "15 cm reaches the bottom of most cars and can stall them."; }
    else                 { headline = "ROAD CLOSED - water falling";  detail = "Keep it closed until the water is below 5 cm."; }
    if (levelRate() < 0)            { etaSec = etaTo(0);         etaLabel = "Below 5 cm in"; }
    else if (level < NUM_PROBES)    { etaSec = etaTo(level + 1); etaLabel = "Next danger level in"; }
  } else if (road == ROAD_CHECK) {
    sev = SEV_WARN; actionLed = ACT_SOLID;
    headline = "INSPECT, THEN REOPEN";
    detail = level == 0 ? "Water is below 5 cm. Check the road and drains, then tap bolt A to reopen."
                        : "Water is back on the road. Wait, then inspect before reopening.";
    etaSec = etaTo(CLOSE_LEVEL); etaLabel = "Closes again in";
  } else if (level == 1) {
    sev = SEV_WARN; actionLed = ACT_SLOW;
    headline = "CAUTION - water on the road";
    detail = "Slow down. The road closes by itself at 15 cm.";
    etaSec = etaTo(CLOSE_LEVEL); etaLabel = "Closure expected in";
  } else {
    sev = SEV_OK; actionLed = ACT_HEARTBEAT;
    headline = "ROAD OPEN";
    detail = "No water on the road. Live depth goes to the control-room phone.";
    etaSec = etaTo(CLOSE_LEVEL); etaLabel = "Closure expected in";
  }

#else
  // ---- School water tank
  if (level == 0 && !alarmOn) { startAlarm(PAT_ALERT); logEvent("TANK EMPTY - toilets unusable"); }
  if (level > 0 && alarmOn) stopAlarm();
  if (level == 1 && lastLevel > 1) { play(PAT_THREE); logEvent("Tank low - refill now"); }
  if (level >= NUM_PROBES && lastLevel >= 0 && lastLevel < NUM_PROBES) {
    play(PAT_STOP); counter++; counterAt = now; bannerUntil = now + 20000;
    logEvent("Tank full - switch the motor OFF");
  }

  if (level == 0) {
    sev = SEV_ALERT; actionLed = ACT_FAST;
    headline = "TANK EMPTY";
    detail = "Toilets and hand-wash have no water. Refill now.";
  } else if (level == 1) {
    sev = SEV_WARN; actionLed = ACT_SLOW;
    headline = "Water low - refill soon";
    detail = "Tell the caretaker to start the motor.";
    etaSec = etaTo(0); etaLabel = "Tank empty in";
  } else if (level >= NUM_PROBES || now < bannerUntil) {
    sev = SEV_INFO; actionLed = ACT_SOLID;
    headline = "TANK FULL - motor OFF";
    detail = "Switch off now to stop overflow and wasted water.";
  } else {
    sev = SEV_OK; actionLed = ACT_HEARTBEAT;
    headline = "Water OK";
    detail = "Toilets and hand-wash stations have water.";
    long up = etaTo(NUM_PROBES), down = etaTo(0);
    if (up >= 0) { etaSec = up; etaLabel = "Tank full in"; }
    else         { etaSec = down; etaLabel = "Tank empty in"; }
  }
#endif

#if THEME != 1
  if (now < testUntil) { sev = SEV_ALERT; actionLed = ACT_FAST; headline = "TEST - alarm check"; detail = "This is only a test."; }
#endif
  if (fault) {
    if (sev < SEV_WARN) sev = SEV_WARN;
    detail = "A higher tip reads wet while a lower one is dry: check the gauge (bubble, loose wire or splash).";
  }
  lastLevel = level;
}

// ============================ 8. OUTPUTS ==================================
void updateLeds() {
  uint32_t t = millis();
  for (uint8_t i = 0; i < NUM_PROBES; i++) digitalWrite(LED_PIN[i], wet[i] ? HIGH : LOW);
  bool on = false;
  switch (actionLed) {
    case ACT_FAST:      on = (t / 125) % 2; break;
    case ACT_SLOW:      on = (t / 500) % 2; break;
    case ACT_SOLID:     on = true; break;
    case ACT_HEARTBEAT: on = (t % 3000) < 80; break;
  }
  digitalWrite(LED_PIN[5], on ? HIGH : LOW);
}

void alarmTick() {
  if (!alarmOn || alarmAcked) return;
  if (alarmPlayNow || millis() - alarmLast >= REALERT_MS) {
    play(alarmPattern);
    alarmLast = millis();
    alarmPlayNow = false;
  }
}

void selfTest() {
  logEvent("Self-test");
  for (uint8_t r = 0; r < 2; r++)
    for (uint8_t i = 0; i < 6; i++) { digitalWrite(LED_PIN[i], HIGH); delay(90); digitalWrite(LED_PIN[i], LOW); }
  buzz(true); delay(120); buzz(false);
}

// ============================ 9. ACTIONS ==================================
void actionOK() {
  pat = nullptr; buzz(false);                          // stop any sound now
  if (alarmOn && !alarmAcked) { alarmAcked = true; logEvent("Alarm acknowledged"); return; }
#if THEME == 2
  if (road == ROAD_CHECK && level == 0) {             // a person has inspected the underpass
    road = ROAD_OPEN;
    logEvent("Inspected - road reopened");
    play(PAT_TWO);
  }
#endif
}

// what bolt A / the first phone button will do right now
const char* buttonALabel() {
  if (alarmOn && !alarmAcked) return "Silence alarm";
#if THEME == 2
  if (road == ROAD_CHECK && level == 0) return "Inspected - reopen road";
#endif
  return "OK";
}

void actionMode() {
#if THEME == 1
  flowering = !flowering;
  logEvent(flowering ? "Flowering mode ON (keep flooded)" : "AWD mode ON (dry to -15 cm)");
  play(flowering ? PAT_TWO : PAT_CHIRP);
  // the alarm was only needed under the old rule: cancel it, no pump needed
  if (phase == NEED_WATER && level > (flowering ? 3 : 0)) { phase = DRYING; stopAlarm(); }
#elif THEME == 2
  testUntil = millis() + 4000; play(PAT_SIREN); logEvent("Siren test");
#else
  testUntil = millis() + 4000; play(PAT_ALERT); logEvent("Test alarm");
#endif
}

void actionCalibrate() { calibrateProbes(); }

// bolt touch-buttons: tap = short action, hold HOLD_MS = long action
void readPads() {
  for (uint8_t k = 0; k < 2; k++) {
    uint32_t v = readMedian(PAD_PIN[k]);
    bool touched = v < padBase[k] * TOUCH_RATIO;
    if (!touched && !padDown[k]) trackBase(padBase[k], v, 0.9f);
    if (touched != padDown[k]) {
      if (++padAgree[k] >= 2) {
        padAgree[k] = 0;
        padDown[k] = touched;
        if (touched) { padSince[k] = millis(); padLongDone[k] = false; }
        else if (!padLongDone[k]) {                 // let go before the hold time = tap
          if (k == 0) actionOK(); else actionMode();
        }
      }
    } else {
      padAgree[k] = 0;
    }
    if (padDown[k] && !padLongDone[k] && millis() - padSince[k] >= HOLD_MS) {
      padLongDone[k] = true;
      if (k == 0) actionCalibrate(); else selfTest();
    }
  }
}

// ============================ 10. PHONE DASHBOARD =========================
const char PAGE[] = R"rawliteral(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Dashboard</title>
<style>
:root{--bg:#eef2f5;--ink:#13212e;--mut:#5b6b7a}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--ink)}
header{padding:14px 16px;background:#0e2a3b;color:#fff}h1{margin:0;font-size:20px}header p{margin:3px 0 0;font-size:13px;opacity:.8}
main{padding:12px;max-width:640px;margin:auto;display:grid;gap:12px}
.card{background:#fff;border-radius:14px;padding:14px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
#st{color:#fff;transition:background .3s}#st h2{margin:0;font-size:24px}#st p{margin:6px 0 0;font-size:15px}
.row{display:flex;gap:14px}.gauge{display:flex;flex-direction:column-reverse;gap:5px;width:118px}
.cell{height:32px;border-radius:7px;background:#dde5ec;display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:600;color:#3a4a58}
.cell.w{background:#2563eb;color:#fff}
.kv{flex:1;display:grid;gap:8px;font-size:15px;align-content:start}.kv b{display:block;font-size:12px;color:var(--mut)}
.btns{display:flex;flex-wrap:wrap;gap:8px}button{font:inherit;border:0;border-radius:10px;padding:10px 14px;background:#0e2a3b;color:#fff}
canvas{width:100%;height:150px;display:block}ul{margin:6px 0 0;padding-left:18px;font-size:13px;line-height:1.5}
table{width:100%;font-size:12px;border-collapse:collapse;margin-top:6px}td,th{padding:5px 4px;border-bottom:1px solid #e3e8ed;text-align:left}
small{color:var(--mut)}
</style></head><body>
<header><h1 id="t">Dashboard</h1><p id="s"></p></header>
<main>
<section class="card" id="st" style="background:#64748b"><h2 id="h">Connecting...</h2><p id="d"></p></section>
<section class="card row"><div class="gauge" id="g"></div><div class="kv">
<div><b>Water level</b><span id="lt">-</span></div><div><b id="ml">Mode</b><span id="md">-</span></div>
<div><b id="el">Estimate</b><span id="et">-</span></div><div><b id="cl">Count</b><span id="cn">-</span></div></div></section>
<section class="card"><div class="btns"><button id="ba" onclick="cmd('ok')">OK</button><button id="bb" onclick="cmd('mode')">Mode</button>
<button onclick="if(confirm('Is the gauge OUT of the water and dry?'))cmd('cal')">Re-calibrate</button></div></section>
<section class="card"><b>Water level history</b> <small>(last 4 minutes)</small><canvas id="c" width="600" height="170"></canvas></section>
<section class="card"><b>Event log</b><ul id="ev"></ul></section>
<section class="card"><b>Sensor data</b> <small>(100% = dry; a wet tip drops below 40%)</small><table id="tb"></table></section>
</main>
<script>
const $=i=>document.getElementById(i),COL=['#15803d','#1d4ed8','#b45309','#b91c1c'];
function cmd(c){fetch('/cmd?c='+c).then(tick)}
function dur(s){return s<60?Math.round(s)+' s':s<3600?Math.round(s/60)+' min':(s/3600).toFixed(1)+' h'}
function ago(s){return s<5?'just now':dur(s)+' ago'}
function draw(a,L){const n=L.length,c=$('c'),x=c.getContext('2d'),W=c.width,H=c.height,M=74,y=v=>H-12-(H-24)*v/n;x.clearRect(0,0,W,H);
x.font='12px sans-serif';x.fillStyle='#8a99a6';x.strokeStyle='#e3e8ed';x.lineWidth=1;
for(let k=0;k<=n;k++){x.beginPath();x.moveTo(M,y(k));x.lineTo(W,y(k));x.stroke();x.fillText(k?L[k-1]:'none',4,y(k)+4)}
if(a.length<2)return;x.strokeStyle='#2563eb';x.lineWidth=3;x.beginPath();
a.forEach((v,i)=>{const X=M+(W-M)*i/(a.length-1);i?x.lineTo(X,y(v)):x.moveTo(X,y(v))});x.stroke()}
function tick(){fetch('/data').then(r=>r.json()).then(j=>{
$('t').textContent=j.title;$('s').textContent=j.sub;document.title=j.title;
$('st').style.background=COL[j.sev];$('h').textContent=j.head;$('d').textContent=j.det;
$('lt').textContent=j.ltext;$('ml').textContent=j.ml;$('md').textContent=j.mode;$('el').textContent=j.etal;$('ba').textContent=j.al;
$('et').textContent=j.eta<0?'-':j.eta<5?'any moment now':'about '+dur(j.eta);$('cl').textContent=j.cntl;
$('cn').textContent=j.cnt+(j.cnt?' (last '+ago(j.cntago)+')':'');$('bb').textContent=j.bl;
$('g').innerHTML=j.pl.map((p,i)=>'<div class="cell'+(j.wet[i]?' w':'')+'">'+p+'</div>').join('');
$('ev').innerHTML=j.ev.map(e=>'<li>'+ago(e.a)+' - '+e.m+'</li>').join('');
$('tb').innerHTML='<tr><th>Tip</th><th>Dry</th><th>Now</th><th>%</th><th></th></tr>'+j.pl.map((p,i)=>
'<tr><td>P'+(i+1)+' ('+p+')</td><td>'+j.base[i]+'</td><td>'+j.raw[i]+'</td><td>'+(j.base[i]?Math.round(100*j.raw[i]/j.base[i]):0)+'%</td><td>'+(j.wet[i]?'WET':'dry')+'</td></tr>').join('');
draw(j.hist,j.pl)}).catch(()=>{$('h').textContent='Reconnecting...';$('st').style.background='#64748b'})}
setInterval(tick,1000);tick();
</script></body></html>)rawliteral";

void jsonStr(String& out, const char* s) {   // add a JSON string, escaping quotes
  out += '"';
  for (const char* p = s; *p; p++) {
    if (*p == '"' || *p == '\\') { out += '\\'; out += *p; }
    else if ((uint8_t)*p >= 0x20) out += *p;
  }
  out += '"';
}

void handleData() {
  uint32_t now = millis();
  String o;
  o.reserve(2600);
  o += "{\"title\":"; jsonStr(o, TITLE);
  o += ",\"sub\":";   jsonStr(o, SUBTITLE);
  o += ",\"sev\":";   o += (int)sev;
  o += ",\"head\":";  jsonStr(o, headline);
  o += ",\"det\":";   jsonStr(o, detail);
  o += ",\"level\":"; o += (int)level;
  o += ",\"ltext\":"; jsonStr(o, LEVEL_TEXT[level]);
#if THEME == 1
  o += ",\"mode\":";  jsonStr(o, flowering ? "Flowering (keep flooded)" : "AWD (dry to -15 cm)");
  o += ",\"bl\":";    jsonStr(o, flowering ? "Switch to AWD mode" : "Switch to Flowering mode");
#elif THEME == 2
  o += ",\"mode\":";  jsonStr(o, road == ROAD_CLOSED ? (alarmOn && !alarmAcked ? "CLOSED - siren on" : "CLOSED")
                               : road == ROAD_CHECK ? "Closed - waiting for inspection" : "Open");
  o += ",\"bl\":";    jsonStr(o, "Test siren");
#else
  o += ",\"mode\":";  jsonStr(o, alarmOn ? (alarmAcked ? "Alarm acknowledged" : "Alarm ON") : "Monitoring");
  o += ",\"bl\":";    jsonStr(o, "Test alarm");
#endif
  o += ",\"ml\":";    jsonStr(o, MODE_LABEL);
  o += ",\"al\":";    jsonStr(o, buttonALabel());
  o += ",\"eta\":";   o += etaSec;
  o += ",\"etal\":";  jsonStr(o, etaLabel);
  o += ",\"cntl\":";  jsonStr(o, COUNT_LABEL);
  o += ",\"cnt\":";   o += (int)counter;
  o += ",\"cntago\":"; o += counter ? (long)((now - counterAt) / 1000) : 0L;
  o += ",\"pl\":[";
  for (uint8_t i = 0; i < NUM_PROBES; i++) { if (i) o += ','; jsonStr(o, PROBE_LABEL[i]); }
  o += "],\"wet\":[";
  for (uint8_t i = 0; i < NUM_PROBES; i++) { if (i) o += ','; o += wet[i] ? 1 : 0; }
  o += "],\"raw\":[";
  for (uint8_t i = 0; i < NUM_PROBES; i++) { if (i) o += ','; o += (long)raw[i]; }
  o += "],\"base\":[";
  for (uint8_t i = 0; i < NUM_PROBES; i++) { if (i) o += ','; o += (long)(base[i] + 0.5f); }
  o += "],\"hist\":[";
  for (uint8_t i = 0; i < histCount; i++) {
    if (i) o += ',';
    o += (int)hist[(histHead + HIST_N - histCount + i) % HIST_N];
  }
  o += "],\"ev\":[";
  for (uint8_t i = 0; i < evCount; i++) {          // newest first
    const LogEntry& e = events[(evHead + EV_N - 1 - i) % EV_N];
    if (i) o += ',';
    o += "{\"a\":"; o += (long)((now - e.t) / 1000); o += ",\"m\":"; jsonStr(o, e.msg); o += '}';
  }
  o += "],\"fault\":"; o += fault ? 1 : 0;
  o += ",\"up\":";    o += (long)(now / 1000);
  o += '}';
  server.send(200, "application/json", o);
}

void handleRoot() { server.send(200, "text/html", PAGE); }

void handleCmd() {
  String c = server.arg("c");
  if (c == "ok") actionOK();
  else if (c == "mode") actionMode();
  else if (c == "cal") actionCalibrate();
  server.send(200, "application/json", "{\"ok\":1}");
}

// ============================ 11. SETUP & LOOP ============================
void setup() {
  Serial.begin(115200);
  delay(200);
  for (uint8_t i = 0; i < 6; i++) { pinMode(LED_PIN[i], OUTPUT); digitalWrite(LED_PIN[i], LOW); }
  pinMode(BUZZER_PIN, OUTPUT);
  buzz(false);
  Serial.println();
  Serial.print("# "); Serial.print(TITLE); Serial.println(" starting");

  // start every touch channel, then give the hardware time for real readings
  // (a channel read too early returns 0, which would look like "wet")
  for (uint8_t i = 0; i < NUM_PROBES; i++) touchRead(PROBE_PIN[i]);
  for (uint8_t k = 0; k < 2; k++) touchRead(PAD_PIN[k]);
  delay(250);
  for (uint8_t k = 0; k < 2; k++) padBase[k] = readAverage(PAD_PIN[k], 8);   // don't touch the bolts now
  if (!loadCalibration()) {
    Serial.println("# No saved calibration: calibrating now - keep the gauge OUT of the water");
    calibrateProbes();
  } else {
    logEvent("Loaded saved calibration");
  }
  // first reading without waiting, so a full bottle at power-on is not a false alarm
  for (uint8_t i = 0; i < NUM_PROBES; i++) {
    raw[i] = readMedian(PROBE_PIN[i]);
    wet[i] = raw[i] < base[i] * WET_RATIO;
  }
  computeLevel();
  decide();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  dns.start(53, "*", ip);                 // any web address opens the dashboard
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/cmd", handleCmd);
  server.onNotFound(handleRoot);
  server.begin();

  Serial.print("# Wi-Fi: "); Serial.print(AP_SSID);
  Serial.print("  password: "); Serial.print(AP_PASSWORD);
  Serial.print("  then open http://"); Serial.println(ip);
  logEvent("System ready");
  play(PAT_CHIRP);
}

uint32_t lastSample = 0, lastPads = 0, lastHist = 0, lastPrint = 0;

void loop() {
  dns.processNextRequest();
  server.handleClient();
  uint32_t now = millis();

  if (now - lastPads >= PAD_MS) {
    lastPads = now;
    readPads();
  }
  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    readProbes();
    decide();
  }
  if (now - lastHist >= HISTORY_MS) {
    lastHist = now;
    hist[histHead] = level;
    histHead = (histHead + 1) % HIST_N;
    if (histCount < HIST_N) histCount++;
  }
  if (now - lastPrint >= 500) {            // Tools > Serial Plotter draws these as graphs
    lastPrint = now;
    for (uint8_t i = 0; i < NUM_PROBES; i++) {
      Serial.print("P"); Serial.print(i + 1); Serial.print(":");
      Serial.print((long)(100.0f * raw[i] / base[i]));
      Serial.print(",");
    }
    Serial.print("Level_x20:"); Serial.println(level * 20);
  }
  alarmTick();
  buzzerTick();
  updateLeds();
}
