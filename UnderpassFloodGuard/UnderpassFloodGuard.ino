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
   This "latch" is saved in flash, so it also survives a power cut.

   MODEL SCALE: 1 cm in the glass = 6 cm of water on the road. Tips sit
   0.8, 2.5, 5, 7.5 and 10 cm above the skewer's point; GND tip at the point.

   PHONE
   Join Wi-Fi "FloodGuard-ESP32" (password kreativity), then open any web
   address, or 192.168.4.1. Pages:
     /             control-room dashboard (big status, underpass drawing,
                   chart, event log, buttons, DEMO storm)
     /sign         full-screen "public road sign" for a second phone
     /history.csv  last few minutes of water levels, for your test table

   SERIAL MONITOR (115200 baud) - type a command and press Enter:
     help, status, ok, test, selftest, cal, log, csv, resetcount

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
const uint8_t  PAD_CONFIRM     = 2;        // bolt readings that must agree
const uint32_t SAMPLE_MS  = 150;           // how often the tips are read
const uint32_t PAD_MS     = 50;            // how often the bolts are read (quick taps)
const uint32_t REALERT_MS = 20000;         // repeat the alarm every 20 s until "OK"
const uint32_t HOLD_MS    = 5000;          // long-press time for the bolts
const uint32_t WARMUP_MS  = 250;           // touch hardware warm-up at power-on
const uint32_t TEST_MS    = 4000;          // how long "TEST" stays on screen

// "Rising fast": two tips get wet within this time of each other.
// These are MODEL seconds (water poured into a glass). On a real road you
// would use minutes, e.g. 10 * 60000UL.
const uint32_t RISE_FAST_MS = 8000;
const uint32_t RISE_SHOW_MS = 60000;       // keep the "rising fast" warning on screen

// Phone history chart: one point every 2 s, 120 points = 4 minutes
const uint32_t HISTORY_MS = 2000;
const uint8_t  HIST_N     = 120;
const uint8_t  EV_N       = 16;            // how many events the log keeps
const uint32_t PLOT_MS    = 500;           // Serial Plotter update

// Flash memory wears out after very many writes, so the event log is saved
// at most once a minute. Closures and the road state are saved at once
// (they only change a few times per storm).
const uint32_t LOG_SAVE_MS = 60000;

// ============================ 2. THEME TEXT ===============================
#if THEME == 1
const char* AP_SSID  = "PaniPipe-ESP32";
const char* TITLE    = "Smart Pani-Pipe";
const char* SUBTITLE = "AWD irrigation advisor for paddy fields";
const char* PROBE_LABEL[NUM_PROBES] = {"-15 cm", "-10 cm", "-5 cm", "0 cm soil", "+5 cm"};
const char* LEVEL_TEXT[NUM_PROBES + 1] = {
  "Below -15 cm", "-15 to -10 cm", "-10 to -5 cm", "-5 to 0 cm",
  "0 to +5 cm (above soil)", "+5 cm or more (target)"};
const int16_t LEVEL_VALUE[NUM_PROBES + 1] = {-20, -15, -10, -5, 0, 5};   // for the chart
const char* UNIT        = "cm";
const char* COUNT_LABEL = "Irrigations";
const char* MODE_LABEL  = "Mode";
const int8_t CLOSE_LEVEL = -1;            // no "closed zone" on the chart
#elif THEME == 2
const char* AP_SSID  = "FloodGuard-ESP32";
const char* TITLE    = "Underpass Flood Guard";
const char* SUBTITLE = "Live water depth at the underpass low point";
const char* PROBE_LABEL[NUM_PROBES] = {"5 cm", "15 cm", "30 cm", "45 cm", "60 cm"};
const char* LEVEL_TEXT[NUM_PROBES + 1] = {
  "Under 5 cm (road clear)", "5-15 cm", "15-30 cm", "30-45 cm",
  "45-60 cm", "60 cm or more"};
const int16_t LEVEL_VALUE[NUM_PROBES + 1] = {0, 5, 15, 30, 45, 60};      // road depth in cm
const char* UNIT        = "cm";
const char* COUNT_LABEL = "Closures";
const char* MODE_LABEL  = "Road status";
const int8_t CLOSE_LEVEL = 2;             // tip P2 (15 cm) closes the road
#else
const char* AP_SSID  = "WaterGuard-ESP32";
const char* TITLE    = "School Water Guard";
const char* SUBTITLE = "Water for toilets & hand-wash, always";
const char* PROBE_LABEL[NUM_PROBES] = {"10%", "30%", "50%", "70%", "90%"};
const char* LEVEL_TEXT[NUM_PROBES + 1] = {
  "Below 10%", "10-30%", "30-50%", "50-70%", "70-90%", "90% or more"};
const int16_t LEVEL_VALUE[NUM_PROBES + 1] = {0, 10, 30, 50, 70, 90};
const char* UNIT        = "%";
const char* COUNT_LABEL = "Refills";
const char* MODE_LABEL  = "Status";
const int8_t CLOSE_LEVEL = -1;
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
const char* statusWord = "START";  // one big word: OPEN / CAUTION / CLOSED / INSPECT
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
uint16_t counter = 0;             // irrigations / closures / refills (saved in flash)
uint32_t counterAt = 0;
bool     counterAtKnown = false;  // false if the last closure was before a restart
uint32_t bannerUntil = 0;         // keeps "STOP THE PUMP" / "TANK FULL" on screen
uint32_t testUntil = 0;           // themes 2 and 3: test alarm on screen
enum Road { ROAD_OPEN, ROAD_CLOSED, ROAD_CHECK };
Road     road = ROAD_OPEN;        // theme 2: open / closed / waiting for inspection
uint32_t closedAt = 0;            // when the road closed (for "closed for 3:20")
bool     resumedClosure = false;  // restarted while the road was closed: same flood

// level-change memory (for the time estimate and "rising fast")
uint32_t chgT[2] = {0, 0};
int8_t   chgL[2] = {-1, -1};
uint32_t upAt[NUM_PROBES + 1];    // when each level was last reached going UP
uint32_t riseFastUntil = 0;

// history for the phone chart (ring buffer)
uint8_t  hist[HIST_N];
uint8_t  histCount = 0, histHead = 0;
uint32_t histTotal = 0;           // points recorded since power-on (the phone asks "what's new?")

// event log (ring buffer, saved in flash). The type picks the icon/colour.
enum EvType { EV_SYS, EV_LEVEL, EV_WARN, EV_ALARM, EV_RISE, EV_SILENCE, EV_CHECK, EV_GOOD, EV_TEST };
struct LogEntry { uint32_t t; uint8_t type; uint8_t old; char msg[50]; };   // old = before restart
LogEntry events[EV_N];
uint8_t  evCount = 0, evHead = 0;
uint32_t evSeq = 0;               // goes up by one for every new event
bool     logDirty = false;        // log changed since it was last saved
uint32_t lastLogSave = 0;

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

// ============================ 5. LOG + FLASH MEMORY =======================
void logEvent(EvType type, const char* msg) {
  LogEntry& e = events[evHead];
  e.t = millis();
  e.type = (uint8_t)type;
  e.old = 0;
  strncpy(e.msg, msg, sizeof(e.msg) - 1);
  e.msg[sizeof(e.msg) - 1] = 0;
  evHead = (evHead + 1) % EV_N;
  if (evCount < EV_N) evCount++;
  evSeq++;
  logDirty = true;
  Serial.print("# EVENT: "); Serial.println(msg);
}

// Save closures, road state and the event log ("log" area of flash).
void saveState() {
  prefs.begin("log", false);
  prefs.putUInt("cnt", counter);
  prefs.putUInt("road", (uint32_t)road);
  prefs.putBytes("ev", events, sizeof(events));
  prefs.putUInt("evh", evHead);
  prefs.putUInt("evc", evCount);
  prefs.end();
  logDirty = false;
  lastLogSave = millis();
}

// Called every loop: saves the log only if it changed AND a minute has passed.
void saveLogIfDue() {
  if (logDirty && millis() - lastLogSave >= LOG_SAVE_MS) saveState();
}

// Read back what was saved before the last power cut / restart.
void loadState() {
  prefs.begin("log", false);
  counter = prefs.getUInt("cnt", 0);
  uint32_t savedRoad = prefs.getUInt("road", ROAD_OPEN);
  if (prefs.getBytesLength("ev") == sizeof(events)) {
    prefs.getBytes("ev", events, sizeof(events));
    evHead  = prefs.getUInt("evh", 0) % EV_N;
    evCount = prefs.getUInt("evc", 0);
    if (evCount > EV_N) evCount = EV_N;
    for (uint8_t i = 0; i < EV_N; i++) events[i].old = 1;   // their times belong to the old run
  }
  prefs.end();
  evSeq++;
#if THEME == 2
  if (savedRoad != ROAD_OPEN) {          // power was lost while the road was closed
    road = ROAD_CHECK;                   // keep it closed until someone inspects
    resumedClosure = true;
  }
#else
  (void)savedRoad;
#endif
}

// Serial command "resetcount": start a fresh demo (closures 0, empty log).
void clearState() {
  counter = 0; counterAtKnown = false;
  evCount = 0; evHead = 0; evSeq++;
  saveState();
}

void startAlarm(const uint16_t* p) { alarmOn = true; alarmAcked = false; alarmPlayNow = true; alarmPattern = p; }
void stopAlarm()                    { alarmOn = false; }
bool sirenActive()                  { return alarmOn && !alarmAcked; }

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
  logEvent(EV_TEST, "Calibrated (gauge dry)");
  play(PAT_TWO);
}

// Two tips got wet within RISE_FAST_MS of each other -> "rising fast".
void checkRiseFast(uint8_t newLevel) {
  uint32_t now = millis();
  for (uint8_t k = level + 1; k <= newLevel; k++) upAt[k] = now;
  if (lastLevel < 0 || newLevel < 2) return;            // not at power-on
  uint32_t prev = upAt[newLevel - 1];
  if (prev == 0 || now - prev > RISE_FAST_MS) return;
  if (now >= riseFastUntil) {                            // log once per spell
    char m[50];
    snprintf(m, sizeof(m), "Rising fast: 2 levels in %lu s", (unsigned long)((now - prev) / 1000 + 1));
    logEvent(EV_RISE, m);
  }
  riseFastUntil = now + RISE_SHOW_MS;
}

void computeLevel() {
  uint8_t l = 0;
  while (l < NUM_PROBES && wet[l]) l++;
  bool f = false;
  for (uint8_t i = l; i < NUM_PROBES; i++) if (wet[i]) f = true;
  fault = f;
  if (l != level) {
    if (l > level) checkRiseFast(l);
    chgT[1] = chgT[0]; chgL[1] = chgL[0];
    chgT[0] = millis(); chgL[0] = l;
    level = l;
    char m[50];
    snprintf(m, sizeof(m), "Level now: %s", LEVEL_TEXT[level]);
    logEvent(EV_LEVEL, m);
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
bool risingFast() { return millis() < riseFastUntil && levelRate() > 0; }

// ============================ 7. DECISIONS ================================
long        etaSec = -1;
const char* etaLabel = "Estimate";

#if THEME == 2
// ---- Underpass Flood Guard. Water depth at the low point -> decision:
//   level 1 =  5 cm : caution
//   level 2 = 15 cm : ROAD CLOSED. Delhi PWD diverts traffic once water
//                     reaches six inches, and six inches reaches the bottom
//                     of most cars, causing loss of control and stalling.
//   level 3 = 30 cm : twelve inches of water can float many cars
//   level 4 = 45 cm : 18-24 inches of flowing water can carry most vehicles away
//   level 5 = 60 cm : emergency
//   (car figures: US National Weather Service flood-safety advice)

// Step 1: change the road state (open -> closed -> inspect -> open)
void updateRoad(uint32_t now) {
  if (level >= CLOSE_LEVEL && road != ROAD_CLOSED) {
    road = ROAD_CLOSED;
    closedAt = now;
    if (resumedClosure) {                      // same flood as before the restart
      logEvent(EV_ALARM, "Still flooded after restart: CLOSED");
    } else {
      counter++; counterAt = now; counterAtKnown = true;
      logEvent(EV_ALARM, "ROAD CLOSED - divert traffic");
    }
    resumedClosure = false;
    startAlarm(PAT_SIREN);
    saveState();
  } else if (road == ROAD_CLOSED && lastLevel >= 0 && level > lastLevel && level >= CLOSE_LEVEL) {
    startAlarm(PAT_SIREN);                     // rising again: sound even if it was silenced
    logEvent(level >= 5 ? EV_ALARM : EV_WARN, level >= 5 ? "60 cm: EMERGENCY - call 112" : "Water still rising");
  }
  if (road == ROAD_CLOSED && level < CLOSE_LEVEL && alarmOn) stopAlarm();
  if (road == ROAD_CLOSED && level == 0) {
    road = ROAD_CHECK;
    play(PAT_TWO);
    logEvent(EV_CHECK, "Water below 5 cm - inspect, then reopen");
    saveState();
  }
  if (road == ROAD_OPEN && level >= 1 && lastLevel == 0) {
    play(PAT_CHIRP);
    logEvent(EV_WARN, "Water on the road - caution");
  }
}

// Step 2: choose what the phone, the stop light and the big word show
void showRoad() {
  if (road == ROAD_CLOSED) {
    sev = SEV_ALERT; actionLed = ACT_FAST; statusWord = "CLOSED";
    if (level >= 5)      { headline = "EMERGENCY - RESCUE ALERT";     detail = "60 cm or more: check for trapped vehicles and call 112."; }
    else if (level == 4) { headline = "ROAD CLOSED - SEVERE";         detail = "45-60 cm of flowing water can sweep vehicles away."; }
    else if (level == 3) { headline = "ROAD CLOSED - cars can float"; detail = "30 cm of water can float many cars. Keep everyone out."; }
    else if (level == 2) { headline = "ROAD CLOSED - divert traffic"; detail = "15 cm reaches the bottom of most cars and can stall them."; }
    else                 { headline = "ROAD CLOSED - water falling";  detail = "Keep it closed until the water is below 5 cm."; }
    if (levelRate() < 0)            { etaSec = etaTo(0);         etaLabel = "Below 5 cm in"; }
    else if (level < NUM_PROBES)    { etaSec = etaTo(level + 1); etaLabel = "Next danger level in"; }
  } else if (road == ROAD_CHECK) {
    sev = SEV_WARN; actionLed = ACT_SOLID; statusWord = "INSPECT";
    headline = "INSPECT, THEN REOPEN";
    detail = level == 0 ? "Water is below 5 cm. Check the road and drains, then tap bolt A to reopen."
                        : "Water is back on the road. Wait, then inspect before reopening.";
    etaSec = etaTo(CLOSE_LEVEL); etaLabel = "Closes again in";
  } else if (level == 1) {
    sev = SEV_WARN; actionLed = ACT_SLOW; statusWord = "CAUTION";
    headline = "CAUTION - water on the road";
    detail = "Slow down. The road closes by itself at 15 cm.";
    etaSec = etaTo(CLOSE_LEVEL); etaLabel = "Closure expected in";
  } else {
    sev = SEV_OK; actionLed = ACT_HEARTBEAT; statusWord = "OPEN";
    headline = "ROAD OPEN";
    detail = "No water on the road. Live depth goes to the control-room phone.";
    etaSec = etaTo(CLOSE_LEVEL); etaLabel = "Closure expected in";
  }
}
#endif

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
      logEvent(EV_ALARM, flowering ? "Below soil during flowering: IRRIGATE" : "Below -15 cm: IRRIGATE NOW");
    }
  } else if (phase == NEED_WATER) {
    phase = FILLING; stopAlarm();
    logEvent(EV_WARN, "Water rising: pump is ON");
  }
  if (level >= NUM_PROBES && phase != DRYING) {
    phase = DRYING; stopAlarm(); counter++; counterAt = now; counterAtKnown = true;
    bannerUntil = now + 20000;
    play(PAT_STOP);
    logEvent(EV_GOOD, "Reached +5 cm: STOP THE PUMP");
    saveState();
  }

  if (phase == NEED_WATER) {
    sev = SEV_ALERT; actionLed = ACT_FAST; statusWord = "IRRIGATE";
    headline = "IRRIGATE NOW";
    detail = flowering ? "Flowering stage: keep water above the soil. Start the pump."
                       : "Water is 15 cm below the soil. Start the pump and fill to +5 cm.";
  } else if (phase == FILLING) {
    sev = SEV_INFO; actionLed = ACT_SLOW; statusWord = "FILLING";
    headline = "Filling - keep the pump ON";
    detail = "Stop when the +5 cm light comes on.";
    etaSec = etaTo(NUM_PROBES); etaLabel = "Pump can stop in";
  } else if (now < bannerUntil) {
    sev = SEV_INFO; actionLed = ACT_SOLID; statusWord = "STOP PUMP";
    headline = "STOP THE PUMP";
    detail = "Target depth (+5 cm) reached. Now let the field dry.";
  } else {
    sev = SEV_OK; actionLed = ACT_HEARTBEAT; statusWord = "OK";
    headline = "No irrigation needed";
    detail = flowering ? "Flowering mode: alert as soon as water drops below the soil."
                       : "AWD: let the field dry down to -15 cm. Every skipped irrigation saves water and power.";
    etaSec = etaTo(trigger); etaLabel = "Next irrigation in";
  }

#elif THEME == 2
  updateRoad(now);
  showRoad();

#else
  // ---- School water tank
  if (level == 0 && !alarmOn) { startAlarm(PAT_ALERT); logEvent(EV_ALARM, "TANK EMPTY - toilets unusable"); }
  if (level > 0 && alarmOn) stopAlarm();
  if (level == 1 && lastLevel > 1) { play(PAT_THREE); logEvent(EV_WARN, "Tank low - refill now"); }
  if (level >= NUM_PROBES && lastLevel >= 0 && lastLevel < NUM_PROBES) {
    play(PAT_STOP); counter++; counterAt = now; counterAtKnown = true; bannerUntil = now + 20000;
    logEvent(EV_GOOD, "Tank full - switch the motor OFF");
    saveState();
  }

  if (level == 0) {
    sev = SEV_ALERT; actionLed = ACT_FAST; statusWord = "EMPTY";
    headline = "TANK EMPTY";
    detail = "Toilets and hand-wash have no water. Refill now.";
  } else if (level == 1) {
    sev = SEV_WARN; actionLed = ACT_SLOW; statusWord = "LOW";
    headline = "Water low - refill soon";
    detail = "Tell the caretaker to start the motor.";
    etaSec = etaTo(0); etaLabel = "Tank empty in";
  } else if (level >= NUM_PROBES || now < bannerUntil) {
    sev = SEV_INFO; actionLed = ACT_SOLID; statusWord = "FULL";
    headline = "TANK FULL - motor OFF";
    detail = "Switch off now to stop overflow and wasted water.";
  } else {
    sev = SEV_OK; actionLed = ACT_HEARTBEAT; statusWord = "OK";
    headline = "Water OK";
    detail = "Toilets and hand-wash stations have water.";
    long up = etaTo(NUM_PROBES), down = etaTo(0);
    if (up >= 0) { etaSec = up; etaLabel = "Tank full in"; }
    else         { etaSec = down; etaLabel = "Tank empty in"; }
  }
#endif

#if THEME != 1
  if (now < testUntil) { sev = SEV_ALERT; actionLed = ACT_FAST; statusWord = "TEST"; headline = "TEST - alarm check"; detail = "This is only a test."; }
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
  if (!sirenActive()) return;
  if (alarmPlayNow || millis() - alarmLast >= REALERT_MS) {
    play(alarmPattern);
    alarmLast = millis();
    alarmPlayNow = false;
  }
}

void selfTest() {
  logEvent(EV_TEST, "Self-test");
  for (uint8_t r = 0; r < 2; r++)
    for (uint8_t i = 0; i < 6; i++) { digitalWrite(LED_PIN[i], HIGH); delay(90); digitalWrite(LED_PIN[i], LOW); }
  buzz(true); delay(120); buzz(false);
}

// ============================ 9. ACTIONS ==================================
void actionOK() {
  pat = nullptr; buzz(false);                          // stop any sound now
  if (sirenActive()) { alarmAcked = true; logEvent(EV_SILENCE, "Alarm silenced"); return; }
#if THEME == 2
  if (road == ROAD_CHECK && level == 0) {             // a person has inspected the underpass
    road = ROAD_OPEN;
    resumedClosure = false;
    logEvent(EV_GOOD, "Inspected - road reopened");
    play(PAT_TWO);
    saveState();
  }
#endif
}

// what bolt A / the first phone button will do right now
const char* buttonALabel() {
  if (sirenActive()) return "Silence alarm";
#if THEME == 2
  if (road == ROAD_CHECK && level == 0) return "Inspected - reopen road";
#endif
  return "OK";
}
// the same, as a short code the phone page uses to pick a style / checklist
const char* buttonAKind() {
  if (sirenActive()) return "silence";
#if THEME == 2
  if (road == ROAD_CHECK && level == 0) return "reopen";
#endif
  return "ok";
}

void actionMode() {
#if THEME == 1
  flowering = !flowering;
  logEvent(EV_SYS, flowering ? "Flowering mode ON (keep flooded)" : "AWD mode ON (dry to -15 cm)");
  play(flowering ? PAT_TWO : PAT_CHIRP);
  // the alarm was only needed under the old rule: cancel it, no pump needed
  if (phase == NEED_WATER && level > (flowering ? 3 : 0)) { phase = DRYING; stopAlarm(); }
#elif THEME == 2
  testUntil = millis() + TEST_MS; play(PAT_SIREN); logEvent(EV_TEST, "Siren test");
#else
  testUntil = millis() + TEST_MS; play(PAT_ALERT); logEvent(EV_TEST, "Test alarm");
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
      if (++padAgree[k] >= PAD_CONFIRM) {
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
// The whole web page lives here (HTML + CSS + JavaScript). The phone has no
// internet while it is on the ESP32's Wi-Fi, so nothing is loaded from
// outside: no fonts, no libraries. The page asks /info once, then /data
// about once a second, and draws everything itself.
const char PAGE[] = R"rawliteral(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="light dark"><meta name="theme-color" content="#15191c">
<title>Flood Guard</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect width='32' height='32' rx='7' fill='%2315191c'/%3E%3Cpath d='M4 9h6l6 12 6-12h6' fill='none' stroke='%23f2f0ea' stroke-width='2.5'/%3E%3Cpath d='M9 19h14v5H9z' fill='%235b9bd5'/%3E%3C/svg%3E">
<style>
/* ---------- colours: light ---------- */
:root{
--paper:#f2f0ea;--panel:#faf9f5;--ink:#15191c;--mut:#5c636a;--rule:#d6d1c6;--earth:#e3ddd0;--hatch:#bdb5a3;
--water:#2a6fb0;--water-soft:rgba(42,111,176,.18);--ok:#1b7745;--info:#24618f;--warn:#e0a10c;--alert:#bd232a;--alert-soft:rgba(189,35,42,.09);
--dsp:"DIN Condensed","DIN Alternate","Bahnschrift","sans-serif-condensed","Roboto Condensed","Arial Narrow",system-ui,sans-serif;
--num:"DIN Alternate","Bahnschrift",system-ui,-apple-system,sans-serif;
--txt:system-ui,-apple-system,"Segoe UI",sans-serif;
--out:cubic-bezier(.16,1,.3,1);--io:cubic-bezier(.65,0,.35,1)}
/* ---------- colours: dark (follows the phone setting) ---------- */
@media (prefers-color-scheme:dark){:root{
--paper:#0f1214;--panel:#161a1d;--ink:#ecebe6;--mut:#9aa2a9;--rule:#2b3135;--earth:#1d2225;--hatch:#3a4247;
--water:#4a95da;--water-soft:rgba(74,149,218,.22);--ok:#2a9a5c;--info:#3a82bd;--warn:#e7ab22;--alert:#d9353c;--alert-soft:rgba(217,53,60,.14)}}
*{box-sizing:border-box}
[hidden]{display:none!important}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.45 var(--txt);overflow-x:hidden}
button{font:inherit;color:inherit}
.wrap{max-width:560px;margin:0 auto;padding:0 16px}
/* ---------- top bar ---------- */
.top{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:14px 16px 12px;max-width:560px;margin:0 auto}
.brand{display:flex;align-items:center;gap:10px;min-width:0}
.brand svg{flex:none}
.brand div{min-width:0}
.brand b{display:block;font:700 15px/1.15 var(--dsp);letter-spacing:.08em;text-transform:uppercase}
.brand small{display:block;color:var(--mut);font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.meta{display:flex;align-items:center;gap:6px;flex:none}
.live{display:inline-flex;align-items:center;gap:6px;font:600 12px var(--txt);letter-spacing:.06em;text-transform:uppercase;color:var(--mut);padding:0 4px}
.live i{width:8px;height:8px;border-radius:50%;background:var(--mut)}
.live.on i{background:var(--ok);animation:beat 1s var(--out)}
.live.off i{background:var(--alert)}
@keyframes beat{0%{box-shadow:0 0 0 0 rgba(27,119,69,.55)}100%{box-shadow:0 0 0 9px rgba(27,119,69,0)}}
.chip{min-width:44px;min-height:44px;border:1px solid var(--rule);background:transparent;border-radius:999px;padding:0 12px;font-weight:600;cursor:pointer}
.chip:focus-visible,.btn:focus-visible,.lnk:focus-visible,summary:focus-visible{outline:3px solid var(--water);outline-offset:2px}
/* ---------- warning strips ---------- */
.tape{position:sticky;top:0;z-index:5;display:flex;align-items:center;justify-content:space-between;gap:10px;padding:8px 16px;background:#15191c;color:#f2f0ea;font:700 13px/1.2 var(--txt);letter-spacing:.04em;border-bottom:6px solid transparent;border-image:repeating-linear-gradient(135deg,#f3c300 0 10px,#15191c 10px 20px) 6}
.tape .btn{min-height:36px;padding:0 12px;background:#f3c300;color:#15191c;border:0}
.offbar{position:sticky;top:0;z-index:6;padding:10px 16px;background:var(--alert);color:#fff;font-size:14px}
.offbar b{font-weight:700}
/* ---------- hero: the big status ---------- */
.hero{--sev:var(--mut);--on:#fff;background:var(--sev);color:var(--on);padding:22px 16px 20px;transition:background-color .5s var(--io)}
.hero[data-sev="0"]{--sev:var(--ok)}.hero[data-sev="1"]{--sev:var(--info)}
.hero[data-sev="2"]{--sev:var(--warn);--on:#15191c}.hero[data-sev="3"]{--sev:var(--alert)}
.hero .in{max-width:528px;margin:0 auto}
.eyebrow{margin:0;font:600 12px var(--txt);letter-spacing:.14em;text-transform:uppercase;opacity:.85;display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.pill{display:none;align-items:center;gap:6px;padding:3px 9px 3px 7px;border-radius:999px;background:rgba(0,0,0,.22);letter-spacing:.08em}
.pill.show{display:inline-flex}
.pill i{width:7px;height:7px;border-radius:50%;background:currentColor;animation:blink 1s steps(2,start) infinite}
@keyframes blink{to{visibility:hidden}}
.word{margin:6px 0 2px;font:700 120px/.84 var(--dsp);letter-spacing:-.005em;text-transform:uppercase;white-space:nowrap;overflow:hidden}
.word span{display:inline-block}
.word span.in{animation:rise .55s var(--out)}
@keyframes rise{from{transform:translateY(.35em);opacity:0}to{transform:none;opacity:1}}
.head{margin:10px 0 0;font:600 18px/1.3 var(--txt)}
.figs{display:grid;grid-template-columns:auto minmax(0,1fr);gap:6px 22px;margin-top:18px;padding-top:14px;border-top:1px solid currentColor;border-color:color-mix(in srgb,currentColor 35%,transparent)}
.lbl{display:block;font:600 11px var(--txt);letter-spacing:.14em;text-transform:uppercase;opacity:.8}
.depth{font:700 clamp(40px,14.5vw,56px)/1 var(--num);font-variant-numeric:tabular-nums;letter-spacing:-.02em;white-space:nowrap}
.depth small{font-size:20px;font-weight:600;letter-spacing:0;margin-left:3px}
.eta{align-self:end;font:700 clamp(20px,6.6vw,26px)/1.05 var(--num);overflow-wrap:anywhere;font-variant-numeric:tabular-nums}
.eta.big{font-size:clamp(24px,8.6vw,34px)}
.det{margin:14px 0 0;font-size:15px;opacity:.92;max-width:44ch}
.fast{display:none;margin:14px 0 0;padding:8px 12px;background:rgba(0,0,0,.25);font:700 14px var(--txt);letter-spacing:.06em;text-transform:uppercase}
.fast.show{display:block}
/* ---------- underpass drawing ---------- */
.section{padding:18px 0 4px}
.cap{display:flex;justify-content:space-between;align-items:baseline;gap:10px;margin:0 0 8px}
h2{margin:0;font:700 13px var(--txt);letter-spacing:.14em;text-transform:uppercase}
.cap small{color:var(--mut);font-size:12px;text-align:right}
#sec{display:block;width:100%;height:auto;overflow:visible}
#sec .road{fill:none;stroke:var(--ink);stroke-width:2.5;stroke-linejoin:round}
#sec .draw{stroke-dasharray:900;stroke-dashoffset:900;animation:draw 1.4s .1s var(--io) forwards}
@keyframes draw{to{stroke-dashoffset:0}}
#sec .earth{fill:url(#hatch)}
#sec .deck{fill:var(--panel);stroke:var(--ink);stroke-width:1.5}
#sec .thr{stroke:var(--mut);stroke-width:1;stroke-dasharray:3 4}
#sec .thr.close{stroke:var(--alert);stroke-width:1.6;stroke-dasharray:none}
#sec text{font:600 10.5px var(--txt);fill:var(--mut);paint-order:stroke;stroke:var(--paper);stroke-width:4px;stroke-linejoin:round}
#sec text.close{fill:var(--alert);font-weight:700}
#sec text.k{font:700 9px var(--txt);letter-spacing:.14em;fill:var(--ink)}
#water{fill:var(--water);opacity:.85;transition:transform 1.1s var(--out)}
#surf{stroke:var(--water);stroke-width:2;transition:transform 1.1s var(--out)}
#sec .stick{stroke:#a0794a;stroke-width:3;stroke-linecap:round}
#sec .tip{fill:var(--paper);stroke:var(--ink);stroke-width:1.5;transition:fill .3s}
#sec .tip.w{fill:var(--water);stroke:#fff}
/* ---------- facts ---------- */
.facts{display:grid;grid-template-columns:1fr 1fr;margin:14px 0 0;border-top:1px solid var(--rule)}
.facts div{padding:12px 0 12px;border-bottom:1px solid var(--rule)}
.facts div:nth-child(odd){padding-right:12px}
.facts div:nth-child(even){padding-left:14px;border-left:1px solid var(--rule)}
.facts dt{font:600 11px var(--txt);letter-spacing:.12em;text-transform:uppercase;color:var(--mut)}
.facts dd{margin:3px 0 0;font:600 17px/1.25 var(--txt);font-variant-numeric:tabular-nums}
/* ---------- buttons ---------- */
.acts{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:18px 0 0}
.btn{min-height:52px;border-radius:12px;border:1.5px solid var(--ink);background:transparent;padding:10px 14px;font-weight:650;cursor:pointer;transition:transform .15s var(--out),background-color .2s}
.btn:active{transform:scale(.97)}
.btn.main{grid-column:1/-1;min-height:60px;font-size:18px;background:var(--ink);color:var(--paper)}
.btn.main.silence{background:var(--alert);border-color:var(--alert);color:#fff}
.btn.main.reopen{background:var(--ok);border-color:var(--ok);color:#fff}
.btn.arm{border-color:var(--alert);color:var(--alert);background:var(--alert-soft)}
.btn[disabled]{opacity:.45}
.note{grid-column:1/-1;margin:0;color:var(--mut);font-size:13px}
/* ---------- chart ---------- */
.block{margin-top:28px}
#ch{display:block;width:100%;height:190px;margin-top:8px}
.legend{display:flex;gap:16px;flex-wrap:wrap;margin-top:6px;color:var(--mut);font-size:12px}
.legend i{display:inline-block;width:14px;height:3px;vertical-align:middle;margin-right:6px;background:var(--water)}
.legend i.z{height:10px;background:var(--alert-soft);outline:1px solid var(--alert)}
.legend i.c{width:2px;height:12px;background:var(--alert)}.legend i.o{width:2px;height:12px;background:var(--ok)}
/* ---------- event log ---------- */
#ev{list-style:none;margin:8px 0 0;padding:0;border-top:1px solid var(--rule)}
#ev li{display:grid;grid-template-columns:28px 1fr auto;gap:10px;align-items:start;padding:10px 0;border-bottom:1px solid var(--rule);font-size:14px}
#ev li:first-child{animation:slide .45s var(--out)}
@keyframes slide{from{opacity:0;transform:translateY(-6px)}}
#ev svg{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round;margin-top:1px}
#ev time{color:var(--mut);font-size:12px;white-space:nowrap;padding-top:2px;font-variant-numeric:tabular-nums}
#ev .k3,#ev .k4{color:var(--alert)}#ev .k2,#ev .k6{color:#a87500}#ev .k7{color:var(--ok)}#ev .k1{color:var(--water)}
#ev .k3 span,#ev .k4 span{font-weight:650}
#ev .old{opacity:.6}
@media (prefers-color-scheme:dark){#ev .k2,#ev .k6{color:var(--warn)}}
.empty{color:var(--mut);font-size:14px;padding:10px 0}
/* ---------- sensors ---------- */
details{margin-top:28px;border-top:1px solid var(--rule);border-bottom:1px solid var(--rule)}
summary{min-height:48px;display:flex;align-items:center;justify-content:space-between;cursor:pointer;list-style:none}
summary::-webkit-details-marker{display:none}
summary::after{content:"+";font:400 22px var(--txt);color:var(--mut)}
details[open] summary::after{content:"\2212"}
table{width:100%;border-collapse:collapse;font-size:13px;font-variant-numeric:tabular-nums;margin-bottom:10px}
th{font:600 11px var(--txt);letter-spacing:.1em;text-transform:uppercase;color:var(--mut);text-align:left}
td,th{padding:7px 4px;border-bottom:1px solid var(--rule)}
.bar{position:relative;height:6px;background:var(--rule);border-radius:3px;min-width:60px}
.bar i{position:absolute;left:0;top:0;bottom:0;background:var(--ink);border-radius:3px}
.bar b{position:absolute;top:-3px;bottom:-3px;width:2px;background:var(--alert)}
.wtag{font-weight:700;color:var(--water)}
/* ---------- footer ---------- */
footer{margin:26px 0 0;padding:16px 0 40px;display:flex;flex-wrap:wrap;gap:8px 18px;align-items:center;color:var(--mut);font-size:13px}
.lnk{display:inline-flex;align-items:center;min-height:44px;color:var(--ink);font-weight:600;text-decoration:none;border-bottom:1.5px solid var(--rule);background:none;border-top:0;border-left:0;border-right:0;padding:0;cursor:pointer}
footer .sp{flex-basis:100%;height:0}
/* ---------- stale data while disconnected ---------- */
.stale .hero,.stale main{filter:grayscale(1);opacity:.5;transition:opacity .4s}
/* ---------- sheet (in-page confirm) ---------- */
.sheet{position:fixed;inset:0;z-index:10;display:flex;align-items:flex-end;justify-content:center;background:rgba(10,12,14,.55)}
.sheet[hidden]{display:none}
.card{width:100%;max-width:560px;background:var(--panel);color:var(--ink);border-radius:18px 18px 0 0;padding:22px 16px calc(18px + env(safe-area-inset-bottom));animation:up .35s var(--out)}
@keyframes up{from{transform:translateY(40px);opacity:0}}
.card h3{margin:0 0 6px;font:700 30px/1 var(--dsp);text-transform:uppercase;letter-spacing:.01em}
.card p{margin:0 0 12px;color:var(--mut);font-size:14px}
.card label{display:flex;gap:12px;align-items:center;min-height:52px;border-top:1px solid var(--rule);font-size:16px;cursor:pointer}
.card input{width:24px;height:24px;accent-color:var(--ok);flex:none}
.card .acts{margin-top:12px}
/* ---------- toast ---------- */
.toast{position:fixed;left:50%;bottom:22px;z-index:11;transform:translate(-50%,30px);opacity:0;pointer-events:none;background:var(--ink);color:var(--paper);padding:11px 16px;border-radius:10px;font-size:14px;font-weight:600;max-width:calc(100% - 32px);transition:.3s var(--out)}
.toast.show{opacity:1;transform:translate(-50%,0)}
@media (min-width:600px){.word{font-size:150px}}
@media (prefers-reduced-motion:reduce){*,*::before,*::after{animation:none!important;transition:none!important}#sec .draw{stroke-dashoffset:0}}
</style></head><body>
<div id="demoBar" class="tape" hidden><span id="demoTxt">DEMO &middot; simulated storm &middot; the real alarm is not affected</span><button class="btn" id="demoStop" type="button">Stop</button></div>
<div id="offBar" class="offbar" hidden role="alert"><b>Disconnected</b> &ndash; check the phone is on Wi-Fi <b id="ssid">FloodGuard-ESP32</b>. Last update <span id="lastUp">&ndash;</span>.</div>
<header class="top">
 <div class="brand"><svg width="30" height="30" viewBox="0 0 32 32" aria-hidden="true"><rect width="32" height="32" rx="7" fill="currentColor"/><path d="M4 9h6l6 12 6-12h6" fill="none" stroke="var(--paper)" stroke-width="2.5"/><path d="M9 19h14v5H9z" fill="var(--water)"/></svg>
 <div><b id="t">Flood Guard</b><small id="s">Connecting to the gauge&hellip;</small></div></div>
 <div class="meta"><span id="live" class="live" aria-live="polite"><i></i><span id="liveTxt">Wait</span></span>
 <button id="lang" class="chip" type="button" aria-label="Hindi / English">&#2361;&#2367;&#2306;</button></div>
</header>
<section id="hero" class="hero" data-sev="-1"><div class="in" aria-live="polite">
 <p class="eyebrow"><span id="ml">Road status</span><span id="siren" class="pill"><i></i>Siren on</span></p>
 <h1 class="word" id="word"><span>&middot;&middot;&middot;</span></h1>
 <p class="head" id="head">Connecting to the gauge&hellip;</p>
 <div class="figs">
  <div><span class="lbl" id="dl">Water depth</span><div class="depth" id="depth">&ndash;</div></div>
  <div id="etaBox" hidden><span class="lbl" id="etal">Estimate</span><div class="eta" id="eta">&ndash;</div></div>
 </div>
 <p class="fast" id="fast">Rising fast &ndash; watch the estimate</p>
 <p class="det" id="det"></p>
</div></section>
<main class="wrap">
 <section class="section" aria-label="Underpass cross-section">
  <div class="cap"><h2 id="secT">Underpass, side view</h2><small id="secS">Water at the low point</small></div>
  <svg id="sec" viewBox="0 0 360 236" role="img" aria-label="Drawing of the underpass with the current water level"></svg>
 </section>
 <dl class="facts" id="facts"></dl>
 <div class="acts">
  <button class="btn main" id="ba" type="button">OK</button>
  <button class="btn" id="bb" type="button">Test siren</button>
  <button class="btn" id="bc" type="button">Re-calibrate</button>
  <p class="note" id="note">Bolt A and bolt B on the model do the same as these buttons.</p>
 </div>
 <section class="block">
  <div class="cap"><h2>Depth history</h2><small id="chS">last 4 min</small></div>
  <canvas id="ch" aria-label="Chart of water depth over the last minutes"></canvas>
  <div class="legend" id="leg"><span><i></i>Water depth</span><span id="lgz"><i class="z"></i>Road closed zone</span><span id="lgc"><i class="c"></i>Closed</span><span id="lgo"><i class="o"></i>Reopened</span></div>
 </section>
 <section class="block">
  <div class="cap"><h2>Event log</h2><small>newest first</small></div>
  <ol id="ev"><li class="empty">No events yet.</li></ol>
 </section>
 <details><summary><h2>Sensor readings</h2></summary>
  <p class="note" style="margin:0 0 6px">100% = dry. A tip counts as wet below 40% (red mark).</p><table id="tb"></table></details>
 <footer>
  <button class="lnk" id="demoBtn" type="button">Run demo storm</button>
  <a class="lnk" href="/sign">Public sign</a>
  <a class="lnk" href="/history.csv">Download history (CSV)</a>
  <span class="sp"></span><span id="up">&nbsp;</span>
 </footer>
</main>
<div class="sheet" id="sheet" hidden role="dialog" aria-modal="true" aria-labelledby="shT"><div class="card">
 <h3 id="shT">Reopen the road?</h3>
 <p>Tick each check after you have looked at the underpass.</p>
 <label><input type="checkbox" class="ck">Water is below 5 cm</label>
 <label><input type="checkbox" class="ck">No stalled vehicles in the dip</label>
 <label><input type="checkbox" class="ck">Drains are clear</label>
 <div class="acts"><button class="btn" id="shNo" type="button">Cancel</button><button class="btn main reopen" id="shYes" type="button" disabled style="grid-column:auto">Reopen road</button></div>
</div></div>
<div class="toast" id="toast" role="status"></div>
<script>
"use strict";
const $=id=>document.getElementById(id);
let I=null;            // fixed facts from /info (labels, thresholds)
let J=null;            // latest live data from /data
let hist=[],H=-1;      // chart points, and how many the ESP32 has sent us
let evs=[],E=-1,evAt=0;// event list, its version number, and when it arrived
let lastOk=0,demo=null,hi=false,calArm=0;

/* ---------- Hindi text for the public-facing words ---------- */
const HI={OPEN:"खुला",CAUTION:"सावधान",CLOSED:"बंद",INSPECT:"जाँच",TEST:"टेस्ट",
"ROAD OPEN":"सड़क खुली है",
"CAUTION - water on the road":"सावधान – सड़क पर पानी",
"ROAD CLOSED - divert traffic":"सड़क बंद – दूसरा रास्ता लें",
"ROAD CLOSED - cars can float":"सड़क बंद – गाड़ियाँ तैर सकती हैं",
"ROAD CLOSED - SEVERE":"सड़क बंद – गंभीर खतरा",
"EMERGENCY - RESCUE ALERT":"आपातकाल – 112 पर कॉल करें",
"ROAD CLOSED - water falling":"सड़क बंद – पानी उतर रहा है",
"INSPECT, THEN REOPEN":"जाँच के बाद ही सड़क खोलें",
"TEST - alarm check":"टेस्ट – अलार्म जाँच",
"Road status":"सड़क की स्थिति","Water depth":"पानी की गहराई"};
const tr=s=>hi&&HI[s]?HI[s]:s;

/* ---------- small helpers ---------- */
const dur=(s)=>{s=Math.max(0,Math.round(s));return s<60?s+" s":s<3600?Math.floor(s/60)+" min "+String(s%60).padStart(2,"0")+" s":Math.floor(s/3600)+" h "+Math.floor(s%3600/60)+" min"}
const clock=(s)=>{s=Math.max(0,Math.round(s));const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=String(s%60).padStart(2,"0");return h?h+":"+String(m).padStart(2,"0")+":"+x:m+":"+x}
const ago=(s)=>{return s<0?"before restart":s<10?"just now":s<60?Math.round(s)+" s ago":s<3600?Math.floor(s/60)+" min ago":(s/3600).toFixed(1)+" h ago"}
const esc=(s)=>{return String(s).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]))}
const toast=(m)=>{const t=$("toast");t.textContent=m;t.classList.add("show");clearTimeout(toast.h);toast.h=setTimeout(()=>t.classList.remove("show"),2600)}
const css=(n)=>{return getComputedStyle(document.documentElement).getPropertyValue(n).trim()}
const getJSON=async (u)=>{const c=new AbortController(),k=setTimeout(()=>c.abort(),3500);try{const r=await fetch(u,{cache:"no-store",signal:c.signal});if(!r.ok)throw 0;return await r.json()}finally{clearTimeout(k)}}

/* depth shown in the hero: a range between two tips, e.g. "15–30 cm" */
const depthText=(l)=>{const v=I.lv,u=I.unit;
 if(I.theme!==2)return esc(I.lt[l]);
 if(l===0)return "0&ndash;"+v[1]+"<small>"+u+"</small>";
 if(l>=5)return v[5]+"+<small>"+u+"</small>";
 return v[l]+"&ndash;"+v[l+1]+"<small>"+u+"</small>"}

/* make the big word as large as fits on one line */
const fitWord=()=>{const w=$("word"),s=w.firstChild;if(!s)return;w.style.fontSize="";
 const max=parseFloat(getComputedStyle(w).fontSize),room=w.clientWidth;let f=max;
 for(let i=0;i<8&&s.offsetWidth>room;i++){f=Math.floor(f*room/s.offsetWidth*.98);w.style.fontSize=f+"px"}}

/* ---------- the underpass drawing (SVG) ---------- */
const LOW=200,K=2;                      // road low point (px) and px per cm
const yAt=cm=>LOW-cm*K;
const ROAD=[[0,52],[44,52],[130,LOW],[230,LOW],[316,52],[360,52]];
const slopeX=(y,side)=>{return side<0?44+(y-52)/(LOW-52)*86:316-(y-52)/(LOW-52)*86}
const buildUnderpass=()=>{const v=I.lv,road=ROAD.map(p=>p.join(",")).join(" ");
 let s='<defs><pattern id="hatch" width="7" height="7" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><rect width="7" height="7" fill="var(--earth)"/><line x1="0" y1="0" x2="0" y2="7" stroke="var(--hatch)" stroke-width="1.2"/></pattern>'+
 '<clipPath id="trough"><polygon points="0,0 '+road+' 360,0"/></clipPath></defs>'+
 '<polygon class="earth" points="'+road+' 360,236 0,236"/>'+
 '<g clip-path="url(#trough)"><rect id="water" x="0" y="0" width="360" height="240" style="transform:translateY(240px)"/>'+
 '<line id="surf" x1="0" x2="360" y1="0" y2="0" style="transform:translateY(240px)"/></g>';
 for(let i=1;i<=5;i++){const y=yAt(v[i]),a=slopeX(y,-1),b=slopeX(y,1),c=I.close===i;
  s+='<line class="thr'+(c?" close":"")+'" x1="'+a.toFixed(1)+'" x2="'+b.toFixed(1)+'" y1="'+y+'" y2="'+y+'"/>'+
  '<text class="'+(c?"close":"")+'" x="'+(b+5).toFixed(1)+'" y="'+(y+3.5)+'">'+v[i]+(i===5||c?" cm":"")+(c?" · closes":"")+'</text>'}
 s+='<rect class="deck" x="150" y="20" width="60" height="32"/><text class="k" x="180" y="14" text-anchor="middle" style="stroke-width:3px">BRIDGE ABOVE</text>'+
 '<polyline class="road draw" points="'+road+'"/>'+
 '<text class="k" x="22" y="44" text-anchor="middle">ROAD</text><text class="k" x="338" y="44" text-anchor="middle">ROAD</text>'+
 '<line class="stick" x1="196" y1="'+(LOW+6)+'" x2="196" y2="'+yAt(v[5]+8)+'"/>'+
 '<rect x="192" y="'+(LOW-2)+'" width="8" height="5" fill="var(--ink)"/>';
 for(let i=0;i<5;i++)s+='<circle class="tip" id="tip'+i+'" cx="196" cy="'+yAt(v[i+1])+'" r="4.5"/>';
 s+='<text x="186" y="'+(LOW+16)+'" text-anchor="end" style="font-size:9px">gauge</text>';
 $("sec").innerHTML=s}
/* other themes: a simple tank / field column */
const buildColumn=()=>{const v=I.lv,lo=v[0],hiV=v[5]+(v[5]-v[4]),Y=x=>214-(x-lo)/(hiV-lo)*190;
 let s='<rect x="120" y="20" width="120" height="196" fill="var(--panel)" stroke="var(--ink)" stroke-width="2"/>'+
 '<clipPath id="col"><rect x="121" y="21" width="118" height="194"/></clipPath><g clip-path="url(#col)"><rect id="water" x="0" y="0" width="360" height="240" style="transform:translateY(240px)"/><line id="surf" x1="0" x2="360" style="transform:translateY(240px)"/></g>';
 for(let i=1;i<=5;i++){const y=Y(v[i]).toFixed(1);s+='<line class="thr" x1="120" x2="240" y1="'+y+'" y2="'+y+'"/><text x="248" y="'+(+y+3.5)+'">'+esc(I.pl[i-1])+'</text>'}
 for(let i=0;i<5;i++)s+='<circle class="tip" id="tip'+i+'" cx="180" cy="'+Y(v[i+1])+'" r="4.5"/>';
 $("sec").innerHTML=s;buildColumn.Y=Y}
const waterY=(l)=>{const v=I.lv;
 if(I.theme===2){if(l===0)return J&&J.wet&&J.wet[0]?yAt(3):LOW+1;const top=l>=5?v[5]+10:v[l]+(v[l+1]-v[l])*.45;return yAt(top)}
 const Y=buildColumn.Y,top=l>=5?v[5]+(v[5]-v[4])*.5:v[l]+(v[l+1]-v[l])*.45;return Y(l===0?v[0]+(v[1]-v[0])*.4:top)}
const drawWater=()=>{if(!J)return;const y=waterY(J.level);
 $("water").style.transform="translateY("+y+"px)";$("surf").style.transform="translateY("+y+"px)";
 for(let i=0;i<5;i++){const t=$("tip"+i);if(t)t.classList.toggle("w",!!J.wet[i])}}

/* ---------- chart (hand-drawn on a canvas) ---------- */
const drawChart=()=>{const c=$("ch"),r=c.getBoundingClientRect();if(!r.width||!I)return;
 const d=window.devicePixelRatio||1;c.width=Math.round(r.width*d);c.height=Math.round(r.height*d);
 const x=c.getContext("2d");x.setTransform(d,0,0,d,0,0);x.clearRect(0,0,r.width,r.height);
 const W=r.width,Hh=r.height,L=38,R=6,T=8,B=22,v=I.lv,lo=v[0],top=v[5]+(v[5]-v[4])*.4;
 const span=I.hn*I.hms/1000,Y=val=>T+(Hh-T-B)*(1-(val-lo)/(top-lo)),X=s=>L+(W-L-R)*(1-s/span);
 const ink=css("--ink"),mut=css("--mut"),rule=css("--rule"),wat=css("--water"),al=css("--alert"),ok=css("--ok");
 x.font="600 10.5px system-ui,sans-serif";x.textBaseline="middle";
 if(I.close>0){x.fillStyle=css("--alert-soft");x.fillRect(L,T,W-L-R,Y(v[I.close])-T);
  x.fillStyle=al;x.textAlign="right";x.fillText("road closed",W-R-4,T+9)}
 x.textAlign="right";
 for(let i=0;i<=5;i++){const y=Math.round(Y(v[i]))+.5;x.strokeStyle=i===I.close?al:rule;x.lineWidth=1;x.beginPath();x.moveTo(L,y);x.lineTo(W-R,y);x.stroke();
  x.fillStyle=i===I.close?al:mut;x.fillText(I.theme===2?String(v[i]):esc(i?I.pl[i-1]:""),L-6,y)}
 x.fillStyle=mut;x.textAlign="left";x.fillText(I.unit,2,T+2);
 x.textAlign="center";x.textBaseline="top";
 for(let s=0;s<=span;s+=60){const X0=X(s);x.fillStyle=mut;x.fillText(s?"−"+clock(s):"now",Math.min(Math.max(X0,L+14),W-R-12),Hh-B+6);
  x.strokeStyle=rule;x.beginPath();x.moveTo(Math.round(X0)+.5,Hh-B);x.lineTo(Math.round(X0)+.5,Hh-B+4);x.stroke()}
 const since=(Date.now()-evAt)/1000;
 for(const e of evs){if(e.a<0)continue;const a=e.a+since;if(a>span)continue;
  if(e.k===3||e.k===7){const X0=Math.round(X(a))+.5;x.strokeStyle=e.k===3?al:ok;x.lineWidth=2;x.setLineDash(e.k===3?[]:[4,3]);
   x.beginPath();x.moveTo(X0,T);x.lineTo(X0,Hh-B);x.stroke();x.setLineDash([])}}
 if(hist.length<1)return;
 const step=I.hms/1000,n=hist.length,pts=[];
 for(let i=0;i<n;i++){const s=(n-1-i)*step;if(s>span)continue;pts.push([X(s),Y(v[hist[i]])])}
 if(!pts.length)return;
 x.beginPath();x.moveTo(pts[0][0],Y(lo));
 for(let i=0;i<pts.length;i++){x.lineTo(pts[i][0],pts[i][1]);if(i+1<pts.length)x.lineTo(pts[i+1][0],pts[i][1])}
 x.lineTo(pts[pts.length-1][0],Y(lo));x.closePath();x.fillStyle=css("--water-soft");x.fill();
 x.beginPath();x.moveTo(pts[0][0],pts[0][1]);
 for(let i=1;i<pts.length;i++){x.lineTo(pts[i][0],pts[i-1][1]);x.lineTo(pts[i][0],pts[i][1])}
 x.strokeStyle=wat;x.lineWidth=2.5;x.lineJoin="round";x.stroke();
 const lp=pts[pts.length-1];x.fillStyle=wat;x.beginPath();x.arc(lp[0],lp[1],4,0,7);x.fill()}

/* ---------- event log ---------- */
const ICON=[
 '<circle cx="12" cy="12" r="8"/><path d="M12 8v4"/>',                                     // 0 system
 '<path d="M3 15c3-3 6 3 9 0s6 3 9 0"/><path d="M3 10c3-3 6 3 9 0s6 3 9 0"/>',              // 1 level
 '<path d="M12 3 2 20h20z"/><path d="M12 10v4"/><path d="M12 17h0"/>',                     // 2 caution
 '<rect x="3" y="9" width="18" height="6"/><path d="M7 9l-3 6M12 9l-3 6M17 9l-3 6M21 9l-3 6"/><path d="M5 15v5M19 15v5"/>', // 3 closed / alarm
 '<path d="M7 11l5-5 5 5"/><path d="M7 18l5-5 5 5"/>',                                     // 4 rising fast
 '<path d="M6 16V11a6 6 0 0 1 9-5"/><path d="M18 11v5l2 2H4"/><path d="M3 3l18 18"/>',      // 5 silenced
 '<circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5 21 21"/>',                      // 6 inspect
 '<path d="M4 12.5l5 5L20 6.5"/>',                                                         // 7 reopened / good
 '<path d="M4 7h10M18 7h2M4 17h2M10 17h10"/><circle cx="16" cy="7" r="2"/><circle cx="8" cy="17" r="2"/>']; // 8 test / calibrate
const drawEvents=()=>{const since=(Date.now()-evAt)/1000;
 if(!evs.length){$("ev").innerHTML='<li class="empty">No events yet.</li>';return}
 $("ev").innerHTML=evs.map(e=>'<li class="k'+e.k+(e.a<0?" old":"")+'"><svg viewBox="0 0 24 24" aria-hidden="true">'+(ICON[e.k]||ICON[0])+
  '</svg><span>'+esc(e.m)+'</span><time>'+ago(e.a<0?-1:e.a+since)+'</time></li>').join("")}

/* ---------- facts + sensors ---------- */
const drawFacts=()=>{const f=[[I.ml,J.mode]];
 if(J.closed>=0)f.push(["Closed for",clock(J.closed)]);
 f.push([I.cntl,J.cnt+(J.cnt?(J.cago>=0?" · last "+ago(J.cago):" · before restart"):"")]);
 f.push(["Depth band",J.ltext]);f.push(["Running for",dur(J.up)]);
 if(f.length%2)f.push(["Free memory",J.heap?Math.round(J.heap/1024)+" KB":"–"]);
 $("facts").innerHTML=f.map(p=>"<div><dt>"+esc(p[0])+"</dt><dd>"+esc(p[1])+"</dd></div>").join("")}
const drawSensors=()=>{let s="<tr><th>Tip</th><th>Dry</th><th>Now</th><th style=\"width:34%\">Signal</th><th></th></tr>";
 for(let i=0;i<5;i++){const b=J.base[i],p=b?Math.min(130,Math.round(100*J.raw[i]/b)):0;
  s+="<tr><td>P"+(i+1)+" &middot; "+esc(I.pl[i])+"</td><td>"+b+"</td><td>"+J.raw[i]+"</td><td><div class=\"bar\"><i style=\"width:"+Math.min(100,p)+"%\"></i><b style=\"left:40%\"></b></div></td><td>"+(J.wet[i]?"<span class=\"wtag\">WET</span>":p+"%")+"</td></tr>"}
 $("tb").innerHTML=s}

/* ---------- paint everything from J ---------- */
let lastWord="";
const render=()=>{if(!I||!J)return;
 const hero=$("hero");hero.dataset.sev=J.sev;
 const w=tr(J.word);if(w!==lastWord){$("word").innerHTML='<span class="in">'+esc(w)+"</span>";lastWord=w;fitWord()}
 $("head").textContent=tr(J.head);$("det").textContent=J.det;$("ml").textContent=tr(I.ml);$("dl").textContent=tr("Water depth");
 $("depth").innerHTML=depthText(J.level);
 $("siren").classList.toggle("show",!!J.siren);
 $("fast").classList.toggle("show",!!J.fast);
 const eb=$("etaBox");eb.hidden=J.eta<0;$("etal").textContent=J.etal;$("eta").textContent=J.eta<0?"":J.eta<5?"any moment":"~"+dur(J.eta);$("eta").classList.toggle("big",!!J.fast);
 const ba=$("ba");ba.textContent=J.al;ba.className="btn main "+J.ak;$("bb").textContent=I.bl;
 drawFacts();drawSensors();drawWater();drawEvents();drawChart();
 $("up").textContent=(demo?"Demo":"Live")+" · "+I.ssid+" · 192.168.4.1"}

/* ---------- talking to the ESP32 ---------- */
const onData=(j)=>{
 if(J&&j.up<J.up-2){hist=[];H=-1;E=-1}                       // the ESP32 restarted
 if(j.h0!==H)hist=[];                                          // we missed points: start over
 hist=hist.concat(j.hist).slice(-I.hn);H=j.ht;
 if(j.ev){evs=j.ev;E=j.es;evAt=Date.now()}
 J=j;lastOk=Date.now();if(!demo)render();
 const l=$("live");l.className="live on";void l.offsetWidth;$("liveTxt").textContent="Live"}
const poll=async ()=>{
 try{if(!I){I=await getJSON("/info");setup()}
  onData(await getJSON("/data?h="+H+"&e="+E))}catch(e){}
 connState();setTimeout(poll,demo?2000:900)}
const connState=()=>{const off=lastOk&&Date.now()-lastOk>3500,never=!lastOk&&performance.now()>6000;
 $("offBar").hidden=!(off||never)||!!demo;document.body.classList.toggle("stale",(off||never)&&!demo);
 if(off){$("lastUp").textContent=dur((Date.now()-lastOk)/1000)+" ago";$("live").className="live off";$("liveTxt").textContent="Offline"}
 if(never){$("lastUp").textContent="never";$("live").className="live off";$("liveTxt").textContent="Offline"}}
const cmd=async (c,msg)=>{if(demo){demoCmd(c);return}
 try{await getJSON("/cmd?c="+c);toast(msg);E=-1;onData(await getJSON("/data?h="+H+"&e="+E))}catch(e){toast("No answer from the ESP32 – check Wi-Fi")}}

/* ---------- buttons ---------- */
$("ba").onclick=()=>{const k=J?J.ak:"ok";if(k==="reopen")openSheet();else cmd("ok",k==="silence"?"Alarm silenced":"OK")};
$("bb").onclick=()=>cmd("mode",I&&I.theme===1?"Mode changed":"Siren test");
$("bc").onclick=()=>{const b=$("bc");
 if(Date.now()-calArm<4000){calArm=0;b.classList.remove("arm");b.textContent="Re-calibrate";cmd("cal","Calibrated – the gauge is now “dry”")}
 else{calArm=Date.now();b.classList.add("arm");b.textContent="Gauge out of water? Tap again";
  setTimeout(()=>{if(calArm&&Date.now()-calArm>=4000){calArm=0;b.classList.remove("arm");b.textContent="Re-calibrate"}},4100)}};
const cks=[...document.querySelectorAll(".ck")];
const openSheet=()=>{cks.forEach(c=>c.checked=false);$("shYes").disabled=true;$("sheet").hidden=false;cks[0].focus()}
const closeSheet=()=>{$("sheet").hidden=true;$("ba").focus()}
cks.forEach(c=>c.onchange=()=>$("shYes").disabled=!cks.every(k=>k.checked));
$("shNo").onclick=closeSheet;
$("shYes").onclick=()=>{closeSheet();cmd("ok","Road reopened")};
$("sheet").onclick=e=>{if(e.target===$("sheet"))closeSheet()};
document.addEventListener("keydown",e=>{if(e.key==="Escape"&&!$("sheet").hidden)closeSheet()});
$("lang").onclick=()=>{hi=!hi;try{localStorage.setItem("fg-hi",hi?"1":"")}catch(e){}
 $("lang").innerHTML=hi?"EN":"&#2361;&#2367;&#2306;";document.documentElement.lang=hi?"hi":"en";lastWord="";render()};
try{hi=localStorage.getItem("fg-hi")==="1"}catch(e){}
if(hi)$("lang").textContent="EN";

/* ---------- DEMO: a pretend storm, drawn on this phone only ---------- */
// Each step: [level, seconds]. The real ESP32, LEDs and siren are not touched.
const STORM=[[0,4],[1,6],[2,7],[3,6],[4,5],[5,7],[4,4],[3,4],[2,4],[1,4],[0,9],[-1,6]];
const demoFrame=(t)=>{
 let i=0,acc=0;while(i<STORM.length-1&&t>=acc+STORM[i][1]){acc+=STORM[i][1];i++}
 const lvl=Math.max(0,STORM[i][0]),done=STORM[i][0]<0||t>=acc+STORM[i][1];
 const d=demo,v=I.lv;
 if(lvl>=2&&d.road!=="closed"){d.road="closed";d.cnt++;d.closedAt=t;d.acked=false;d.ev.unshift({a:0,k:3,m:"ROAD CLOSED - divert traffic",t})}
 if(d.road==="closed"&&lvl>d.prev&&lvl>2){d.acked=false;d.ev.unshift({a:0,k:lvl>=5?3:2,m:lvl>=5?"60 cm: EMERGENCY - call 112":"Water still rising",t})}
 if(d.road==="closed"&&lvl===0){d.road="check";d.ev.unshift({a:0,k:6,m:"Water below 5 cm - inspect, then reopen",t})}
 if(d.road==="open"&&lvl===1&&d.prev===0)d.ev.unshift({a:0,k:2,m:"Water on the road - caution",t});
 if(lvl!==d.prev&&t>0){d.ev.unshift({a:0,k:1,m:"Level now: "+I.lt[lvl],t});if(lvl===3&&d.prev===2)d.ev.unshift({a:0,k:4,m:"Rising fast: 2 levels in 7 s",t})}
 if(done&&d.road==="check"&&!d.autoOpened){d.autoOpened=true;d.road="open";d.ev.unshift({a:0,k:7,m:"Inspected - road reopened",t})}
 d.prev=lvl;d.ev=d.ev.slice(0,16);
 const siren=d.road==="closed"&&lvl>=2&&!d.acked;
 let word,head,det,sev;
 if(d.road==="closed"){sev=3;word="CLOSED";
  [head,det]=lvl>=5?["EMERGENCY - RESCUE ALERT","60 cm or more: check for trapped vehicles and call 112."]:lvl===4?["ROAD CLOSED - SEVERE","45-60 cm of flowing water can sweep vehicles away."]:
  lvl===3?["ROAD CLOSED - cars can float","30 cm of water can float many cars. Keep everyone out."]:lvl===2?["ROAD CLOSED - divert traffic","15 cm reaches the bottom of most cars and can stall them."]:["ROAD CLOSED - water falling","Keep it closed until the water is below 5 cm."]}
 else if(d.road==="check"){sev=2;word="INSPECT";head="INSPECT, THEN REOPEN";det="Water is below 5 cm. Check the road and drains, then tap bolt A to reopen."}
 else if(lvl===1){sev=2;word="CAUTION";head="CAUTION - water on the road";det="Slow down. The road closes by itself at 15 cm."}
 else{sev=0;word="OPEN";head="ROAD OPEN";det="No water on the road. Live depth goes to the control-room phone."}
 const rising=i+1<STORM.length&&STORM[i+1][0]>lvl,left=Math.max(0,acc+STORM[i][1]-t);
 const wet=[0,1,2,3,4].map(k=>k<lvl?1:0);
 return{sev,word,head,det,level:lvl,ltext:I.lt[lvl],siren:siren?1:0,fast:d.road==="closed"&&lvl>=2&&lvl<=4&&rising?1:0,
  mode:d.road==="closed"?(siren?"CLOSED - siren on":"CLOSED"):d.road==="check"?"Closed - waiting for inspection":"Open",
  al:siren?"Silence alarm":d.road==="check"?"Inspected - reopen road":"OK",ak:siren?"silence":d.road==="check"?"reopen":"ok",
  eta:rising?Math.round(left):-1,etal:d.road==="closed"?"Next danger level in":"Closure expected in",
  cnt:d.cnt,cago:d.cnt?Math.round(t-d.closedAt):-1,closed:d.road==="closed"?Math.round(t-d.closedAt):-1,
  wet,raw:wet.map(w=>w?3:60),base:[62,62,62,62,62],up:Math.round(t),heap:0}}
const demoTick=()=>{if(!demo)return;const t=(Date.now()-demo.t0)/1000;
 const j=demoFrame(t);
 if(t-demo.lastHist>=I.hms/1000){demo.hist.push(j.level);demo.lastHist=t}
 J=j;hist=demo.hist.slice(-I.hn);evs=demo.ev.map(e=>({a:t-e.t,k:e.k,m:e.m}));evAt=Date.now();render();
 if(t>STORM.reduce((s,x)=>s+x[1],0)+4)stopDemo();else demo.h=setTimeout(demoTick,500)}
const startDemo=()=>{if(!I)return;demo={t0:Date.now(),road:"open",cnt:0,prev:0,acked:false,ev:[{a:0,k:0,m:"Demo storm started",t:0}],hist:[],lastHist:-9,save:{J,hist,evs,evAt}};
 $("demoBar").hidden=false;$("demoBtn").textContent="Stop demo";lastWord="";window.scrollTo({top:0,behavior:"smooth"});demoTick()}
const stopDemo=()=>{if(!demo)return;clearTimeout(demo.h);const s=demo.save;demo=null;J=s.J;hist=s.hist;evs=s.evs;evAt=s.evAt;
 $("demoBar").hidden=true;$("demoBtn").textContent="Run demo storm";lastWord="";render();toast("Demo ended – back to live data")}
const demoCmd=(c)=>{if(c==="ok"){const t=(Date.now()-demo.t0)/1000;
  if(J.ak==="silence"){demo.acked=true;demo.ev.unshift({a:0,k:5,m:"Alarm silenced",t});toast("Demo: alarm silenced")}
  else if(J.ak==="reopen"){demo.road="open";demo.autoOpened=true;demo.ev.unshift({a:0,k:7,m:"Inspected - road reopened",t});toast("Demo: road reopened")}}
 else toast("Demo mode – the real ESP32 was not changed")}
$("demoBtn").onclick=()=>demo?stopDemo():startDemo();$("demoStop").onclick=stopDemo;

/* ---------- start ---------- */
const setup=()=>{$("t").textContent=I.title;$("s").textContent=I.sub;document.title=I.title;$("ssid").textContent=I.ssid;
 $("chS").textContent="last "+Math.round(I.hn*I.hms/60000)+" min";
 if(I.theme===2)buildUnderpass();else{buildColumn();$("secT").textContent="Water level";$("secS").textContent="";$("demoBtn").hidden=true}
 if(I.close<0){$("lgz").hidden=$("lgc").hidden=$("lgo").hidden=true}
 if(I.theme!==2)$("note").textContent="Bolt A and bolt B on the model do the same as these buttons.";
 if(location.search.indexOf("demo")>=0)setTimeout(startDemo,600)}
let rz;window.addEventListener("resize",()=>{clearTimeout(rz);rz=setTimeout(()=>{fitWord();drawChart()},120)});
setInterval(()=>{if(J&&!demo){drawEvents();connState()}},5000);
poll();
</script></body></html>)rawliteral";

// ============================ 11. PUBLIC SIGN PAGE (/sign) ================
// A second phone at the "entrance" of the model shows this full-screen,
// like the LED message board drivers see before the underpass.
const char SIGN[] = R"rawliteral(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><meta name="theme-color" content="#000">
<title>Road Sign</title><link rel="icon" href="data:,">
<style>
:root{--c:#5b636a;--dsp:"DIN Condensed","DIN Alternate","Bahnschrift","sans-serif-condensed","Roboto Condensed","Arial Narrow",system-ui,sans-serif}
*{box-sizing:border-box}html,body{height:100%}
body{margin:0;background:#050607;color:var(--c);font-family:var(--dsp);display:flex;flex-direction:column;overflow:hidden;-webkit-user-select:none;user-select:none}
body[data-sev="0"]{--c:#37d67a}body[data-sev="1"]{--c:#4aa8ff}body[data-sev="2"]{--c:#ffb21a}body[data-sev="3"]{--c:#ff3b3b}
.board{flex:1;display:flex;flex-direction:column;justify-content:center;align-items:center;text-align:center;padding:6vmin;
background:radial-gradient(circle,#15181a 1.1px,transparent 1.6px) 0 0/7px 7px}
.led{color:var(--c);text-transform:uppercase;font-weight:700;line-height:.9;letter-spacing:.02em;
-webkit-mask:radial-gradient(circle,#000 62%,transparent 70%) 0 0/6px 6px;mask:radial-gradient(circle,#000 62%,transparent 70%) 0 0/6px 6px;
text-shadow:0 0 18px color-mix(in srgb,var(--c) 60%,transparent)}
#a{font-size:min(16vh,13vw)}#b{font-size:min(30vh,30vw)}
#d{margin-top:4vmin;font-size:min(9vh,8vw)}
body[data-sev="3"] #b{animation:flash 1.2s steps(2,jump-none) infinite}
@keyframes flash{50%{opacity:.25}}
.bar{display:flex;justify-content:space-between;align-items:center;padding:10px 16px calc(10px + env(safe-area-inset-bottom));font:600 13px system-ui,sans-serif;color:#80878d;letter-spacing:.06em;text-transform:uppercase}
.bar a{color:#c9ced2;min-height:44px;display:inline-flex;align-items:center}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#555;margin-right:8px}
.dot.on{background:#37d67a}.dot.off{background:#ff3b3b}
@media (prefers-reduced-motion:reduce){body[data-sev="3"] #b{animation:none}}
</style></head><body data-sev="-1">
<div class="board" id="board"><div class="led" id="a">Road</div><div class="led" id="b">&middot;&middot;&middot;</div><div class="led" id="d"></div></div>
<div class="bar"><span><i class="dot" id="dot"></i><span id="st">Connecting</span></span><a href="/">Control room</a></div>
<script>
"use strict";
const $=id=>document.getElementById(id);let I=null,ok=0;
const hi=location.search.indexOf("hi")>=0;
const HI={OPEN:"खुला",CAUTION:"सावधान",CLOSED:"बंद",INSPECT:"बंद",TEST:"टेस्ट",Road:"सड़क"};
const get=async (u)=>{const c=new AbortController(),k=setTimeout(()=>c.abort(),3500);try{const r=await fetch(u,{cache:"no-store",signal:c.signal});return await r.json()}finally{clearTimeout(k)}}
const show=(j)=>{document.body.dataset.sev=j.sev;
 const road=I.theme===2,w=road&&j.word==="INSPECT"?"CLOSED":j.word;   // drivers see CLOSED until it is reopened
 $("a").textContent=road?(hi?HI.Road:"Road"):I.title;$("b").textContent=hi&&HI[w]?HI[w]:w;
 const v=I.lv,l=j.level,u=I.unit;
 $("d").textContent=road?(l===0?"Water under "+v[1]+" "+u:l>=5?"Water "+v[5]+"+ "+u:"Water "+v[l]+"–"+v[l+1]+" "+u):j.ltext}
const tick=async ()=>{try{if(!I)I=await get("/info");show(await get("/data?e=-2"));ok=Date.now()}catch(e){}
 const live=ok&&Date.now()-ok<3500;$("dot").className="dot "+(live?"on":"off");$("st").textContent=live?"Live":"No signal";
 if(!live&&ok)document.body.dataset.sev="-1";setTimeout(tick,1000)}
$("board").onclick=()=>{const e=document.documentElement;if(e.requestFullscreen)e.requestFullscreen().catch(()=>{})};
tick();
</script></body></html>)rawliteral";

// ============================ 12. WEB SERVER ==============================
void jsonStr(String& out, const char* s) {   // add a JSON string, escaping quotes
  out += '"';
  for (const char* p = s; *p; p++) {
    if (*p == '"' || *p == '\\') { out += '\\'; out += *p; }
    else if ((uint8_t)*p >= 0x20) out += *p;
  }
  out += '"';
}

// /info: things that never change (names, labels, thresholds). Asked once.
void handleInfo() {
  String o;
  o.reserve(700);
  o += "{\"theme\":"; o += (int)THEME;
  o += ",\"title\":"; jsonStr(o, TITLE);
  o += ",\"sub\":";   jsonStr(o, SUBTITLE);
  o += ",\"ssid\":";  jsonStr(o, AP_SSID);
  o += ",\"ml\":";    jsonStr(o, MODE_LABEL);
  o += ",\"cntl\":";  jsonStr(o, COUNT_LABEL);
  o += ",\"unit\":";  jsonStr(o, UNIT);
#if THEME == 1
  o += ",\"bl\":";    jsonStr(o, "Switch mode");
#elif THEME == 2
  o += ",\"bl\":";    jsonStr(o, "Test siren");
#else
  o += ",\"bl\":";    jsonStr(o, "Test alarm");
#endif
  o += ",\"close\":"; o += (int)CLOSE_LEVEL;
  o += ",\"hn\":";    o += (int)HIST_N;
  o += ",\"hms\":";   o += (long)HISTORY_MS;
  o += ",\"pl\":[";
  for (uint8_t i = 0; i < NUM_PROBES; i++) { if (i) o += ','; jsonStr(o, PROBE_LABEL[i]); }
  o += "],\"lt\":[";
  for (uint8_t i = 0; i <= NUM_PROBES; i++) { if (i) o += ','; jsonStr(o, LEVEL_TEXT[i]); }
  o += "],\"lv\":[";
  for (uint8_t i = 0; i <= NUM_PROBES; i++) { if (i) o += ','; o += (int)LEVEL_VALUE[i]; }
  o += "]}";
  server.send(200, "application/json", o);
}

// what the "Road status" line says
const char* modeText() {
#if THEME == 1
  return flowering ? "Flowering (keep flooded)" : "AWD (dry to -15 cm)";
#elif THEME == 2
  if (road == ROAD_CLOSED) return sirenActive() ? "CLOSED - siren on" : "CLOSED";
  if (road == ROAD_CHECK)  return "Closed - waiting for inspection";
  return "Open";
#else
  return alarmOn ? (alarmAcked ? "Alarm acknowledged" : "Alarm ON") : "Monitoring";
#endif
}

// /data?h=<points the phone already has>&e=<event version the phone has>
// Sends the live state, only the NEW chart points, and the event list only
// if it changed. That keeps each answer small (about 1 KB).
void handleData() {
  uint32_t now = millis();
  long hasH = server.arg("h").toInt();
  long hasE = server.arg("e").toInt();
  String o;
  o.reserve(1800);
  o += "{\"sev\":";   o += (int)sev;
  o += ",\"word\":";  jsonStr(o, statusWord);
  o += ",\"head\":";  jsonStr(o, headline);
  o += ",\"det\":";   jsonStr(o, detail);
  o += ",\"level\":"; o += (int)level;
  o += ",\"ltext\":"; jsonStr(o, LEVEL_TEXT[level]);
  o += ",\"mode\":";  jsonStr(o, modeText());
  o += ",\"al\":";    jsonStr(o, buttonALabel());
  o += ",\"ak\":";    jsonStr(o, buttonAKind());
  o += ",\"siren\":"; o += sirenActive() ? 1 : 0;
  o += ",\"fast\":";  o += risingFast() ? 1 : 0;
  o += ",\"eta\":";   o += etaSec;
  o += ",\"etal\":";  jsonStr(o, etaLabel);
  o += ",\"cnt\":";   o += (int)counter;
  o += ",\"cago\":";  o += counterAtKnown ? (long)((now - counterAt) / 1000) : -1L;
  o += ",\"closed\":"; o += road == ROAD_CLOSED ? (long)((now - closedAt) / 1000) : -1L;
  o += ",\"fault\":"; o += fault ? 1 : 0;
  o += ",\"up\":";    o += (long)(now / 1000);
  o += ",\"heap\":";  o += (long)ESP.getFreeHeap();
  o += ",\"wet\":[";
  for (uint8_t i = 0; i < NUM_PROBES; i++) { if (i) o += ','; o += wet[i] ? 1 : 0; }
  o += "],\"raw\":[";
  for (uint8_t i = 0; i < NUM_PROBES; i++) { if (i) o += ','; o += (long)raw[i]; }
  o += "],\"base\":[";
  for (uint8_t i = 0; i < NUM_PROBES; i++) { if (i) o += ','; o += (long)(base[i] + 0.5f); }
  // chart points: send from what the phone has, or everything we still keep
  bool lite = (hasE == -2);                        // -2 = the sign page: no chart, no log
  uint32_t oldest = histTotal - histCount;
  uint32_t from = (hasH < (long)oldest || hasH > (long)histTotal) ? oldest : (uint32_t)hasH;
  if (lite) from = histTotal;
  o += "],\"h0\":";   o += (unsigned long)from;
  o += ",\"ht\":";    o += (unsigned long)histTotal;
  o += ",\"hist\":[";
  for (uint32_t n = from; n < histTotal; n++) {
    if (n != from) o += ',';
    o += (int)hist[n % HIST_N];
  }
  o += ']';
  o += ",\"es\":";    o += (unsigned long)evSeq;
  if (hasE != (long)evSeq && !lite) {
    o += ",\"ev\":[";
    for (uint8_t i = 0; i < evCount; i++) {        // newest first
      const LogEntry& e = events[(evHead + EV_N - 1 - i) % EV_N];
      if (i) o += ',';
      o += "{\"a\":"; o += e.old ? -1L : (long)((now - e.t) / 1000);
      o += ",\"k\":"; o += (int)e.type;
      o += ",\"m\":"; jsonStr(o, e.msg); o += '}';
    }
    o += ']';
  }
  o += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", o);
}

// /history.csv: the chart data as a table (open it in a spreadsheet)
void handleCsv() {
  String o;
  o.reserve(64 + histCount * 34);
  o += "seconds_ago,level,depth\r\n";
  for (uint8_t i = 0; i < histCount; i++) {
    uint8_t l = hist[(histHead + HIST_N - histCount + i) % HIST_N];
    o += (long)((histCount - 1 - i) * (HISTORY_MS / 1000));
    o += ','; o += (int)l; o += ','; o += LEVEL_TEXT[l]; o += "\r\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=flood-history.csv");
  server.send(200, "text/csv", o);
}

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", PAGE); }
void handleSign() { server.send_P(200, "text/html; charset=utf-8", SIGN); }

void handleCmd() {
  String c = server.arg("c");
  if (c == "ok") actionOK();
  else if (c == "mode") actionMode();
  else if (c == "cal") actionCalibrate();
  server.send(200, "application/json", "{\"ok\":1}");
}

// ============================ 13. SERIAL COMMANDS =========================
// Type these in the Serial Monitor (115200 baud, "Newline" line ending).
void printHelp() {
  Serial.println("# Commands: help | status | ok (silence/reopen) | test (bolt B tap)");
  Serial.println("#           selftest | cal (gauge OUT of water) | log | csv | resetcount");
}

void printStatus() {
  Serial.print("# Status: "); Serial.print(headline);
  Serial.print(" | level "); Serial.print((int)level);
  Serial.print(" ("); Serial.print(LEVEL_TEXT[level]); Serial.print(")");
  Serial.print(" | "); Serial.print(COUNT_LABEL); Serial.print(" "); Serial.print((int)counter);
  Serial.print(" | up "); Serial.print((long)(millis() / 1000)); Serial.print(" s");
  Serial.print(" | free heap "); Serial.print((long)(ESP.getFreeHeap() / 1024)); Serial.println(" KB");
  Serial.print("# Tips (% of dry):");
  for (uint8_t i = 0; i < NUM_PROBES; i++) {
    Serial.print(" P"); Serial.print(i + 1); Serial.print("=");
    Serial.print((long)(100.0f * raw[i] / base[i])); Serial.print(wet[i] ? "% WET" : "%");
  }
  Serial.println();
}

void printLog() {
  for (uint8_t i = 0; i < evCount; i++) {            // oldest first, like a diary
    const LogEntry& e = events[(evHead + EV_N - evCount + i) % EV_N];
    Serial.print("# ");
    if (e.old) Serial.print("(before restart) ");
    else { Serial.print((long)(e.t / 1000)); Serial.print(" s  "); }
    Serial.println(e.msg);
  }
}

void printCsv() {
  Serial.println("seconds_ago,level,depth");
  for (uint8_t i = 0; i < histCount; i++) {
    uint8_t l = hist[(histHead + HIST_N - histCount + i) % HIST_N];
    Serial.print((long)((histCount - 1 - i) * (HISTORY_MS / 1000)));
    Serial.print(","); Serial.print((int)l); Serial.print(","); Serial.println(LEVEL_TEXT[l]);
  }
}

void runCommand(const char* c) {
  if      (!strcmp(c, "help"))       printHelp();
  else if (!strcmp(c, "status"))     printStatus();
  else if (!strcmp(c, "ok"))         actionOK();
  else if (!strcmp(c, "test"))       actionMode();
  else if (!strcmp(c, "selftest"))   selfTest();
  else if (!strcmp(c, "cal"))        actionCalibrate();
  else if (!strcmp(c, "log"))        printLog();
  else if (!strcmp(c, "csv"))        printCsv();
  else if (!strcmp(c, "resetcount")) { clearState(); Serial.println("# Closures and event log cleared"); }
  else { Serial.print("# Unknown command: "); Serial.println(c); printHelp(); }
}

// collect typed letters until Enter, then run the command
void serialTick() {
  static char line[20];
  static uint8_t n = 0;
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      line[n] = 0;
      if (n) runCommand(line);
      n = 0;
    } else if (n < sizeof(line) - 1) {
      line[n++] = (char)tolower((unsigned char)ch);
    }
  }
}

// ============================ 14. SETUP & LOOP ============================
void setup() {
  Serial.begin(115200);
  delay(200);
  for (uint8_t i = 0; i < 6; i++) { pinMode(LED_PIN[i], OUTPUT); digitalWrite(LED_PIN[i], LOW); }
  pinMode(BUZZER_PIN, OUTPUT);
  buzz(false);
  Serial.println();
  Serial.print("# "); Serial.print(TITLE); Serial.println(" starting");

  loadState();                       // closures, event log, "road was closed" latch
  if (resumedClosure) logEvent(EV_CHECK, "Restarted while closed: inspect first");

  // start every touch channel, then give the hardware time for real readings
  // (a channel read too early returns 0, which would look like "wet")
  for (uint8_t i = 0; i < NUM_PROBES; i++) touchRead(PROBE_PIN[i]);
  for (uint8_t k = 0; k < 2; k++) touchRead(PAD_PIN[k]);
  delay(WARMUP_MS);
  for (uint8_t k = 0; k < 2; k++) padBase[k] = readAverage(PAD_PIN[k], 8);   // don't touch the bolts now
  if (!loadCalibration()) {
    Serial.println("# No saved calibration: calibrating now - keep the gauge OUT of the water");
    calibrateProbes();
  } else {
    logEvent(EV_SYS, "Loaded saved calibration");
  }
  // first reading without waiting, so a full glass at power-on is not missed
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
  server.on("/sign", handleSign);
  server.on("/info", handleInfo);
  server.on("/data", handleData);
  server.on("/cmd", handleCmd);
  server.on("/history.csv", handleCsv);
  server.onNotFound(handleRoot);
  server.begin();

  Serial.print("# Wi-Fi: "); Serial.print(AP_SSID);
  Serial.print("  password: "); Serial.print(AP_PASSWORD);
  Serial.print("  then open http://"); Serial.println(ip);
  printHelp();
  logEvent(EV_SYS, "System ready");
  saveState();
  play(PAT_CHIRP);
}

uint32_t lastSample = 0, lastPads = 0, lastHist = 0, lastPrint = 0;

void addHistoryPoint() {
  hist[histHead] = level;
  histHead = (histHead + 1) % HIST_N;
  if (histCount < HIST_N) histCount++;
  histTotal++;
}

void plotLine() {                        // Tools > Serial Plotter draws these as graphs
  for (uint8_t i = 0; i < NUM_PROBES; i++) {
    Serial.print("P"); Serial.print(i + 1); Serial.print(":");
    Serial.print((long)(100.0f * raw[i] / base[i]));
    Serial.print(",");
  }
  Serial.print("Level_x20:"); Serial.println(level * 20);
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  serialTick();
  uint32_t now = millis();

  if (now - lastPads >= PAD_MS)     { lastPads = now;   readPads(); }
  if (now - lastSample >= SAMPLE_MS) { lastSample = now; readProbes(); decide(); }
  if (now - lastHist >= HISTORY_MS)  { lastHist = now;   addHistoryPoint(); }
  if (now - lastPrint >= PLOT_MS)    { lastPrint = now;  plotLine(); }
  alarmTick();
  buzzerTick();
  updateLeds();
  saveLogIfDue();
}
