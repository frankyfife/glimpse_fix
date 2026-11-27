# Deadline (TimeTrialPlus) and the DLC loader in the Xbox 360 TU4 module

Source: SPLITSECOND1.DLL with title update 4 applied offline (`tools\dlc\xex\xex2.py patch`, version 1 -> 0x401,
image 0xFD0000 at 0x88000000), Ghidra project `scratchpad\dlc\ghidra_xbox_tu4` (SplitSecondX360TU4) plus capstone
listings (`tools\dlc\xex\ppc`). All addresses are in the patched image. Read from code unless marked *inferred*.
Mode type enum (string table 0x88ECB0D0): 0 Race, 1 Elimination, 2 Time Trial, 3 Survival, 4 Air Strike,
5 Air Revenge, 6 Survival Race, 7 King of the Air, **8 Time Trial Plus**, 9 Other. Every Deadline branch tests
`FUN_88482440(DAT_88f1a630) == 8`.

## 1. Data and tweakables

| name | address (value) | default / range | Deadline value |
|---|---|---|---|
| /RaceSetup/Logic/Pickup `Enabled` | 0x88F21145 (u8) | false | true (mode params) |
| `Freeze pickup time bonus` | 0x88EC723C (f32) | 1.0, 0..5 | 0.5 |
| `Pickup Drift Multiplier` | 0x88EC7240 | 2.0, 0..5 | 4.0 |
| `Penalty pickup time` | 0x88EC7244 | 1.0, 0..5 | 0 |
| RaceSetupOverlay `TimeTrialPlusCountdown` | 0x88ECB654 | 60, 0..120 | 60-70 per track |
| RaceSetupOverlay/TimeTrialPlus/`<Rival>Time` (Hammer, Raptor, Vixen, LiveWire, Torpedo, Brawn, Rigg) | 0x88ECB65C..0x88ECB68C step 8 | 1.0, 0..120 | per track |
| debug `Pickups/Render/Draw Dist`, `Fade Dist` | 0x88EC5710 / 0x88EC5718 | 100 / 75 | |
| debug `Pickups/Render Shadows` | 0x88EC5720 | true | |
| debug `Pickups/<colour>/enable` | green 0x88EC5730, yellow 0x5750, red 0x5740, white 0x5760, black 0x5770 | true | |
| debug `Pickups/<colour>/value` | green +1, red -1, yellow/white/black +1 (-5..5) | | **no reader in code** |
| debug `Pickups/Drift Pickups On`, `Pickups/Reset`, `Pickups/DebugRender Boxes` | 0x88F1F4CC, 0x88F1F4C4, - | | Reset is polled in 0x882A9E50 |

Registration: pickup params 0x8838F960, rival/countdown 0x88E76200.., pickup debug 0x88E55A60..0x88E55EA8.

## 2. Pickup boxes

- **Load** (race setup 0x8823C870 -> 0x882AA4E0, only when mode == 8): file
  `Resident_DLC_Redirect|Environments/Levels/<Level>/Subtracks/<Sub>/Pickups/<Level>.pkps` (XML
  `<Pickup pickup_colour node_id group_node_id><transform Row0..Row3>`). All 11 Deadline files match this name
  (ARK key = h*31+c of the lower-case path): docks A/B/C 36/34/36, Downtown A/B/C 60/56/43, Powerplant A/B 59/49,
  Graveyard A/B 38/48, airport_test_03 A 37 = 496, all green, one group per file. Manager 0xA4 bytes
  (0x882A9BD8), pickup array initial capacity 0x800. Per colour it loads
  `Resident_DLC_Redirect|Generic/Models/<Colour>PickupBox/<Colour>PickupBox` .fxc/.shaders/.model
  (colour order green, yellow, red, white, black; name table 0x88EC56D8, lookup 0x882A8858, unknown -> 5 none).
- **Pickup object** (0xC0, ctor 0x882A8FA0 -> 0x882A8D58, vtable 0x88018B6C): +0xA0 node_id, +0xA4 group_node_id,
  +0xA8 collected, +0xAC colour, +0xB0 near-zone flag, +0x10 collision OBB (0x50: transform + half extents).
  Half extents = 0.5 * (per-axis value from the transform) + 0.25, i.e. *inferred* 0.75 m (1.5 m cube) for the
  unit-row transforms in the files (VMX128 code, not fully decoded).
- **Active** (slot 4, 0x882A88C0): not collected and the colour's enable tweakable set.
- **Per frame** (0x882A9E50, from race update 0x8824CA60; counters reset first by 0x882A8CF0):
  1. external events: event[0] -> slot 2 (0x882A8998, collect all or the matching group_node_id), event[1] ->
     slot 1 (0x882A8970, re-arm all or the group) (*source of these events not traced*);
  2. only while the race state == 6 (racing), for each uncollected pickup:
     - slot 9 (0x882A8A80): active and OBB(player vehicle box at vehicle+0x2B80) intersects the pickup OBB
       (0x888C48B0, SAT over 3 axes) -> slot 7 (0x882A8A30): green -> counter[0]++, red -> counter[1]++,
       then collected = 1 (yellow/white/black are only hidden);
     - slot 10 (0x882A8AF0, green only): distance from the player to the box centre < 10 m sets the near flag;
       leaving to > 15 m with the flag still set (not collected) returns 1 -> counter[2]++ ("missed pickup").
- **Pickup logic** (class "Pickup", ctor 0x883815F0, vtable 0x88040408, update 0x88389050, only if `Enabled`):
  `frozen += green * FreezeBonus * (player drifting ? DriftMultiplier : 1)` and
  `penalty += red * PenaltyTime` (0x88381680; drifting = byte +0x8A of the drift component at that frame).
  Then: `if penalty > 0: penalty -= dt, delta = +dt`; `if frozen > 0: frozen -= dt, delta = -dt`. Output state
  (logic+0x2C): +4 delta, +8 picked this frame, +9 picked while drifting, +0x0C frozen remaining, +0x10 previous
  frozen, +0x14 missed this frame. Freeze time stacks (each pickup adds).
- **Effect on the countdown** (countdown logic update 0x8837F2D8): `remaining -= dt`, but dt = 0 when the pickup
  delta < 0, i.e. **the clock is stopped while frozen time > 0**. A positive delta (red/penalty) is ignored by
  the countdown, so red pickups have no clock effect in TU4 (Deadline sets the penalty to 0 and has no red boxes
  anyway). Clamp 0..3600. Deadline numbers: plain hit 0.5 s stopped, hit while drifting 0.5 * 4 = 2.0 s (matches
  the two hint texts TEXT_TIMETRIALPLUS_OBJECTIVE1/2, which name 0.5 s for a plain hit and 2.0 s for a hit in a
  drift; the texts themselves are game data and not quoted here).
- **Countdown start**: countdown reset 0x88379E10 and update use `TimeTrialPlusCountdown` instead of
  `Initial Countdown` when mode == 8 (the PC stage-1 mode puts the same value into `Initial Countdown`).

- **Look** (render 0x882AA010, from 0x882AAB78 for the shadow pass): each pickup not collected (+0xA8) and active
  (slot 3) passes the frustum test 0x882A8780 and is drawn with the transform at +0x20 (set by slot 8, 0x882A9020,
  from the file's rows; +0x60 = the previous transform for the motion vectors, `gbuffer_MotionID` 2). No time-based
  rotation, bobbing or scale; a collected box is simply no longer drawn. Draw/Fade Dist go to the constant setter
  0x882A9200 (main shader 0x58f57228 reads no fade). The only animation is the shader's UV slide show (7 frames at
  10 fps). Model and shader: `pickupmodel.py`, README "Pickup model".

## 3. HUD, sound, hints (0x882609A8, Deadline only)

Events from the frozen value (prev = state+0x10, now = state+0x0C):

| condition | event | HUD timer (0x8834C4C0, switch event-9) | sound (0x8833E098) | HUD text (0x8834D428, switch event-3) |
|---|---|---|---|---|
| prev <= 0 < now, now <= 1.0 | 0xA0 + 0x9E | SetTimerToStoppedColor (0x8834A098) | DLCMenu/TrialPlus/Pickup | TEXT_TIMETRIALPLUS_PICKUP, 0.5 s |
| prev <= 0 < now, now > 1.0 | 0xA1 + 0x9F | SetTimerToStoppedColorLong (0x8834A0E0) | .../PickupWithDrift | TEXT_TIMETRIALPLUS_DRIFTPICKUP, 0.5 s |
| prev > 0, now > prev, now - 1 <= prev | 0xA2 + 0x9E | PulseFrozenTimer (timer+0x208) | .../Pulse | PICKUP text |
| prev > 0, now - 1 > prev | 0xA3 + 0x9F | PulseFrozenTimer | .../PulseWithDrift | DRIFTPICKUP text |
| prev > 0 >= now | 0xA4 | SetTimerToRunningColor (0x8834A050) | .../FreezeExpired | - |

Only sent when the HUD has its timer object (HUD+0x104). Hints (3 s cooldown, at most once each):
0x9C when (not yet shown and race value > 3.0) or 3 missed pickups in a row; 0x9D (TEXT_TIMETRIALPLUS_OBJECTIVE2,
4 s) after 0x9C, or after 3 non-drift pickups in a row. Event 0x9C has no case in the HUD text handler
(OBJECTIVE1 is only in the dictionary). Counters kept for stats: +0x360 pickups, +0x364 drift pickups,
+0x368 misses. Post presets "Frozen Time Heavy/Subtle": two more states of the post process database (table
0x88F29A10, 0x4C each: Subtle index 20, Heavy 21; the PC's table 0xD6FCB8 has the base game's 36), registered by
0x88E78A58, debug ForceFrozenTime*; their parameters come with the Deadline package as
`PostProcessDatabase/Frozen_Time_Heavy.params` / `_Subtle.params` (the loader 0x884D3308 reads
`Resident_DLC_Redirect|PostProcessDatabase/<name, spaces as _>.params` besides the resident and per-level PPD files):
fade in 0, fade out 1.01 / 0.96 s; overwrite the colour cube (Enabled false, Blending 0, Input Blending 1) and the
vignette (colour 0 0.776 1; Heavy inner 2.03 outer 4.97 U 6.26 V 6.07, Subtle 1.82 / 6.37 / 6.49 / 6.22); nothing
else (gradient, bloom, tone mapping not). The selecting code was not traced (*inferred*: Heavy for the drift freeze,
Subtle for a plain one; the HUD events 0xA0..0xA4 above have no other handler found).

Details (read 2026-11-21 for the PC HUD parity, `README.md` "Deadline's HUD"):
- Timer element calls (0x8834A098 / 0x8834A0E0 / 0x8834A050, as-objects at +0x1B8 / +0x1E0 / +0x190 of the large
  timer element, PulseFrozenTimer at +0x208): DialsInfo.gfx functions without arguments; the element keeps a
  "running colour" flag at +0x240 (stopped colours only from running, running only from stopped).
- Texts (0x8834D428, switch on event - 3): events 0x9E / 0x9F -> 0x88342888(0.5 s, hud, text, id = the event,
  teaching 0, type 1, style 0) -> 0x8833E7B8, the message queue (an existing message with the same id gets the new
  text); event 0x9D -> 0x883427E0(3 + 1 = 4 s, hud, OBJECTIVE2, id, 1, 1): clears the queue, a teaching message.
  The texts come from the dictionary (0x8863CCD0). The PC has the same functions: 0x561E10, 0x561DB0, 0x5619E0 on
  cHud+0x28C (the PC debug tweakables `UI/HUD/Send Fake Positive/Negative/Neutral Message` use type/style 1/0, 2/2,
  3/1: style 0 is the positive message with the cyan bars, 1 the neutral one).
- Hint inputs: the race value is `[game logic]+8` compared with 3.0; the cooldown HUD+0x344 starts at 3.0;
  +0x36C counts missed boxes in a row (reset by a pickup), +0x370 pickups in a row without a drift (reset by a drift
  pickup); hint 1 resets +0x36C, hint 2 resets +0x370.
- Last seconds (0x88239530, from the per-frame audio update 0x88244B90, every mode with an enabled countdown, up to
  two players): when the countdown passes a whole second 5..1 (prev +0x10 above it, now +0x0C at or below), 2D sound
  `DLCMenu/TrialPlus/<n>`. The bank exists only in Deadline: the level audio load 0x8857F188 compares the mode name
  (KingOfTheAir, NemesisRace, Nemesis, Helicopter, TimeTrialPlus) and for TimeTrialPlus loads `Menu/DLCMenu` into the
  memory group CPUCar (0x8857BAD8). DLCMenu: 10 events (TrialPlus: Pickup, PickupWithDrift, Pulse, PulseWithDrift,
  FreezeExpired, 5..1), 7 waves (pickup_normal2, pickup_drift6, 1..5).
- HUD movies: TU4 still creates `TimerOverlay.gfx` (0x88341C58, layer 0x5A) and loads `UI\HUD\DialsInfo.gfx`; the
  DLC packages (Deadline and Onslaught, the same file) carry the new DialsInfo.gfx with the four timer functions.

## 4. Rivals, results, restart

- Results/standings 0x882E7C58 (= PC 0x7D6770, the pre-race race info builder): when the event kind (+0x194C) == 0xB
  it appends THE HAMMER, RAPTOR, VIXEN, LIVE WIRE, TORPEDO, BRAWN, RIGG (literal names, .rdata 0x88020D78..) with
  time `(TimeTrialPlusCountdown - <Rival>Time) * 100` (hundredths; 0x882D42E0 = PC 0x7D5730, flag 1, type 0). So a
  rival value is the clock used and `<Rival>Time` the **time left on the clock** when that rival finishes; Rigg
  (about 4-8 s) is the easiest. At the finish 0x882E8980 (= PC 0x799E50) treats kind 0xB as kind 1: the player's
  value is the race time record x 100, which the lap timer 0x88380670 does not advance while the clock is frozen
  (dt = 0 when the pickup delta < 0, as the countdown): the clock used too. Rows: up to 8 entries, rivals in the
  dummy player slots ('Fill remaining slots with dummy players'). Shown as time left: the end screen 0x882D25B8 and
  the rows 0x882E5F78 print `(int)Countdown * 100 - value` (`TEXT_DNF` when not below); the end screen hides the
  position for 0xB as for 1 (0x882DA1D0). A row's DNF also when the countdown output's time left <= 0
  (0x882E8E60).
- **Best time**: the results writer at 0x882E6588 requires one viewport (0x882E66B0), reads the selected
  row's cRecordTime hundredths through 0x88375AD8, and computes `score = 100000000 - clock used` at
  0x882E6768. It skips rows with the DNF flag at +0x323. The native writer at 0x8848CF38 keeps the higher
  unsigned score (+0x30), or takes a free row when this board has no record. The HUD reader at 0x8825BBF8
  takes the mode-8 branch at 0x8825BE38. It decodes `used = 100000000 - score`, verifies
  `0 < used < (int)Countdown * 100`, and shows `(int)Countdown * 100 - used`. The finish card at
  0x882D2714 compares the new clock used against that decoded old time. The DNF builder at 0x882E8FC8
  marks the row DNF when the remaining countdown reaches zero.
- Restart (0x882499A8) calls the pickup reset 0x882A9DB8: every pickup slot 5 (clear near flag, collected = 0);
  mode 8 re-arms all pickups, other modes hide green/yellow/red (white/black stay). Logic resets: pickup logic
  slot 2 (0x88376640) frozen = penalty = 0 and clears the state; countdown reset 0x88379E10 back to the start value.

## 5. DLC loader (boot 0x88228BE0)

- **Enumeration** 0x885EF6B8 (thread object 0x885F04C0, global 0x88F1A60C): XContent type 2 on device 0xFE and
  up to 4 devices, each mounted as `DLC_%02x` (`%s:\`); package table at +0x10, 0x4C per entry, **32 entries**
  (no bound check seen). License: XContent license mask, else `DLCLicense.txt` then `DLCInfo.txt`
  (0x885EE918), override `DLC/DebugLicenseMask` (0x88F400F8). Packages with the same name hash merge their masks
  and the duplicate is unmounted.
- **DLCInfo.txt** (max 0x100 bytes, separators space/tab/CR/LF/VT/FF): `1 <VEHICLE|LEVEL|GAMEMODE> <name>` (the
  first number must be 1; type 0/1/2 at entry+0x38; name CRC32 table 0x8811DF70, init -1, no final xor, at
  +0x3C), then `LICENSEBITS <n>` -> mask at +0x40/+0x44. Entry with mask 0 counts as not owned.
- **TrackDescription.txt** (0x885EF600 -> 0x885EEF18), per licensed package: `<index> <Level> <Sub> <path>
  <movie>` -> 0x88633210. TU4 replaced the base game's static 15-track tables (base 0x88E91300..) by a dynamic
  registry 0x88633958: **25 slots** of 0x270 bytes, 15 built in (index 0..14, airport_test_03 .. ), count
  0x88F41E28; extra tracks beyond 25 are dropped silently. Onslaught registers `17 Quarry A`.
- **AudioBlacklist.txt** per licensed package (0x885EF0A8) -> `Audio|`.
- **Vehicles** (0x885673B8): `Resident(noassert)|Vehicles/VehicleDatabase.xml`, then for every licensed VEHICLE
  package `<mount>(noassert)|Vehicles/VehicleDatabase.xml` (0x885670C0). Same Car ID replaces the entry, new IDs
  append. **Capacity 256 entries in base and TU4** (base 0x8853BBF0 alloc 0x1BC00 = 256 x 0x1BC; TU4 0x885674A8
  alloc 0x1E400 = 256 x 0x1E4, no bound check). The TU4 entry is 0x28 bytes longer (mount name first, ID at
  +0x28, `DLCRestricted` +0x1C9, available +0x1CA = (mask bit0 and not restricted) or bit1, shown +0x1CB =
  available or (restricted and bit0)). DLC unlock flags `DLCUnlocks%02d/isUnlocked`: 16 in base and TU4. No
  separate car-select array limit was found (the lists iterate the vehicle DB); the TU4 car select itself was not
  traced.
- **TrackFiltration.xml** GameMode (0x884B8E88): Name, SplitScreenName, MultiplayerName, `DLCPack` (default
  empty), `DLCGameModeIndex` (int, default -1), Tracks/Track; Track (0x884B4590): Name, SubTrack, DLCPack.
  0x884B6118 / 0x884B6358 hide entries whose DLCPack is not a licensed package.
- **Race-time archives** (0x8822BD40 -> 0x885F0150): `<levelpack>|RESIDENT_DLC.ARK` as `Resident_DLC_Level`,
  `<modepack>|RESIDENT_DLC.ARK` as `Resident_DLC_Gamemode` (skipped if the same pack), and the redirect archive
  `Resident_DLC_Redirect` (0x428 bytes, max 3 entries: Resident, level pack, mode pack). Lookup 0x885EF518 goes
  from the last entry back: **mode pack, then level pack, then base Resident**. Messages "DLC level/mode selected
  but unable to find the mounted name!" when a pack is missing.

## For the PC implementation

- Logic to rebuild: load the .pkps for (level, subtrack) when the mode is Deadline; per frame while racing test
  the player OBB against 1.5 m boxes; green hit adds 0.5 s (2.0 s if drifting) of frozen time; stop the countdown
  while frozen > 0; HUD stopped/pulse/running colour and the five sounds from section 3; reset on restart.
- Rival standings = countdown minus rival value. The PC enum has no type 8; stage 1 keeps `Type` = Time Trial,
  so the pickup layer must key on the mode name, not the type.
- Capacities that matter: 256 vehicle DB entries (the PC's too), 25 track slots on TU4 (the PC has the base's
  static 15), 3 archives in the redirect, 32 DLC packages.

## Confidence and open points

High: patch, tweakable addresses/defaults, freeze formula, countdown stop, HUD/sound event mapping, loader file
formats, vehicle DB 256, redirect order. The personal-best writer and display are confirmed by the instruction trace above. Medium: collision box size (VMX128). Answered since: the PC's vehicle DB also holds 256 entries; the race value compared with 3.0 in the hint
logic is the run's racing time in seconds (the hints come after 3 s of racing; `src\dlcpickups.cpp` HudFrame);
the post presets are implemented as *Frozen Time Heavy* for a freeze from a drift hit and *Frozen Time Subtle* for
a plain one (`[DLC] PickupVignette`). Open: who raises the group show/hide events; hint 1 display; car-select UI
limits on TU4.
