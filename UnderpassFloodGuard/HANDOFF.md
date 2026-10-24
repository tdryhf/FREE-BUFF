# Handoff: Underpass Flood Guard (ESP32) — improve the firmware and phone dashboard

## 1. What this is
A school competition prototype (Kreativity League 2026, Kreativity Show Jr., grades 6–8, theme **Smart Infrastructure**).
It is a flood-depth warning system for road underpasses:

- A "depth gauge" (bamboo skewer with 5 bare jumper-wire tips + 1 GND tip) stands in a glass of water that models the underpass low point.
- The ESP32 reads the tips with its **built-in capacitive touch sensor** (`touchRead`): a wet tip reads near 0, a dry tip reads ~40–80.
- Decision levels (water depth on the real road; model scale 1 cm in the glass = 6 cm on the road):

| Tip | Road depth | Action |
|---|---|---|
| P1 | 5 cm | CAUTION (amber), chirp |
| P2 | 15 cm | **ROAD CLOSED**: LED6 flashes, siren, phone alert, closure counted |
| P3 | 30 cm | siren again ("cars can float") |
| P4 | 45 cm | siren again ("severe") |
| P5 | 60 cm | siren again ("EMERGENCY – call 112") |
| — | below 5 cm after a closure | stays closed, "INSPECT, THEN REOPEN" until bolt A / phone button |

- Outputs: 6 green LEDs on a breadboard "depth sign", an active buzzer (siren), and a **phone dashboard** served by the ESP32 itself (Wi-Fi access point + captive DNS + `WebServer`, no internet).
- Inputs: two hex bolts wired to touch pins act as buttons (tap / 5-second hold).

The current sketch is `UnderpassFloodGuard.ino` (single file, ~740 lines). It already works in simulation; your job is to **make it better without changing the hardware**.

## 2. Hard constraints (do not break)
1. **No new hardware.** Only these parts exist: ESP32-WROOM-32 38-pin dev board (USB-C, CH340), 3 breadboards, 10 male–male + 10 male–female jumpers (20 cm), 6 green 5 mm LEDs, 6 × 220 Ω resistors, 1 active buzzer (SFM-27 style, may be passive — `BUZZER_IS_ACTIVE` flag), 2 bolts as touch pads. No display, no servo, no extra sensors, no SD card. The team cannot solder, cut or drill.
2. **Pin map is fixed** (already wired by students):
   - Gauge tips P1..P5: GPIO 32, 33, 27, 14, 13 (touch). GND tip → GND.
   - LEDs 1..6: GPIO 21, 19, 18, 5, 17, 16 (LED6 = ROAD CLOSED light).
   - Buzzer: GPIO 23. Bolt A: GPIO 4 (touch). Bolt B: GPIO 15 (touch).
3. **Arduino IDE + ESP32 core 2.0.x and 3.x must both compile.** Only libraries bundled with the core (WiFi, WebServer, DNSServer, Preferences, etc.). No external libraries, no CDN assets in the web page (phone has no internet while on the ESP32's Wi-Fi). Everything inline in the sketch.
4. **Must stay understandable to 12–14-year-olds** who will explain it to judges. Keep code commented, sectioned and readable; prefer clarity over cleverness.
5. Keep `THEME` 1/3 compiling (other themes share the code), but **theme 2 is the priority**.
6. Wi-Fi AP `FloodGuard-ESP32`, password `kreativity`, dashboard at `192.168.4.1` — keep these defaults.
7. Safety logic must not regress: closure at P2, re-alarm on each rise even after silencing, latch closed until below P1 **and** human confirm, restart during a flood must close immediately, no false "wet" at boot (touch channels need a warm-up; a cold channel returns 0).

## 3. Current architecture (in the sketch)
- `readProbes()` every 150 ms: median of 5 `touchRead`s, wet if `< 0.40 × base`, dry if `> 0.55 × base`, 3 confirming samples. `trackBase()` slowly tracks the dry baseline. Calibration saved in `Preferences` (NVS).
- `computeLevel()`: level = number of wet tips counted from the bottom; `fault` if a higher tip is wet while a lower one is dry.
- `decide()`: theme-2 state machine `ROAD_OPEN → ROAD_CLOSED → ROAD_CHECK → ROAD_OPEN`, sets `sev`, `headline`, `detail`, stop-light pattern, ETA via `levelRate()/etaTo()`.
- Buzzer: non-blocking pattern player (`play()`, `buzzerTick()`), alarm repeats every 20 s until acknowledged.
- Pads: `readPads()` every 50 ms; tap A = silence / reopen, hold A 5 s = re-calibrate; tap B = test siren, hold B 5 s = LED/buzzer self-test.
- Web: `PAGE` (inline HTML/CSS/JS) polls `/data` (JSON) every 1 s; `/cmd?c=ok|mode|cal`. History ring buffer 120 points × 2 s; event log ring buffer 12 entries.

## 4. Improvement backlog (priority order)
### A. Dashboard UI/UX (highest value for judging)
1. **Big at-a-glance hero**: huge status word (OPEN / CAUTION / CLOSED / INSPECT), large depth number in cm, colour-coded full-width banner; readable from 1–2 m on a phone propped on the table during the demo video.
2. **Visual underpass gauge**: replace the 5 stacked cells with an SVG cross-section (road dipping under a bridge) whose water fill rises to the current level, with threshold lines at 5/15/30/45/60 cm.
3. **Chart**: label axes in cm, shade the closure zone (≥15 cm), mark closure/reopen events on the timeline, show real clock-ish "mm:ss ago" ticks. Keep it a hand-drawn `<canvas>` (no libraries).
4. **Event log**: icons/colours per event type, newest first, "x min ago".
5. **Confirm dialogs in-page** (not `confirm()`), e.g. Re-calibrate requires a second tap; "Inspected – reopen road" shows a short checklist (water below 5 cm, no stalled vehicles, drains clear) before confirming.
6. **Connection state**: show "Live" pulse, and a clear "Disconnected – check Wi-Fi" state with last-known data greyed out.
7. **Dark mode** (`prefers-color-scheme`), large tap targets (≥44 px), works at 320–430 px width, no horizontal scroll.
8. **Demo mode**: a hidden/explicit "Demo" toggle that replays a simulated storm on the dashboard only (no effect on the real alarm), so the team can show the UI even if the hardware misbehaves. Must be clearly labelled DEMO on screen.
9. **Optional**: Hindi labels toggle (EN/हिं) for the public-facing status text.

### B. Firmware quality
1. Replace 1 s polling with **Server-Sent Events** (`/events`) if it works on both core 2.x and 3.x with the bundled `WebServer`; otherwise keep polling but send only changed fields. Do not add AsyncWebServer (external library).
2. Persist closure count and last 12 events across reboots in NVS (rate-limit writes to protect flash).
3. Show uptime and a "time since closure" counter; keep everything relative (no RTC / no internet time).
4. Rate-of-rise alert: if the water climbs ≥2 levels within N seconds, log "rising fast" and show ETA prominently.
5. Serial: keep the Serial Plotter output, add a `help` command over Serial (e.g. `cal`, `test`, `status`).
6. Keep functions short and commented; move magic numbers into the settings block.

### C. Nice-to-have
- Second page `/sign` that renders a full-screen "public LED sign" view (big ROAD CLOSED / OPEN text, depth) for a second phone placed at the "entrance" of the model.
- Export history as CSV via `/history.csv` for the students' testing table / PDF.

## 5. Acceptance criteria
- Compiles with **Arduino-ESP32 2.0.17 and 3.x** (board: "ESP32 Dev Module"), zero new warnings at default warning level.
- Flash/RAM fit comfortably (sketch < 1.2 MB, free heap after boot > 100 KB).
- Dashboard: no external requests, no console errors, works in Chrome Android and Safari iOS, 1 s or faster updates, readable in light and dark.
- Simulated scenario passes (see §6): dry boot = OPEN; 5 cm caution; 15 cm closed + siren; tap A silences; siren re-sounds at 30/45/60; drain keeps CLOSED until <5 cm; INSPECT; tap A → OPEN; second storm counts closure 2; restart mid-flood (cold touch channels returning 0 for 30 ms) closes immediately with no transient false level; zero false-wet onsets with neighbour-wire sag (dry tip reading down to ~58% of base).
- Real-hardware smoke test (students will run): upload, calibrate dry, lower gauge into ~10.5 cm of water → LEDs 1–5 in order, siren at LED2; lift out → INSPECT; tap bolt A → OPEN.

## 6. Testing without hardware
`sim/` contains a host-side harness used to validate the current sketch:
- `stubs/` — minimal fakes for `Arduino.h`, `WiFi.h`, `WebServer.h`, `DNSServer.h`, `Preferences.h`.
- `harness2.cpp` — simulated glass of water (tips at 0.8/2.5/5/7.5/10 cm), bolt touches, touch-channel warm-up zeros, neighbour-wire sag; runs the theme-2 flood scenario and prints events plus `expect` checks.

Run:
```bash
cd sim
cp ../UnderpassFloodGuard.ino sketch2.cpp
g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Istubs -include Arduino.h -o sim2 harness2.cpp
./sim2
```
`/data` JSON and the `/` page can be dumped from the harness (`server.request("/data")`) and served locally to screenshot the dashboard with Playwright/Chrome. Extend the stubs if you add endpoints (e.g. SSE). The harness is not a substitute for a real compile: also compile with `arduino-cli compile --fqbn esp32:esp32:esp32` against core 2.0.17 and 3.x.

## 7. Deliverables
1. Updated `UnderpassFloodGuard.ino` (still a single file, same folder name).
2. Short CHANGELOG (what changed, why, anything the students must re-do — ideally nothing).
3. Screenshots of the dashboard in OPEN, CAUTION, CLOSED, INSPECT states (light + dark, 390 px wide).
4. Updated harness/stubs if behaviour or endpoints changed, with the scenario still passing.
