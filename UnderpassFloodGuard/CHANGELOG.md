# Underpass Flood Guard: changelog

## What students must re-do

**Nothing.** The hardware, the pins, the Wi-Fi name and password, and the saved
calibration all stay the same. Upload the new sketch and it carries on.

Two optional things before a demo:
- Type `resetcount` in the Serial Monitor to set Closures back to 0 and clear the log.
- Ask a Hindi speaker to read the Hindi words once (tap **हिं** on the dashboard).

## Checked

| Check | Result |
|---|---|
| Arduino-ESP32 **2.0.17**, ESP32 Dev Module | compiles, 0 warnings, 825 KB (62%) |
| Arduino-ESP32 **3.3.12**, ESP32 Dev Module | compiles, 0 warnings, 1.00 MB (76%) |
| THEME 1 and THEME 3, both cores | compile |
| `--warnings all` | no warnings from the sketch |
| Simulator (`sim/harness2.cpp`) | 36 / 36 checks pass |
| Dashboard in Chromium, 390 px and 320 px, light + dark | no console errors, no sideways scroll |

Not checked here (needs the real board): free heap after boot. The sketch prints it
with the `status` Serial command and on the dashboard ("Free memory"). It should be
well above 100 KB, because global variables only use 47 KB.

## Dashboard (phone)

- **Big status band**: one huge word (OPEN / CAUTION / CLOSED / INSPECT), shrunk
  to fit on one line. The band is green, amber or red, readable from 1–2 m away.
- **Depth in cm**: shown as the range between two tips, for example "15–30 cm".
  The gauge only knows which tips are wet, so a range is the honest answer.
- **Underpass drawing**: side view of the road dipping under a bridge. Water rises
  to the current level, the gauge tips light up when wet, and dashed lines mark
  5/15/30/45/60 cm. The 15 cm line is red: that is where the road closes.
- **Chart**: depth in cm, shaded "road closed" zone, red lines where the road
  closed and green dashed lines where it reopened, time ticks like −3:00 … now.
  Drawn by hand on a canvas. No libraries.
- **Event log**: an icon and colour for each kind of event, newest first, "2 min ago".
  Events from before a restart are shown as "before restart".
- **In-page confirms**:
  - *Re-calibrate* needs a second tap within 4 s ("Gauge out of water? Tap again").
  - *Inspected – reopen road* opens a checklist (water below 5 cm, no stalled
    vehicles, drains clear). Reopen only works once all three are ticked.
- **Connection**: a "Live" dot pulses with every update. If updates stop for 3.5 s,
  a red bar says "Disconnected – check the phone is on Wi-Fi FloodGuard-ESP32",
  and the old data turns grey so nobody mistakes it for live data.
- **Dark mode** follows the phone setting. Buttons are at least 44 px tall.
  Works from 320 px wide with no sideways scrolling.
- **Demo storm** (footer button, or open `192.168.4.1/?demo`): replays a pretend
  storm on that phone only, with a yellow-and-black "DEMO" strip on top. It never
  touches the real LEDs, siren or closure count. Use it if the hardware misbehaves
  during filming.
- **Hindi** (हिं / EN button): translates the big word, the headline and two labels.
- **Reduced motion**: phones set to "reduce motion" get no animations.
- **Fonts**: no downloads. On iPhone the big text uses DIN, the road-sign typeface;
  Android uses its built-in condensed font.

## New pages

- **`/sign`**: a full-screen "LED road sign" for a second phone at the model's
  entrance: ROAD OPEN in green, ROAD CLOSED in red (slow flash), plus the depth.
  It keeps saying CLOSED while the road waits for inspection, because drivers must
  not enter yet. Add `?hi` for Hindi. Tap the screen to go full-screen (Android).
- **`/history.csv`**: the last 4 minutes of levels as a spreadsheet file, for the
  testing table.

## Firmware

- **Saved across restarts** (flash memory, `Preferences`):
  - closure count and the last 16 events;
  - **the road-closed latch.** If the power goes off while the road is closed or
    waiting for inspection, it comes back as INSPECT (or CLOSED if still flooded).
    It never reopens by itself. The same flood is not counted twice.
  - To protect the flash, the event log is written at most once a minute.
    Closures and the road state are written at once, because they only change a
    few times per storm. The whole simulated test makes 96 writes.
- **Rising fast**: if two tips get wet within 8 s of each other (model seconds), the
  log says "Rising fast: 2 levels in 3 s", the hero shows a "Rising fast" strip and
  the time estimate gets bigger. On a real road you would set `RISE_FAST_MS` to minutes.
- **"Closed for 3:20"** and **"Running for 12 min"** on the dashboard. All times
  are relative, because the ESP32 has no clock and no internet.
- **Serial Monitor commands** (115200 baud, Newline): `help`, `status`, `ok`,
  `test`, `selftest`, `cal`, `log`, `csv`, `resetcount`. The Serial Plotter output
  is unchanged.
- **Magic numbers moved to SETTINGS**: `PAD_CONFIRM`, `WARMUP_MS`, `TEST_MS`,
  `RISE_FAST_MS`, `RISE_SHOW_MS`, `HIST_N`, `EV_N`, `PLOT_MS`, `LOG_SAVE_MS`, plus
  `LEVEL_VALUE`, `UNIT` and `CLOSE_LEVEL` per theme.
- **Theme 2 decision code split** into `updateRoad()` (change the state) and
  `showRoad()` (choose the words and lights). The rules themselves did not change.
- Event log entries now have a type (`EV_ALARM`, `EV_GOOD`, …) so the phone can pick
  an icon.

## Why not Server-Sent Events?

The bundled `WebServer` handles one connection at a time. An SSE stream keeps its
connection open, which would block `/data`, `/cmd` and the second phone on `/sign`.
Both cores behave slightly differently here too. So the page still asks about once
a second, but each answer is small: `/info` sends the fixed labels once, and `/data`
only sends **new** chart points and sends the event log **only when it changed**.
An up-to-date answer is about 430 bytes instead of about 2.5 KB.

## Safety logic: unchanged, and tested

Closure at P2. The siren re-sounds on each rise even after silencing. The road
stays closed until the water is below P1 **and** a person confirms. A restart during
a flood closes the road immediately with no in-between level. Warm-up zeros never
count as wet. Neighbour-wire sag never causes a false "wet".

## Files

- `UnderpassFloodGuard.ino`: still one file.
- `screenshots/`: OPEN, CAUTION, CLOSED, INSPECT in light and dark (390 px), plus the
  reopen checklist, the disconnected state, the demo and the public sign.
- `sim/`: stubs extended (flash blobs, `send_P`, `sendHeader`, Serial input, `ESP`),
  and the harness now checks restarts, the latch, rising fast, the small `/data`
  answers, `/info`, `/sign`, `/history.csv`, Serial commands and flash wear.
  `mock-server.js` + `screenshots.js` serve the page from the simulator's output so
  you can screenshot it with Playwright.

```bash
cd sim
cp ../UnderpassFloodGuard.ino sketch2.cpp
g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Istubs -include Arduino.h -o sim2 harness2.cpp
./sim2            # ends with "ALL CHECKS PASSED", exit code 0
node mock-server.js .  &  node screenshots.js .   # optional: screenshots into sim/shots/
```
