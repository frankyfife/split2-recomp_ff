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
