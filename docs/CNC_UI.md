# CNC UI (portrait)

A portrait CNC operator UI for DuetScreen, ported from the PanelDue CNC UI
(PanelDue_Modern_UI_CNC). It replaces the printer UI when built with `DUETSCREEN_CNC_UI=ON`.
Everything else (communication, object model, settings storage, Wi-Fi, updates) is stock
DuetScreen.

## Build

```bash
# PC simulator: opens a 600 x 1024 window
cmake --preset Simulation-Release -DDUETSCREEN_CNC_UI=ON
cmake --build --preset Simulation-Release

# Screen (T113): runs rotated by 90 degrees
cmake --preset T113-Release -DDUETSCREEN_CNC_UI=ON
```

With the option OFF (default) the build is the normal printer UI.

## Testing without a machine: the fake Duet

`scripts/fake_duet.py` pretends to be a CNC Duet in standalone mode on the network, so every page
of the simulator can be used live. It needs only Python 3 (no packages).

```bash
# terminal 1 (repo root)
python3 scripts/fake_duet.py                # listens on 127.0.0.1:8080
# terminal 2 (repo root)
./out/build/CNC-Simulation-Release/DuetScreen
```

In the simulator: SYSTEM > SETTINGS > CONNECTION > NETWORK, address `127.0.0.1:8080` (the `:` is on
the keyboard's `#+=` page). The simulator keeps this in `config.json` (repo root, not committed).

What it does:

- A 3-axis mill (`--axes 4` adds A): X 0..600, Y 0..400, Z -120..0 machine coordinates. It starts
  unhomed like a real machine (`--homed` to skip that). Moves take real time (`--speed 3` = three
  times faster).
- Homing (G28), jogging and G0/G1/G2/G3, G53, workplaces G54..G59.3 with G10 L2 / L20, tool offsets
  (G10 P), tools T0..T3, spindle M3/M4/M5 (ramps up), M220, M290 babystepping.
- Probing G38.2/G38.3 touches a fake stock top at machine Z-70 and edges at machine X150 / Y100.
- Jobs (M32, M23 + M24): progress, time left, pause M25, resume M24, cancel M0, M0 in the file.
- Macros (M98) with meta commands: if/elif/else, while, var, global, set, echo (also to a file),
  abort, expressions like `{move.axes[2].userPosition + 1}`.
- Machine messages M291 S0..S7 (choices, numbers, text) and the panel's M292 answers.
- The panel's settings: `global` / `set global`, `echo >"file"`, M471 rename. config.g loads
  `cnc-settings.g` again when the fake restarts, as on a real machine.
- E-stop: M112 halts, M999 restarts (unhomed, config.g runs again).

The virtual SD card is a normal folder, `~/fake_duet_sd` (`--sd` for another one), created with
demo jobs and macros on the first run (`--reset-sd` recreates it). Put your own jobs in
`~/fake_duet_sd/0/gcodes/` and macros in `~/fake_duet_sd/0/macros/`. `--card1` mounts `1:/`.

The terminal shows every command the panel sends (`>`) and every reply (`<`). Unknown commands are
listed as `(ignored ...)`. It is a test tool, not RepRapFirmware: timing, limits and error texts
are close to RRF, not exact.

## Where things are

| Path | What |
| --- | --- |
| `src/UI/Cnc/CncLayout.h` | All geometry: the PanelDue `CncLayout.hpp` values scaled by 1.25 (`S()`, `SL()`, `SB()`) |
| `src/UI/Cnc/CncTheme.*` | PanelDue colours (RGB888), Liberation Sans 24 / 36 px, the tile style |
| `src/UI/Cnc/CncGlyphs.*` | The PanelDue code-drawn glyphs (nav icons, crosshair, STOP, X / check, backspace, probe origins, search, shift, enter), drawn through a 1.25x `Pen` |
| `src/UI/Cnc/CncHomeView.*` | The frame: full DRO (CONTROL, WCS), compact DRO (JOB, MACROS, SYSTEM), one content area per page / sub-tab, sub-tab strips, nav bar |
| `src/UI/Cnc/CncHomePresenter.*` | Live data from the object model, ZERO (`G10 L20`), page switching, STOP |
| `src/UI/Cnc/CncState.*` | Shared state (PanelDue `CncCommon`): job state, homing, large-jog guard, spindle set speed, SLOW jog feed, work offsets / babystep / unsaved flag, tool table, TOOL SETTER, probe mode and reading, COOLANT / VACUUM, CUSTOM macros, sending G-code |
| `src/UI/Cnc/CncPopup.*` | The shared popups: ALERT, confirmations, preset choices, forms, questions (YES / NO), machine messages, page windows, the numpad (STOP still works over them) |
| `src/UI/Cnc/CncWidgets.*` | Buttons with the PanelDue looks (selected, locked, toggle), the RAPID / SLOW toggle, the sub-tab strip |
| `src/UI/Cnc/CncControlPage.*` | CONTROL: axis, step, move, actions, HOME column; the full DRO's T / S / F tiles |
| `src/UI/Cnc/CncWcsOffsetsPage.*` | WCS > OFFSETS: G54..G59, offset cells, ACTIVATE / COPY TO / SAVE / RESET / SHIFT / APPLY, OFFSET (babystep) column |
| `src/UI/Cnc/CncWcsToolsPage.*` | WCS > TOOLS: tool list (paging), LOAD / MEASURE / SET Z |
| `src/UI/Cnc/CncWcsProbePage.*` | WCS > PROBE: origin picker, mode tiles, RUN; auto probe progress popup, M291 jog prompt |
| `src/UI/Cnc/CncJobListPage.*` | JOB > JOB LIST: file list (folders, paging), select + RUN, SEARCH |
| `src/UI/Cnc/CncJobPreviewPage.*` | JOB > PREVIEW: 3D view with ISO / G0 buttons, operation strip, info strip; running operation |
| `src/UI/Cnc/CncPreviewView.*` | PREVIEW's 3D view: software line renderer into a canvas (stock box, toolpaths, presets, drag) |
| `src/UI/Cnc/CncPreviewSource.*` | PREVIEW's file: HTTP download from the machine whatever the machine connection, read in the background |
| `src/UI/Cnc/CncGcode.*` | G-code reader: moves and arcs, operations (Fusion 360 / FreeCAD comments, tool changes), tools, stock, times, short names |
| `src/UI/Cnc/CncJobStatusPage.*` | JOB > JOB STATUS: progress, times, per-tool FEED / SPINDLE overrides, PAUSE / RESUME / ABORT, tool change row |
| `src/UI/Cnc/CncMacrosPage.*` | MACROS: macro list or grid (folders, paging), RUN MACRO, SEARCH, pick for SETTINGS |
| `src/UI/Cnc/CncAlertsPage.*` | SYSTEM > ALERT: Error: / Warning: history, 5 pages of 10, ERROR / WARNING popup |
| `src/UI/Cnc/CncConsolePage.*` | SYSTEM > CONSOLE: log, sending with the keyboard, command history |
| `src/UI/Cnc/CncSettingsPage.*` | SYSTEM > SETTINGS: 3 pages (SYSTEM, CUSTOMIZATION, JOG DIRECTION), FACTORY RESET |
| `src/UI/Cnc/CncSettings.*` | The machine settings (`global.cnc*`): read from the object model, set live, `0:/sys/cnc-settings.g` written and renamed; MOVE button arrows |
| `src/UI/Cnc/CncPanel.*` | The screen settings (accent, brightness, info timeout, dimming, rotation, connection) and SOUND (buzzer / speaker, touch click, M300) |
| `src/UI/Cnc/CncKeyboard.*` | The shared keyboard (JOB LIST and MACROS search, SYSTEM > CONSOLE, M291 text prompts) |
| `src/UI/Cnc/CncSplash.*` | Start-up splash: drawn end mill and block, CNC / MODERN UI in the accent colour, version, three logo slots (files loaded when present) |
| `src/UI/Cnc/CncMessages.*` | Messages from the machine: M291 routed to the message popup, choice / number / text prompts, the jog prompt, the auto probe popup or the JOB STATUS tool change row; reply popups; M117 display messages; errors during a job |
| `assets/cnc/` | Liberation Sans (SIL OFL), kept out of `assets/fonts/` so it is not in the printer UI's font menu; the splash logos go here too (`logo_duet3d.png`, `logo_rrf.png`, `logo_millennium.png`, not in the repo) |
| `tests/src/test_cases/UI/test_cnc_home.cpp` | Frame: screenshots of every page and sub-tab (3 and 4 axes), sub-tab memory, content areas |
| `tests/src/test_cases/UI/test_cnc_control.cpp` | CONTROL: screenshots, G-code sent, refusals, job locks, idle timeout, DRO tiles |
| `tests/src/test_cases/UI/test_cnc_wcs_offsets.cpp` | WCS > OFFSETS: screenshots, G-code sent, popups, job locks, unsaved tracking |
| `tests/src/test_cases/UI/test_cnc_wcs_tools.cpp` | WCS > TOOLS: selection, paging, LOAD / MEASURE / SET Z, job locks |
| `tests/src/test_cases/UI/test_cnc_job_list.cpp` | JOB > JOB LIST and the keyboard: listing, folders, RUN, search |
| `tests/src/test_cases/UI/test_cnc_macros.cpp` | MACROS: display names, list / grid, folders, RUN MACRO, search, SETTINGS pick |
| `tests/src/test_cases/UI/test_cnc_alerts.cpp` | SYSTEM > ALERT: collecting, repeats, paging, ages, popup |
| `tests/src/test_cases/UI/test_cnc_console.cpp` | SYSTEM > CONSOLE: log order and colours, wrapping, sending, history, draft |
| `tests/src/test_cases/UI/test_cnc_settings.cpp` | SYSTEM > SETTINGS: globals in / out, file writing, pages, CUSTOM pick, presets, overrides, probe values, jog arrows, sound, factory reset |
| `tests/src/test_cases/UI/test_cnc_gcode.cpp` | G-code reader: operations, tools, stock, arcs, G20 / G91 / G53, merging, short names |
| `tests/src/test_cases/UI/test_cnc_job_preview.cpp` | JOB > PREVIEW: notices, operations, paging, info, view presets, G0, running job; a manual real-HTTP test |
| `tests/src/test_cases/UI/test_cnc_job_status.cpp` | JOB > JOB STATUS: job start / end, overrides, spindle emulation, PAUSE / ABORT, tool change row, errors |
| `tests/src/test_cases/UI/test_cnc_wcs_probe.cpp` | WCS > PROBE and M291 messages: macros, auto probe popup, jog prompts, message popups |
| `tests/src/test_cases/UI/test_cnc_splash.cpp` | Splash: look (orange, blue, with test logos), 3 s timeout, touch closes, skipped after the UI's own restart |
| `tests/src/test_cases/UI/test_cnc_messages.cpp` | Machine popups: replies (timeout, ALERT !, where not shown), M117, M291 S4-S7 prompts |
| `tests/object_model/cnc/` | RRF object model replies of a 3- and 4-axis mill, file listings and G-code files (`gcode/`: Fusion 360 and FreeCAD style), used by the tests |
| `tests/ref_imgs/cnc/` | Reference screenshots |

## Changes outside src/UI/Cnc

The CNC UI needs the probe reading (PROBE tile on WCS > PROBE) and the tool an automatic tool change is
going to (JOB STATUS), which DuetScreen's object model did not read. Small additions, the printer UI is
not affected:

| File | Change |
| --- | --- |
| `src/ObjectModel/Sensor.*` | `UpdateProbeValue` / `GetProbeValue` / `ResetProbes` (sensors.probes[].value[0]) |
| `src/Subscribers/SensorSubscribers.*` | subscribes to `sensors:probes^:value^` |
| `src/ObjectModel/Tool.*` | `SetNextTool` / `GetNextTool` (state.nextTool) |
| `src/Subscribers/StateSubscribers.*` | subscribes to `state:nextTool` |
| `src/UI/Core/EventTypes.h` | `ProbeData` and `NextTool` events |
| `src/Comm/Commands.*`, `src/Comm/JsonDecoder.cpp`, `src/Comm/Communication.cpp` | `seqs.global`: the machine's globals are fetched (CNC builds only, last in the list), for the CNC settings |
| `src/ObjectModel/Files.*` | `RequestFilesAt(fullPath, …)`: a listing of any folder (JOB LIST on a second card, "1:/"); `RequestFiles` uses it |
| `src/main.cpp` | the CNC UI instead of the printer UI (`-DDUETSCREEN_CNC_UI=ON`), portrait, rotation from SETTINGS, the start-up splash |
| `tests/src/test_cases/Comm/test_communication.cpp` | `SendNext` expects the extra `global` request when the build has it |
| `scripts/fake_duet.py` | new: a pretend CNC Duet on the network for the simulator (see "Testing without a machine") |

## Rules

- Positions come from `CncLayout.h`, never raw numbers (same rule as on PanelDue).
- CNC objects style themselves (`lv_obj_remove_style_all`) and do not use the DuetScreen themes.
- Each screen is checked against the PanelDue reference screenshots (PanelDue screen x 1.25,
  nav bar anchored to the bottom; the panel's extra 24 px go to the page content).

## Status

| Part | State |
| --- | --- |
| Start-up splash | done: 3 s over the home screen, a touch closes it; not after the UI's own restart (ACCENT COLOR); logos from `assets/cnc/` |
| Frame (DRO, T / S / F, nav, STOP) | done |
| Navigation: full / compact DRO per page, sub-tabs (WCS: OFFSETS TOOLS PROBE, JOB: JOB LIST PREVIEW JOB STATUS, SYSTEM: ALERT CONSOLE SETTINGS), each page opens on its first sub-tab and remembers the last one | done |
| CONTROL | done, with its popups (ALERT, HOME confirm, CUSTOM confirm, spindle numpad, SLOW JOG FEEDRATE). Hardware dial skipped (not implemented / tested on PanelDue either) |
| DRO tiles: T spindle on / standby, S speed numpad, F SLOW jog feed presets | done |
| WCS > OFFSETS | done, with its popups (numpad + POS, RESET / CLEAR / APPLY confirm, COPY TO and SHIFT forms, OVERWRITE, STEP SIZE) |
| WCS > TOOLS | done, with its popups (IS T# IN THE SPINDLE?, tool change and MEASURE confirm, SET Z numpad) |
| WCS > PROBE | done: AUTO / SEMI-AUTO / MANUAL, auto probe progress popup, M291 jog prompt (PROBE / READ / OK) |
| Machine replies (G-code responses) | done (as the PanelDue Modern UI): an "Error" reply as ALERT ! until X, other replies without a title, closing after INFO TIMEOUT (0 = not shown); the newest replaces the one shown; never over a popup in use or a prompt the machine waits for; not while SYSTEM is shown, not for a command typed in CONSOLE. Errors during a job: ALERT ! also over other popups |
| Display messages (M117) | done: MESSAGE popup with the check mark, not over a popup in use or the keyboard; from the PanelDue push message (UART) and from `state.displayMessage` when it changes (USB / network), the same text shown once |
| Machine messages (M291) | done: S0-S3 (message, OK, OK / cancel, jog prompt); S4 choices (header with the text, buttons in 3 / 2 / 1 columns as the longest fits, 4 choices as 2 x 2, < > pages for more than 4 rows, default selected); S5 / S6 numpad with header, the message's range, '.' / '-' only where allowed; S7 keyboard with header (modal): typing stops at the message's maximum length, too short gets an ALERT. X only with a cancel button (M292 P1). Answered elsewhere: closes. TOOL... prompts during a job: the JOB STATUS tool change row |
| JOB > JOB LIST | done: listing (DuetScreen's cached listing shows at once, then refreshes), folders, select + RUN (M32), SEARCH with the keyboard, SD tile (only when another volume is mounted: next card, its root; back to card 0 when it is removed) |
| JOB > JOB STATUS | done: progress / DONE / ABORTED, times, per-tool FEED WORK (M220) and SPINDLE (emulated M3 / M4) overrides with numpad, COOLANT / CUSTOM 2 / 3, tool change row (M291 TOOL... prompt, automatic change), PAUSE / RESUME / ABORT, JOB DONE notice, errors during a job as ALERT !; opens by itself when a job starts or a tool change prompt arrives |
| MACROS | done: list (11 rows) or grid (2 x 5), names without "!", "12_" and ".g", folders, RUN MACRO (M98, refused while a job runs, allowed while paused), SEARCH with the keyboard, pick mode for SETTINGS > CUSTOM n > MACRO |
| SYSTEM > ALERT | done: Error: / Warning: lines of every reply, newest first, 5 pages of 10 (PanelDue: 11 on one page) with a page bar, age, unread dot, repeat of the newest only refreshes it, tap = ERROR / WARNING popup. Kept until power off |
| SYSTEM > CONSOLE | done: 15-line log, newest message first, colours (command / reply / warning / error), word wrap, ages; keyboard sends and stays open, 5-command history, draft kept; 7 lines stay visible above the keyboard (PanelDue 6) |
| SYSTEM > SETTINGS | done: page 1 SYSTEM (ACCENT COLOR (restarts the UI), BRIGHTNESS, INFO TIMEOUT, SCREEN DIMMING, SCREEN ROTATION, SOUND, CONNECTION, SPLIT OPERATIONS (PREVIEW: AUTO / TOOL CHANGES), ABOUT), page 2 CUSTOMIZATION (as PanelDue, plus IDLE TIMEOUT, JOG PRESETS, OVERRIDES), page 3 JOG DIRECTION (arrows and "°" on the MOVE buttons of CONTROL and the jog prompt), PROBE values saved, FACTORY RESET (screen / machine / both) |
| JOB > PREVIEW | done (phase 1): toolpath lines in the stock box, selected operation in the accent colour, others dimmed; ISO / TOP / FRONT, drag to rotate, double tap = ISO; G0 shows the selected operation's rapids; operation strip (number, name in 2 rows of 7 letters, tool) with paging; info strip (full name, time, tool, Z range, S, F, coolant, WCS); running operation marked and followed. Info strip first row: [n NAME i] opens the info popup (X / Y and Z range, cut and rapid length, plunge feed, tool description); a warning button (amber triangle), only when needed, opens the WARNING(S) popup (each warning and its reason); a small triangle on an operation button marks its warnings (below the stock bottom, cutting with the spindle off, tool not in the tool table, outside the X / Y travel); the time has its own column; a rejected download password opens the keyboard (MACHINE PASSWORD). Later: material removal (height map), live tool position, read-only G-code view |
| CNC settings from / to `global.cnc*` | done (SYSTEM > SETTINGS). config.g must run the file at power-on: `M98 P"0:/sys/cnc-settings.g"`; the globals are guarded in the file, so config.g need not declare them |

## JOB > PREVIEW

**File download.** RRF cannot send a file's contents over UART or USB, so PREVIEW downloads the job
file over the network, whatever the machine connection is: commands stay on USB (Duet3D's
recommendation), the file comes from the address the machine reports (`network.interfaces[].actualIP`)
with `rr_connect` / `rr_download` / `rr_disconnect`; with a NETWORK connection, from that address. The
password is DuetScreen's own setting (`duet:password`), RRF's default `reprap` when not set. DSF (SBC
mode) answers the same requests. No address: "PREVIEW needs a network connection". Files up to 32 MB;
very large files are thinned (tiny moves merged) and drawn thinner while dragging.

**What the G-code should contain** (all optional):
- Operation names: a comment line before each operation (Fusion 360 does this), FreeCAD's
  `(Begin operation: …)`, or your own `;CNC_OP Name` line (100 % reliable, e.g. added to a post).
  A comment repeated 3 times or more (a post's `(Retract)`) is never a name.
- No names at all: operations at tool changes, and the same tool is split where an operation probably
  starts: the start block again (M3 / M4 or G54…G59 after cutting), or a feed / spindle speed the
  operation has not cut with, right after a retract to its highest Z. Pieces are called "T2 (1)",
  "T2 (2)". These are guesses: SETTINGS > SPLIT OPERATIONS: TOOL CHANGES turns them (and splits by
  name within one tool) off.
- Tools: Fusion's header lines `(T1  D=6 CR=0 - ZMIN=-5 - flat end mill)`, or `;CNC_TOOL T1 D=6 TYPE=FLAT`,
  or `(T1: 6mm Endmill)`. Without them the info strip shows the tool number only.
- Stock: `;CNC_STOCK X=120 Y=80 Z=20` (optionally `X0= Y0= Z0=`: corner at minimum X / Y and the top Z,
  default 0 0 0). Without it the box around the cutting moves is shown, labelled TOOLPATH.

**Warnings** (a small triangle on the operation button, the warning button in the info strip, the reasons in the WARNING popup):
below the stock bottom (needs `;CNC_STOCK`), cutting moves before M3 / M4, a tool number not in the
machine's tool table, X / Y outside the axis limits with the work offset of the operation's WCS (rapids
count; Z is not checked: it depends on the tool length offset).

**Password**: when the machine rejects the download password, tap the notice and type it; it is kept in
DuetScreen's password setting and the download is tried again.

To be checked on hardware: render speed on the T113 (drag), USB command channel plus network
download side by side, the download in SBC mode.

## Start-up splash

Shown when the CNC UI starts, after DuetScreen's own Linux boot splash (`/etc/splash.png`, not changed):
a drawn end mill cutting a pocket, "CNC", "MODERN UI" and a rule in the accent colour, and the firmware
version (`FIRMWARE_VERSION`, the release tag; a local build shows e.g. `v1.0.0-dirty`). It stays 3 s,
a touch closes it earlier. When the UI restarts itself (a new ACCENT COLOR) it is skipped, as on
PanelDue: a marker file in the temp folder (`/tmp/duetscreen_cnc_skip_splash`, gone after a reboot).

**Logos.** Three slots at the bottom, 170 x 100 px each, filled from your own files when present:

| Slot | File |
| --- | --- |
| left | `assets/cnc/logo_duet3d.png` |
| middle | `assets/cnc/logo_rrf.png` |
| right | `assets/cnc/logo_millennium.png` |

PNG with a transparent background (the splash background is #12161C). Up to 170 x 100 px is shown at its
own size, centred; larger is scaled down to fit. A missing file leaves its slot empty. On the panel the
files go to `/etc/assets/cnc/` with the rest of the assets. The logos are the owners' trademarks: they
are not in this repository; use them with their permission.

## Adding a page's content

Each page / sub-tab already has its own container: `CncHomeView::getContent(page, subTab)`.
Its top edge is at screen y `CncHomeView::contentTop(page)` (PanelDue 321 under the full DRO,
142 under the compact one) and it ends at `contentBottom(page)` (above the sub-tabs or the nav bar).
A page is written like `CncControlPage`: `create(parent, parentTop)`, positions `S(pdY) - parentTop`.

## Rotation

The panel is 1024 x 600 landscape. The CNC build rotates it by 90 degrees in software. In LVGL's
`direct` render mode the fbdev driver rotates the whole screen on every refresh, so the CNC build
uses `partial` mode (`CMakeLists.txt`), which rotates only the areas that changed.
To be measured on the screen: tab switches and live DRO updates should feel as quick as in landscape.
