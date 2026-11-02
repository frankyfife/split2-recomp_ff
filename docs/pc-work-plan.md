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

Found by the player (2 Nov 2026):

- **Glass flickers blue with frame interpolation on.** The frosted windows
  of Siberia's first huts (and other surfaces with an overlay layer) are
  darker in every in-between frame than in every real one: the small hut
  window outside the tunnel measures (105, 115, 116) in each real frame and
  (88, 100, 101) in each in-between one (`RECOMP_FRAME_INTERP=2`,
  `RECOMP_INTERP_DUMP` beside `RECOMP_HLE_D3D8_DUMP_EVERY=1`, both from swap
  5300 of the Siberia script plus `80000:lstick_up:1800`). At 120 Hz the two
  alternate, which reads as blue flicker. An F11 capture and its replay are
  grey, i.e. right. A capture of that very frame (`RECOMP_D3D8_CAPTURE`,
  `_SWAP=5300`) replayed draw by draw (`d3d8_replay --draws n`) gives the
  window (88, 100, 101) after draw 358 (its dark glass, indexed) and
  (105, 115, 116) only after draw 388: the frost quad, a DrawPrimitiveUP of
  two triangles in world space with a 64x64 texture, alpha-blended, depth
  test LESSEQUAL and no depth write. So in the in-between frame draw 388
  has no effect. Its vertices are kept (`host_DrawPrimitiveUP`,
  `retain_call` copies them); not yet known whether it is not redrawn,
  drawn with another texture, or fails the depth test there.
  `RECOMP_INTERP_BLEND=1` draws every in-between frame with frame N's own
  matrices, to tell the replay from the blending (a static scene is needed
  for a whole-image compare; Siberia's falling snow gets in the way).
  The screen copy is not it (it reads the in-between image,
  `d3d8_screencopy.c`). Meanwhile the player's settings have
  `frame_interp = 1`.
- **A periodic hitch, flicker and blue glass after F11** were the toolkit's
  F11 turning on the 24 interval dumps (fixed, `shadow_dump_frame`); a
  single F11 can still flash the glass once (the back buffer's read-back).

For each glitch:

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

## 7. A free camera (to do)

Asked for by the player, on the usual chord: L3 and R3 held together (on
the keyboard Ctrl and the right button, both of which they are bound to)
detach the camera from the player and fly it, the left stick or WASD
moving it and the mouse or right stick turning it; the same chord brings it
back. The game is paused meanwhile, or not (a setting), for screenshots.

- The game has a fly camera of its own: `FUN_00033730` (PAL) reads all four
  stick axes and keeps its angles in globals (0x353AF8, 0x353AFC, wrapped
  by the double at 0x21C050). Find what turns it on (a debug flag) before
  writing one.
- Otherwise: hold the player still (`ts2_aim_in_control`'s +0x998 is
  "something holds the player") and replace the view the renderer is given
  (camera_setup's caller, `FUN_00034550`) with one of our own.
- [ ] The chord does not also crouch and aim (L3 is crouch, R3 aim).
- [ ] Leaving puts the view back exactly; nothing in the level moved.

## 8. Changing weapons faster (to do)

The D-pad (the wheel, on the PC layout) steps through the weapons, and the
change between two is slow: the player waits for the old one to be put away
and the new one drawn (+0xC8 is the weapon in hand, +0xCC the one wanted,
on the character at [[player+0x1BC]+0x124]). Find the timer that runs the
change and shorten it, or let a quick run of wheel notches skip the weapons
in between, as PC shooters do.

## 9. Rate of fire per weapon (to do)

Asked for by the player: each weapon's rate of fire settable, on a tab of
its own in the launcher ("Waffen").

- The weapon table (PAL 0x2C3890, USA 0x2C3470, rows 0x240 apart, the scope
  flag 0x20 already read by `ts2_aim_mode_auto`) is the first place to look:
  find the field the firing code waits on between two shots (a watchpoint
  on the character's refire timer while holding fire, then which row field
  it is loaded from), and its unit (ticks of 1/60).
- Write the rows at level start from the settings (`ts2_rof_<weapon> = %`,
  100 = the game's), not the XBE: the table is data, so nothing is relifted
  and every release only needs its table address.
- Launcher: a "Waffen" tab listing the weapons by their in-game names (the
  table or the text banks give them), a percentage each, and a reset.
- [ ] A changed rate shows in Siberia (the tracker, the sniper rifle);
      multiplayer and the AI's weapons see the same table, say so on the tab.

## 10. An icon for the launcher (to do)

The launcher and the game have Windows' default icon. Needs a `.ico` (16 to
256 px) and a resource file for both executables in `CMakeLists.txt`, and
`setWindowIcon` for the Qt window. It must be our own drawing, not TS2's
artwork or logo, which are not ours to ship.

## 11. The blur filter (done, Nov 2026)

Seen by the player: the game's blur ("Unschärfe") was drawn at a low
resolution, so at a resolution scale above 1 it smeared the sharp picture.

What it is, from the player's F11 capture in Siberia (`d3d8_replay
--list-draws`): after the world and before the HUD, TS2 copies the whole
back buffer into a 640x480 texture (CopyRects, no render target), draws it
back as a full-screen quad shifted by one pixel at about a fifth opacity,
copies again and draws again: three passes (draws 250-252 there). The
toolkit made that copy at the title's 640x480 by taking one scene pixel in
25 at scale 5, unfiltered, so a coarse, shimmering image lay over the sharp
one. Both fixes are in the toolkit (`widescreen-and-resolution.md`, R3):

- The copy is held at the scene's size; the shift stays one console pixel,
  so the softening is the console's, now sharp underneath.
- `screen_filters = 0` (launcher: "Unschärfe-Filter des Spiels", Graphics
  tab) leaves the passes out.

Checked: the capture replayed at scale 5 (cliff detail sharp with it off,
soft but no longer blocky with it on), and a live run to Siberia at scale 3
with frame interpolation (no faults, 120 frames shown). The handheld's map
(a rectangle copy) is a different path and unchanged.

## 12. Dialogue behind the pictures: PAL's 50 ticks a second (done, Nov 2026)

Reported by the player: in cutscenes the dialogue came one to two seconds
after the pictures. A cutscene's sound is one DirectSound stream
(`music/cs_*_full*.wav`, music, effects and voices mixed), and the stream
was not late: the pictures were fast. Measured on the intro: its soundtrack
is 143.6 s, its pictures ended after about 120 s, the ratio of 60 to 50.

The PAL release turns seconds into ticks with 50 and 1/50 where the USA
release has 60 and 1/60 (its timer, `FUN_001AE5D0`, multiplies seconds by
50.0; the USA one, `FUN_001AE190`, by 60.0), and its clock counts vertical
blanks, which the host gives at 60 Hz. Listing every use of the four
constants in both releases (Ghidra, `ConstUses.java`) and pairing them gave
330 sites of 1/50 and 35 of 50.0 to change, and one immediate; each now
reads the PAL release's own 1/60 or 60.0 (`tools/ts2_tick_rate.py`, in
`config/pal/xbe_patches.json`). After it the intro's pictures ran about
140 s, the rest being the music's tail after the last shot.

- [ ] By hand: dialogue on the lips in the intro and Siberia's cutscenes.
- [ ] Anything that now feels slow or fast in play (a timer, a door, an
      animation): integer tick counts were not looked for, and 22 uses
      were left alone as undecided (the tool's docstring lists them).

## 13. Sun glare and falling snow, switchable (done, Nov 2026)

Reported by the player: Siberia's snow softens the picture further. Its
outdoor frames carry three effects over the scene, each found by its call
site (`RECOMP_TS2_UI_SITES=1`, a capture, `--skip-draw`): the sun's glare
(four large additive sprites), its flare and a faint whole-screen tint
(`FUN_001B0570`, `FUN_001B4B50`), and the falling snow (`FUN_001B1960`, a
few hundred particles around the camera). The launcher's Graphics tab has
"Sun glare" and "Falling snow and rain" (`ts2_sun_glare`, `ts2_weather`,
both on by default); off, those sites get the toolkit's new HIDE placement
and their draws are left out. A few large star-shaped flakes come from the
general particle system (`FUN_001BFFCA`, smoke and sparks too) and stay.
USA's sites are PAL's found by their bytes, not run.

## 14. 5.1 sound (done, Nov 2026)

Asked by the player: is there 5.1? TS2 mixes its music, voices and menu
sounds to the front pair (mix bins 0 and 1) and positions world sounds as
DirectSound 3D voices (`SetPosition`, min/max distance, a rolloff curve),
which the XDK spreads over the four crosstalk bins: front and rear when the
console is in surround mode (Dolby Digital), front only otherwise. The
toolkit always took the second. Now (`hle_dsound_3d.c`) a device with four
or more channels is a console in surround mode: a 3D voice is shared
between front and rear by where it is relative to the listener's front,
constant power as the ears share left and right, fading to both inside the
source's minimum distance (the player's own gunshot sits 0.1 m behind the
camera). `surround` (`RECOMP_SURROUND`, launcher "Surround sound") forces
it either way. Checked in Siberia's trace: sources to the right and behind
on BR, ahead and left on L. Centre and LFE stay unused, as the game sends
nothing there.

- [ ] By hand on the 5.1 receiver: a guard behind you is heard behind you.
