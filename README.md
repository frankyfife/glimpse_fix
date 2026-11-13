# glimpse_fix — Split/Second PC fix

A drop-in `dinput8.dll` for the **Steam** PC version of **Split/Second** (Disney / Black Rock, 2010) that
adds what the port never had:

* **Proper controller support** for every XInput pad (Xbox 360/One/Series, 8BitDo, Steam Input,
  DS4Windows, …): full menu navigation, the console button layout, **Xbox (or PlayStation)
  button prompts**, independent analog triggers and rumble
* **60 fps** with the simulation stepped exactly as before, so physics, Havok and AI behave like
  at 30 fps
* **No more blurry, grey or missing world textures and props**: a larger streaming pool (needs the
  large address aware flag, a tool for that is included)
* **No pop-in of buildings beside the track**, longer LOD, streaming, particle and powerplay
  distances
* Powerplay icons at console size on high resolutions, motion blur matched to 60 fps, optional
  vignette and screen gradient, anisotropic filtering, internal render resolution independent of
  the quality option
* **Camera:** field of view, the correct view on ultrawide screens, zoom keys, a **photo mode**
  with a free camera and a key to switch the HUD off
* **Extras nobody else has** (off by default) — see below
* **A settings program** (`glimpse_config.exe`) instead of editing the INI by hand

No game files are modified at runtime, and the game's code is not changed either: everything works
through the game's own data, after checking the expected bytes. The game's copy protection
(SecuROM) stays fully in place, see
[Copy protection](#copy-protection). You need your own copy of the game.

## Extras: Boost, Power Play on demand, everything unlocked

Split/Second never had a boost. Now it does — and it feels like it belongs there.

| | What it does | INI |
|---|---|---|
| **Boost** | Hold **X** (keyboard: **left Shift**) and your car surges forward along its heading, up to 324 km/h. The motion blur swells with it (3× by default) and eases off when you let go, so speed *feels* like speed. Works only while driving forward, never in the pause menu. | `Boost=1` |
| **AI boost** *(experimental)* | The AI cars boost too, in bursts while they are behind you or not far ahead, so the field stays tight. Three levels from *easy* to *hard*, on the straights only or everywhere, and a **chaos mode** (`AIBoostChaos=1`) where every AI car boosts all the time, always has a full power play bar and drives at its highest skill. Turn it off with one line if it misbehaves. | `AIBoost=1` |
| **Full Power Play bar** | Press **LS + RS** (both sticks; keyboard: **left Ctrl**) and the power play bar is full: drop a building on the leader whenever you like. | `PowerBarFill=1` |
| **Everything unlocked** | All tracks, events, episodes, game modes and cars selectable from the start, using the game's own switches. Your profile is not changed; set it back to 0 and everything is as you earned it. | `UnlockAll=1` |
| **Bonus content** | The extras the PC version hid behind cheat codes: the **Expressway** track, the Cobretti SlipStream, the retailer branded cars and the Euro livery pack. | `BonusContent=1` |
| **Achievements and decals** | Unlocks the achievements and with them the car decals: all of them, or only the ones nobody can earn any more (the six from ranked online races and the Pure decal). Done by the game's own unlock in the next single player race and saved with the profile, which is backed up to `glimpse_backup` first. | `UnlockDecals=1` / `2` |
| **Skip the intro** | Skip the publisher logo, or the logo and the intro, at every start. The movie files stay untouched. | `SkipIntro=1` / `2` |

All of them can be switched in `glimpse_config.exe` (*Gameplay*), the AI boost under *AI*, the photo
mode under *Camera*. Both keys can be changed (`BoostKey`, `PowerBarFillKey`); the defaults are bound
in none of the game's own keyboard layouts. Tune the boost with
`BoostAcceleration` (m/s², default 15 ≈ 1.5 g), `BoostMaxSpeed` (m/s, default 90) and
`BoostMotionBlur` (1–5), the AI boost with `AIBoostLevel`, `AIBoostUnlimited`, `AIBoostStraightOnly`
or simply `AIBoostChaos`. Like everything else here, the extras only change game data, never game
code, so they are safe with the copy protection.

> **Compatibility: Steam version only.** Developed and tested exclusively with the Steam release
> (`SplitSecond.exe` 1.0.0.1, app 297860) on Windows 11. The retail DVD version
> (Split/Second: Velocity), other regions and any other release have **not** been tested. All
> addresses come from the Steam executable; on a different exe the byte checks fail and the
> game-specific patches (60 fps, streaming pool, prompt icons, trigger preset) switch themselves
> off, and whether the rest works there is unknown. Packs that replace the exe are not supported,
> see [The Xbox content modpack](#the-xbox-content-modpack-xenondll).

## Install

1. Copy `dinput8.dll`, `SplitSecondPad.ini` and `glimpse_config.exe` into the game folder
   (`…\steamapps\common\SplitSecond`).
2. Drag `SplitSecond.exe` onto `setlaa.exe` (or run `setlaa.exe <path to SplitSecond.exe>`).
   It sets the large address aware flag and keeps the original as `SplitSecond.exe.orig`.
   Steam's "Verify integrity of game files" restores the original exe; just run it again.

3. Optional: run `glimpse_config.exe` to change the settings (see below), or edit `SplitSecondPad.ini`
   by hand.

Uninstall: delete `dinput8.dll`, `SplitSecondPad.ini` and `glimpse_config.exe`, rename
`SplitSecond.exe.orig` back.

## What was wrong, and what the fix does

### Controllers

The port reads joysticks only through DirectInput and picks a binding preset by USB VID/PID.
A complete layout (menus, driving, camera) exists only for the wired Xbox 360 pad
(`045E:028E` / `045E:02A1`). Every other controller — Xbox One/Series included — gets the
generic preset, which is **empty**: no bindings at all, so menus can't be used. On top of that
DirectInput puts LT and RT on one shared Z axis, so throttle and brake cancel out, and the
prompt bar is hard-wired to keyboard keys although its Flash movie still contains the Xbox and
PS3 icons.

* Every XInput slot is exposed to the game as a virtual "Controller (XBOX 360 For Windows)", so
  the original console layout applies to any XInput device. Plugging a pad in while the game
  runs works.
* LT and RT get their own axes; the 360 preset is adjusted (throttle RT, brake LT).
* The virtual pads come first in the device list, so the game's joystick index matches the
  XInput slot it rumbles.
* The prompt bar shows Ⓐ Ⓑ Ⓧ Ⓨ Start (or ○ □ △) while the pad is the device in use and
  switches back to key captions as soon as a key is pressed. The title screen reads
  "Press START" instead of "Press ENTER" (any language). The controls screen names pad inputs
  A/B/X/Y/LB/RB/LT/RT/LS…/RS… instead of "Button 1" or "Z rotation".

### 30 fps

The game simulates in fixed 1/60 s ticks but its main loop renders every 1/30 s and hands two
ticks to each frame; physics and race logic use `dt = ticks × 1/60`. The only per-frame code that
assumes a 1/30 s frame is the AI player update (once per car per frame; its steering controller
limits the steering change per call by 1/30 s). Speeding up just the main loop makes the AI react
twice as fast — the reason the old 60 fps patch made AI cars crash constantly.

The loop adds the measured frame time to an accumulator, takes two ticks per 1/30 s in it, sleeps
away the rest and clamps the ticks to a "max ticks" tweakable. All of that is plain data, read only
by the loop, so 60 fps work without changing its code:

* After every presented frame (Direct3D `Present`) the fix waits for the next 1/60 s on a high
  resolution timer, sets "max ticks" to the number of 1/60 s steps that have passed (1, more after
  a slow frame) and tops the accumulator up so the loop finds that many ticks and never sleeps.
* Each AI car is updated every other frame and drives on its last inputs in between, exactly as
  at 30 fps with two ticks per update (the AI player's vtable slot). Restarting a race clears each
  AI car's track section and the next update finds it again, so that update is never skipped:
  another AI method reads the section every frame.
* Above 60 fps would need render interpolation, since the ticks are fixed at 1/60 s.

### Blurry / missing textures and props

The sector streaming pool is set to 57 MB, a console-era budget. When it's full the streamer
evicts sector data: walls and signs drop to their smallest mip levels (colour smears, flat grey)
and small props such as bushes disappear. Without the large address aware flag the 32-bit
process has 2 GB of address space, and at 4K only about 500 MB of it is left, so the pool can
only grow with the flag set.

* With the flag, the pool is raised to 512 MB (configurable). Without it, the pool stays stock.
  Levels write their own value (57) into the pool tweakable before the pool is created; the
  tweakable's record is pointed at a variable of the fix, so the level's value lands there and the
  real one keeps the configured size.

### Pop-in

Every track sector carries a precomputed visibility list, and buildings or props that are not
on the current sector's list are skipped, whatever their distance. Crossing a sector border makes
large halls and terminals beside the track appear at once. The game still has the debug switches
that turn these lists off (`Sectors/Visibility/Disable Powerplay Visibility`,
`Disable Sector Visibility`, `Disable Visibility Map`); with `PopInFix=1` they are set, and the
streaming distance, LOD distances and the view frustum decide what is drawn.

`LodDistanceScale` multiplies the high/mid environment LOD distances, sector and entity streaming
distances, particle and debris distances and the per-level powerplay asset distances. The low LOD
hand-over to each level's backdrop models stays as the level sets it, since moving it makes both
draw at once.

### Other image fixes

* **Powerplay icons:** the port multiplies their scale by width / 1280 on top of the renderer's
  own width scaling (1.5x too big at 1080p, 3x at 4K); the loaded scales are divided back.
  The route change markers (red and blue arrows) get their size as width x 2.25/1280, a constant
  only that code reads; it is set to 2.25 / width.
* **Motion blur:** it blurs along one frame of camera motion, which at 60 fps is half as much;
  the amount is doubled. `MotionBlurStrength` scales it further. During the blur pass the textures
  are clamped to the edge (through the Direct3D device's sampler states), so samples that reach past
  the picture at speed repeat its edge.
* **Line at the bottom at Very High:** all full screen passes (tone mapping, motion blur, depth of
  field, grading ...) draw one quad template that the game moves by half a pixel plus a fixed 0.0005
  up and 0.00015 left in clip space. That is a fraction of a pixel up to 2160 rows, but Very High on
  a 4K screen renders at 7680x4320, where it adds up to more than a pixel: every pass moved the picture
  up a row and left its bottom row unwritten, so whatever landed there once stayed as a line. For
  render targets above 2160 rows (or 3840 columns) the template's position values are set so the fixed
  part stays what it is at native 4K; smaller targets are left as they are.
* **Vignette and gradient:** the race cameras darken the screen edges and lay a translucent blue
  band over the top of the screen in the grading pass. `Vignette=0` sets the vignette's U/V scale
  to 0 for that pass, `Gradient=0` the gradient's alpha; the grading itself stays.
* **Bloom and flares:** bloom, the sun/light and screen space lens flares and the particle flares
  can each be switched off (`Bloom`, `LensFlare`, `ParticleFlare`), and the bloom look and exposure
  tuned live (`BloomStrength`, `BloomTone`, `BloomCurve`, `Exposure`). Bloom is cleared from the
  tone mapping pass's shader choice, the particle flare pass from its own state flag (both through
  their vtable slots), the lens flares through the game's two enable tweakables.
* **Anisotropic filtering:** the game filters all textures linearly; mipmapped textures that are
  not render targets get anisotropic filtering.
* **Graphics quality:** "Very High" is exactly "High" plus 2x supersampling: of all places in the
  game that read the quality level, only the supersampling ones tell Very High from High. To render
  at screen resolution, pick "High" in the game; nothing else changes.

### Camera

* **Field of view:** `FOV` scales the race cameras' (chase and bumper) field of view; the game's
  widening with speed stays on top of it. The game's near plane is 1 m; with a much wider view the
  bottom of the picture looks down at the road right below the camera and would see through it, so
  the near plane moves closer by the same ratio (the camera state's own near plane setting).
* **Ultrawide:** the game keeps the horizontal field of view, so screens wider than 16:9 lose
  picture at the top and bottom. `UltrawideFix` widens the view until the vertical one matches
  16:9 again (single screen only).
* **Zoom keys:** hold a key to zoom the race camera closer or wider, another one resets it.
* **Photo mode:** during a race, a key or the pad's Back button holds the world still (the game's
  own slow motion at 0.000001), hides the HUD and the on-screen messages (the power play icons in
  the world stay) and frees the camera: left
  stick or W A S D to move, right stick or I J K L to look, the triggers or Q E down and up, A or
  Shift faster, LB RB or the zoom keys to zoom, Y + LB RB or U O to tilt. The d-pad or the arrow
  keys set the look: up and down the focus distance (it switches the depth of field on at half
  strength when it is off), left and right the depth of field (the game's own depth of field pass,
  left end = off; not at the lowest graphics quality); with X or Ctrl held, up and down bloom (down
  to off) and left and right exposure; the left stick click or Backspace resets it. No car is put
  back on the track while it is on. Back, B, Esc or the key again returns to the race. The game's
  sound is paused meanwhile (FMOD's own category pause). Crashes take it over only in part, see
  Known limitations.
* **Camera shake:** a slider for the race cameras' shake on bumps and hits, and a separate switch for
  the shake from explosions and power plays, both against motion sickness.
* **HUD key:** switches the power bar and the info bar off and on during a race.

## Copy protection

The Steam executable is protected with SecuROM, which decrypts the game at runtime and runs parts
of it in its own virtual machine. **This project does not remove, bypass or weaken it**; it is
built to work alongside it.

Why this matters: SecuROM checks the integrity of the game code at fixed times after launch, each
time a different part of it (about 4½, 5½, 6½ and 17 minutes in). A changed instruction does not
fail at once. Once a check finds one, its check threads (every ~22 s) stop releasing their VM
contexts; once all ten are taken, everything that needs protected code waits forever. The music
stops and the game hangs, typically after the finish line. This is also the likely reason why
earlier 60 fps patches and mods got a reputation for "breaking" the game: they patch the main
loop, and that part is checked about 6½ minutes after launch.

How the fix avoids it: **no game code is changed at all.** Everything works through

* values in the game's data: tweakables and their records (frame pacing, streaming pool, LOD and
  streaming distances, debug, unlock and lens flare switches, the achievements' debug unlock, the
  slow motion and HUD render switches of the photo mode, the race cameras' FOV, near plane and smoothed rotation, the explosion shake switch, AI
  skill limits), loaded HUD
  parameters, the full screen passes' quad template positions, powerplay distance records, the main
  loop's accumulator, the power bar, the AI skill and the cars' chassis velocity,
* swapped vtable slots (motion blur, grading, bloom, depth of field and particle flare passes, AI
  player update, chase and bumper camera update, with the photo mode the other camera classes'
  update, the respot logic's update with the photo mode or `WreckTime`),
  import table entries (`XInputSetState`, Bink's `BinkOpen`/`BinkWait`/`BinkDoFrame`, FMOD's
  `Event::start`), read-only
  strings and constants (prompt bar icon paths, route marker size),
* the Direct3D device's vtable, and calls into the game's own functions (dictionary lookup,
  Scaleform `Invoke`/`SetVariable` for the prompt bar, `SetVisible` for the photo mode's overlays,
  the depth of field blur setup) from the input poll and the hooked passes.

The prompt bar glyphs, the title screen text, the icon size, the 60 fps and the streaming pool were
first done with code patches and each set off exactly this hang, so they were rebuilt this way.
An internal resolution option (`RenderScale`) needed code patches too and set off the same hang,
so it was removed in 1.7.0.

* `[Debug] PatchMask` (bits as in `src/common.h`) switches individual patches off, to find out
  which one causes a side effect. With `Log=1` the log also shows the number of SecuROM VM
  contexts in use whenever it changes; a check that found something shows up as two more
  contexts (`8B03 8AF3`) every ~22 s.

## The Xbox content modpack (Xenon.dll)

There is a pack that brings the Xbox 360 DLC to the PC (the "elysium" package with `Xenon.dll`).
glimpse_fix does not support it and will not build on it. What the pack is:

* **The free PC demo instead of the game.** The pack overwrites `SplitSecond.exe` with the
  executable of the official Split/Second PC demo (version 1.3.8.0, "Split Second DEMO
  executable"). The demo exe is patched on disk to load `Xenon.dll` at startup.
* **Xenon.dll turns the demo into the full game** by patching it in memory, about 30 places,
  roughly half of them code (jumps, immediates, push operands), some in code that SecuROM only
  decrypts at runtime:
  * demo names and demo parameter files are replaced by those of the full game, a demo setting
    is switched off, the demo's end-of-race state is skipped and its graphics quality limit lifted,
  * the track tables are rebuilt with 17 instead of 15 entries (Quarry and Nem_Quarry added),
    together with the matching track count limits,
  * 14 in-game music tracks in shuffled order instead of the demo's 3,
  * the `PCcheat` switches are renamed so `SplitSecond.params` unlocks the retailer cars and the
    Euro livery pack,
  * the prompt bar uses the Xbox 360 or PC prompt movie from the pack.
* **Data:** the pack replaces `Resident.ark` and the level archives, and adds the paid Xbox 360
  DLC (the Quarry tracks, Deadline, Onslaught, Survival, the car and livery packs), which was
  never released for PC, without a license.

Why glimpse_fix stays away from it:

1. **It is not the Steam game.** glimpse_fix is written for the unmodified Steam exe and checks
   the expected bytes before every patch. On the demo exe those checks fail and most of the fix
   switches itself off (60 fps, streaming pool, pop-in fix, button prompts, icon size, ...).
2. **Copy protection.** glimpse_fix never changes game code and leaves SecuROM fully in place.
   The pack patches game code and runs the full game on the demo exe, so the full version's
   executable and its protection are not used at all.
3. **Content.** The pack redistributes Disney's DLC and game data without a license. That is
   tolerated, not allowed, and it has no place here.

What glimpse_fix does instead: everything the Steam version already ships but keeps locked can be
unlocked through the game's own switches, without code changes: all tracks, modes and cars
(`UnlockAll`), the PC cheat code extras including the Expressway track (`BonusContent`) and all
achievements and decals (`UnlockDecals`). Using the pack is your choice, but please reproduce
problems without it before reporting them.

## Settings

`glimpse_config.exe` edits `SplitSecondPad.ini` next to it (or one picked with *Open INI...*): one
tab per section (the AI boost on a page of its own), checkboxes, sliders and lists, a help line with the details of the option under the mouse (warnings in red), and
three presets (*Recommended*, *Original game*, *Chaos*). Options that depend on another one are
greyed out while it is off. Every change is saved at once; bloom and exposure apply while the game
runs, everything else at the next start. Only values are changed, so comments, order and keys the
program does not know stay as they are. It is a plain Win32 program without any runtime to install
and does not touch the game.

## SplitSecondPad.ini

The file is split into sections. INIs from older versions with everything under `[Game]` are still read.

**[Controller]**

| Key | Default | Meaning |
|---|---|---|
| `VirtualPads` | 4 | XInput slots exposed as pads (0–4). The game accepts at most 4 joysticks; lower this to make room for a wheel. At 0 the per-frame extras (photo mode, HUD and zoom keys, unlocks, decals, boost, LOD scaling, `PopInFix`, `LensFlare=0`) do not run, as they run with the pads' poll. |
| `HideXInputFromDirectInput` | 1 | Don't offer the real XInput devices through DirectInput as well. |
| `SeparateTriggers` | 1 | 0 = stock shared Z axis. |
| `StickDeadzone` | 0.15 | Radial stick deadzone (0–1). |
| `TriggerDeadzone` | 0.05 | Trigger deadzone (0–1). |
| `RumbleStrength` | 1.0 | 0 = off. |
| `ButtonGlyphs` | xbox | `xbox`, `playstation` or `keyboard` (never switch). |

**[Graphics]**

| Key | Default | Meaning |
|---|---|---|
| `FrameRate` | 60 | 30 = stock. |
| `Anisotropy` | 16 | Anisotropic filtering 2–16, 0 = stock (linear). |
| `MotionBlurStrength` | 1.0 | Motion blur multiplier on top of the 60 fps compensation. 0 = off. |
| `Vignette` | 0 | 1 = keep the cameras' darkened screen edges. |
| `Gradient` | 1 | 0 = no translucent blue band at the top of the screen. |
| `Bloom` | 1 | 0 = no bloom (glow around bright areas); the frame is still tone mapped. |
| `BloomStrength`, `BloomTone`, `BloomCurve`, `Exposure` | 1.0 | Multipliers on the cameras' bloom values (0–10): glow on bright areas, a softer second bright pass value, the add curve, the brightness of the whole picture. Read again every 2 s, so they can be tuned while the game runs. |
| `LensFlare` | 1 | 0 = no sun, light or screen space lens flares. |
| `ParticleFlare` | 1 | 0 = no flares from sparks, explosions and lights. |
| `FixPowerplayIconSize` | 1 | Powerplay icons and route change markers at console size at every resolution. 0 = stock. |

**[World]**

| Key | Default | Meaning |
|---|---|---|
| `StreamPoolMB` | 512 | Streaming pool in MB, 0 = stock (57). Only used when the exe has the LAA flag. |
| `LodDistanceScale` | 2.0 | Multiplier for LOD, streaming, particle, debris and powerplay distances (0.5–8). |
| `PopInFix` | 1 | Ignore the sectors' precomputed visibility lists. 0 = stock. |

**[Camera]**

| Key | Default | Meaning |
|---|---|---|
| `FOV` | 1.0 | Field of view of the race cameras as a factor of the game's own (0.3–2.5). |
| `UltrawideFix` | 1 | Screens wider than 16:9 keep the picture height of 16:9. 0 = stock. |
| `ZoomKeys` | 0 | 1 = zoom the race camera while `ZoomInKey` / `ZoomOutKey` is held, `ZoomResetKey` back to `FOV`. |
| `ZoomInKey`, `ZoomOutKey`, `ZoomResetKey` | 0x31, 0x32, 0x33 | Virtual key codes (1, 2, 3 on the top row). |
| `PhotoMode` | 0 | 1 = photo mode with `PhotoModeKey` or the pad's Back button during a race. |
| `PhotoModeKey` | 0x46 | Virtual key code (F). Keys the game uses itself (Esc, P, Enter, arrows) do not work, nor do the photo mode's own controls (W A S D, I J K L, Q E, U O, Shift, Ctrl, the zoom keys). |
| `CameraShake` | 1.0 | How much the race cameras (chase and bumper) shake on bumps and hits, against motion sickness: below 1.0 their rotation is smoothed (a low pass up to 0.12 s at 0.0), so quick shaking is filtered and turns follow a little later. |
| `ExplosionShakeOff` | 0 | 1 = no camera shake from explosions, collapsing buildings and power plays nearby (the game's own switch for it). |
| `HudKey` | 0 | Virtual key code that switches the HUD off and on during a race (0x48 = H). 0 = no key. |

**[Gameplay]**

| Key | Default | Meaning |
|---|---|---|
| `UnlockAll` | 0 | 1 = all tracks, events, episodes, game modes and vehicles selectable (the game's own switches; the profile is not changed). |
| `SkipIntro` | 0 | Boot movies at every start: 1 = skip the publisher logo, 2 = also the game intro. They end at once without decoding (the movie files stay untouched). 0 = stock. |
| `BonusContent` | 0 | 1 = the PC version's cheat code extras: Expressway track, Cobretti SlipStream, retailer branded cars, Euro livery pack. |
| `WreckTime` | 0 | **Highly experimental: can softlock the game, especially together with the photo mode.** Seconds from a wreck of your car to the respot (about 4 s in the game; 0 = as the game): the game's respot logic is left out until then, the crash camera keeps showing the wreck. The photo mode holds the respot of all cars while it is on. |
| `UnlockDecals` | 0 | Achievements and their decals: 1 = all, 2 = only the unobtainable ones (achievements 41–46 from ranked online races and the Pure decal; the "all achievements" decal follows once the rest is earned). Unlocked in the next single player race and saved with the profile, so they stay when set back to 0; the Pure decal shows while this is set. The `.sav` files are copied to `glimpse_backup` before the first unlock. |
| `Boost` | 0 | 1 = X held gives player 1 extra speed along the car's heading. |
| `BoostKey` | 0xA0 | Keyboard key that works like X for the boost, a virtual key code (0xA0 = left Shift). 0 = pad only. |
| `BoostAcceleration` | 15 | Boost in m/s² (1–100). |
| `BoostMaxSpeed` | 90 | No boost above this speed, m/s (10–200; 90 = 324 km/h). |
| `BoostMotionBlur` | 3.0 | Motion blur multiplier at full boost (1–5). |
| `PowerBarFill` | 0 | 1 = LS+RS (both sticks pressed) fills player 1's power play bar. |
| `PowerBarFillKey` | 0xA2 | Keyboard key that works like LS+RS (0xA2 = left Ctrl). 0 = pad only. |
| `AIBoost` | 0 | Experimental: 1 = AI cars boost in bursts while behind player 1 or not far ahead. |
| `AIBoostLevel` | 2 | 1 easy (0.8× your boost, 3 s tank, up to 30 m ahead), 2 normal (1×, 5 s, about once a second, 60 m), 3 hard (1.3×, 8 s, almost constantly, 150 m). |
| `AIBoostUnlimited` | 0 | 1 = the AI's boost tank never empties. |
| `AIBoostStraightOnly` | 1 | 1 = AI boosts only on straights at speed, 0 = anywhere while driving forward. |
| `AIBoostChaos` | 0 | 1 = chaos mode: level 3, endless tank, anywhere, also far ahead of you, the AI's power play bar is always full and the AI drives at its highest skill (no slowing down when ahead). Overrides the three keys above while set. |

**[Debug]**

| Key | Default | Meaning |
|---|---|---|
| `Log` | 0 | 1 = write `SplitSecondPad.log` (Direct3D device, FMOD events, music state, SecuROM VM contexts, every change of the profile `.sav` files). For bug reports it starts with the fix version, Windows version, CPU, RAM, graphics card with video memory and driver, the game executable (size, CRC32, build check) and every DLL in the game folder or loaded from outside the Windows folder, so other mods and different executables show at once. With `Log=1`, F9 in the game saves the next frame pass by pass to `glimpse_dump` (each render target as JPG plus its bottom rows as PNG, the passes and their inputs in the log), for image errors; the game stands still for a few seconds meanwhile. |
| `TestFeed`, `Profile` | 0 | Development only: pad state from shared memory (`tools/padfeed.py`), call counters. |
| `PatchMask` | 0xFFFFFFFF | Development: bit mask of the patches that may be applied (`PATCH_*` in `src/common.h`). |

## Known limitations

* With virtual pads active, the controls screen uses Xbox names only when no other DirectInput
  joystick (e.g. a wheel) is present, since those names are shared by all devices.
* 60 fps: races, AI and physics were checked; not every power play and cutscene was. Set
  `FrameRate=30` if something looks off.
* Boost, the power bar fill, the photo mode and the HUD key work for player 1 only; the photo mode
  only on a single screen.
* With `VirtualPads=0` the per-frame extras do not run (see `VirtualPads`).
* Photo mode on crashes: once your car is wrecked, the game's crash sequence decides the view.
  On a crash on the road (walls, other cars, power plays) the photo camera stays tied to the car
  and is not fully free. After leaving the track (falls) the game holds a waiting view that no
  camera drives, so the photo camera cannot take it over at all. Both would need changes to the
  game code, which this project does not make (see Copy protection). Enter the photo mode before
  the crash happens for a free camera.
* `UnlockDecals` works in single player races only (the game's own unlock runs there), and the
  profile is saved when the race ends: finish it or leave it through the pause menu. The Pure
  decal is reset by the game whenever it looks for Pure saves, so it is shown only while the
  option is set.
* `PopInFix=1` draws somewhat more per frame than the stock visibility lists allow.
* Audio: the game forces FMOD to DirectSound in 5.1. The LFE channel stays silent and the pause
  menu is completely silent; both are the game's own mix.

## Building

`build.bat` with the Visual Studio 2022 Build Tools (x86) builds `build\dinput8.dll`,
`build\setlaa.exe` and `build\glimpse_config.exe` (`config/glimpse_config.cpp`).

### Research tools

The scripts in `tools/` were used to reverse-engineer and test the fix. They expect a decrypted
memory dump of the running game (`tools/dump.py`) and a Ghidra project in `ghidra/`; neither is
part of this repository.

* `dump.py` — dump the decrypted `SplitSecond.exe` image from the running process (SecuROM/Steam
  DRM decrypt it at runtime).
* `dec.sh` + `ghidra_scripts/` — headless Ghidra: decompile functions or ranges, list xrefs.
* `dis.py`, `xref.py`, `strs.py`, `memgrep.py`, `mem.py`, `vmem.py` — quick disassembly, string,
  memory and address space tools; `objscan.py` finds live objects by vtable or pointers to an
  address, `rtti.py` names the class of a vtable.
* `padfeed.py` (with `TestFeed=1`) — scripted virtual pad; `test_race.sh` builds, installs and
  drives into a quick race; `ishot.sh` saves a frame straight from the Direct3D back buffer
  (works in exclusive fullscreen).
* `loopback/loopback.cpp` + `wavstat.py` — record the system mix (WASAPI loopback) and measure
  per-channel levels.

### Notes (Steam exe, image base 0x400000)

| Address | What |
|---|---|
| `0x00D646C8` | PC input manager singleton (vtable `0x00BA2904`) |
| `0x0040F0F0` / `0x0040EE10` | `GetActionValue` / `GetActionState(controller, action)` |
| `0x00410490`, `0x0040FFC0` | Joystick enumeration and its callback |
| `0x0040E850` | Adds a controller record; retries `Acquire` while it returns `DIERR_INPUTLOST` |
| `0x0040DB50`, `0x00CFABF8` | Preset choice by `guidProduct`; presets, 57 × 5 DWORDs each |
| `0x0040FC70` | Rumble: `XInputSetState(joystick index)` |
| `0x004DADE0` | `cLayer_PromptBar`: sets the PC key frames and captions |
| `0x00CFA5E0` / `0x00CFA6E0` | JOY_AXIS_* / JOY_BUTTON_* name tables |
| `0x00558880` | Main loop: measure frame time, accumulate ticks, `Update(ticks)`, `Render` |
| `0x00D747F4`, `0x00D747F0`, `0x00D199D8` | Main loop accumulator, its tick counter (`Sleep(5)` at 5), "max ticks" tweakable (10) |
| `0x00BA0C14` | 1/30, shared by the main loop, the AI steering controller `0x524AC0`, AI look-ahead grids and data conversions |
| `0x00BC0BC0` | `cAIPlayer` vtable slot of the AI player update `0x51A8B0`; the AI's skill (0–1) at +0x24 |
| `0x00BC0BF8` | `cAIPlayer` vtable slot 17 `0x51A550`, every frame: reads the track section `[[[ai+0x18]]+0x28]+0x54` (NULL after a restart until the next update) |
| `0x00D17044` … `0x00D1705C` | AI catch-up skill limits (initial/final min and max, 8 bytes apart) |
| `0x00D72534` | "SSAI/Debug Cheats/AI Full Power Bar" |
| `0x00D389E0`, `0x00D389E4` | Tweakable "Sectors/Streaming Memory Pool MB" (57), read on level load (`0x7C654F`); its record (vtable `0xBB2498`, +4 variable) |
| `0x00D66B28` | `cInGameResources`: +0xE4 `cGameLogic` (+0x18 player count, +0x1C players); player +0xE8 `cPowerBar` (+0x2C output, +8 power 0–100), +0xAC `cVehicleState` (+0x40 vehicles, +4 + i×4); vehicle +0x584 chassis `hkpRigidBody` (+0x100 forward, +0x110 position, +0x1A0 velocity) |
| `0x00D6FC54`, `0x00D6FBA0`, `0x00D7D978`, `0x00D6F8F0` | UI/All Gamemodes / Episodes / Events / Vehicles Unlocked |
| `0x00D6C49C` | 10 `PCcheat;…` switches, 8 bytes apart (Cobretti SlipStream, Expressway = Downtown B, branded cars, Euro livery) |
| `0x00D8B368`, `0x00D82788`, `0x00D9AFCC` | Disable Powerplay Visibility / Sector Visibility / Visibility Map |
| `0x00D8B384` | Powerplay draw distance records (name +0x10, distance +0x90) |
| `0x00D66B24` | HUD; parameters at +0x20, powerplay icon scales +0x214/+0x218 |
| `0x00BB8494` | Route change marker size factor 2.25/1280 (read only by `0x4C7910`) |
| `0x00BB42B0`, `0x00BB4048` | vtable slots of the motion blur pass `0x49B490` and the grading/vignette pass `0x49A7A0` |
| `0x00D67758` | Full screen quad helper `0x511BA0`: template at `[+4]` (4 vertices of 7 floats, position xyz and texture coordinates), offset by `0x510CA0` (±0.5 / size) plus 0.0005 up, 0.00015 left |
| `0x00BB3678`, `0x00BB39B8` | vtable slots of the bloom/tone mapping pass `0x492F50` (parameters: +0x38 bloom add, +0x3C/+0x40 bright pass, +0x58 add curve, +0x20 exposure) and the particle flare pass `0x496C60` |
| `0x00D10A3C`, `0x00D37D08` | "PostProcess/SSLensFlare/z; Enable", "LensFlare/Config/a; Enable" |
| `0x00B9B39C`, `0x00B9B3B0`, `0x00B9B380` | Import table: `BinkOpen`, `BinkWait`, `BinkDoFrame` (boot movies: `data/movies/bootup`) |
| `0x01E0266A` | SecuROM VM context table (10 entries, +0x24 = `0x66666666` while in use) |
| `0x00A15BF0` | Dictionary lookup, key → UTF-8 text |
| `0x00C5AE64`, `0x00C5B1F0` | vtable slots of the chase `0xA89240` and bumper `0xA8D9A0` camera update (camera state: +0x14 axes, FOV) |
| `0x00D71208`, `0x00D113B8` | "Physics/Change Phys dt/Slow motion activated", "Slow Motion cheat speed" |
| `0x00D1BC94`, `0x00D1BC9C` | "UI/HUD/3D Components/Render Powerbar", "Render Infobar" |
| `0x00D64E0C` | UI layers: `[+8][id]`; player 1's `Hud.gfx` is layer `0x3B` (vtable `0xBD0E1C`, `[+0x68]+4` GFx movie view) |
| `0x00D64E00` | +0x208 `cAnchoviesPC` achievements (vtable `0xC4AAC4`, unlock `0xA17F90`; +0x2C unlocked, +0x38 count 47); ids 41–46 come from ranked online results (`0x802870`) |
| `0x00D71954`, `0x00D11B6C` | "Anchovies/UnlockAnchovy", "UnlockAnchovieID", read by the race update `0x4BF180` |
| `0x00D64E6C` | HasPureSave (decal id 47); decal table `0x00D6FABC` (id, title), 48 entries |
| `0x00B9B43C` | Import table: FMOD `Event::start` |
| `0x008674F0` | FMOD init: DirectSound, `setSpeakerMode(5POINT1)` |

Actions: `0x29` throttle, `0x2A` brake, `0x1D`–`0x2C` the 16 console buttons, `0x03`–`0x06` menu
up/down/left/right, `0x0A`–`0x0D` menu Y/A/X/B.

## License

GPL-3.0 — see [LICENSE](LICENSE). Split/Second is © Disney; this project contains no game code or
data.
