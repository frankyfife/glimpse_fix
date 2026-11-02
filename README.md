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
  vignette, anisotropic filtering, internal render resolution independent of the quality option
* **Extras nobody else has** (off by default, one line each in the INI) — see below

No game files are modified at runtime, and the game's code is not changed either: everything works
through the game's own data, after checking the expected bytes. The game's copy protection
(SecuROM) stays fully in place, see
[Copy protection](#copy-protection). You need your own copy of the game.

## Extras: Boost, Power Play on demand, everything unlocked

Split/Second never had a boost. Now it does — and it feels like it belongs there.

| | What it does | INI |
|---|---|---|
| **Boost** | Hold **X** and your car surges forward along its heading, up to 324 km/h. The motion blur swells with it (3× by default) and eases off when you let go, so speed *feels* like speed. Works only while driving forward, never in the pause menu. | `Boost=1` |
| **AI boost** *(experimental)* | The AI cars boost too, in bursts while they are behind you or not far ahead, so the field stays tight. Three levels from *easy* to *hard*, on the straights only or everywhere, and a **chaos mode** (`AIBoostChaos=1`) where every AI car boosts all the time. Turn it off with one line if it misbehaves. | `AIBoost=1` |
| **Full Power Play bar** | Press **LS + RS** (both sticks) and the power play bar is full: drop a building on the leader whenever you like. | `PowerBarFill=1` |
| **Everything unlocked** | All tracks, events, episodes, game modes and cars selectable from the start, using the game's own switches. Your profile is not changed; set it back to 0 and everything is as you earned it. | `UnlockAll=1` |
| **Bonus content** | The extras the PC version hid behind cheat codes: the **Expressway** track, the Cobretti SlipStream, the retailer branded cars and the Euro livery pack. | `BonusContent=1` |

Tune the boost with `BoostAcceleration` (m/s², default 15 ≈ 1.5 g), `BoostMaxSpeed` (m/s, default 90)
and `BoostMotionBlur` (1–5), the AI boost with `AIBoostLevel`, `AIBoostUnlimited`, `AIBoostStraightOnly` or simply `AIBoostChaos`. Like everything else here, the extras only change game data, never game
code, so they are safe with the copy protection.

> **Compatibility: Steam version only.** Developed and tested exclusively with the Steam release
> (`SplitSecond.exe` 1.0.0.1, app 297860) on Windows 11. The retail DVD version
> (Split/Second: Velocity), other regions and any other release have **not** been tested. All
> addresses come from the Steam executable; on a different exe the byte checks fail and the
> game-specific patches (60 fps, streaming pool, prompt icons, trigger preset) switch themselves
> off, and whether the rest works there is unknown.

## Install

1. Copy `dinput8.dll` and `SplitSecondPad.ini` into the game folder
   (`…\steamapps\common\SplitSecond`).
2. Drag `SplitSecond.exe` onto `setlaa.exe` (or run `setlaa.exe <path to SplitSecond.exe>`).
   It sets the large address aware flag and keeps the original as `SplitSecond.exe.orig`.
   Steam's "Verify integrity of game files" restores the original exe; just run it again.

Uninstall: delete `dinput8.dll` and `SplitSecondPad.ini`, rename `SplitSecond.exe.orig` back.

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
  at 30 fps with two ticks per update (the AI player's vtable slot).
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
  the amount is doubled. `MotionBlurStrength` scales it further.
* **Vignette:** the race cameras darken the screen edges in the grading pass; `Vignette=0` sets
  the vignette's U/V scale to 0 for that pass, grading and gradient stay.
* **Anisotropic filtering:** the game filters all textures linearly; mipmapped textures that are
  not render targets get anisotropic filtering.
* **RenderScale:** "Very High" is exactly "High" plus 2x supersampling. The factor can be set
  independently of the quality option.

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
  streaming distances, debug and unlock switches), loaded HUD parameters, powerplay distance
  records, the main loop's accumulator, the power bar and the player car's chassis velocity,
* swapped vtable slots (motion blur and grading passes, AI player update), the import table entry
  of `XInputSetState`, read-only strings and constants (prompt bar icon paths, route marker size),
* the Direct3D device's vtable, and calls into the game's own functions (dictionary lookup,
  Scaleform `Invoke`/`SetVariable`) from the input poll.

The prompt bar glyphs, the title screen text, the icon size, the 60 fps and the streaming pool were
first done with code patches and each set off exactly this hang, so they were rebuilt this way.
The one exception is `RenderScale` set to a number (see Known limitations).

* `[Debug] PatchMask` (bits as in `src/common.h`) switches individual patches off, to find out
  which one causes a side effect. With `Log=1` the log also shows the number of SecuROM VM
  contexts in use whenever it changes; a check that found something shows up as two more
  contexts (`8B03 8AF3`) every ~22 s.

## SplitSecondPad.ini

| Section / key | Default | Meaning |
|---|---|---|
| `[Controller] VirtualPads` | 4 | XInput slots exposed as pads (0–4). The game accepts at most 4 joysticks; lower this to make room for a wheel. |
| `HideXInputFromDirectInput` | 1 | Don't offer the real XInput devices through DirectInput as well. |
| `SeparateTriggers` | 1 | 0 = stock shared Z axis. |
| `StickDeadzone` | 0.15 | Radial stick deadzone (0–1). |
| `TriggerDeadzone` | 0.05 | Trigger deadzone (0–1). |
| `RumbleStrength` | 1.0 | 0 = off. |
| `ButtonGlyphs` | xbox | `xbox`, `playstation` or `keyboard` (never switch). |
| `Log` | 0 | 1 = write `SplitSecondPad.log` (Direct3D device, FMOD events, music state, SecuROM VM contexts, every change of the profile `.sav` files). |
| `TestFeed`, `Profile` | 0 | Development only: pad state from shared memory (`tools/padfeed.py`), call counters. |
| `[Game] FrameRate` | 60 | 30 = stock. |
| `StreamPoolMB` | 512 | Streaming pool in MB, 0 = stock (57). Only used when the exe has the LAA flag. |
| `FixPowerplayIconSize` | 1 | Powerplay icons and route change markers at console size at every resolution. 0 = stock. |
| `MotionBlurStrength` | 1.0 | Motion blur multiplier on top of the 60 fps compensation. 0 = off. |
| `RenderScale` | auto | Internal resolution factor 0.5–4.0; `auto` = 2.0 at Very High, 1.0 below. |
| `Anisotropy` | 16 | Anisotropic filtering 2–16, 0 = stock (linear). |
| `LodDistanceScale` | 2.0 | Multiplier for LOD, streaming, particle, debris and powerplay distances (0.5–8). |
| `PopInFix` | 1 | Ignore the sectors' precomputed visibility lists. 0 = stock. |
| `Vignette` | 0 | 1 = keep the cameras' darkened screen edges. |
| `SkipIntro` | 0 | Boot movies at every start: 1 = skip the publisher logo, 2 = also the game intro. They end at once without decoding (the movie files stay untouched). 0 = stock. |
| `UnlockAll` | 0 | 1 = all tracks, events, episodes, game modes and vehicles selectable (the game's own switches; the profile is not changed). |
| `BonusContent` | 0 | 1 = the PC version's cheat code extras: Expressway track, Cobretti SlipStream, retailer branded cars, Euro livery pack. |
| `Boost` | 0 | 1 = X held gives player 1 extra speed along the car's heading. |
| `BoostAcceleration` | 15 | Boost in m/s² (1–100). |
| `BoostMaxSpeed` | 90 | No boost above this speed, m/s (10–200; 90 = 324 km/h). |
| `BoostMotionBlur` | 3.0 | Motion blur multiplier at full boost (1–5). |
| `PowerBarFill` | 0 | 1 = LS+RS (both sticks pressed) fills player 1's power play bar. |
| `AIBoost` | 0 | Experimental: 1 = AI cars boost in bursts while behind player 1 or not far ahead. |
| `AIBoostLevel` | 2 | 1 easy (0.8× your boost, 3 s tank, up to 30 m ahead), 2 normal (1×, 5 s, about once a second, 60 m), 3 hard (1.3×, 8 s, almost constantly, 150 m). |
| `AIBoostUnlimited` | 0 | 1 = the AI's boost tank never empties (chaos mode). |
| `AIBoostStraightOnly` | 1 | 1 = AI boosts only on straights at speed, 0 = anywhere while driving forward. |
| `AIBoostChaos` | 0 | 1 = chaos mode: level 3, endless tank, anywhere, also far ahead of you. Overrides the three keys above while set. |
| `[Debug] PatchMask` | 0xFFFFFFFF | Development: bit mask of the patches that may be applied (`PATCH_*` in `src/common.h`). |

## Known limitations

* With virtual pads active, the controls screen uses Xbox names only when no other DirectInput
  joystick (e.g. a wheel) is present, since those names are shared by all devices.
* 60 fps: races, AI and physics were checked; not every power play and cutscene was. Set
  `FrameRate=30` if something looks off.
* `RenderScale` other than `auto` patches code in the render target and HUD setup (nine places),
  the only option that changes game code. Leave it at `auto` if the game hangs after a few
  minutes with it set.
* Boost and the power bar fill work for player 1 only.
* `PopInFix=1` draws somewhat more per frame than the stock visibility lists allow.
* Audio: the game forces FMOD to DirectSound in 5.1. The LFE channel stays silent and the pause
  menu is completely silent; both are the game's own mix.

## Building

`build.bat` with the Visual Studio 2022 Build Tools (x86) builds `build\dinput8.dll` and
`build\setlaa.exe`.

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
| `0x00BC0BC0` | `cAIPlayer` vtable slot of the AI player update `0x51A8B0` |
| `0x00D389E0`, `0x00D389E4` | Tweakable "Sectors/Streaming Memory Pool MB" (57), read on level load (`0x7C654F`); its record (vtable `0xBB2498`, +4 variable) |
| `0x00D66B28` | `cInGameResources`: +0xE4 `cGameLogic` (+0x18 player count, +0x1C players); player +0xE8 `cPowerBar` (+0x2C output, +8 power 0–100), +0xAC `cVehicleState` (+0x40 vehicles, +4 + i×4); vehicle +0x584 chassis `hkpRigidBody` (+0x100 forward, +0x110 position, +0x1A0 velocity) |
| `0x00D6FC54`, `0x00D6FBA0`, `0x00D7D978`, `0x00D6F8F0` | UI/All Gamemodes / Episodes / Events / Vehicles Unlocked |
| `0x00D6C49C` | 10 `PCcheat;…` switches, 8 bytes apart (Cobretti SlipStream, Expressway = Downtown B, branded cars, Euro livery) |
| `0x00D8B368`, `0x00D82788`, `0x00D9AFCC` | Disable Powerplay Visibility / Sector Visibility / Visibility Map |
| `0x00D8B384` | Powerplay draw distance records (name +0x10, distance +0x90) |
| `0x00D66B24` | HUD; parameters at +0x20, powerplay icon scales +0x214/+0x218 |
| `0x00BB8494` | Route change marker size factor 2.25/1280 (read only by `0x4C7910`) |
| `0x00BB42B0`, `0x00BB4048` | vtable slots of the motion blur pass `0x49B490` and the grading/vignette pass `0x49A7A0` |
| `0x01E0266A` | SecuROM VM context table (10 entries, +0x24 = `0x66666666` while in use) |
| `0x00A15BF0` | Dictionary lookup, key → UTF-8 text |
| `0x008674F0` | FMOD init: DirectSound, `setSpeakerMode(5POINT1)` |

Actions: `0x29` throttle, `0x2A` brake, `0x1D`–`0x2C` the 16 console buttons, `0x03`–`0x06` menu
up/down/left/right, `0x0A`–`0x0D` menu Y/A/X/B.

## License

GPL-3.0 — see [LICENSE](LICENSE). Split/Second is © Disney; this project contains no game code or
data.
