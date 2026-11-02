# PC work plan (Nov 2026)

What was prepared without the game (a cloud session has no disc, so
nothing here has been lifted or run against TimeSplitters 2), and what is
left to do on the Windows PC, in order. Branch `claude/lucid-edison-1jul46`
in this repository and in `frankyfife/xboxrecomp_ff` (the toolkit).

## 0. Get the branch

```bash
git fetch origin claude/lucid-edison-1jul46
git checkout claude/lucid-edison-1jul46
git submodule sync
git submodule update --init
```

`.gitmodules` points at `frankyfife/xboxrecomp_ff` on this branch, because
the toolkit commits below exist only there (on top of DirectXbox-Recomp
`main` 45c3b57). Upstreaming them is a pull request to
phobos665/DirectXbox-Recomp; after it lands, point `.gitmodules` back.

## 1. Build and check nothing regressed

```bash
py -3 scripts/build.py --relift
```

`--relift` because `recomp_manual.c` changed. Then the scripted run from
CLAUDE.md (muted, fresh saves). Expected: as before. With `mouse_look` off
nothing new runs; with it on, `[TS2-AIM] native mouse aim: this release's
view angles are not known yet, so the mouse turns the right stick` and the
mouse behaves exactly as before.

Toolkit test: `ctest -R input_mouse` in a toolkit build (it was run here on
Linux with the pad layer stubbed).

## 2. Find the view angles (PAL first)

`docs/mouse-aim.md`, "Finding the view angles". In short:

```bash
RECOMP_TS2_AIM_PROBE=1 build/Release/split2_recomp.exe 2> probe.log
```

play into Siberia with the pad, turn and look, rest between, a minute; take
the `[TS2-PROBE] try:` line, run with it and `RECOMP_MOUSE_LOOK=1`, check:

- [ ] Turning with the mouse holds (does not snap back next frame).
- [ ] Pitch stops at the game's limits; no flip past straight up/down.
- [ ] The crosshair stays centred while turning (cross_x/cross_y found;
      second probe with the aim button held for the aim mode's offset).
- [ ] Shots land on the crosshair (hip fire and with a scope).
- [ ] A scope zooms and the mouse slows with it.
- [ ] The same addresses after a restart and in a second level; else find
      the player pointer (`base=`), see the doc.
- [ ] Pause menu: does the view jump on resume? If so find `active`.
- [ ] Split screen: only player 1 aims with the mouse.

Then write the values into `config/pal/overrides.inc` (`k_ts2_aim`) with
how they were found, and repeat for USA (`config/us/overrides.inc`).

Scripted probe, 1 Nov 2026 (PAL, the Siberia path from CLAUDE.md, then
`rstick_<dir>:3000` every 11 s from 95 s, `RECOMP_TS2_AIM_PROBE_AFTER=1200`
to skip the opening cutscene): yaw went 16 M -> 12 -> 6 -> 0 candidates,
pitch never narrowed, and the only survivors were "sway x" values on the
stack (0x00F7F6C8.., 0x00F7FA6C..), which are temporaries. Without
`_AFTER` (USA) every yaw went at once: the cutscene's camera moves alone.
Next: `RECOMP_TS2_AIM_PROBE=int` (the angles may be integers), and a probe
played by hand, which the rules were written for.

Later the same evening (PAL), all without a result:

- The int probe, a short probe played by hand and a probe with 15 s holds
  and 25 s rests: no yaw survives a held turn plus what follows it.
  `RECOMP_TS2_AIM_PROBE_TRACE=1` prints each stick state change: the
  states are right (left held 356 frames, up 881; the full scan runs at
  about 24 fps, not 5). One real bug was fixed on the way: while up/down
  was held the rule cleared the up/down lean instead of the yaw ("other").
- Frame dumps during a scripted turn: the right stick does turn the view;
  up looks down (inverted, which the sign rule allows).
- RAM dumps (`RECOMP_RAM_DUMP`, 16 of them 1.5 s apart around separated
  2 s turns, the swaps matched to `RECOMP_INPUT_LOG`'s lines): no float
  is still at rest, moves one way on left and the other on right and is
  still through up/down; nothing in RAM equals an angle taken from the
  3x4 matrix at 0x1EA778 (copy at 0x47B5D8), which turned out to be a
  per-object scratch matrix, not the view.
- The game's own pad table is at 0x2CDB48 (per pad; the right stick's
  bytes at +0x34 and +0x3C, 0..255, rest 128), written by sub_000DA9E0.
  Its readers reach it through a pointer, so a grep finds only the writer.

Found the next morning (2 Nov), by reading the code: the angles are at
+0x148/+0x14C of the player object in degrees, the object in the 0x80000000
window, which every earlier dump had left out (`RECOMP_RAM_DUMP` writes the
low view unless `RECOMP_RAM_DUMP_BASE=0x80000000`), and so had the probe,
which scans 0x10000 up to the end of RAM in that same low view. PAL's `k_ts2_aim` has
them now, checked in Siberia with `RECOMP_TS2_AIM_FAKE`; how, and the rule
that keeps the mouse off the title screen, are in `docs/mouse-aim.md`.
Still to check by hand: the list at the top of this section.

So far the angles had looked like no plain float or int that changes every
frame; they were, in memory the dumps did not cover. Next is to read the code: from
sub_000DA9E0's pad table to the function that turns the player (a
`RECOMP_WATCH_WRITE` on a candidate once the player object is known), or
the view matrix the renderer is given each frame (c64's source) back to
the angles it is built from.

Turn speed: `TS2_AIM_RAD_PER_COUNT_80` in `src/recomp_manual.c` is a guess
(about 18 cm a turn at 800 dpi, sensitivity 80); tune it by playing.

## 3. The Qt launcher on Windows

```bash
cmake -S launcher -B build/launcher -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
cmake --build build/launcher --config Release
```

or `-DSPLIT2_QT_LAUNCHER=ON` on the game's own configure, which puts
`split2_launcher.exe` beside `split2_recomp.exe`. Then `windeployqt
--release --no-translations split2_launcher.exe` for the Qt DLLs. Check:

- [ ] Settings written on Play/Save reach the game (the `[CONFIG]` line
      names the file); keys the launcher has no row for survive.
- [ ] The controls: key capture, mouse buttons, the PC layout, controller 1
      reading a pad and the keyboard at once.
- [ ] Renderer Vulkan (needs `third_party\dxc` at build time), mute,
      volume, mods (RECOMP_MODS) reach the game.
- [ ] In a release folder (release.json beside it): the builder runs from a
      disc image with progress, Cancel, a second Play.
- [ ] The look on Windows 11 (the "windows11" style) and on Windows 10.

Not done: the release workflow still ships the toolkit's launcher as
`split2.exe`; switching it to the Qt one means windeployqt in
`.github/workflows/release.yml` and the release recipe's `launcher` name.
Gamepad navigation in the launcher (Qt has no pad module in Qt 6) is not
there either: on a Steam Deck the toolkit's launcher is the better one.

## 4. Graphics glitches

Nothing specific is recorded in the docs. For each glitch:

1. Where (level, screen) and with which settings (resolution scale,
   widescreen, frame interpolation, renderer).
2. F11 at the moment it shows: a BMP and a replayable capture beside the
   executable. Capture with frame interpolation off.
3. `xboxrecomp/build/.../d3d8_replay <capture>` draws it with no game; the
   two backends can be compared on the same frame.
4. Toolkit bug (wrong state, format, blend) or TS2-specific (a 2D site
   placed wrongly in 16:9: `RECOMP_TS2_UI_SITES=1`, `docs/enhancements.md`,
   "Checking a screen's widescreen layout").

Known candidates to look at first:

- Resolution scale above 1: render-target textures scaled with it are the
  riskiest item (`xboxrecomp/docs/technical/widescreen-and-resolution.md`):
  glows, water, screen copies at the wrong size or offset.
- Frame interpolation: snow and particles step at 60 among 120; the first
  person weapon, muzzle flashes, bone matrices unchecked.
- USA widescreen: 22 of 93 placement sites never seen drawing, so some
  screens may still squeeze or stretch.

## 5. Missing options (what the launcher now has, and what is left)

Added to the launcher: TS2's menus 16:9/4:3 (`ts2_menus`), mouse aim mode,
mouse invert (`mouse_invert`, new in the toolkit), sound path
(`audio`), volume and mute, PAL 60 Hz (`pal60`), renderer, mods, extra
switches. The toolkit's launcher had no row for these and dropped them on
every Play.

Also new, in `ts2_camera_setup`: a field of view (`ts2_fov`, 70-150 % of
the game's; launcher row "Field of view"). It scales camera_setup's
fov_degrees argument the way widescreen scales the aspect, so projection
and culling agree. Not run yet:

- [ ] 120 %: wider in Siberia, nothing missing at the edges, HUD unchanged.
- [ ] A scope still zooms; cutscenes look acceptable (they are widened too).

Not yet: MSAA (the toolkit has none), cutscene skip, controller icons
(`docs/enhancements.md`).

## 6. The mouse in the menus (to do)

Asked for by the player: the front end and the pause menu driven with the
mouse, with a pointer drawn by the game rather than the Windows cursor.
`docs/enhancements.md` ("Native mouselook") puts clicking TS2's menus at
2-3 weeks, TS2's side.

- Pointer: one of the game's own textures, drawn on the 2D layer at the
  mouse's position. The crosshair is the first candidate (it is already
  loaded in play; for the front end find whether it is loaded there, else
  take a front-end texture: a highlight, an arrow, a cursor glyph). The
  texture dump (`RECOMP_HLE_D3D8_DUMP*`) names them.
- Placement: in widescreen the 2D layer is laid out per call site
  (`k_ts2_ui_places`), so the pointer goes through the same mapping as the
  menu items it points at.
- Hover and click: find where the front end keeps its menu items' rectangles
  and its selected index (a watchpoint on the index while moving with the
  D-pad), then set the index on hover and send A on a left click, B on a
  right click. Sliders and the name entry screen need their own handling.
- Where the mouse aims (in play, `ts2_aim_frame`) the pointer is hidden and
  the window keeps the pointer captured; in menus it is shown and released,
  which the toolkit's mouse look already does on its side.
- [ ] Front end: every screen reachable and usable with the mouse alone.
- [ ] Pause menu in a level; resuming does not turn the view.
- [ ] 4:3 and 16:9, resolution scale 1 and above: the pointer's tip is where
      the click lands.
