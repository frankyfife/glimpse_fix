# glimpse_fix — Split/Second PC fix

A drop-in `dinput8.dll` for the PC version of **Split/Second** (Disney / Black Rock, 2010) that
adds what the port never had:

* **Proper controller support** for every XInput pad (Xbox 360/One/Series, 8BitDo, Steam Input,
  DS4Windows, …): full menu navigation, the console button layout, **Xbox (or PlayStation)
  button prompts**, independent analog triggers and rumble
* **60 fps** with the simulation stepped exactly as before, so physics, Havok and AI behave like
  at 30 fps
* **No more blurry, grey or missing world textures and props**: a larger streaming pool (needs the
  large address aware flag, a tool for that is included)

No game files are modified at runtime; everything is patched in memory after checking the
expected bytes. You need your own copy of the game.

Tested with the Steam release (`SplitSecond.exe` 1.0.0.1, app 297860) on Windows 11.

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
* The prompt bar shows Ⓐ Ⓑ Ⓧ Ⓨ Start (or ✕ ○ □ △) while the pad is the device in use and
  switches back to key captions as soon as a key is pressed. The title screen reads
  "Press START" instead of "Press ENTER" (any language). The controls screen names pad inputs
  A/B/X/Y/LB/RB/LT/RT/LS…/RS… instead of "Button 1" or "Z rotation".

### 30 fps

The game simulates in fixed 1/60 s ticks but its main loop renders every 1/30 s and hands two
ticks to each frame; physics and race logic use `dt = ticks × 1/60`. The only per-frame code that
assumes a 1/30 s frame is the AI steering controller (once per car per frame). Speeding up just
the main loop makes the AI react twice as fast — the reason the old 60 fps patch made AI cars
crash constantly.

* Main loop period 1/60 s with one tick per frame; the AI steering controller gets the 1/60 s
  frame time; the limiter waits on a high resolution timer instead of `Sleep` (15.6 ms steps).
* Above 60 fps would need render interpolation, since the ticks are fixed at 1/60 s.

### Blurry / missing textures and props

The sector streaming pool is set to 57 MB, a console-era budget. When it's full the streamer
evicts sector data: walls and signs drop to their smallest mip levels (colour smears, flat grey)
and small props such as bushes disappear. Without the large address aware flag the 32-bit
process has 2 GB of address space, and at 4K only about 500 MB of it is left, so the pool can
only grow with the flag set.

* With the flag, the pool is raised to 512 MB (configurable). Without it, the pool stays stock.

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
| `Log` | 0 | 1 = write `SplitSecondPad.log` (Direct3D device, FMOD events, music state). |
| `TestFeed`, `Profile` | 0 | Development only: pad state from shared memory (`tools/padfeed.py`), call counters. |
| `[Game] FrameRate` | 60 | 30 = stock. |
| `StreamPoolMB` | 512 | Streaming pool in MB, 0 = stock (57). Only used when the exe has the LAA flag. |

## Known limitations

* With virtual pads active, the controls screen uses Xbox names only when no other DirectInput
  joystick (e.g. a wheel) is present, since those names are shared by all devices.
* 60 fps: races, AI and physics were checked; not every power play and cutscene was. Set
  `FrameRate=30` if something looks off.
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
  memory and address space tools.
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
| `0x00BA0C14` | 1/30, shared by the main loop, the AI steering controller `0x524AC0`, AI look-ahead grids and data conversions |
| `0x00D389E0` | Tweakable "Sectors/Streaming Memory Pool MB" (57), read on level load |
| `0x00A15BF0` | Dictionary lookup, key → UTF-8 text |
| `0x008674F0` | FMOD init: DirectSound, `setSpeakerMode(5POINT1)` |

Actions: `0x29` throttle, `0x2A` brake, `0x1D`–`0x2C` the 16 console buttons, `0x03`–`0x06` menu
up/down/left/right, `0x0A`–`0x0D` menu Y/A/X/B.

## License

GPL-3.0 — see [LICENSE](LICENSE). Split/Second is © Disney; this project contains no game code or
data.
