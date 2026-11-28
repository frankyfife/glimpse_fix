# glimpse_fix: how it works

The details behind the features in the [README](../README.md): what the port gets wrong, how the fix changes it without touching the game code, and the notes for building and research.

## Supported executables

All addresses come from the Steam executable. On a different exe the byte checks fail and the
game-specific patches (60 fps, streaming pool, prompt icons, trigger preset) switch themselves off;
whether the rest works there is unknown.

The Steam version ships more than one `SplitSecond.exe`: the language you pick in Steam decides
which one you get. Supported are the build of the German and English depots (built `4BE13C66`, the
one the fix was made on) and the build of the Russian depot 297867 (built `4BE3EA74`), whose
addresses are mapped from a table (`src/build_ru.inc`). The Polish depot 297866 is a third build and
not supported; French, Italian and Spanish have not been checked. The log names the build it found
(`SplitSecond.exe build recognised (...)`) or says `unknown SplitSecond.exe build`.

The converted Xbox DLC (`[DLC]`, the overlays included) needs the Steam version 1.0.0.1 of the German and
English depots, not the Russian depot: on any other build the DLL leaves all of `[DLC]` off.

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
* The prompt bar shows A, B, X, Y, Start (or Circle, Square, Triangle) while the pad is the device in use and
  switches back to key captions as soon as a key is pressed. The title screen reads
  "Press START" instead of "Press ENTER" (any language). The controls screen names pad inputs
  A/B/X/Y/LB/RB/LT/RT/LS…/RS… instead of "Button 1" or "Z rotation".

### Menu repeat

The menus take their directions from the game's UI input update (0x4D60F0, every frame in the front end
and in a race). The D-pad's actions (0x19-0x1C, bound to the POV) count only on the frame they are
pressed, so a held D-pad gives one step and nothing more; scrolling through a long list (the car select)
takes one press per entry.

* With `MenuRepeat=1` the virtual pads report a held D-pad as a train of presses: after
  `MenuRepeatDelay` ms the POV goes centred for 40 ms and at least two `GetDeviceState` calls (so a frame
  in which more than one reader polls the pad still sees the release), then back, which the game takes as
  a new press. Then the same every `MenuRepeatRate` ms after the press, so a step every rate + 40 ms. A
  POV press is exactly what a tap gives, so every screen counts one step.
* An earlier version pushed the left stick instead, to use the game's own typematic (0.4 s / 0.1 s); some
  screens also read the stick themselves, so a step could count twice and fields were skipped.
* Not in a race: the race camera hooks tell a race (`RaceRunning`, a race camera ran in the last half
  second), so without them (an unknown build) nothing repeats. A direction already held when a race ends
  or the pause menu opens (also one pressed within that half second) waits until it is let go.
  DirectInput devices passed through are not touched (`src\dinput_wrap.cpp`, `MenuPov`).

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

**Opponent cars.** The PC release loads the PS3's car detail settings
(`Generic/Params/PlayerLOD_ps3.params`): at most 3 opponents get the mid detail level, all others
the far one, and every opponent switches to the far level at 100 m, so cars visibly change their
model during a race. The Xbox 360's file in the same archive (`PlayerLOD_xenon.params`) allows 31
opponents at the mid level up to 200 m. `CarDetail=1` writes those numbers into the tweakables
(`PlayerLOD/02;MaxAIMidLODs`, `PlayerLOD/04;AIMidToFar`); `CarDetail=2` also gives all opponents
the near level up to 40 m (`MaxAINearLODs`, `AINearToMid`, stock 2 and 20 m). The game reads them
every frame as a budget per level (0x7B0A2A), not as array sizes; they are written again whenever
a level loads the file. On the Russian build the addresses are mapped and checked against a dump,
not yet played (as for `ShadowDistance`).

**Shadows.** At the High and Very High quality levels in single player the game takes its sun
shadow cascades from the global `Lighting/CSM_Max` settings (chosen at 0x88A3E0, copied at
0x894B60): three cascades up to 300 m, faded out over the last 100 m. `ShadowDistance` multiplies
both. The three cascades then cover a longer distance, so close shadows get a little softer. Split
screen and the Low and Medium levels keep their per-level settings.

**Per-frame extras without virtual pads.** The per-frame extras (`OnInputFrame`) normally run from
the poll of virtual pad 0. When the game's DirectInput interface is not wrapped (`VirtualPads=0`, or
no XInput DLL) nothing polls a virtual pad, so the fix runs them from the first `PeekMessageA` call
in the game's input update instead, and keeps the XInput slots' connection state there. The main
loop calls that update once per pass, before simulation and rendering, at both frame rates. The
hook checks the call site and import slot at startup and logs whether installation succeeds. The
game then reads the pad itself, and the fix cannot take a button away from it, so the boost, the
handbrake, the power bar fill and the photo mode take their keys only.

**Shadow resolution.** In single player all three cascades go into one depth texture of 1024x3072
(INTZ, or the first of RAWZ, DF24, DF16, D24S8 the device takes), one above the other, drawn through
viewports at y = 0, 1024 and 2048, with an A8R8G8B8 colour target of the same size bound that is never
written. The game never asks either texture for its size, and its shaders pick and filter a cascade in
texture coordinates. `ShadowResolution` (2 by default, 4 experimental) creates both that many times as
wide and high, and while the colour target is render target 0 the viewports, scissor and clear rects
the game sets in its 1024x3072 units are scaled with them (Direct3D vtable slots: `CreateTexture`,
`SetRenderTarget`, `SetViewport`, `SetScissorRect`, `Clear`). The soft edge keeps its width in texture
coordinates and is sampled finer. The factor is lowered to the device's largest texture; if a large
target cannot be made, both stay at the game's size. Split screen (two maps of 1024x1024) is left
as it is.

**Shadows off.** `Shadows=0` sets the game's own switch `Lighting/Debug/Cast shadows - Disable all
shadows` (0xD716EC), which every pass that draws into the shadow maps checks.

### Other image fixes

* **Powerplay icons:** the port multiplies their scale by width / 1280 on top of the renderer's
  own width scaling (1.5x too big at 1080p, 3x at 4K); the loaded scales are divided back.
  The route change markers (red and blue arrows) get their size as width x 2.25/1280, a constant
  only that code reads; it is set to 2.25 / width.
* **3D HUD resolution (`HudResolution`):** the game draws lap, position, timers and scores
  (`DialsInfo.gfx`) and the power bar (`DialsPower.gfx`) into its `512x288` HUD texture in
  single-player. Split screen draws the text into the scene directly and uses a `256x128`
  HUD texture for the power bar. The option scales only the offscreen target and its depth
  surface together. Auto selects 2x up to 1080p effective 16:9 height and 4x above that or
  in split screen. The device texture limit can reduce the factor. Both surfaces are staged
  before either is handed to the game. The color surface carries a private-data tag. The
  device hooks and Scaleform `SetViewport` scale its matching drawing rectangles without
  changing `cRenderTarget` dimensions or the HUD quad UVs.
  The stock race glyph cache uses up to 28 px per slot. Large text can fall back to a
  vector path with different colors, including the split-screen lap slash at 1x. On the
  render thread, `SetViewport` (vtable `0xC41CF4` slot 20) checks the live font cache,
  invalidates its glyph batches, raises the slot limit to 128 and raises DialsInfo's
  raster scale to 4.0. This keeps the text and glow together in the bitmap atlas for
  both modes, including at 1x. The atlas uses up to about 8 MiB. In single-player the
  larger color and depth surfaces add about 3 MiB at 2x or 17 MiB at 4x. `HudResolution=-1`
  or clearing `PATCH_HUD` (`0x4000000`) leaves both paths as the game configured them.
  Steam build 385319 only (not the Russian depot).
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
* **HDR output** (`HDR=1`): Direct3D 9 has no way to an HDR10 display on Windows. The game's device
  is made windowed (a borderless window over the whole screen) and its frames go out through a
  Direct3D 11 swap chain of the fix with the HDR10 colour space: at Present the back buffer is
  converted to linear light at the paper white level (`HDRPaperWhite`), BT.2020 and the PQ curve,
  read back one frame later and presented. The light above white comes from the game's own half
  float image: its filmic tone mapper (the last draw of the bloom pass, vtable slot `0xBB3678`)
  squeezes it into 8 bits with `T(L) = k L^p / (L^p + q)`. Right after that pass, with the game's
  textures and constants still bound (read from the shader's constant table), one more pass of the fix
  runs the same curve twice per pixel, as the game does and opened up above a knee (same value and
  slope there, then rising towards `HDRPeakBrightness`), and keeps the ratio as a gain per pixel. When
  the first Scaleform movie of the frame is drawn (`GFxMovieRoot::Display`, vtable `0xC41CF4` slot 31),
  the game's target is copied: the picture without the HUD. At Present every pixel that still equals
  that copy gets the gain, so the HUD and menus stay at paper white. The output costs about 1.6 ms a
  frame at 4K and adds one frame of delay.
* **Anti-aliasing (SMAA):** SMAA 1x (the reference shaders by Jimenez et al., unchanged in
  `third_party/smaa`, compiled with `fxc` at build time) runs on the 3D picture right before the game
  draws its HUD: at the first Scaleform movie after the tone mapper (`GFxMovieRoot::Display`, vtable
  `0xC41CF4` slot 31) on a target of the tone mapper's size, so text and HUD stay sharp. Only the
  tone mapper's viewport is written back, through a scissor rect with the full-target quad (in 2.2.0
  a viewport squeezed the whole picture into it, so split screen showed three pictures, issue #15);
  in split screen the viewports of both tone mappings before the HUD are united (split screen fix not
  yet confirmed in the game). A frame without a HUD
  (photo mode) is smoothed at Present. The device state is captured and put back with a state block.
  About 0.1 ms a frame at 4K on an RTX 5090.
* **Anisotropic filtering:** the game filters all textures linearly; mipmapped textures that are
  not render targets get anisotropic filtering (through the device's `SetTexture` and
  `SetSamplerState`).
* **Direct3D hooks kept in place:** right after the device's first `Reset` the Direct3D runtime writes
  its own functions back into the device's vtable. Before 2.2.0 that silently undid every hook of the
  fix except Present and Reset (the anisotropic filtering among them). Every hooked slot is now checked
  after each Reset and every input frame; where the runtime's function is back, the fix's hook goes in
  front of it again and calls on to it. A slot another module took over is left to it.
* **Texture detail:** `TextureDetail=1..3` raises the sampler state `MAXMIPLEVEL` on the same samplers,
  so mipmapped textures are drawn at half, a quarter or an eighth of their size; textures without mip
  levels (text, most of the HUD) are not affected.
* **Texture load stutter (`TextureLoadFix`, `PATCH_TEXLOAD` `0x8000000`):** the game's texture
  loader (`Crayon2::cTextureDataLoadHandler` at `0x005069F0`, helper `0x00511880`, thunk
  `0x00755D54`) loads every texture through the import
  `d3dx9_42.dll!D3DXCreateTextureFromFileInMemory` (`0x00B9B3DC` on the Steam build). That helper
  passes `MipLevels = D3DX_DEFAULT` and `MipFilter = D3DX_DEFAULT`, so whenever a DDS file ships
  without mip levels, `d3dx9_42.dll` calls `D3DXFilterTexture` on the render thread to
  CPU-decompress, filter and recompress a full 9–11 level mip chain. This affects the 2D menu, HUD
  and post-race UI sprite packs in `resident.ark` (for example the 22-texture and 7-texture
  post-race packs loaded right before `RaceEnd.gfx`, including `1024x1024` DXT1/DXT4 atlases —
  stalling transitions for ~250 ms at race end and over a second across boot, menu and race load),
  and also non-streamed single-level DDS entries in level `.ark` files and `resident.ark`: sky
  panoramas (`4096x512` / `2048x512` / `2048x256` / `1024x256`), `hdr_flare` lens flare sprites
  (`256x256` / `256x1024` DXT4), `1024x32` colour-grading LUTs (`default`, `pause_menu`,
  `race_finished`, etc.), dust/spark bokeh sprites (`MSC_Dust_Bokeh*`, `MSC_Spark_Bokeh_Single`)
  and a handful of single-level `resident.ark` prop textures (all of which also ship single-level
  with `mip_max = 0` on Xbox 360). All 5,405 streamed 3D world textures in `*.stream.ark` carry
  full mip chains and pass straight through. `TextureLoadFix=1` wraps the
  `D3DXCreateTextureFromFileInMemory` import slot (located dynamically by name via `FindImport`, no
  game code or hardcoded addresses; tested on the Steam build): when the source buffer is a plain 2D
  DDS header with at most 1 mip level (`dwMipMapCount <= 1`, not a cubemap/volume or DX10 header), it
  calls `D3DXCreateTextureFromFileInMemoryEx` with `MipLevels = 1`, `MipFilter = D3DX_FILTER_NONE`
  and `D3DPOOL_MANAGED`, falling back to the original import on failure. Pre-mipped and non-DDS
  textures pass straight through. Compared live at `1280x720` with the fix on and off, skies
  (`sky_Midday` 360° panorama), sun and distant light lens flares (`hdr_flare`), and colour grading
  (`default`, `pause_menu`, `race_finished`) show no visible difference at `TextureDetail=0`; under
  `TextureDetail=2`, keeping single-level textures at one level prevents `MAXMIPLEVEL` from
  downsampling the `1024x32` colour-grading LUTs (which otherwise causes green/magenta colour
  banding) and blurring the sky and single-level UI.
* **Graphics quality limit:** the game sets its quality level (0xD0F7E8) only through one function
  (0x471530), which clamps it to the range of the level's tweakable (the object at `[0xD0F7EC]`,
  maximum at +0xC). `QualityCap` lowers that maximum before the device is made and every frame; the
  level the options menu keeps and saves stays the player's own.
* **Graphics quality:** "Very High" is exactly "High" plus 2x supersampling: of all places in the
  game that read the quality level, only the supersampling ones tell Very High from High. To render
  at screen resolution, pick "High" in the game; nothing else changes.

### Car glass

The game's deferred G-buffer keeps the specular intensity in the alpha of its first target (RT0.a, x0.5),
the motion byte in the alpha of the second and the specular power in the alpha of the third (RT2.a, /64);
the sun pass lights every pixel with `2 * RT0.a * pow(.., 64 * RT2.a)`. The car glass shaders
(VehicleGlassAndDamage, VehicleGlass01) write the glass opacity (about 0.87) into the alpha of all three
targets. The Xbox 360 blends each target on its own: RT0's alpha with ZERO / blend factor 0, so a glass
pixel ends up with specular intensity 0, and RT2's alpha not written. Direct3D 9 has one blend state for
all targets: the PC port's race pass ('Render Vehicles Alpha') blends alpha ONE/ZERO, so every window
writes its opacity as specular intensity while the power stays that of the surface behind. Where that
power is 0 (the window frames of the converted DLC cars) `pow(x, 0) = 1` and the sun paints the glass flat
white; on the base cars the glass gets a sun highlight with the shine of whatever lies behind it. The PC's
car select already does it right (alpha ZERO / blend factor, factor 0).

* `GlassFix=1` tags the glass pixel shaders when they are made (`CreatePixelShader`, by the hash of their
  code that the frame dump uses) and notes in `SetPixelShader` whether the shader set last is one of
  them; a draw costs one test of that flag.
* For a glass draw with exactly the race pass's blend state, `SRCBLENDALPHA` goes from ONE to ZERO
  (RT0.a = 0, as on the Xbox) and `COLORWRITEENABLE1` from 0xF to 7, so RT1.a keeps the motion byte of the
  surface behind (the same car in nearly every pixel; written as 0 the motion blur would smear the
  windows like the world). Both states are put back after the draw, so the game's state cache stays right.
* What remains different from the Xbox: the normal in RT1 is blended like the other targets instead of
  replaced, which only moves the glass's diffuse sun term. The glass's reflections (RT0/RT2 colour) stay.
  Device vtable slots only (`src\glass.cpp`, `src\diag.cpp`); not on a pure device, which the game does
  not make.

### Audio

* **Opponents' engines:** every opponent's engine is a sound bank of its own, loaded into one of seven
  `CPUCar` slots. Four PC release faults are fixed under `OpponentEngines`:
  1. *Bank slot size* (known Steam builds): the audio system sizes each slot for the Xbox 360 (104857
     bytes), while PC MP2 banks are 145–203 KB, so the loader refuses them. The slot size is plain data
     in the exe (`0xD3DBC0`) and is raised to 256 KB at DLL load. Pointed out by the
     [Back on Air](https://github.com/xenophobentx/back-on-air) project (an inspiration only; no code
     was taken) and checked against this build.
  2. *Event capacity* (Steam build): `AudioInit` (`0x7F0D65`) caps the `EventMgr` table
     (`[0xD91C50]`, vtable `0xC2B3D4`) and the event-object pool (`0xD4390C`) at 511.
     Dry Docks needs `506 + N_unique` registrations and Downtown Central needs `505 + N_unique`,
     where `N_unique` is the number of loaded opponent banks. The stock `RegisterEvent` (`0x867CD0`)
     drops excess events. With only the table grown, the first spill into the locked pool returned a
     null event object and crashed at `0x52FAE9`.
     On the audio thread, before raising the table to 2048, reserve 2048 objects with the stock grow
     routine (`0x5081D0`) in the persistent `"LLAudio2"` heap (`0xAF4D00`/`0xAF48F0`).
     Reserve with allocator flag bit 0 at `0xD4391C` cleared, then restore it. Verify the pool layout,
     allocator vtable `0xBA29B0`, allocation slot target `0x410D00` and active-heap ID 2 before the call.
     The pool and table use the game's allocator pair (`0x530290`/`0x5302D0`). The game's pool
     teardown (`0x86F1D0` → `0x86E880` → `0x512B30`) frees the extra chunk with the normal chunks.
     A failed reserve leaves the table capped at 511. In the Steam lab with seven distinct banks,
     Dry Docks reached 513/2048 and Downtown Central reached 512/2048. Readback showed the same
     counts in the object pool and FMOD reported all seven opponent engines playing on both tracks.
  3. *Multi-race slot cleanup* (Steam build): at race end message 51 (`0x835620` -> `0x7F3400`) unloads
     `CPUCar` (`0x865980` -> `0x86DD00`), releasing each FMOD project without moving its record from the
     group's `used` list (`+0x128`) back to `free` (`+0x118`). In later races any repeated model matches
     the stale record, gets marked kept (`+0x641`), skips reload and plays silent. Unloaded records (not
     present in FMOD `EventSystem::getProject`) are moved back to `free` after message 51 and before
     message 43, leaving live projects intact across race restarts.
  4. *Car-to-engine matching* (Steam build): message 43 (`0x835340` -> `0x7F30C0`) carries every racer's
     actual `AudioFile` from `VehicleDatabase.xml`, yet ignores it for CPU racers and picks randomly from
     a 22-entry table (`0xD3E428`, which even includes missing `Truck_07`). All 27 base-game `AudioFile`
     entries (covering 84/84 base cars) and 3/3 Elite DLC cars map to the 30 shipped
     `Vehicles/CPU/<name>` banks in `resident.ark`, and every CPU `.fev` sets `maxPlaybacks = 7`, so
     duplicate opponents share one loaded bank slot and play independently. Message 43 is wrapped (audio
     message table `0xD8B500` slot
     43) to run the stock handler with CPU entries hidden (keeping racer count, split-screen mix and
     player Tonic synths untouched), then queue each unique opponent bank (`0x7F2FA0`) and assign its
     `<model>/Tonic/<model>` event (`0x8359A0`); Tutorial (`+0x0F`) and Survival/Nemesis (`+0x0D`) keep
     the stock path's seven fixed banks. The 4 Deadline DLC cars carry no CPU `.fev` on PC or Xbox 360,
     but their `.tonic`/`.tonic2` binaries are 100 % byte-identical (every grain and sound) to base cars
     and map by `AudioFile` to those exact base CPU banks (`Hypercar_20` -> `Hypercar_04`, `Muscle_20` ->
     `Muscle_14`, `Truck_20` -> `Truck_08`, `Truck_21` -> `Truck_06`; neither Deadline nor High Octane
     DLC cars appear in base `AIDriversDatabase.params`, so both DLC paths apply only outside normal
     Season, Elite and championship grids, such as custom rosters, a forced Demolition boss or Steam LAN).
     The 4 High Octane DLC supercars (models `Supercar_17..20`, `AudioFile` `Supercar_20`, `Supercar_21`,
     `Supercar_24`, `Supercar_26`) carry bespoke Tonic grain recordings with no CPU bank on PC or Xbox
     360. LAN drivers without a local screen also enter message 43 as CPU entries (`0x4AB180`).
     The resolver checks for an exact bank first, then checks the Deadline donors and the four High Octane
     stand-ins. It otherwise chooses the first available bank in stock table order with the same `AudioFile`
     prefix through the first underscore, or the first available stock bank overall. The log includes
     the requested name, chosen bank and fallback reason. See
     [High Octane opponent audio](HIGH_OCTANE_AUDIO.md) for the mappings and a future bank conversion.
  Their events are in the FMOD category `CPUEngine` (the player's engine is in `PlayerEngine`). The
  game's mixer sets that category's channel group level itself (presets, fades, ducking) through the exe's
  import of `ChannelGroup::setVolume`; `OpponentEngineVolume` wraps that import (found by name in the
  import table) and multiplies the levels set on the `CPUEngine` group (passing levels unchanged at 100 %),
  which is found from the category tree of an event that starts and cleared on FMOD project swaps while
  retaining the last unscaled level for the same group. The setting is a percentage (0..200, one parser
  for the start and the live re-read). FMOD caps a group's level at 1.0 (`ChannelGroupI::setVolume`); in
  the logged races the mixer's level for `CPUEngine` peaked at about 0.17, so 200 % has room. The INI is
  read on the game thread, which only stores the value; FMOD is called where the game calls it (the
  wrapper, and the next `Event::start`, which applies a change and finds the group again if the sound
  system was made anew).
* **The game's own DSP effects (`Ducking`, `PowerPlayFlanger`, `SoundPlacement`):** the audio code wraps every effect in an
  object whose virtual attach (vtable slot 9) is called with the channel group the effect belongs on.
  FMOD's own effects call `ChannelGroup::addDSP` there (0x864CE0); in the PC build the game's plugin
  effects only return their DSP (`mov eax,[ecx+off]; ret 4`), so none of them hears a sample. These effects get a
  real attach through their vtable entry (`.rdata`, checked against the RTTI class name and the stub's
  bytes, set at DLL load; Steam build only), the game's code is untouched. The missing connection was
  described by Back on Air; this is a separate implementation.
  * `Ducking`: the mixer (`DynamicMixer.xml`, the same on PC and Xbox 360) ducks groups while others are
    loud: collisions duck music (−2 dB) and both engines (−5 dB); power plays duck opponents' engines and
    surfaces (−12 dB), player engine and collisions (−6 dB); HUD ducks music (−3 dB); speech mutes music,
    swooshes and particles (−60 dB) and ducks engines, collisions, power plays, surfaces and HUD (−18 dB)
    and ambience (−12 dB). Every side chain listens to a `VolumeMeter` in slot 7 of its group (vtable
    0xC1E028), attached when the game builds the group (0x86C4C3). With `Log=1` each attach and volume
    moves of 1 dB or more (including silence crossings) of a meter's group are logged (at most every
    250 ms per group, up to 3000 lines).
  * `PowerPlayFlanger`: the Xbox 360 base game and Title Update 4 attach the Flanger to slot 2 of
    the `Powerplays` group. The Steam game builds the same mixer slot at 0x86C4C3 and sets its
    parameters, but the attach stub (0x7F6A80) only returns the DSP at instance offset 0x25CC.
    With `PowerPlayFlanger=1` and `PATCH_POWERPLAY_FLANGER` (0x2000000), slot 9 of the Flanger
    vtable (0xC1DE54) calls FMOD's `ChannelGroup::addDSP` for the existing DSP. The game keeps
    ownership of the instance and controls its parameters. The stock read callback (0x7F2050)
    processes the block in parts of up to 256 frames (0x7EF950), using up to six effect channels
    and leaving LFE unprocessed. The mixer's `Game` stack sets WetMix to 0, `Voice_3D_PP_Flanger`
    targets 0.6, ordinary Crash Cam targets 1.0, and bridge impact sets it to 0. With `Log=1` the
    hook logs the attach and its FMOD result.
  * `SoundPlacement`: positional sounds, the opponents' engines among them, get a `VoiceDSP` unit (vtable
    0xC32824) that writes the event's stereo signal to the six 5.1 speakers with levels the game computes
    every update from the listener. Unattached, all seven opponent engines play at one level everywhere.
    The unit's read callback (0x8A5470) always processes 256 frames (the console mixer's block) and writes
    six channels; FMOD on PC asks for 1024. The exe's import of `System::createDSP` is wrapped and, for
    that description only, FMOD gets a copy whose read callback runs the game's in 256-frame blocks
    through a 5.1 buffer (sampling only channels 0 and 1, matching the stock `0x8A8520` deinterleave
    where `0x8A5F20` sets all six speaker levels to 1.0). Because the Steam build always initializes
    FMOD in 5.1 (`0x8674F0`), on stereo speakers or headphones Windows/DirectSound folds the placed
    5.1 output down; `VoiceRead`'s `< 6`-channel ITU-R BS.775 fold-down is a defensive guard for
    non-5.1 mixers and does not execute on the Steam build. Units come
    from a pool and stay attached when their voice ends while the event's channel group goes to the next
    voice, so a unit attached to a group takes the group's previous unit off. Units made before the wrap
    (none were seen) stay unattached and are logged once.
* **Sound bank offsets (`SoundBankFix`):** every PC sound bank (FSB4, all MPEG) starts each sound's data on a
  32-byte boundary and its header's data size counts the padding. fmodex.dll 4.28 finds a sound by adding
  up the compressed lengths of the sounds before it without the padding, so it starts reading early by the
  padding of every sound before it: in `music.fsb` all 149 sounds begin with an MPEG frame sync at the padded
  offset, and the result screen tracks (sounds 132 to 142) are read 1970 to 2124 bytes early, inside the
  track before them (CONFIRMED in the file; the effect was first described by Back on Air). The exe's import
  of `System::setFileSystem` is wrapped, and so are the game's file callbacks: when a `.fsb` opens, its header
  is read through the game's reader and, only for exactly this layout (FSB4, every sound MPEG, the sample headers
  filling their block up to at most 31 bytes of padding, the padded lengths adding up to the data size and the
  packed ones not), each compressed length is rounded up to 32
  as FMOD reads the header. The file on disk and the sound data are not changed.
* **Reverb:** in the PC release six of the seven reverb presets (halls, tunnels, under bridges, the
  menu) hold FMOD's stock "Generic" values and the seventh the stock "Bathroom". The Xbox 360
  release's copy of the same event file holds the presets the sound designers set: more reverb and a
  longer decay where it belongs. FMOD's preset lookup in `fmod_event.dll` is wrapped and hands the game
  the console's values (`ConsoleReverb`), read from the Xbox 360 disc. With `Log=1` a by-name preset
  lookup that FMOD fails is logged with FMOD's result (once per name and result, at most 16 lines). In an
  airport race no preset was asked for by name (CONFIRMED), so a `HANGER` preset (named in Back on Air's
  table) has no way in and is not added.

### Camera

* **Field of view:** `FOV` scales the race cameras' (chase and bumper) field of view; the game's
  widening with speed stays on top of it. The game's near plane is 1 m; with a much wider view the
  bottom of the picture looks down at the road right below the camera and would see through it, so
  the near plane moves closer by the same ratio (the camera state's own near plane setting).
* **Ultrawide:** the game keeps the horizontal field of view, so screens wider than 16:9 lose
  picture at the top and bottom. `UltrawideFix=1` widens the race cameras' view until the vertical
  one matches 16:9 again (single screen only).
* **Full ultrawide (experimental):** `UltrawideFix=2` corrects the game instead. Every camera takes
  its horizontal field of view from one 16/9 constant (`0x00BAA9DC`); the screen's real aspect there
  gives every camera the 16:9 picture height: menus, power plays, crash cameras, split screen. The
  Scaleform movies get a centred 16:9 viewport, so menus, HUD, cards and result screens keep their
  proportions. The renderer hooks extend full-width bands and backgrounds to the screen edges,
  including Season car unlocks, decal unlocks and rewards. The matching depth-1 reveal strip mask
  and its screen-wide content extend across the display. Deeper masks and separate root masks for
  art such as track previews and mode icons retain their authored bounds. `UltrawideBlackBackdrop=1`
  fills the dimmed scenery above and below the unlock and reward banners with solid black.
  The loading screen is zoomed with black sides, and the split screen HUD is drawn once per half.
  Bink cinematics keep their aspect with black sides, and 3D player tags keep their proportions.
  Pause and intro letterbox bars are disabled above 16:9 and restored when switching back to 16:9
  or narrower. The last pixel column (wrong in the stock game above 16:9) is copied from its
  neighbour. Tested at 32:9
  (5120x1440, 3840x1080), 24:9 (3840x1440), 21:9 (3440x1440, 2560x1080) and 16:9 (unchanged).
  Steam build only; elsewhere it falls back to 1.
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
  sound is paused meanwhile on the audio thread at the game's next `Event::start` or
  `ChannelGroup::setVolume` call, pausing each top-level category directly under `master` that has
  started a sound (and any that starts while paused), while leaving `master` itself unpaused and
  preserving categories that were already paused. Crashes take it over only in part, see the
  [known limitations](../README.md#known-limitations).
* **Camera shake:** a slider for the race cameras' shake on bumps and hits, and a separate switch for
  the shake from explosions and power plays, both against motion sickness.
* **HUD key:** switches the power bar and the info bar off and on during a race.

### Laps

`LapSelect=1` changes the resolved difficulty action on offline Quick Play Race's track select
(one screen or split screen: the Split Screen entry opens the same `cNewQuickPlay` screen).
The input manager resolves action `0x21` into byte 3 of the UI action packet (0x4D628E).
`cNewQuickPlay::OnInput` (vtable 0xBAFD34, slot 11, 0x47A170) takes that action only when the
stock difficulty check (0x479C70) and locked-track check (0x4866A0) permit it. The game's
bindings work with keyboard, virtual pads, and native DirectInput. Other screens and locked tracks keep their stock input.

The Scaleform movie hooks (vtable 0xC41CF4, slots 18 and 11) observe the prompt bar's
`TEXT_CHOOSE_TRACK` title and the highlighted track. The difficulty button reads
"LAPS (DEFAULT d): n". Its position stays where it settled during the slide-in. The default
comes from `GameLogic/Tracks/<location>/Params/RaceSetupOverlay_<subtrack>.params` through
the game's open and close functions (0x403010 and 0x403080). The ArkLoader read and size
slots point to 0x5739A0 and 0x573A00. The WinFileLoader equivalents are 0x572850 and
0x5727B0. The game keeps its own lap count when the default is unreadable.

The main menu identifies Quick Play by `TEXT_QUICKPLAY` and split screen by `TEXT_SPLIT_SCREEN`. The Quick Play mode list
(vtable 0xBB2360, slot 1, 0x4867B0) provides the internal name `QuickRace`.
The observer takes selected rows only while their owning screen is in mode-select phase
(`+0x240 == 0`). Moving to car select commits a track and requested count. Main menu,
mode select, and a new track selection clear that request.

`cLaps::Reset` (vtable 0xBA8DE8, slot 2, 0x43FF40, `__thiscall(context)`) checks the active setup for the same
track, Quick Race mode, one or two player viewports (+0x9C), and offline non-Season play. It reads the game's
default at 0x43FFA5 or the override byte/count at 0x43FFAF–0x43FFBD. When the requested
value differs from the live stock count, the hook sets the override byte (0xD6B6B5) and
count (0xD090CC) for that call, then restores both. A setup that consumed changed laps is
tracked through results. Restart can use the same committed choice, and Deadline's
separate timer slot 4 remains independent. `LapSelect=0` leaves all counts to the game.

A network race has no lap count of its own either: every PC's reset reads the same game data, and only
the setup kinds 6 and 7 (the multiplayer setups) are then capped at 8 (0x43FFCB–0x43FFEB); nothing in the
network code writes the override pair or the track default (0x441218 sets their initial values, the track
overlay loader writes the default at 0x791DC8). With `SteamLAN=1` the session rules (`src/online.cpp`)
carry the host's pick as `l=` in `glimpse_rules` / `glimpse_own`; every PC sets
`sessionLaps` from them and the same reset hook applies it to a Quick Race network setup while the session
lasts (at most 8, as the game would cap it). A member publishes `glimpse_lapok=1` when its race hooks are in;
the host offers its pick only while every lobby member does, so a member that would keep the game's laps
(an older version, the Russian depot) makes everybody keep them. The race hooks go in with `LapSelect=1` or
`SteamLAN=1`, the lap choice only with `LapSelect=1`. With `SteamLAN=1` the Scaleform `Invoke` slot holds
online.cpp's wrapper first; the menu hook wraps that one in turn.

The stock results update (0x802AF0, vtable 0xC1F83C, slot 3) advances the movie, checks
ready rows, then calls 0x802870. The writer builds `<track>_<sub>_<mode>` from the setup,
normalizes it at 0x530D40 and calls 0x795B60. The store checks the live leaderboard catalogue at
0x788C40 before writing the time or holder. Autosave starts within the same stock call.
Before using changed laps, the mod checks the normalized replacement key ending in
`_LapSelect` against that catalogue. The tagged results update repeats the check and
substitutes `LapSelect.params` at setup +0x988 for that call, then restores the original.
The stock store rejects the verified missing key. The finish card replaces the stock
`TEXT_NEW_PERSONAL_BEST` message with "NO RECORD: LAPS CHANGED" for that race.

LapSelect writes the Quick Play input slot at 0xBAFD60 and the lap reset slot at 0xBA8DF0,
in addition to the mode list (0xBB2364), results update (0xC1F848), and Scaleform slots
(Invoke at 0xC41D3C and SetVariable at 0xC41D20). Build, patch bit, dependencies and slot
signatures are checked before LapSelect activates.
An installation failure rolls back its slots. UiLog and DLC modes can keep their
independently installed wrappers.

The game writes `Laps` under `Local\GlimpseFixIni` and retries a pending choice if the
lock or write fails. The config tool shares the mutex for reading, editing, and replacing
the INI, including backup restore. Restore Backup keeps the current lap choice. Preset undo
restores the earlier count only while Original game's saved zero is present.

### Cutscenes

`CutsceneSkip` makes the season's cutscenes ("Coming up", "Next time", the end sequence), the demo
movies and the boot movies skippable. They are Bink movies: `BinkOpen`, `BinkWait` and `BinkDoFrame`
are wrapped through their import slots. The first press of A, B, X, Y, Start (any pad), Enter or Space shows "Press
again to skip" for 3 seconds; a second press within that time moves the movie to its last frame, so
the game's player ends it as if it had played through. A press held when the movie starts does not
count.

### Post-race Continue timing

On the post-race finish card (`cRaceEnd`, `UI/InGame/RaceEnd.gfx`), `cRaceEnd::Update` (`0x8088C0`,
vtable `0xC208DC` slot 4 at `0xC208EC`) shows the Continue prompt as soon as its timer reaches 2.0 s
(`0xD3FA80`), but the input handler (`0x808940`) ignores Continue until the card reaches state 2.
State 2 is set by the movie's `TransInFinished` callback on `mcMain` frame 200 (~6.6 s at 30 fps);
in the stock `RaceEnd.gfx` (27516 bytes, CRC32 `0x46B24DB4`), `mcMain` only has scripts on frames
2, 28, 67, 77 (`TotalCreditsActive`) and 200 (`Stop` and `TransInFinished`). With `RaceEndTiming=1`
the fix swaps only vtable slot 4 (no game code changed), holds the original update while in state 1
so Continue appears when `TransInFinished` sets state 2, and in offline single-player Quick Play
advances `mcMain` from frames 77–198 to 199 after 3.0 s so the next movie tick fires frame 200
without skipping callbacks. Season plays through its full credits and unlocks; split screen, online
and demo races stay stock. Steam build only (not the Russian depot), gated on the stock
`RaceEnd.gfx` size and CRC32.

## Texture dump

`DumpTextures=1` exports the game's textures at the next start, before the game sets up Direct3D:
a small window says "glimpse_fix: dumping the game's textures, please wait", a worker thread reads
every `.ark` archive under `data`, writes each texture as a DDS file to
`glimpse_textures\<archive path>\`, the key is set back to 0 and the process ends. Only files are read and written; no game code runs and none is changed.

* Archive: a header (`u32 0x10000`, header size), then an index of 20-byte entries (type, offset,
  packed size, size, key), sorted by key. The key is a hash of the lower-case file name with `/`
  (`h = h * 31 + c`, 0x571080); the names themselves are not stored. Entries are stored (type 0) or
  LZSS-packed (type 1: a `u32` size, then a 4 KB window starting at 0xFEE); types 2 and 3 exist in
  the loader but do not occur.
* A texture entry is a 136-byte `SXET` header followed by a complete DDS file (DXT1/3/4/5, ATI1/2,
  uncompressed and float formats, cube maps).
* Files are named `<key>.dds`; each folder's `index.txt` lists name, size, format, mip levels and
  bytes. The sector archives (`*.static.ark`, `*.stream.ark`) hold geometry only.
* Steam build: 42 archives, 551 textures, 573 MB, about 10 seconds.

This is the groundwork for texture packs, which need the original textures and their keys. Loading
replacement textures is not part of this version. The large address aware flag stays for that
reason too: high-resolution textures need address space on top of the streaming pool.

## Online over Steam

The game's network code is Quazal's Net-Z over PRUDP. In LAN mode the host answers search broadcasts
on UDP port 9100 and the players connect to its game port. Quazal's sockets go through a replaceable
transport, its SocketDriver (`[0xD77734]`, taken when its network starts); the fix puts a driver
there that carries the datagrams through `ISteamNetworkingMessages` to the members of a Steam lobby
(every player gets a virtual address 10.x.y.z from the Steam account, a broadcast goes to every
member) and answers Quazal's interface list query with that address through the `WSAIoctl` import
slot. A data write, two import slots (`WSAIoctl`, and `OutputDebugStringA` for the game's debug text
in the log) and the Scaleform movie's `Invoke` vtable slot; no game code.

* **Lobbies:** public lobbies are found by their name (`glimpse_name`, the oldest one wins); friends
  only lobbies through the friends' current game (`GetFriendGamePlayed`). A lobby that Steam no longer
  reports is noticed after 3 s and found or made again; a switch joins the new lobby before it leaves
  the old one.
* **Session rules:** every member publishes its own settings and whether it hosts (lobby member
  data). The host is the player that creates a game (a game port open without a connection to a host)
  or answers a search; a guest's host is the first player it connects to. Until a host is known, the
  Steam lobby owner's settings stand in. The extras follow the host's
  settings; until a guest has them, they are off.
* **Game mode:** the game's lobby screen sets its title to "LOBBY - <mode>" in the game's language;
  the fix reads it from the Scaleform calls (`GFxMovieRoot` vtable slot 18, `Invoke`) and the host
  publishes it for the others in the Steam lobby.
* **DLC tracks** (`[DLC] TracksOnline=1`): the host's game setup is the Net-Z well known object
  `gWellKnownGameSetup` (made at `0x872750`); `0x871C30` writes three bytes into it: the mode (+0x59,
  `[0xD82518]`), the track index (+0x5A, `[0xD82528]`, from race setup +0x9A0 or picked at random by
  `0x872340` from the mode's TrackFiltration list) and two option nibbles (+0x58). A guest copies the index
  (`0x871D40`), and at the race start every player reads it (`0x8717A0`) and looks name, letter and level
  path up in its own track tables (`0x4838B0`). No name or list is sent: 0..14 mean the same everywhere,
  15 and up only with the same DLC tracks, and the host can switch to a DLC track inside a running
  session (Quazal also migrates the host). So the players of one LAN game must have the same DLC tracks
  or none: each member publishes a signature (`glimpse_dlc` member data: 64-bit FNV-1a over the fix
  version, the registered tracks, their four level archives' sizes and CRC-32 of the first MB and the last 64 KB, and
  the merged `track_*.ark`, with DLC cars online also the cars, below; `none` without a track overlay and
  without them, `dlcfs.cpp`), and between two players whose
  signatures differ (for a player with DLC tracks also: not known yet) the driver passes only broadcasts, the game's session
  searches: the host's answer to a search (its game in the list) and all game port traffic are held back
  on both sides. A member of an older release publishes no signature and has no DLC tracks (`none`); the
  signature is published again after every lobby join or create (Steam drops member data on leave). The
  last signature seen per Steam ID is kept while the game runs: a member that leaves the lobby and comes
  back is taken by it until its new member data arrives (and by the new one if it differs), instead of
  being "not known yet" for a second or two, during which a player with DLC tracks would hold back all game
  traffic to it, also in a running race.
* **DLC cars:** a player's car travels by name, not by record index: every player's `cATVNetDOPlayerSetup`
  carries the dataset `cATVNetDSVehicleData` {`mVehicleName` string, `mColour` int32} (dataset 2 at
  +0x80, serialised by `0x87D220`/`0x87D3C0`), filled by `0x883160` from the network manager's car
  (`[0xD64E18]`+0x75C/+0x760, the profile's online car, slot 1) at the session start (`0x8737A0`) and from
  the race setup's car in the lobby (`0x477390`). Each player puts the name into its race setup (player
  entries of 200 bytes at +0x348, the car at +0x40) and looks it up in its own vehicle database
  (`0x51A1E0` -> `0x51BBE0`); a name it lacks gives "Couldn't find vehicle info for %s" and no vehicle
  record. The Placeholder slots the car pack fills therefore do not matter, the names do. The fix's own car
  select hook never lists DLC cars online; with `UnlockAll=1` the game's all-records branch does (no game
  type check), so then the signature also covers the car IDs and every merged overlay (`none` stays `none`
  only without DLC tracks online and without such cars).

## Converted Xbox DLC

The converters in `tools\dlc` turn the user's own Xbox 360 DLC packages into PC-format archives and loose
files (`tools\dlc\README_build.md`; cars `tools\dlc\assemble\README.md`, tracks
`tools\dlc\assemble\README_track.md`, modes `tools\dlc\mode\README.md`, audio `tools\dlc\audio\README.md`).
The DLL's `[DLC]` layer makes the result visible to the game without changing the game folder's files or
any game code byte: everything is data writes, vtable slots and import slots, and every part checks the
build (Steam version 1.0.0.1, not the Russian depot; the overlay merge too) and that the code it relies on is
as expected, and otherwise stays off.

* **Overlay merge** (`src\dlcfs.cpp`, `src\vark.cpp`, `src\iohook.cpp`): the game reads all its resident
  content from `data\resident.ark`, one index of keys (the lower-case path hashed `h*31+c`) with offsets.
  Every `glimpse_dlc\*.ark` is laid over it in name order: the game is shown one archive with a merged
  index (an overlay entry replaces a base entry with the same key, new keys are added) whose data comes
  from the original file and the overlays. The game opens resident.ark with `CreateFileA` and asks for
  its size right away; from then on that handle is served as the merged archive through `ReadFile`,
  `SetFilePointer`, `GetFileSize` and `CloseHandle`.
* **kernel32 import-slot redirect:** the game reads resident.ark before its own import table is filled,
  and its `CreateFileA` slot holds the copy protection's wrapper, which is left alone. So the four
  functions are taken over in kernel32's own import table behind its `jmp [slot]` export thunks, while
  the DLL loads. The same way, `CreateFileA/W` and `FindFirstFileA` send opens and existence checks of
  the DLC tracks' level archives, sound banks and movies (and the modes' movies) to
  `glimpse_dlc\<name>\data\...` when they are not below `data` (all three or none; without them the
  tracks whose files lie there are left out). When the overlays cannot be used after all, every slot
  swapped so far is put back; the DLL puts them back when it unloads, too.
* **Track registration** (`src\dlcfs.cpp`): the game knows its tracks by an index 0..14 through five enum
  tables; each is copied with the DLC tracks appended (Quarry 15, nem_quarry 16) and its descriptor
  pointed at the copy. The quick play screen's movie slot serves the tracks' movies, and their entries in
  the quick play track list are unlocked. The track overlay (`track_*.ark`) is only merged when every
  track it lists will be registered.
* **Modes** (`src\dlcmode.cpp`): the quick play mode list's item update (a vtable slot) unlocks
  Deadline's entry and gives the DLC modes movie ids from 64 on, which the swapped movie slot of the quick
  play screen turns into their movies. **Cars** (`src\dlccars.cpp`): the front-end car select's
  activation slot appends the DLC car records to the screen's own list. **Pickups**
  (`src\dlcpickups.cpp`, `src\dlcrivals.cpp`, `src\pickups.hlsl`): the countdown's update and reset slots
  hold the clock while frozen time is left (and add `DeadlineBonus` with `Pickups=0`), and the boxes are
  drawn by the DLL right after the game's tone mapping; the D3D hooks for them go in only with an overlay
  that brings the mode. Slot 2 of the event objectives, race end and race results screens
  (`cEventObjectives`, `cRaceEnd`, `cRaceResults`) is wrapped to keep the race kind Time Trial, add the
  Xbox's rivals to the race info and write the time left (or DNF) into the end screen and the result rows
  (`dlcrivals.cpp`); slot 4 of the lap timer `cLaps` holds the race time while the clock is frozen. The
  HUD runs through the game's own HUD message queue and the DLC's `DialsInfo.gfx` timer functions, the
  top centre counter is hidden through its movie's `SetVisible`, the DLC's `DLCMenu` bank is loaded on the
  sound thread for the Deadline sounds (`dlcmusic.cpp`), and the *Frozen Time* post presets go onto the
  game's colour cube pass (`postfx.cpp`). See `tools\dlc\mode\README.md` for the mode pack and the Xbox
  rules. **Music** (`src\dlcmusic.cpp`): two
  entries of the sound thread's message table run the game's handlers and then, on Quarry, swap
  `Music/Music` for `DLC3_Music`, alias the event names and set the picker's group.

Every key is described in `docs\INI.md` (`[DLC]`); with `Log=1` each part logs what it found and did.

## Copy protection in detail

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
  slow motion and HUD render switches of the photo mode, the race cameras' FOV, near plane and smoothed
  rotation, the explosion shake switch, AI skill limits,
  the rear-bumper HUD's raster-scale tweakable `0xD1CCC4`), loaded HUD parameters, Scaleform
  `GFxFontCacheManager` texture-config and raster-scale fields, the full screen passes'
  quad template positions, powerplay distance records, the main loop's accumulator, the power bar, the
  AI skill, the cars' chassis velocity, the opponent engine bank slot size (`0xD3DBC0`), the `EventMgr`
  event table (`[0xD91C50]`), event-object pool (`0xD4390C`, allocator flags at `0xD4391C`),
  `CPUCar` sound bank slot lists (`[0xD91C58]`), and audio thread message
  table entries (slots 43 and 51 at `0xD8B5AC` and `0xD8B5CC` for `OpponentEngines`, slot 51 also for
  `SoundPlacement`, slots 2 and 9 for `[DLC] Music` / Deadline),
* swapped vtable slots (motion blur, grading, bloom, depth of field and particle flare passes, AI
  player update, chase and bumper camera update, with the photo mode the other camera classes'
  update, the respot logic's update with the photo mode or `WreckTime`, the Scaleform movie's
  `Display` with `HDR=1`, `SMAA=1` (on by default) and `UltrawideFix=2`, its `SetViewport` with
  `UltrawideFix=2` or an active `HudResolution`, its `Invoke` with `SteamLAN=1`, its `Invoke` and `SetVariable` with
  `LapSelect=1` or `UiLog=1`, the Quick Play mode list's item update (`0xBB2364`) with
  `LapSelect=1` or the DLC modes, Quick Play input (`0xBAFD60`) with `LapSelect=1`, lap reset (`0xBA8DF0`)
  and race results update (`0xC1F848`) with `LapSelect=1` or `SteamLAN=1`,
  `VolumeMeter` slot 9 at `0xC1E028` with `Ducking=1`,
  `Flanger` slot 9 at `0xC1DE54` with `PowerPlayFlanger=1`,
  `VoiceDSP` slot 9 at `0xC32824` with `SoundPlacement=1`,
  and `cRaceResults` slot 16 (`0xC1F87C`), `cSeasonEpisodeSelect` slot 2 (`0xC2101C`) and
  `cInGameMovieState` slot 1 (`0xBA351C`) with `UnlockAll=1` on the Steam build),
  import table entries (`XInputSetState`, Bink's `BinkOpen`/`BinkWait`/`BinkDoFrame`, FMOD's
  `Event::start` with `OpponentEngines=1` (default), `PhotoMode=1` or `Log=1`, `ChannelGroup::setVolume`
  with `OpponentEngines=1`, `PhotoMode=1` or `Log=1`, `System::createDSP` with `SoundPlacement=1`,
  `System::setFileSystem` with `SoundBankFix`, `OpponentEngines`, `Ducking`, `SoundPlacement`,
  `PhotoMode` or `Log` (for the sound-system generation; file callbacks wrapped only with
  `SoundBankFix=1`), `System::setReverbProperties` with `Log=1`, `PeekMessageA` with `VirtualPads=0`,
  and `d3dx9_42.dll!D3DXCreateTextureFromFileInMemory` with `TextureLoadFix=1`),
  read-only strings and constants (prompt bar icon paths, route marker size),
* the Direct3D device's vtable, and calls into the game's own functions (dictionary lookup,
  Scaleform `Invoke`/`SetVariable` for the prompt bar, `SetVisible` for the photo mode's overlays,
  Scaleform font cache reconfiguration `0x413840`/`0x8EDE80`/`0x8EBD60` and the font cache's
  validated release function (`0x8E9B50`, `0x8E9990` or `0x7D8120`) for `HudResolution`,
  the depth of field blur setup) from the input poll, the hooked passes, and Scaleform's
  `SetViewport`. LapSelect uses the file,
  difficulty, track and leaderboard functions listed under Laps. Audio calls include the
  `VoiceDSP` read callback `0x8A5470` from FMOD's mixer thread with `SoundPlacement=1`; the stock
  Flanger read callback `0x7F2050` from that thread with `PowerPlayFlanger=1`; the game's
  file callbacks from FMOD's threads with `SoundBankFix=1`; and on the audio thread the stock message
  43/51 handlers `0x835340`/`0x835620` (51 also with `SoundPlacement=1`), `FileExists` `0x402300`,
  bank load `0x7F2FA0`, racer engine assignment `0x8359A0`, `"LLAudio2"` heap scope `0xAF4D00`/`0xAF48F0`
  and the game allocator `0x530290`/`0x5302D0` with `OpponentEngines=1`. The event pool reserve calls
  `0x5081D0`, which uses its existing allocator virtual slot `0xBA29B4` (target `0x410D00`).

The prompt bar glyphs, the title screen text, the icon size, the 60 fps and the streaming pool were
first done with code patches and each set off exactly this hang, so they were rebuilt this way.
An internal resolution option (`RenderScale`) needed code patches too and set off the same hang,
so it was removed in 1.7.0.

* `[Debug] PatchMask` (bits as in `src/common.h`) switches individual patches off, to find out
  which one causes a side effect. With `Log=1` the log also shows the number of SecuROM VM
  contexts in use whenever it changes; a check that found something shows up as two more
  contexts (`8B03 8AF3`) every ~22 s.

## The Xbox content modpack (Xenon.dll) in detail

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
achievements and decals (`UnlockDecals`). On the Steam build (not the Russian depot), `UnlockAll=1`
also hooks `cRaceResults` slot 16 (`0x00C1F87C`), `cSeasonEpisodeSelect` slot 2 (`0x00C2101C`),
and `cInGameMovieState` slot 1 (`0x00BA351C`) so finishing a career episode's Elite Race opens the
episode-unlock screen against the profile's real episode state and keeps `0x00D6FBA0` cleared until
the movie state begins (without wrapping `cSeasonEpisodeSelect::OnInput`), instead of skipping the
screen and soft-locking on the missing `NT_Episode1.bik` movie. Using the pack is your choice, but
please reproduce problems without it before reporting them.

## Known limitations in detail

* With virtual pads active, the controls screen uses Xbox names only when no other DirectInput
  joystick (e.g. a wheel) is present, since those names are shared by all devices.
* 60 fps: races, AI and physics were checked; not every power play and cutscene was. Set
  `FrameRate=30` if something looks off.
* The photo mode and the HUD key work for player 1 only, the photo mode only on a single screen. In
  split screen the boost, the power bar fill and the handbrake work for both players, each with their
  own pad (Steam build only). Which pad drives which car is learnt from the first steering in each race
  (the car that steers with a pad's stick while the other one does not). A pad whose stick stays centred
  gets a short steer to the left from the fix (about 40 % for 8 frames, only while neither player
  steers, tried again every half second, at most ten times a race). The fix reads each player's steering input as the game reads it
  (`cInGameResources` +0x138, before the start lock that keeps the cars' steering at 0 during the
  countdown), so the pad whose player's input turns left is found before the start; so a player who does
  not steer at the start is found too, and the keys reach the keyboard player. Until then that pad's
  extras stay off. The keys go to the player without a learnt pad, else to player 1. The boost's motion blur
  stays off there, as one blur covers the whole screen. Tested with two pads (`HandbrakeMode=1`) and
  with one player on the keyboard and one on a pad (#16). `HandbrakeMode=2` presses the brake trigger of
  the player's own pad; a player without one (on the keyboard, or no pad, or `VirtualPads=0`) gets the
  drift handbrake (mode 1) with the key instead (#16: the key did nothing before).
* With `VirtualPads=0` the boost, the handbrake, the power bar fill and the photo mode work with
  their keys only (see `VirtualPads` in [INI settings](INI.md)).
* `UltrawideFix=2` is experimental. Checked: single player menus, season screens, races, pre-race
  cards, pause and result screens at 32:9 and 21:9; menus, cards and races at 24:9. Not checked yet:
  split screen, online and LAN menus, power play close-ups, the end-of-season cutscene. A screen whose
  band stops at the 16:9 edge or stretches oddly needs an entry in the shape table in `src/ultrawide.cpp`.
* Photo mode on crashes: once your car is wrecked, the game's crash sequence decides the view.
  On a crash on the road (walls, other cars, power plays) the photo camera stays tied to the car
  and is not fully free. After leaving the track (falls) the game holds a waiting view that no
  camera drives, so the photo camera cannot take it over at all. Both would need changes to the
  game code, which this project does not make (see [Copy protection in detail](#copy-protection-in-detail)). Enter the photo mode before
  the crash happens for a free camera.
* `UnlockDecals` works in single player races only (the game's own unlock runs there), and the
  profile is saved when the race ends: finish it or leave it through the pause menu. The Pure
  decal is reset by the game whenever it looks for Pure saves, so it is shown only while the
  option is set.
* `PopInFix=1` draws somewhat more per frame than the stock visibility lists allow.
* HDR: the game runs in a borderless window instead of exclusive fullscreen, and the Steam overlay may
  not show while it is on. The HUD is told apart from the picture by comparing it with the copy taken
  before the first menu is drawn, so a white HUD element over a white sky gets the highlight gain too.
* `SteamLAN` is experimental; it needs `steam_api.dll`, which is not part of this fix. It runs on both
  supported builds; on the Russian one it is mapped and checked against a dump, not yet played.
* The options new in 2.2.0 (`SMAA`, `CutsceneSkip`, `ShadowResolution`, `Shadows`, `QualityCap`,
  `TextureDetail`) are mapped on the Russian build and checked against a dump, not yet played.
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
* `buildmap.py` — map every fixed address of the source from the supported build to another
  build from two dumps and write its table (`src/build_ru.inc`); it checks the result against the
  other dump (code bytes, swapped vtable slots, every byte and prologue check).
* `dec.sh` + `ghidra_scripts/` — headless Ghidra: decompile functions or ranges, list xrefs.
* `dis.py`, `xref.py`, `strs.py`, `memgrep.py`, `mem.py`, `vmem.py` — quick disassembly, string,
  memory and address space tools; `objscan.py` finds live objects by vtable or pointers to an
  address, `rtti.py` names the class of a vtable.
* `padfeed.py` (with `TestFeed=1`) — scripted virtual pad; `test_race.sh` builds, installs and
  drives into a quick race; `ishot.sh` saves a frame straight from the Direct3D back buffer
  (works in exclusive fullscreen).
* `loopback/loopback.cpp` + `wavstat.py` — record the system mix (WASAPI loopback) and measure
  per-channel levels.
* Opponent-engine listening isolation (local diagnostic recipe, not shipped): to audition `CPUEngine`
  by ear without the player's engine or music masking silent cars or mismatches, resolve the
  `PlayerEngine` and `music` channel groups from the root category on the audio thread in
  `audio_diag.cpp` (`Event::start`), clear both in `AudioDiagProjectsChanged()` and
  `AudioDiagResetState()`, and pin those two groups to `0.0f` in `Hook_GroupSetVolume` while keeping
  `CPUEngine` at neutral `100 %`. Keep this strictly out of shipping builds and INI settings.

### Notes (Steam exe, image base 0x400000)

Addresses of the supported build (`4BE13C66`); in the source they go through `G()`, which gives
the Russian build's place from `src/build_ru.inc`.

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
| `0x00558991` → `0x00410380` | Main-loop call to the input update. Its first `PeekMessageA` call returns to `0x0041039C` via import slot `0x00B9B270`. |
| `0x00D747F4`, `0x00D747F0`, `0x00D199D8` | Main loop accumulator, its tick counter (`Sleep(5)` at 5), "max ticks" tweakable (10) |
| `0x00BA0C14` | 1/30, shared by the main loop, the AI steering controller `0x524AC0`, AI look-ahead grids and data conversions |
| `0x00BC0BC0` | `cAIPlayer` vtable slot of the AI player update `0x51A8B0`; the AI's skill (0–1) at +0x24 |
| `0x00BC0BF8` | `cAIPlayer` vtable slot 17 `0x51A550`, every frame: reads the track section `[[[ai+0x18]]+0x28]+0x54` (NULL after a restart until the next update) |
| `0x00D17044` … `0x00D1705C` | AI catch-up skill limits (initial/final min and max, 8 bytes apart) |
| `0x00D72534` | "SSAI/Debug Cheats/AI Full Power Bar" |
| `0x00D389E0`, `0x00D389E4` | Tweakable "Sectors/Streaming Memory Pool MB" (57), read on level load (`0x7C654F`); its record (vtable `0xBB2498`, +4 variable) |
| `0x00D66B28` | `cInGameResources`: +0xE4 `cGameLogic` (+0x18 player count, +0x1C players); player +0xE8 `cPowerBar` (+0x2C output, +8 power 0–100), +0xAC `cVehicleState` (+0x40 vehicles, +4 + i×4); vehicle +0x584 chassis `hkpRigidBody` (+0x100 forward, +0x110 position, +0x1A0 velocity) |
| `0x00D6FC54`, `0x00D6FBA0`, `0x00D7D978`, `0x00D6F8F0` | UI/All Gamemodes / Episodes / Events / Vehicles Unlocked |
| `0x00D64E40` | `cSeasonManager`: +0 `cSeason` (+0x38 count 12, +0x3C episodes, each +0x80 unlock order `-1` when locked), +8 in season, +0xC current episode (0–11), +0x10 current event, +0x16 episode just completed, +0x20 season completed |
| `0x00C1F87C`, `0x00C2101C`, `0x00BA351C` | `UnlockAll=1` career episode progression vtable slots (Steam build only): `cRaceResults` slot 16 (`0x801EC0`) suspends `0x00D6FBA0` on season episode completion and `cSeasonEpisodeSelect` slot 2 (`0x80BEB0`) holds it cleared until `cInGameMovieState` slot 1 (`0x417370`) restores it (or `ApplyUnlocks` restores it once the season manager no longer reports an episode completion), leaving `cSeasonEpisodeSelect::OnInput` unwrapped; `cInGameMovieState` slot 1 also skips a missing `NT_Episode%d.bik` (only `NT_Episode2..12.bik` exist) if all 12 episodes are already unlocked in the save |
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
| `0x00D6EE78` | Front end's race setup (`0x453300`): +0 game type, +0x9C screens, +0xD0 the two local players' cars, +0xD8 their controllers (the game's own numbering, not the XInput slot); set by `0x78E3E0` / `0x78E240`, read by the player input through `0x78E4C0` |
| `0x00D64E00` | +0x208 `cAnchoviesPC` achievements (vtable `0xC4AAC4`, unlock `0xA17F90`; +0x2C unlocked, +0x38 count 47); ids 41–46 come from ranked online results (`0x802870`) |
| `0x00D71954`, `0x00D11B6C` | "Anchovies/UnlockAnchovy", "UnlockAnchovieID", read by the race update `0x4BF180` |
| `0x00D64E6C` | HasPureSave (decal id 47); decal table `0x00D6FABC` (id, title), 48 entries |
| `0x00B9B43C` | Import table: FMOD `Event::start` |
| `0x00BAA9DC` | 16/9 aspect constant (.rdata) of every camera: FOV conversion `0xA59A51`, aspect getter `0x794390`, frustum size `0x451680`, tripod cameras `0xAAB02D` |
| `0x00C41CF4` | `GFxMovieRoot` vtable: slot 11 `SetVariable` (`0x991010`), 18 `Invoke` (`0x98D2E0`), 20 `SetViewport` (`0x98CDF0`, viewport at +0x38), 31 `Display` (`0x987E60`); +0x28 movie definition (vtable `0xC3D5CC`), +0x80 scale mode, +0x88 visible frame |
| `0x00BC93DC` | Scaleform renderer vtable: slots 7 `SetMatrix`, 14 `SetVertexData`, 17 `DrawIndexedTriList`, 18 `DrawLineStrip`, 26–28 mask begin/end/disable |
| `0x00D66000` | Viewport manager: +0 screen viewports (1 = one player), +8 forced viewport (−1 = none) |
| `0x00D36F54` | Tweakable "LetterBox/LetterBox Amount" (0.118) |
| `0x00D1DB68`, `0x00D1DB78`, `0x00D1DB88` | 3D player tag name width, icon width, icon offset x (per viewport width) |
| `0x00D714D0` | Split screen HUDs: `[+0xCC+4i]` player HUD, +0x12C power bar, +0x128 info bar → +8 dial → +0x78 quad corners at +0x2C |
| `0x00B9B388` | Import table: `BinkClose` |
| `0x00B9B3DC` | Import table: `d3dx9_42.dll!D3DXCreateTextureFromFileInMemory` (thunk `0x00755D54`, sole caller `0x00511880` from `Crayon2::cTextureDataLoadHandler` `0x005069F0`) |
| `0x008674F0` | FMOD init: DirectSound, `setSpeakerMode(5POINT1)` |

Actions: `0x29` throttle, `0x2A` brake, `0x1D`–`0x2C` the 16 console buttons, `0x03`–`0x06` menu
up/down/left/right, `0x0A`–`0x0D` menu Y/A/X/B.

## Original DLC personal bests (Steam build 385319)

`[DLC] PersonalBests=1` enables the local catalogue below. The Xbox base
and Steam base catalogues have 40 boards. The table has space for 100 realized
records. The 18 extra definitions do not reserve rows.

| Xbox source | Track keys | Mode suffixes | Score |
|---|---|---|---|
| Deadline Pack | `AIRPORT_TEST_03_A`, `DOCKS_A/B/C`, `DOWNTOWN_A/B/C`, `GRAVEYARD_A/B`, `POWERPLANT_A/B` | `TIMETRIALPLUS` | Time |
| Onslaught | `QUARRY_A` | `QUICKRACE`, `TIMETRIAL`, `HELICOPTERREVENGE`, `TIMETRIALPLUS` | Time |
| Onslaught | `QUARRY_A` | `HELICOPTERSURVIVAL` | Points |
| Survival At The Rock | `NEM_QUARRY_A` | `NEMESIS`, `NEMESISMULTI` | Points |

**Confirmed from local data.** The three packs' loose leaderboard definitions
supply the keys and score types. The converted TrackFiltration gives Quarry the
ordinary Race, Detonator, Air Strike and Air Revenge rules. Onslaught specifies
three laps for Race. The DLL checks the actual target in cLaps output +0xC.
The PC converts Deadline to a single-player countdown Time Trial with an
implementation of the TU4 pickup logic. Each eligible Deadline track must have
the original pickup bytes and start countdown. An increased start or
pickups-disabled run can still finish under its own rules. SteamLAN with
`TracksOnline=1` and matching DLC supports multiplayer Quarry Race and Minepit
Park Survival. Original LAN uses the same committed multiplayer mode key. The
local viewport's car ID comes from setup +0xD0. The Xbox DLC catalogues contain
no Elimination, King of the Air or Survival Race boards. The PC conversion
exposes Survival Race in split screen. The single-viewport board eligibility
keeps split-screen results out of the native record table. DLC conversion
continues to exclude the Xbox network leaderboard XML.

**Confirmed from the Xbox TU4 image.** Its results handler writes
`100000000 - clockUsedCentiseconds` at 0x882E6768–0x882E6778. The writer at
0x8848CF38 keeps the unsigned maximum of the old and new score. HUD code at
0x8825BE38 decodes clock used and displays the difference from the initial
countdown. The Quick Play track-card updater at 0x882948E0 reads a saved record
using the selected track, subtrack and game mode (0x8827C578). It formats elapsed
time, grouped points or Deadline time remaining before the card's selected
animation (0x88294FBC). See [TU4 Deadline rules](../tools/dlc/mode/TU4_RULES.md).

**Steam implementation.** cRaceResults vtable 0xC1F83C slot 3 wraps 0x802AF0.
The hook checks the same pending and populated flags as 0x802B69–0x802B9D,
matches the local viewport against both the result's car ID and its native record
collection pointer, and rejects DNF.
Deadline's existing results display converts the cRecordTime to time left.
The writer inverts that conversion only for a converted record in the current
race. The rest of the native update and autosave runs through the original
function. The hook wraps LapSelect's update when both features are active and
independently checks the actual Quarry lap count.

The local catalogue uses the native case-sensitive board hash from 0x530450
and stores ordinary rows in the table at `[0xD64E38]+0x2E0`. It bypasses the
network catalogue and shares the native serializer at 0x419DC0/0x419F30. The
four written fields are hash +0, 64-bit score +0x30, and the two validity flags
+0x38 and +0x1658. A free row needs both zero hash and zero score and both
invalid flags. Existing rows are compared by score. Registered PC boards own
their hashes. The save contains the same `LH%d` and `RS%d` fields in all
configurations. There is no extension in Controls.

The pre-render cRaceEnd hook at vtable 0xC208DC slot 2 (0x808600) sets the
native personal-best flag when a single-player finish beats an existing record. The Deadline racing HUD
uses the game's `TEXT_BEST` text and time element through the existing DialsInfo
setters 0x560600 and 0x560570. The INI switch and PatchMask bit 0x10000000 gate
the feature. The integration checks the Steam signatures before enabling
writes.

The solo Quick Play track card reads that same table. The existing DLC track
list wrapper (vtable 0xBB224C slot 1, 0x485C30) matches the selected item to the
game's board key builder 0x485A70 and handles the selected byte on the
`default` card widget. It uses the native track and mode lock readers 0x4851C0
and 0x479670 (tail call 0x486F40) and the NEW badge check 0x772CB0, whose
comparison uses 0x530970, 0x530640 and 0x5312F0. Within that item update, the
existing GFxMovieRoot Invoke hook (vtable 0xC41CF4 slot 18, also used
by SteamLAN) supplies the text and visibility arguments for the same movie's
`PlaySelectedAnim` at 0x486225. The update is synchronous through 0x536AC0.
Deadline uses the original countdown and requires the original pickup data.
The menu issues no record writes and uses the game's personal-best label.
