# Phantom Hourglass Recomp

A native port of **The Legend of Zelda: Phantom Hourglass** (Nintendo DS) to Windows, Linux and Android, with a
Nintendo Switch fork below. The game's ARM9 code is statically recompiled to C and compiled ahead of time instead of
emulated; the rest of the DS comes from [melonDS](https://github.com/melonDS-emu/melonDS).

> **This project is about one game.** It is not a DS emulator and it will not run other DS titles. Everything here
> is aimed at getting *Phantom Hourglass* right. (The Switch fork is Phantom Hourglass only; a Spirit Tracks port
> is not started.)

> **No game data is included.** You build the game from your own dump. Please don't share the builds you make: the
> game folder and the APK contain the game.

## What you need: your own dump

Supported dumps (checked by SHA-1):

| Dump | SHA-1 |
|---|---|
| USA | `4c8f52dd719918bbcd46e73a8bae8628139c1b85` |
| USA, D-pad patch | `7e0d26d143992f1eee182b24f1abb9e7f038eacf` |
| USA, Button controls patch | `8915d99da2dd99bd2c05bc7a79fd2660a5e8ce5e` |
| Europe | `02be55db55cf254bd064d2b3eb368b92a5b4156d` |
| Europe, D-pad patch | `8e3bbda6056c47f06fecf1a06d9f144a19283d88` |

With a clean dump the D-pad doesn't move Link (the patch adds that); turn on **Analog stick** in the menu instead.
On Switch, the clean USA dump (`azee.nds`, SHA-1 above) is the one used: it goes at
`sdmc:/switch/ph-recomp/rom/azee.nds`. See [Installing on Switch](#installing-on-switch).

- For Switch: a modded Switch running Atmosphère (see Installing on Switch below). Only the clean USA dump is
  used on Switch — every Switch build and flight to date was built from it. The D-pad and Button-controls
  patches are in the table because the recompiler accepts them, but no Switch build has ever been made from a
  patched dump; a Europe dump (`azep.nds`) is the code fallback only if `azee.nds` is absent, and no Europe
  Switch build has been staged or booted.

## Nintendo Switch (this fork)

This fork adds a Nintendo Switch target to the upstream project,
[dguillot-gh/phantom-hourglass-recomp](https://github.com/dguillot-gh/phantom-hourglass-recomp):
a homebrew `.nro` for a console running Atmosphère, launched from a Sphaira forwarder tile (not title
takeover, not the homebrew menu). The fork lives at
[Eladerino/phantom-hourglass-recomp-nx](https://github.com/Eladerino/phantom-hourglass-recomp-nx),
branch `switch-skeleton`. Everything below the Switch section describes the upstream project and its PC/Android
builds; there is no Switch download.

### Status on Switch

It boots and plays on real hardware. The renderer is software only, at 1x: the OpenGL and Compute renderers are
not built for Switch, and the menu rows for them stay on Software / 1x / Pixels / 4:3. Frames are presented by a
real Mesa GLES presenter (OpenGL ES 3.2, Mesa 20.1.0-rc3 NV120). It runs at stock clocks (1020/384/1331 MHz):
the forwarder title (`054918A52C8D1000`) is not in the sys-clk boosted list. It has not been played from start to
finish, so later areas should be expected to have bugs. Stick direction (cursor and movement) is disputed —
see Known issues below.

Playable, early, and not yet played from start to finish. The furthest any test session has got is the opening
through Mercay Island; later areas are little tested — expect bugs. What follows is what the Switch port does
today, measured on hardware at stock clocks (see Performance below). Nothing in this section is taken from the
PC/Android version.

| | |
|---|---|
| Title screen, file select, dialogue | works — title reached on every boot; file select reachable by pad alone via the touch cursor (Phase M); dialogue advances by cursor tap and by A-tap (Phase N) |
| Opening through Mercay Island | works — first live gameplay on hardware in Phase K (Mercay Island, Link walking); Phases M and N played the opening (Niko, Tetra, Ghost Ship, Bellum) through to free gameplay on Mercay beach |
| Saving and loading | works, with a caveat — Phase K verified the save fix: zero "save: cannot write" lines in a 4,808-line log, `azee.sav` rewritten and the leftover `azee.sav.tmp` gone. The save loads at boot (524,288 B). Caveat: the save on the card is still the Phase L opening state — the Mercay progress from Phases M and N was never saved, because those sessions were closed from HOME instead of with the game's Save and Quit |
| Screenshots | works — right-stick down writes a 256×384 PNG to `data/screenshots/` as `shot-<n>.png` (Phase K: 4 files; Phase L: 1) |
| ZL/ZR layout swap, menu, save states | works — ZL/ZR toggles verified in Phase K (13 ZL + 22 ZR notes, all legal single toggles); the menu opens with MINUS+PLUS; States saves and loads (Phase L: slot 12 saved with thumbnail, loaded back to the title) |
| Touch cursor (L3) | works, direction **disputed** — see Known issues |
| Interpolation: 3D scenes blended to 60 fps | works — Phase N: `gx-interp` logged 1,816 lines (one per 60-frame window); interpolation active in 1,334/1,816 windows (73%), and in 1,334/1,393 (96%) of the 3D windows (opening cutscenes + Mercay gameplay). It smooths motion; it does not fix speed |
| Later areas (Temple of the Ocean King onwards) | little tested — expect bugs |
| Spirit Tracks | not started |

#### Performance

Measured on the Switch at stock clocks (1020/384/1331 MHz — the forwarder title `054918A52C8D1000` is not
among sys-clk's boosted IDs, so no overclock applies). `-O2` was kept for the game module after Phase M.
The frame budget is 16.71 ms per frame at 59.83 Hz. Phase N medians, over 1,816 60-frame windows
(frames 1–108,960):

| | emu | arm9 | sys | gpu3d | fps |
|---|---|---|---|---|---|
| Whole session | 34.32 ms/f | 3.42 | 28.97 | 1.95 | 25.20 |
| Title (first 600 windows) | 33.24 ms/f | 3.50 | 28.08 | 1.95 | 26.10 |
| In game (last 300 windows) | 35.04 ms/f | 3.47 | 29.62 | 2.16 | 24.40 |

So: about 25 fps in game, roughly twice the frame budget, with `sys` dominant everywhere (ARM7/DMA/timers/2D/SPU),
not the software 3D rasterizer (`gpu3d` ~2 ms/f). 60 fps is not reached and not claimed. Two caveats on the
numbers: Phase N ran slower than Phase M (emu 27.44 ms/f, fps 31.70 over 1,286 windows), but the content mix
differs — Phase N is mostly 3D Mercay gameplay, Phase M's session was mostly title/dialogue — so do not read
that as a regression; and the frame-time probe itself inflates `emu` by an estimated ~0.8–1.9 ms/f of timer
overhead (Phase L: ~578k timed segments per 60-frame window), so quote the counts with the numbers.

#### Known issues

- **Cursor Y direction — DISPUTED, not fixed.** Phase N's flight said PASS (all four directions correct on the
  title screen, viewed captures + crosshair finder). A live hands-on session on 2026-11-27 17:53 NST in the
  Phase O build reported stick-up moves the cursor DOWN, title and in-game. The Phase O flight's own pre-hold
  measurements agreed with Phase N — and the live report contradicts them. A revert of the Y flips
  (`70152e6`, pushed in Phase P) is **unproven, pending a flight**; do not call this fixed until a flight
  settles it with viewed captures.
- **Stick movement Y — DISPUTED, same story.** Phase N: stick-up walks Link SOUTH (proven twice: pond to
  beach/sea, facing south). Phase M: stick-up walked Link NORTH (camera panned up). The flight evidence
  contradicts itself, and the live report says the inversion is not cursor-only. The same unproven
  revert (`70152e6`) is supposed to fix it; pending a flight.
- **A-tap step — quick tap turns Link but does not walk him** (Phase N: 0 px on open ground, 5× facing east,
  landmarks byte-identical). The fix in `d6af4a9` (A-tap held at level, so a held A should walk Link to the
  tap point) is **not flight-proven** — the Phase O flight was halted before its gameplay checks ran. What
  does work: A-tap opens NPC conversations and advances dialogue one line per tap (8 taps through Oshus's
  lines, Phase N). Pad A alone does *not* advance PH dialogue (Phase M, twice) — the game advances dialogue
  on stylus tap only; that is game behaviour, not a port bug.
- **Title touch — needs a sustained touch, probably.** Phase N: 50 ms taps failed twice, a 500 ms held-A
  advanced to file select. Phase O saw a 50 ms tap advance on its second attempt — but that segment is
  unreliable (hands-on overlap), so treat "hold the touch" as the safe way, not a proven rule.
- **50 ms click — mixed.** Title: fails (Phase N). File select: passes (slot/Start/Adventure, 3/3). Dialogue:
  passes (41 batches × 3 taps advanced Niko/Tetra/Ciela/Oshus dialogue, Phase N).
- **Trophies Reset — touch-tap only.** The Reset confirm has no gamepad or key binding (source-checked,
  Phase L); it needs a physical finger tap, which the test driver cannot inject. Untested, not broken.
- **Trophies — the viewer works; no achievement has yet been observed unlocking on Switch.**
- **HD text — in the menu, but in-game dialogue text was never compared on Switch hardware.** Phase L tested
  the wrong surface: the menu's own glyphs are identical with HD text on and off at 3× zoom.
- **Continue — row enabled, activation untested.** The menu footer read "Continue from 11-27 08:43" in
  Phase M (matching the `resume.state` written by Phase L's Quit), but nobody has selected it; Phase N
  found the row visible but not selectable from the Settings tab, and the States tab was not tried.
- **Flickering shadows — UNDER INVESTIGATION.** Reported live 2026-11-27: flickering shadows "anywhere in the
  overworld", in sustained play. Leading hypothesis is a pre-existing interpolate artifact (blended shadow
  polygons alternating with real frames), because flights never do sustained overworld walking — but that is
  **not proven**. The Phase O build changed nothing rendering-side except one write with no known reader, so
  do not attribute it to Phase O without evidence. The distinguishing test (same walk with
  `interpolate=0`) has not been run yet.
- **Intro cutscene does not skip on A.** The storybook panels are timer-driven (~30–150 s each); a held A
  during the intro did not skip (Phase N). This is the game's own pacing, not a stuck input.
- **Menu quirks (Phase L, minor).** The Settings list does not scroll — the Quit row (row 14, last) sits
  below the visible area and the cursor bar disappears off the bottom. The Continue row overlaps the
  "Software renderer only (Switch)" footer at 720p. Long D-pad batches lose presses on this menu (use
  short batches with a capture between). The MINUS+PLUS chord's PLUS sometimes leaks through to the game
  (intermittent — it advanced past the title in some boots, not others).
- **ZL/ZR held across the menu closing fires one spurious layout toggle** (desk-found from the Phase K log
  and source, minor). Fixed in code (`61f3490`: the menu-close re-arm now polls the live axis state instead
  of clearing it); not flight-proven.

#### JIT (ARM7): the gate passed, the speedup does not exist yet

The sys-split probe (Phase N) says an ARM7 JIT is worth building: overhead-corrected ARM7 medians are
12.16 ms/f on the title (42.7% of `sys`) and 11.71 ms/f in game (41.0% of `sys`), against a gate of
≥ 5 ms/f AND ≥ 25% of `sys` — passed by more than 2× on both legs. But there is **no JIT build and no
measured speedup anywhere**: the first LOQ compile probe FAILED (stopped at [85/138], three patched
translation units), the fix is committed as `1488e08` and pushed (Phase P, remote `switch-skeleton` at
`70152e6`), and it is still unproven — no successful JIT compile, no JIT NRO booted. The JIT is OFF by
default. Quote no speedup factor and no fps claim for it.

#### GPU renderer: none exists on Switch

There is no deko3d/GPU renderer in the Switch build — the only renderer is Software at 1x. A Phase 0
deko3d spike (design + implementation) has landed and been reviewed READY, but it lives on a local branch
(`spike-phase0`, commit `32e5be8`), is not pushed, has never been built on the LOQ and has never booted. Do not present any
GPU renderer as existing.

### Installing on Switch

1. Dump your own Phantom Hourglass cartridge (see "What you need" above). The Switch build uses the
   **clean USA dump** only.
2. On the SD card, copy the staged build into `sdmc:/switch/ph-recomp/` (see the layout below).
3. Put your clean USA dump at `sdmc:/switch/ph-recomp/rom/azee.nds` — that exact file name. The game
   looks for `azee.nds` first and only falls back to `azep.nds` if it is absent.
4. Launch from the HOME Menu: tap the **Phantom Hourglass** forwarder tile
   (titleId `054918A52C8D1000`). Do not launch through hbmenu title takeover.
5. First boot defaults to the software renderer with the Analog stick on. `settings.ini` is created
   when you Quit from the menu.

```
sdmc:/switch/ph-recomp/
  ph-recomp.nro          (the game)
  ph-recomp.nacp         (title metadata, staged alongside the NRO)
  rom/azee.nds           (your clean USA dump, exact name)
  data/settings.ini      (created when you Quit from the menu)
  data/saves/azee.sav    (512 KB flash save)
  data/states/           (save states + resume state, created by the game)
  data/screenshots/      (screenshots, created by the game)
  data/temple_floors.txt (Temple of the Ocean King floors reached)
  stderr.log             (startup + probe log, overwritten every boot)
```

Re-staging the NRO never touches `data/saves/`: your save is left alone.

Last verified staged build (Phase O, 2026-11-27): NRO 42,043,461 bytes, sha256
`e801108acfb2d37b53c960e10d3039348fedd9867218580ad7c47e50dfcb5583`, built from `9a65dd9`.
HEAD has since moved to `70152e6` (pushed 2026-11-27); at the time of writing that build had not been
staged yet — see the phase logs for the latest staged build.

### Building the Switch version

You build it yourself, from your own dump. There is no Switch download and no distributable NRO: the generated C
is the game's code, so it is built on your machine from your own ROM. There is no one-click Switch builder yet.
The build that actually works is a two-stage cross-compile, driven from the LOQ so far:

1. **Host build first** (Windows PowerShell, from the fork clone). This runs `ntr-recomp` on your
   clean USA dump and generates the recompiled C:
   ```powershell
   git clone --recurse-submodules https://github.com/Eladerino/phantom-hourglass-recomp-nx.git ph-recomp-nx
   cd ph-recomp-nx
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\RUN-ALL.ps1 -NoTest -Rom <path to your clean USA dump>
   ```
   `-NoTest` skips the PC launch/smoke test. This leaves `build/release/recomp_generated/`
   (`func_table.c` + 64 `funcs_*.c` shards). melonDS is pinned at `906e9eb`
   (`git submodule update --init --depth 1` if it is missing).
2. **Switch configure + build** (devkitPro devkitA64: `switch-dev`, `switch-cmake`, `switch-mesa`,
   `switch-libdrm_nouveau`; `DEVKITPRO` set — the `switch` preset requires it):
   ```sh
   cmake --preset switch -DPH_ROM=<path to your clean USA dump> -DPH_GENERATED_DIR=<build/release/recomp_generated>
   cmake --build --preset switch
   ```
   453 steps; configure ~5 min, build ~6 min on the LOQ (Phase O). SDL3 comes from
   devkitPro/SDL @ `c329016c` (`switch-sdl-3.4`), fetched at configure time.
3. **Output:** `build/switch/frontend/ph-recomp.nro` (+ `ph-recomp.nacp`), packaged automatically
   by `elf2nro` + `nacptool` from `$DEVKITPRO/tools/bin`. Stage both files to
   `sdmc:/switch/ph-recomp/` and your dump to `sdmc:/switch/ph-recomp/rom/azee.nds`.

### Playing on Switch

The gamepad is the controller; the touchscreen (handheld) or the gamepad cursor (docked) is the stylus.
Analog-stick movement is on by default (change it in the menu, or `stick=` in `settings.ini`). The default
layout is Bottom (touch screen only); ZL swaps the shown screen and ZR toggles Both screens stacked.

#### In game

| Switch | DS |
|---|---|
| A | A. Also taps the stylus 24 px (one tile and a half) in front of Link, so in Analog mode it talks to people and advances dialogue — see A-tap below |
| B | B |
| X | X |
| Y | Y |
| L | L — except in the Temple of the Ocean King lobby, where it steps the floor picker (see below) |
| R | R |
| PLUS | Start |
| MINUS | Select |
| D-pad | D-pad |
| Left stick, D-pad mode | D-pad, 8-way, dead zone 0.35 |
| Left stick, Analog mode (default) | Moves Link: a virtual stylus held 22–80 px from him (22 px, just past the game's 20 px dead zone, is a slow walk; 80 px is a run), dead zone 0.22 |

A on its own (KeyA) does not advance dialogue — the game advances dialogue on a stylus tap only. What makes A work
for dialogue is the A-tap: pressing A also puts a stylus tap 24 px in front of Link, held for 100 ms at minimum
(and held as long as A is held, so holding A walks Link to the tap point and he stops there). It only fires in
Analog mode, and only after you have moved with the stick at least once since boot — that movement latches both the
direction Link faces and his position, and the tap is computed from them. A quick tap stays a 100 ms tap: one
dialogue advance per press. In D-pad mode there is no direction to latch, so A never taps and dialogue needs the
cursor or a finger.

#### Touch and the gamepad cursor

| Input | Stylus |
|---|---|
| Finger on the touchscreen (handheld) | Stylus at that spot, bottom screen only. Lifting clears within one frame — the frontend polls the touch state directly, because the Switch driver never delivers a finger-up event |
| L3 (menu closed) | Toggles the touch cursor on/off. Crosshair overlay; the left stick moves it and A taps at it |
| Left stick, cursor on | Moves the cursor: dead zone 0.15, 60 px/s just past the dead zone, 480 px/s at full tilt — it crosses the 256 px-wide screen in about half a second |
| A, cursor on | Taps at the cursor. The tap is a 100 ms time hold, not a frame count: while A is held the stylus stays down, and on release it stays asserted for 100 ms |

Priority when more than one wants the stylus: finger beats cursor tap beats A-tap beats analog stick. While the
cursor is on, the pad is a mouse, not a controller: DS buttons are swallowed (A is the tap, not KeyA), the
right-stick functions, R3 mic and the analog stick are gated off, and ZL/ZR stay live. Opening the menu suspends
the cursor (its position is kept) and it resumes where it was when the menu closes. With the Top-only screen
layout the bottom screen is not shown, so the cursor cannot tap.

> **Stick directions — open dispute, not proven.** The code at this HEAD reads stick-up as up for the cursor, the
> left stick, and the D-pad: HEAD 70152e6 reverted three Y flips (cursor, shared stick read, D-pad mode) that had
> been added on the premise that the Switch driver reports stick-up as positive Y. A live hands-on report on
> 2026-11-27 said stick-up moved the cursor down, on the title screen and in game, while an earlier instrumented
> flight had called the cursor direction a pass. The revert has not had a flight prove it yet, so the directions
> above are what the code intends — if up feels backwards, that is this known dispute, not your controller.

#### Right stick and triggers (menu closed, cursor off)

| Input | Does |
|---|---|
| Right stick up, held | Fast-forward (the game runs unpaced, audio dropped) |
| Right stick down, edge | Screenshot, to `data/screenshots/` |
| Right stick left, held | Close the DS lid (both screens black; release reopens) |
| Right stick right, held | Blow sample (the bundled mic sample) |
| R3, held | Mic blow (generated noise). Menu-gated and cursor-gated: it does not blow with the menu open or the cursor on |
| ZL, past halfway (>16383) | Swap the shown screen: Touch ↔ Top (from Both it goes to Touch) |
| ZR, past halfway (>16383) | Toggle Both screens stacked ↔ the single Touch screen |

The right stick reads its dominant axis only, past 0.6 deflection, so a diagonal picks one function and a light
bump picks none. ZL/ZR are trigger axes, not buttons: they fire on the edge past halfway, and a trigger held
through the menu opening does not fire when the menu closes.

#### Overlay menu — MINUS+PLUS together

Press MINUS and PLUS together, in either order; the menu opens on the second press. The first button's press still
reaches the game as Start/Select until the second press lands.

| Input | Menu |
|---|---|
| D-pad up/down | Move between rows (Settings), slots (States), or trophies (Trophies, ±1) |
| D-pad left/right | Change the row's choice (Settings); move the slot (States); cycle the filter All / Done / Left (Trophies) |
| A | Confirm: steps the row's choice (Settings); loads the selected slot, if it has a save (States); nothing (Trophies) |
| B | Close the menu |
| L3 | Next page: Settings → States → Trophies → Settings |
| R3 | Save a state to the selected slot — States page only |
| Finger tap | Taps the row, chip, tab, slot card, or button under it. This is the only way to use the click-only controls: the Trophies Reset and Prev/Next buttons, and the States Save/Load buttons |

**Settings** (15 rows) — the Switch values, since the GPU renderers are not built here:

| Row | Switch choices |
|---|---|
| Screen | Touch / Top / Both |
| Renderer | Software only |
| Resolution | 1x only |
| 3D textures | Pixels only |
| 3D fixes | Off / Seams / Warp / Both — Seams and Both are greyed out without a GPU renderer; Warp works with software |
| Picture | Sharp / Smooth |
| HD text | Off / On |
| Picture size | Fit / Stretch |
| Widescreen (test) | 4:3 only |
| FPS counter | Off / Top L / Top R / Bot L / Bot R |
| Fullscreen | Off / On |
| Gamepad's left stick | D-pad / Analog |
| Language (restart) | Auto / EN / FR / ES — Europe dumps add DE / IT |
| Continue | Continue from where you quit (greyed out until there is a resume point) |
| Quit | Quit the game |

**States:** 12 slots in a 4-column grid. Arrows move the slot (left/right ±1, up/down ±4, wrapping around). A loads
the selected slot if it has a save; R3 saves to it. Tapping a card selects it; the Save and Load buttons below the
grid do the same by tap. There is no pad shortcut for save/load outside the menu — F5/F8 are keyboard-only.

**Trophies:** 67 achievements. D-pad up/down scrolls ±1, left/right cycles the filter (All / Done / Left).
PgUp/PgDn (±6) have no gamepad bind. Reset is touch-tap only, with a two-tap confirm: the first tap arms it, the
second tap resets. The Prev/Next buttons scroll ±5 by tap.

### Settings and logs

Settings live in `data/settings.ini` beside the saves. A few `key=value` lines. Edit the file by hand or change
the matching menu row where one exists; the file is rewritten with all keys when you Quit from the menu.

| Key | Default on Switch | What it does |
|---|---|---|
| `stick` | `1` | Left stick moves Link (Analog). Menu row "Gamepad's left stick: D-pad / Analog". |
| `interpolate` | `0` | Blend the game's 30 Hz 3D scenes to 60 fps, one frame of latency. Smooths motion, does not fix speed. No menu row; also `--interpolate`. Proven on hardware in Phase N. |
| `gx_probe` | `0` | Log the gx-probe scene-match stats and the gx-interp counts every 60 frames to `stderr.log`. The gx-interp line needs `interpolate` on too. No menu row; also `--gx-probe`. |
| `frame_time` | `0` | Log a per-frame time split every 60 frames to `stderr.log`. No menu row; also `--frame-time`. |

The settings file as last staged reads `interpolate=1`, `gx_probe=1`, `frame_time=1`, `stick=1` — the probe keys
enabled, though their code defaults are 0. Note: a saved `renderer=compute` line is the saved name for the menu's
Software row on Switch, not a GPU renderer — there is no GPU renderer in the Switch build.

Each boot writes `sdmc:/switch/ph-recomp/stderr.log`, truncated on every boot. Found a bug?
[Open an issue](../../issues) and include:

- **Where:** Switch (this fork)
- **Settings:** the contents of `settings.ini`
- **Dump:** USA clean (or what you used)
- **What happened:** the steps to get there, plus a screenshot or a save state if you can
- **Logs:** `stderr.log` at `sdmc:/switch/ph-recomp/stderr.log` — pull it off the card **before you launch
  the game again**, because it is truncated on every boot

Please don't attach your ROM or any game files.

### Switch code layout

The Switch build is the `switch` CMake preset (`PH_SWITCH=ON`). The Switch-only code is in `frontend/src/`:

- `main.cpp` — the frontend loop, gamepad/touch input, the touch cursor, A-tap, the overlay-menu chord and
  nav, the right-stick functions, settings load/save
- `switch_present.cpp` — the Mesa GLES presenter: the software renderer's two 256×192 frames are uploaded
  each frame as a 2-layer array texture and drawn as quads, through SDL3-nx's EGL context
- `menu.cpp` / `menu.hpp` — the overlay menu (Settings / States / Trophies pages)
- `ph_stick.cpp` / `ph_stick.hpp` — the analog-stick virtual stylus, which moves Link from his tracked position
- `ph_temple.cpp` — the Temple of the Ocean King floor picker (L only)
- `game_module.cpp`, `ph_hooks.cpp`, `ph_text.cpp`, `achievements.cpp` — the game module, runtime hooks,
  HD text, and Trophies; shared with PC/Android

### What is not here yet

- **ARM7 JIT:** gated, default off. The compile probe failed; the fix is committed (`1488e08`) and pushed
  (Phase P, remote `switch-skeleton` at `70152e6`) but still unproven — no successful JIT compile, no JIT NRO
  booted. No measured JIT speedup exists — no factor is quoted.
- **deko3d renderer:** none exists in this build. A Phase 0 coexistence spike (design + implementation) sits on
  the local branch `spike-phase0` (commit `32e5be8`), not pushed; it is a spike, not a renderer.
- **Spirit Tracks:** not started.
- **Upstream rebase:** deferred. Upstream HEAD is `8b85adf`; the rebase has one known trivial conflict in the
  top-level `CMakeLists.txt`.

## Upstream project (PC and Android)

Everything below is the upstream project's own documentation, kept for reference. It describes Windows, Linux and
Android only; Switch differences are in the section above.

<details>
<summary>Upstream: screenshots (PC and Android)</summary>

<p>
  <img src="docs/images/pc-16x9-gpu.jpg" width="100%" alt="PC: a temple room in 16:9 with the GPU renderer at 8x">
</p>
<p>
  <img src="docs/images/phone-16x9-compute-4x.png" width="49%" alt="Phone: 16:9 rendering with Compute at 4x">
  <img src="docs/images/pc-menu.jpg" width="49%" alt="PC: the side menu (F1) over the game">
</p>
<p>
  <img src="docs/images/phone-minimap-temple.jpg" width="100%" alt="Phone: 21:9 in the Temple of the Ocean King, with the minimap of the floor map and sand timer in the corner">
</p>

</details>

<details>
<summary>Upstream: features</summary>

## Features

- Software, OpenGL and Compute renderers, up to 8x on PC and 4x on Android
- Widescreen (16:9 and 21:9) with Compute
- HD dialogue text (vector font at screen resolution)
- 67 offline achievements with a Trophies viewer (matching RetroAchievements set #14323)
- 12 save-state slots, plus Continue from where you quit
- Language selector: English, French and Spanish, plus German and Italian on Europe dumps
- Touch, keyboard and gamepad; analog-stick movement; mic and blow buttons
- Android: movable on-screen controls, portrait mode with both screens stacked
- Android: a minimap of the other screen in the bottom-left corner (landscape), like a game's corner map: tap it to
  swap the screens, hold it to move it, pinch it to resize it, or hide it with the MAP button. It moves above dialogue
  boxes and hides in cutscenes.
- Temple of the Ocean King floor checkpoints: every floor Link reaches is remembered, and 1F's stairs down can take
  him straight to any of them (see below)

Tested devices and fps measurements: [docs/TESTED.md](docs/TESTED.md).

Ideas and features being considered next: [docs/WISHLIST.md](docs/WISHLIST.md).

</details>

<details>
<summary>Upstream: what you need (PC and Android)</summary>

## What you need

- Your own dump. Supported dumps, checked by SHA-1, are listed in the table at the top of this README.

  With a clean dump the D-pad doesn't move Link (the patch adds that); turn on **Analog stick** in the menu instead.
- Windows 10/11 64-bit or Linux, about 10 GB of disk, and an OpenGL 3.3 GPU for the GPU renderer.
- For Android: an Android 8+ arm64 phone (OpenGL ES 3.2 for the GPU renderer) and a USB cable.

</details>

<details>
<summary>Upstream: quick start (Windows PC and Android)</summary>

## Quick start (new to this?)

You build the game on a Windows PC from your own copy of the game. It takes about an hour the first time, mostly
waiting. Step by step:

1. **Install Git** (to download this project): open the Start menu, type `PowerShell`, open it, and paste this line
   (right-click pastes), then press Enter:
   ```powershell
   winget install --id Git.Git -e
   ```
   Close PowerShell and open it again afterwards.
2. **Make a folder for everything**, for example `C:\Games\ph-workspace`, and inside it a folder named `roms`. Put
   your Phantom Hourglass dump in `roms` (any file name).
3. **Download the project** into that folder. In PowerShell, paste these two lines (change the path if you used another
   folder):
   ```powershell
   cd C:\Games\ph-workspace
   git clone --recurse-submodules https://github.com/dguillot-gh/phantom-hourglass-recomp.git ph-recomp
   ```
   Don't use GitHub's green "Download ZIP" button: the ZIP leaves out melonDS, and the build then fails.
   The folder should now look like this:
   ```
   C:\Games\ph-workspace\
     ph-recomp\           (the project)
     roms\your-dump.nds   (your game)
   ```
4. **Build it** (installs the build tools the first time; Windows asks for admin once):
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File ph-recomp\scripts\RUN-ALL.ps1
   ```
   When it says it's done, open `C:\Games\ph-workspace\games\PhantomHourglass\` and double-click
   `PhantomHourglass.exe` (`PLAY.cmd` does the same). That folder is the whole game: move it anywhere you like.
5. **For an Android phone:**
   - Turn on USB debugging once: Settings → About phone → tap **Build number** seven times; then Settings → System →
     Developer options → **USB debugging** on.
   - Plug the phone into the PC with a USB cable and tap **Allow** on the phone when it asks.
   - In the same PowerShell window:
     ```powershell
     powershell -NoProfile -ExecutionPolicy Bypass -File ph-recomp\scripts\RUN-ANDROID.ps1
     ```
     It builds the app, installs it and copies your game in. Without a cable, add `-NoPhone`: it only makes the app (in
     `apks\`); copy that file to the phone, tap it to install, and pick your ROM on the app's start screen.

**If something goes wrong:** the script stops with a `FAIL` line that says what's missing. Every step's full output is
in the `logs\` folder; attach the newest log when you [open an issue](../../issues). Don't attach your ROM.

</details>

<details>
<summary>Upstream: building (Windows and Linux)</summary>

## Building

The short version, for people who already use Git and PowerShell.


Put the repository and your ROM side by side:

```
workspace/
  ph-recomp/          (this repository)
  roms/your-dump.nds  (any name)
```

**Windows**

```powershell
git clone --recurse-submodules <this repository> ph-recomp
powershell -NoProfile -ExecutionPolicy Bypass -File ph-recomp\scripts\RUN-ALL.ps1
```

This installs the build tools, builds the game and makes a `PhantomHourglass\` folder; run its `PLAY.cmd`. For the phone,
run `scripts\RUN-ANDROID.ps1` next. Every script takes `-DryRun`. Details and what each script changes on your PC:
[scripts/README.md](scripts/README.md).

**Linux**

```sh
nix develop --command ./scripts/build-linux.sh
nix develop --command games/PhantomHourglass/play.sh
```

Without Nix, install CMake 3.28+, Ninja, GCC and SDL3's build dependencies, then run `./scripts/build-linux.sh --rom
<path>`. Details: [scripts/README-LINUX.md](scripts/README-LINUX.md).

**Switch**

The Switch build is a two-stage cross-compile; see "Building the Switch version" under "Nintendo Switch" above.
In short: host-build with the `release` preset to generate the recompiled C, then `cmake --preset switch`
under devkitA64. There is no in-tree one-click Switch builder.

</details>

<details>
<summary>Upstream: controls (PC and Android)</summary>

## Controls

**PC:** mouse is the stylus. Arrows = D-pad, X/Z/S/A = A/B/X/Y, Q/W = L/R, Enter = Start, Right Shift or Backspace = Select.
M = mic (shout), N = blow, C = close the lid. F1 or Esc opens the menu. F5/F8 save/load a state, F9 continues.
Tab/F2 switch screens, F11 fullscreen, \` (hold) fast-forward. Gamepads work too.

**Android:** touch is the stylus. The small buttons are the menu (≡), TOP (top screen), ⟳ (portrait), MAP (the minimap), MIC, BLOW and
LID.
The menu has the renderer, save states and "Move and resize controls".

**Temple of the Ocean King checkpoints:** every floor Link walks down to is remembered (in `temple_floors.txt`, in
the data folder). Once he has been past B1, the Entry Hall (before 1F) shows *1F Stairs Down: B1 (L)* at the top of
the screen: press L to step through the floors reached (B1, then the deeper ones). Then go down to 1F as usual, where
the sand timer starts, and 1F's stairs down lead to the floor picked instead of B1. Link arrives as if he had come
down that floor's own stairs: the floor starts fresh, and keys from floors above are not carried down. L is used only
while the picker is shown; R and everything else go to the game. The floors are kept for every save file alike.

</details>

<details>
<summary>Upstream: testing and reporting issues</summary>

## Testing and reporting issues

Known issues and the devices it's been tested on are in [docs/TESTED.md](docs/TESTED.md).

Found a bug? [Open an issue](../../issues) and include:

- **Where:** PC (Windows or Linux) or phone, with the phone model
- **Settings:** renderer (Software, OpenGL or Compute), resolution, widescreen on or off
- **Dump:** USA or Europe, clean or D-pad patch
- **What happened:** the steps to get there, plus a screenshot or a save state if you can
- **Logs:** build problems: the log in the workspace's `logs/` folder. Game problems on Android: `adb logcat` output.

Please don't attach your ROM or any game files.

**For contributors:** the unit tests need no ROM. After building, run `ctest --preset release` (Windows) or
`ctest --preset linux-release` (Linux). On Windows, `scripts\4-TEST.ps1` also plays the game for a minute as a smoke
test. Before a PR that touches settings or menus, check both the PC menu and the Android menu and settings.

</details>

<details>
<summary>Upstream: how it works</summary>

## How it works

`ntr-recomp` turns the ROM's functions into C, which compiles into a game module that `ntr-runtime` runs natively,
with melonDS providing the rest of the DS hardware. Porting notes: [PH-PORT.md](PH-PORT.md).

</details>

## Credits and licence

Recompiler and runtime from [FFCCEOT-Recomp](https://github.com/JonathanDC64/FFCCEOT-Recomp) by JonathanDC64; DS
hardware from [melonDS](https://github.com/melonDS-emu/melonDS); function boundaries from the
[zeldaret/ph](https://github.com/zeldaret/ph) decompilation. Switch port in this fork (2026). Details in
[NOTICE.md](NOTICE.md).

GPL-3.0 ([LICENSE](LICENSE)). *The Legend of Zelda: Phantom Hourglass* is © Nintendo; this is an unofficial fan
project, not affiliated with or endorsed by Nintendo.
