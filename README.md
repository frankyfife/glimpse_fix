# glimpse_fix — Split/Second PC fix

A drop-in `dinput8.dll` for the **Steam** PC version of **Split/Second** (Disney / Black Rock, 2010)
that adds what the port never had, with a settings program instead of hand-editing an INI.

> **Steam version only** (`SplitSecond.exe` 1.0.0.1, app 297860), tested on Windows 11. The retail
> DVD version and packs that replace the exe are not supported ([why](#copy-protection)). Supported
> language builds: [technical notes](docs/TECHNICAL.md#supported-executables).

> **2.3.0 is in beta.** New in it: your own Xbox 360 DLC on the PC (experimental), set up with one
> button. Please report what works and what doesn't with the
> [issue forms](https://github.com/frankyfife/glimpse_fix/issues/new/choose) and attach
> `SplitSecondPad.log` (`Log=1`).

## Features

**Fixes**

* **Controllers:** every XInput pad (Xbox, 8BitDo, Steam Input, DS4Windows, ...) with the console
  layout, full menu navigation, Xbox or PlayStation prompts, separate analog triggers and rumble.
* **60 fps**, with the simulation stepped as at 30 fps, so physics and AI behave as intended.
* **Sharp, complete worlds:** a larger streaming pool (no blurry or missing textures), no pop-in
  beside the track, longer draw and shadow distances, sharper shadows, the Xbox 360's opponent car
  detail.
* **Image:** SMAA anti-aliasing with a sharp HUD, console-size power play icons, motion blur matched to
  60 fps, anisotropic filtering; vignette, gradient, bloom and flares switchable; car glass as on the
  Xbox 360; texture load stutter fix.
* **Sound as on the console:** opponents' engines are audible again, matched to each car, and
  adjustable in volume; dynamic mixer ducking, 3D voice placement, sound bank offset fixes and the
  Xbox 360's reverb presets.
* **Ultrawide** and **HDR output** (paper white and highlights up to the display's peak).
* **Skippable cutscenes:** press twice to skip the season's movies, the end sequence and the boot
  movies.

**Extras** (all off by default)

| | |
|---|---|
| **Boost** / **Handbrake** | Hold X / Y (keyboard: left Shift / Space): surge up to 324 km/h, or brake into a slide. |
| **AI boost** *(experimental)* | The AI boosts too, up to a *chaos mode* with a full power play bar. |
| **Full power play bar** | LS + RS (keyboard: left Ctrl) fills it. |
| **Everything unlocked** | All tracks, events, modes and cars, plus the PC's bonus content; your profile is not changed. |
| **Achievements and decals** | All, or only those nobody can earn any more (ranked online, Pure). |
| **Pick the laps** | 1 to 9 laps on Quick Play Race's track select, also in split screen (up to 16 in the settings program, but the HUD shows only one digit); with Online over Steam the host's pick applies to its LAN game (up to 8). A race with a changed count keeps no personal best. |
| **Quicker finish card** | Continue shows as soon as the single-player finish card accepts it. |
| **Save backups** | At every game start a copy of your saves, up to 5 kept, outside the save folder. |
| **Camera** | Field of view, zoom, less shake, a HUD key and a **photo mode** with depth of field. |
| **Potato presets** | For very weak PCs, at 60 fps. |
| **Online over Steam** *(experimental)* | The game's LAN mode over the internet with a Steam lobby. [Details](docs/ONLINE.md) |
| **Your own Xbox DLC** *(experimental)* | Converts your own Xbox 360 DLC dump on your PC: 11 cars, the Deadline mode, the tracks Quarry and Minepit Park. [Below](#your-own-xbox-dlc-experimental) |
| **Texture dump** | All the game's textures as DDS files, the groundwork for texture packs. [Details](docs/TEXTURE_DUMP.md) |

All buttons and keys can be changed. Every feature in detail: [technical notes](docs/TECHNICAL.md).

## Install

1. Copy `dinput8.dll`, `SplitSecondPad.ini` and `glimpse_config.exe` into the game folder
   (`...\steamapps\common\SplitSecond`).
2. Drag `SplitSecond.exe` onto `setlaa.exe` (the large address aware flag for the bigger streaming
   pool; the original stays as `SplitSecond.exe.orig`). Repeat it after Steam's "Verify integrity".
   The settings program warns when the flag is missing and sets it after your yes. Without it the game
   has only 2 GB of memory and can crash in a race at high resolutions, with HDR or with the DLC.
3. Run `glimpse_config.exe` for the settings.

Uninstall: delete the three files and rename `SplitSecond.exe.orig` back.

## Settings

`glimpse_config.exe` edits `SplitSecondPad.ini` next to it: one page per topic, a help line for the
option under the mouse, every change saved at once. The *Presets* page has *Recommended*, *Original
game*, *Potato*, *Peeled potato* and *Chaos*, and *Back to your settings*. Presets switch *Pick laps on the track select* off;
only *Original game* also resets the lap count picked in the game. Presets leave *Save backups* as it is. Every key:
[INI reference](docs/INI.md).

## Your own Xbox DLC (experimental)

If you bought Split/Second's DLC on your Xbox 360, glimpse_fix converts **your own copy** so the PC
game loads it. **No DLC content comes with glimpse_fix or is downloaded by it**, and the converted files
stay on your PC; please do not share them. Steam version, German and English depots; the Russian depot
may follow after the beta.

You need your DLC as the console stores it (the folder `425607E7` with `00000002`), about 4 GB free on
C:, and the folder `glimpse_tools` from the release zip in the game folder. Then, in the settings
program, page *DLC*: **Set up DLC**. It

1. finds the game folder through Steam and your DLC in the usual places (or asks),
2. downloads the **DLC tools** (Python, numpy, vgmstream, ffmpeg; about 90 MB, checksum-checked) only
   if they are missing and only after your yes,
3. offers Microsoft's DirectX runtime if `D3DCompiler_41.dll` is missing (needed for the tracks),
4. converts everything on your PC (about 10 minutes the first time),
5. backs up your saves, installs into `glimpse_dlc` in the game folder and turns on the settings.

*Uninstall* on the same page takes it out again. Details, the command line and `setup_deps.bat`:
[docs/DLC.md](docs/DLC.md).

## Copy protection

The Steam exe is protected with SecuROM. **glimpse_fix does not remove, bypass or weaken it and does
not change any game code**: it works through the game's data, swapped function pointers, calls into the
game's own functions and Direct3D hooks. A changed instruction would make the game hang minutes later,
which is likely why older 60 fps patches had a bad name. [In detail](docs/TECHNICAL.md#copy-protection-in-detail).

The "elysium" Xbox content pack (`Xenon.dll`) is **not supported**: it swaps in the patched demo exe and
redistributes the paid Xbox DLC without a license. Please reproduce problems without it.
[More](docs/TECHNICAL.md#the-xbox-content-modpack-xenondll-in-detail).

## Known limitations

* 60 fps: races, AI and physics were checked, not every power play and cutscene; `FrameRate=30` if
  something looks off.
* Photo mode and HUD key are for player 1 only, the photo mode only on a single screen; once your car
  is wrecked the crash sequence decides the view. In split screen boost, handbrake and power bar fill
  work for both players (Steam build, not the Russian depot); tested with two pads and with one player on
  the keyboard. With `HandbrakeMode=2` a player on the keyboard gets the drift handbrake, as the brake
  goes through a pad's trigger.
* Full ultrawide mode, Online over Steam and the DLC are experimental.
* HDR runs in a borderless window; the Steam overlay may not show.
* With a wheel or other DirectInput device, the controls screen names inputs generically.
* `UnlockDecals`: finish the race or leave it through the pause menu so the profile is saved.
* Russian Steam build: the options new in 2.2.0 are checked against a dump, not yet played; no DLC.
* SMAA in split screen: the fix for [#15](https://github.com/frankyfife/glimpse_fix/issues/15) is in,
  not yet confirmed in the game.
* Audio: the game forces FMOD to DirectSound 5.1; the LFE channel and the pause menu are silent in the
  game's own mix. High Octane DLC supercars use individual stock engine fallbacks as opponents when
  a matching bank is unavailable ([details](docs/HIGH_OCTANE_AUDIO.md)). On the Russian Steam depot `Ducking`, `SoundPlacement`,
  opponent car matching, event table growth and slot cleanup do not apply (only the 256 KB bank slot
  size, `SoundBankFix`, `ConsoleReverb` and `OpponentEngineVolume` apply there).
* `TextureDetail` below full also blurs menu and HUD pictures; `ShadowResolution=4` is experimental.

Bug reports: `Log=1` (settings program, *Debug*), then attach `SplitSecondPad.log` to an
[issue](https://github.com/frankyfife/glimpse_fix/issues/new/choose).

## Credits

* [Back on Air](https://github.com/xenophobentx/back-on-air) by xenophobentx gave the ideas for the
  opponent engine fix, the opponent car detail and the longer shadow distance; worked out and written
  independently here, no code taken.
* MR-KOKOS: the full ultrawide mode (`UltrawideFix=2`).
* EndlessPapercuts: the request for skippable cutscenes
  ([#11](https://github.com/frankyfife/glimpse_fix/issues/11)).
* [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando
  Navarro and Diego Gutierrez, unchanged in `third_party/smaa`.

## Building

`build.bat` with the Visual Studio 2022 Build Tools (x86). Research tools and address notes:
[technical notes](docs/TECHNICAL.md#building).

## AI disclosure

glimpse_fix is developed with AI assistance: the code, the reverse engineering and the documentation
are written with Claude (Anthropic). Changes are tested in the game on the German/English Steam version
before a release; what could not be tested is marked as such.

## License

GPL-3.0, see [LICENSE](LICENSE). `third_party/smaa` is under the MIT license
([its license](third_party/smaa/LICENSE.txt)). Split/Second is © Disney; this project contains no game
code or data.
