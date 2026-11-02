# Aiming with the mouse

How TimeSplitters 2 aims on the Xbox, why the mouse should not copy it, and
how the view angles are found for each release. The code is the "aiming
with the mouse" section of `src/recomp_manual.c`; each release's table is
`k_ts2_aim` at the end of `config/<region>/overrides.inc`.

## What the console does

The right stick drives two motions laid over each other:

- **Without the aim button** the stick turns the camera, and the crosshair
  and the gun lag a little behind the turn: hip fire.
- **With the aim button held** the camera stops turning and the stick moves
  the crosshair across the screen instead. It is slow and imprecise,
  because a thumb is. With a scoped weapon the same button zooms.

Both exist because a stick says *how fast*, not *where*.

## What the PC does

A mouse says *where*. So the mouse does not go through the stick at all:

- Its counts are added straight to player 1's view angles, once a frame
  (`ts2_aim_frame`, after the game's present). The toolkit's
  `recomp_mouse_take` hands them over and, while TS2 keeps taking them,
  keeps them off controller 1's right stick, so the camera never turns
  twice.
- The game therefore sees a centred right stick: the crosshair has nothing
  to lag behind and stays in the middle, and a shot goes where the view
  points. Where the crosshair and sway offsets are known (`cross_x`,
  `cross_y`) they are also held at 0.
- The aim button is only needed where the game needs it: a scope's zoom.
  Zoomed in, the mouse still turns the view directly, slower by the ratio
  of the fields of view (taken from `camera_setup`'s fourth argument), so
  the same hand movement covers the same part of the picture.
- In the front end, on loading screens, while the addresses are unknown, and
  with `ts2_mouse_aim = stick`, TS2 does not take the mouse and the
  toolkit's mouse look on the stick has it, as before.

Needs `mouse_look = 1` (the launcher's "Aim with the mouse"), which is what
makes the window take the pointer.

Default speed: at mouse sensitivity 80, about 5,700 counts turn the view
once round (18 cm on an 800 dpi mouse). `mouse_sensitivity` scales it
linearly; `mouse_invert` swaps up and down.

## Finding the view angles (once per release)

**PAL: found (Nov 2026), USA: not yet.** The angles are floats in degrees
in the player's object, which the players' table at 0x497268 (PAL) points
at: +0x148 yaw in [0, 360), +0x14C pitch in -40..40 (the limit is the
global at 0x497344). The probe below never isolated them; reading the code
did, and is the faster way for the next release:

1. In Ghidra (the `ghidra-xbe` loader), list the stores to `[reg+0x148]`
   and `[reg+0x14C]`; the look code is PAL `FUN_00148EB0`, which adds the
   right stick to both, scaled by the frame time (0x348CEC), and turns
   only while bit 3 of +0xC is set, +0x998 is 0 and the movement mode
   +0x158 is a free one. `ts2_aim_in_control` makes the same tests, so the
   mouse leaves the title screen's and the menus' levels alone.
2. `RECOMP_RAM_DUMP` with `RECOMP_RAM_DUMP_BASE=0x80000000` (the players
   live in the contiguous window) around scripted turns shows the fields
   move.
3. `RECOMP_TS2_AIM_FAKE=<dx>,<dy>` adds that many mouse counts every frame
   in a scripted run (no pointer in a background window) and logs the
   first writes; `20,0` must turn right, `0,-20` look up to +40.

For the USA release the same object layout should hold; only the table's
address moves (find where `FUN_00148EB0`'s counterpart reads the current
player, `*0x497340` on PAL, and the table that fills it).

The probe, for the record (it found nothing in TS2; the reasons are in
`docs/pc-work-plan.md`), finds them while you play; nothing has to be
rebuilt to try what it finds.

1. Mouse look **off** (the probe reads the stick the game was given; the
   mouse would not show). A pad, keys bound to the right stick, or an input
   script all work.
2. Run with the probe and play into a level (Siberia is fine):

   ```bash
   RECOMP_TS2_AIM_PROBE=1 ./split2_recomp.exe 2> probe.log
   ```

   Turn left, rest a second or two, turn right, rest, look up, rest, look
   down, rest, for about a minute. Rest really means let go of the stick:
   for half a second after it is released nothing that moves counts.
   The game runs slowly while the probe reads all of RAM each frame; it
   speeds up once fewer than 4,096 candidates are left.
3. Every five seconds `[TS2-PROBE]` lines say what is left; once the field
   is small they list the candidates with their range and their change a
   frame at full stick, and end with a ready line:

   ```text
   [TS2-PROBE] try:  RECOMP_TS2_AIM=yaw=0x...,pitch=0x...,yaw_sign=1,pitch_sign=1,units=1,pitch_min=...,pitch_max=...,wrap=6.283185,cross_x=0x...
   ```

4. Try it with the mouse:

   ```bash
   RECOMP_MOUSE_LOOK=1 RECOMP_TS2_AIM="yaw=0x...,pitch=0x...,..." ./split2_recomp.exe
   ```

   The start-up line `[TS2-AIM] native mouse aim: ...` says what was taken.

What to expect and what to check:

- **More than one yaw.** The game may keep the player's angle and a copy
  for the camera (one in radians, one in degrees, `units=57.29578`). Write
  to the one the game *reads* to turn: if turning with the mouse snaps
  back the next frame, it was a copy; take the next candidate.
- **The addresses move.** A player object on the heap can sit elsewhere in
  the next level or after a restart. If a second run (or another level)
  gives other addresses, find the pointer: `RECOMP_FIND_VALUE=<the player
  object's address>` lists who holds it; give that as `base=` and the
  angles as offsets (`yaw=0x<offset>`). `RECOMP_WATCH_WRITE=<yaw address>`
  names the function that turns the player, for the record.
- **Pitch limits.** `pitch_min`/`pitch_max` are what was seen: look fully
  up and down during the probe so they are the game's own limits.
- **The crosshair.** "sway x/y" are values that lean off a resting value
  while the stick is held and come back when it is let go: the hip-fire
  lag. Run the probe a second time **holding the aim button**: the aim
  mode's crosshair moves like a "turn" then, which gives its offset.
  Both go into `cross_x`/`cross_y` (only one pair is held today).
- **Pause and cutscenes.** Mouse motion in the pause menu would turn the
  view behind it. The game's own answer is its frame-time scale (PAL
  0x348CEC, 1.0 in play, 0 while paused), which the look code multiplies
  the stick by: give it as `frame_time=` and the mouse stops there too. It
  was found by RAM dumps before and during the pause menu. `active=` (a
  32-bit value equal to `active_is`, read from the player's object when
  `base` is set) is for any other such test.
- **Integers.** If nothing floats, `RECOMP_TS2_AIM_PROBE=int` tries 32-bit
  integers (the native path writes floats, so that would need code).

Once the values hold in two levels and after a restart, write them into
that release's `k_ts2_aim` (with how they were found, as beside every
override) and `ts2_mouse_aim = native` needs no environment variable.

## Not done yet

- Split screen: player 1 only, by design (one mouse).
- Turrets and vehicles: movement modes 7 and 8 turn something else within
  limits of their own (`FUN_00148EB0`), so `ts2_aim_in_control` leaves
  them to the stick: the mouse turns the right stick there, as before.
  Not tried in a level that has one.
- The aim mode (bit 1 of +0xC, set by `FUN_00140F40`) keeps the look flag,
  so the mouse aims with the aim button and through a scope; the stick,
  centred, leaves the crosshair in the middle. Checked by reading, not yet
  by hand.
- Forcing the game's own auto-aim and auto-lookahead off for the mouse: the
  player can switch them off in the game's control options meanwhile.
