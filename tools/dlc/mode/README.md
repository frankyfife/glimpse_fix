# mode: the Xbox DLC game mode Deadline (TimeTrialPlus) on the PC, stages 1 and 2

`deadline_mode.py` adds Deadline to a PC overlay archive as a data-only countdown time trial (stage 1), plus the
11 pickup files of the green boxes (stage 2 data). The boxes themselves are code in the DLL (`src/dlcpickups.cpp`,
`[DLC] Pickups=1`, see "Stage 2" below). Offline only, `python -I`. Nothing here starts the game, writes to the game
folder or touches a save.

    python -I -X utf8 deadline_mode.py [--base <overlay.ark>] [--out <test.ark>] [--pc <PC resident.ark>]
        [--dlc <Deadline package dir>] [--extra-seconds N] [--no-per-track] [--no-pickups]

Defaults: base `scratchpad\dlc\overlay\deadline.ark` (the car pack, read only), out
`scratchpad\dlc\overlay\deadline_mode_test.ark`, `G:\...\SplitSecond\data\resident.ark` (read only) and
`scratchpad\dlc\x_deadline`. The script refuses to write `deadline.ark`, `deadline_v2.ark`, the base archive, or
anything on `G:` or in the game folder. It takes about 2 s, sets `sys.dont_write_bytecode` and exits with 1 on the
first failed check.

The test archive holds every entry of the base plus the mode. Install it instead of `deadline.ark`, not next to it
(a later `glimpse_dlc\*.ark` replaces the keys of an earlier one).

## For the pack build

    sys.path.insert(0, <tools\dlc\mode>)
    import deadline_mode
    files = deadline_mode.apply(files, pc_ark, dlc_dir)          # pc_ark: arkio.Ark or a path
    deadline_mode.check(files_before, files, pc_ark, dlc_dir)    # optional, raises ModeError

`apply()` takes the overlay entries `{key: bytes}` the build has produced and returns a new dict; the input is not
changed. Every shared file is read from `files[key]` if it is there (for example the car pack's `directory.txt` and
dictionaries), else from the PC resident.ark. Call it once, after the cars: a second call stops because the mode
file exists. Options: `extra_seconds` (added to every start time, default 0 = the Xbox values), `per_track`
(default True), `log` (default `print`), `mode_icon` (default True: the 3 icon files, see "Mode icon").
`mode_paths(dlc_dir, per_track)` lists the archive paths it writes.

The pack build does this with `--mode`:

    python -I -X utf8 ..\assemble\build_pack.py --out <scratchpad>\dlc\overlay\deadline_v3.ark --mode
        [--mode-extra-seconds N] [--mode-no-per-track]

`--mode-extra-seconds` defaults to 0 (since stage 2): the pack carries the Xbox start times and works with and
without the boxes; with `[DLC] Pickups=0` the DLL adds `[DLC] DeadlineBonus` (default 50 s) at run time. The
`deadline_v3` build described here was made with the old default of 50.

The car pack is built and checked as before under `deadline_v3_nomode.ark` (byte-identical to `deadline_v2.ark`),
then `apply(..., extra_seconds=50)` goes on top and the result is written to `deadline_v3.ark`; `check()`, the
archive layout, a read-back and the virtual resident.ark read go into `deadline_v3.report.txt`, the mode's entries
into `deadline_v3.manifest.tsv` (car `mode`). The track (`../assemble/assemble_track.py --base deadline_v3.ark`)
merges its lines into the pack's `directory.txt`, `TrackFiltration.xml` and dictionaries and keeps the mode's.

## What goes in

| entry | source | change |
|---|---|---|
| `GameLogic/GameModes/TimeTrialPlus.params` (new) | DLC `RESIDENT_DLC.ARK` | the 78 keys no PC mode file has are left out; `Type` `'Time Trial Plus'` -> `'Time Trial'`; every other line as on the Xbox |
| `GameModeHUDs/TimeTrialPlus.params` (new) | PC `GameModeHUDs/TimeTrial.params` | the 5 `'Is Timer Using Countdown'` values `true`; the result is byte-identical to the DLC file |
| `directory.txt` | pack or PC | `TimeTrialPlus.params` inserted in GAMEMODES (sorted, after `TimeTrialNoPowerplays.params`) |
| `UI/Logic/TrackFiltration.xml` | PC | the DLC's `PlayableGameMode` line after Elimination (SinglePlayer only) and the DLC's `GameMode` element (11 tracks) before `</GameModes>`, with one comment line |
| `GameLogic/Tracks/<track>/Params/RaceSetupOverlay_<sub>.params` (11) | PC | one block appended: `/RaceSetup/Logic/Countdown` `'Initial Countdown'` = the Xbox `TimeTrialPlusCountdown` of that track; then the Xbox overlay's `/RaceSetupOverlay/TimeTrialPlus` section (the 7 `<Rival>Time` lines) and its `/RaceSetupOverlay` `TimeTrialPlusCountdown` line as they are (the PC's parser drops these keys; the DLL reads them for the rival standings, "Rival standings" below; Quarry's converted overlay has them already) |
| `glimpse/deadline/RivalNames.txt` (new) | TU4 table (`tu4text.py`, `--tu4-texts`) | the 7 rival names (`HammerTime=THE HAMMER` ...); only with a TU4 table that has them, else left out (the DLL makes the names from the keys) |
| `PostProcessDatabase/Frozen_Time_Heavy.params`, `_Subtle.params` (new) | DLC `RESIDENT_DLC.ARK` | none (byte-identical, the Xbox's path): TU4's two "Frozen Time" post presets; read only by the DLL with `[DLC] Pickups=1` (the PC's post process database has 36 fixed states and never asks for them). Left out with `--no-pickups` |
| `dictionary/<lang>_win32.dct` (6) | pack or PC | `TEXT_GAMEMODE_NAME_TimeTrialPlus` and `TEXT_GAMEMODE_TimeTrialPlus_HELP_LINE1..5` via `../ui/dictadd.py add_checked` |
| `Environments/Levels/<track>/Subtracks/<sub>/Pickups/<track>.pkps` (11, new) | DLC `RESIDENT_DLC.ARK` | none (byte-identical, the Xbox's path); read only by the DLL with `[DLC] Pickups=1`, the game never asks for them. `--no-pickups` / `apply(..., pickups=False)` leaves them out |
| `glimpse/pickups/GreenPickupBox.bin` (new) | DLC `RESIDENT_DLC.ARK` `Generic/Models/GreenPickupBox/*` | converted by `pickupmodel.py` into our own blob (below); read only by the DLL with `[DLC] Pickups=1`. `--no-pickup-model` / `apply(..., pickup_model=False)` leaves it out (the DLL then draws cubes) |
| `UI/HUD/DialsInfo.gfx` (replaced) | DLC `UI/HUD/DIALSINFO.GFX` (loose, the same in Deadline and Onslaught) | as is: the TU4 HUD movie, the PC's (= the Xbox base game's) re-exported with the timer functions Deadline calls (`SetTimerToStoppedColor(Long)`, `SetTimerToRunningColor`, `PulseFrozenTimer`) and King of the Air's waves; for every mode. Checked: the same 10 external images and 2 imports as the PC movie, every name of the PC movie's code. `--no-dlc-hud` / `dlc_hud=False` keeps the PC's |
| `Audio/Release/Menu/DLCMenu.fev`, `.fsb` (new) | DLC `AUDIO/RELEASE/MENU/DLCMENU.*` | converted by `../audio/dlcaudio.py` (XMA -> Layer II; `build_all.py` runs it and passes the folder as `build_pack.py --mode-audio` / `apply(..., menu_audio=<dir>)`); the DLL loads `Menu/DLCMenu` and plays its `TrialPlus` events. Without the folder they are left out (no sounds) |

Mode params: the result differs from the PC `TimeTrial.params` (over the keys both have) in exactly Countdown
`Enabled` = true, `'Initial Countdown'` = 30, `'End Condition Countdown'` = true, `'Start Timer On Lap Start'` =
false, Drifting `'Power enabled'` and `'Score enabled'` = true. `Type` stays `'Time Trial'` (the PC enum, string
table 0xD36A68, has no Time Trial Plus), `'Game Mode Name'` (cameras) is TimeTrial. The DLC file has 1393 keys the
PC TimeTrial file lacks but other PC mode files have (1358 tutorial lessons, end sequence, drifting power rates);
they stay. The 25 PC TimeTrial keys the DLC file lacks (`AdviceSlowMo`, `'Calculate Overall Rank'`) are names the
exe does not have at all.

Left out (78 keys, the Xbox-only logic): `Logic/Pickup` (pickups, freeze bonus, drift multiplier), King of the
Air / waves (`PowerBar/King`, `Scoring/KingWave`, `Num Waves`, end conditions), `Scoring/InLead`, chopper and
Nemesis respot options, `StartPosition` survival hacks, prototype leaderboard, lesson `EventCustomData`. The PC
parser would ignore them (below), so leaving them out changes nothing now; the pickup stage adds what it needs.

Texts: glimpse_fix's own (`TEXTS` in `deadline_mode.py`, five languages), used when no TU4 text table is set. They
explain the same rules as the official texts in other words: the name DEADLINE, three help lines (one lap against
the countdown; a pickup holds the clock for 2 s in a drift; without a drift only half a second), and under the TU4
keys the DLL asks the dictionary for, the pickup messages (`+0.5s` / `+2.0s`, with the language's decimal sign) and
the two hint lines. With a TU4 text table (below) the official texts take their place.

StringIDs maps each key to its lower-case form.

## How the PC handles a game mode (SplitSecond_dump.exe, Ghidra read-only)

- Mode list. `directory.txt` GAMEMODES: 0x78EF00 parses every listed file at start-up. TrackFiltration.xml:
  0x77B830 reads `PlayableGameMode` Name into a 16-entry array (0x77B970 allocates 16 x 0x48; the append has no
  bounds check), 0x77B6C0 reads `GameMode` Name / SplitScreenName / MultiplayerName and up to 16 tracks (0x77B620).
  `DLCPack` and `DLCGameModeIndex` are read by nothing. The quick-play mode menu has room for 96 items (0x45E230).
- Race setup (0x793550): `GameLogic/GameModes/<Name>.params`, `RaceSetupStatic.params`, the track overlay
  (0x790D90: `GameLogic/Tracks/<track>/Params/RaceSetupOverlay_<sub>[<variation>].params`, the digit only from
  variation 2 on), then `GameModeHUDs/<Name>.params` (fallback QuickRace). The type `Time Trial` loads the same
  powerplay exclusion files as TimeTrial (0x790F30).
- Params parser (0xABC460): `!directory` lines go to an empty handler (0x8881C0), a section line sets an absolute
  path (0x41D1A0), a key is offered to the registered tweakables and dropped when none matches (0x41D630, 0x41D9B0,
  0x41DB10). Unknown keys are therefore ignored; the PC's own TimeTrial.params already ships 25 of them.
- Countdown: `RaceSetup/Logic/Countdown` Enabled (0xD6BC44) and `'Initial Countdown'` (0xD0B7E0). The timer runs
  only when Enabled (0x4454F0, `jz` at 0x44551E).
- Texts: `TEXT_GAMEMODE_NAME_<Name>` (0x77B4F0 quick play, 0x804A70 in-game instructions) and
  `TEXT_GAMEMODE_<Name>_HELP_LINE1..5` (0x804A70; `BLANK` gives an empty line).
- Profile: `newQuickPlayGameModes` (0x7732C0) and `playedGameModesStr` (0x773B10, `0PLAYER`/`1PLAYER`) iterate
  GAMEMODES and read or write one flag per mode name through the element API (existence test first) into sorted
  maps (0x7735C0, inserts grow the array). No fixed count.

### Per-track start time

The Xbox overlays `RaceSetupOverlay_<sub>_TimeTrialPlus.params` are never read on the PC: 0x790D90 only builds
`RaceSetupOverlay_<sub>.params` (and `<sub>2` etc. for variations). They are full Xbox overlay copies plus
`RaceSetupOverlay/TimeTrialPlus/<rival>Time` and `RaceSetupOverlay/TimeTrialPlusCountdown`, which the PC does not
know. The start time is expressed instead with the PC key `RaceSetup/Logic/Countdown/'Initial Countdown'` in the
overlay the PC reads for that track. The overlay is applied after the mode file, so it wins; the mode file's 30 s
is the fallback. The overlay is shared by every mode on that track, but the value is inert for them: every PC mode
that runs on these 11 tracks has Countdown Enabled = false, and the only PC modes with it true (Nemesis,
NemesisMulti, NemesisSplitScreen) run only on the nem_* tracks (TrackFiltration and Season.xml).

| track | start (Xbox) | PC Time Trial season target |
|---|---|---|
| airport_test_03 A | 60 | 85 |
| docks A / B / C | 65 / 60 / 60 | 89 / 69 / 70 |
| downtown A / B / C | 65 / 65 / 65 | 98 / 109 / 77 |
| graveyard A / B | 65 / 70 | 88 / 114 |
| powerplant A / B | 60 / 70 | 81 / 95 |

The Xbox rival values (`RiggTime` ... `VixenTime`, 4-45 s) look like time left at the finish, which only pickup
bonuses make possible. Without pickups the start times are 9-44 s below the PC's own Time Trial targets, so a lap
can hardly be finished. `--extra-seconds 50` (or `apply(..., extra_seconds=50)`) gives 110-120 s for a stage-1
test; the default keeps the Xbox values. Since stage 2 packs keep the Xbox values and the DLL adds
`[DLC] DeadlineBonus` (default 50 s) at run time when `[DLC] Pickups=0`, to the countdown tweakable 0xD0B7E0 before
the countdown's reset copies it (once per race; a restart does not add it again), so start value, time left, the
HUD's used share (0x4CB7E0) and the timer (0x4C5110) agree. The rival standings (below) rank by the clock used
(start - time left) against TimeTrialPlusCountdown - `<Rival>Time`, so the bonus gives no better rank; it shows as
more time left on every row.

### Race end screen

The end screens do not go by the params' Type but by a "race kind" the PC derives from the mode's name (0x7D5290,
`[0xD719F8]+0x194C`: QuickRace/ChampRace 0, TimeTrial 1, Elimination 2, HelicopterSurvival 3, HelicopterRevenge 4,
Nemesis 5, NemesisMulti/SplitScreen 6, WeeklyChallenge 7, any other name 8). TimeTrialPlus gets 8 ("bonus"). At the
race end 0x799E50 gives the player's time to `UI/InGame/RaceEnd.gfx` (0x807730, `%.2d:%.2d:%.2d`) and the results
list only for kinds 0, 1, 3, 4, 5; the screen (0x808600, `SetVisibility(position, credits, unlock, time)`) shows
the time line in quick play for every kind but 2 and the position for kinds 0, 2, 6-8. So a stock Deadline race
ends with a position ("2.") and an empty time line (or, after a restart, the time text of a run before: 0x807730 is
the only writer of the screen's time text +0x814). No data file can change the kind (exact name match). The DLL
(`src/dlcpickups.cpp` DeadlineResult, with the countdown hook) sets kind 8 to 1 while a Deadline race is on (one
screen, offline; the event objectives screen sets 8 again, so the event objectives and race end screens' enter
slots are wrapped as well, see "Race end screen, finished race" below): the end screen
shows a time without a position (as the Xbox for its kind 0xB, 0x882DA1D0), the results list the times under the
Time Trial icon. The DLL shows time left on both screens, as TU4 does (see "Rival standings"). With
`[DLC] PersonalBests=1`, an eligible run also updates the player's local record. When the clock runs out,
the game marks the player DNF using these flags: the Disqualified record (record 12 of the record collection, logic id
`[0xD03C78]` = 0, output +0x34 = the cRecordInt at cPlayerStateOutput +0xC; cPlayerState only ever writes 1 there,
its reset 0x43E770 writes 0 on a restart), so 0x799E50 calls 0x807730 as disqualified: the time text is emptied, no
best time check; and cEndGame's output +0xA (set by the game for the online modes, cleared by the reset 0x442520),
which 0x79A210 passes to the end screen (+0x89C, ShowDNF) and to the player's result row (0x803400 argument 7,
row +0x323: with the Disqualified record the row shows TEXT_DNF, 0x802420).

### Rival standings (src/dlcrivals.cpp, with the countdown hook; Xbox TU4 0x882E7C58 / 0x882E8980)

TU4 adds the rivals to the game's own race info (`[0xD719F8]`: 101 entries of 0x40 at +8, count +0x1948; entry +0
name, +0x28 0 = a local player / 1 = rival / 2 = friend, +0x2C type 0 time / 1 score / -1 none, +0x30 u64 value,
+0x38/+0x3C index) when its pre-race builder (0x882E7C58 = PC 0x7D6770) runs for kind 0xB: THE HAMMER, RAPTOR,
VIXEN, LIVE WIRE, TORPEDO, BRAWN, RIGG, each `(TimeTrialPlusCountdown - <Rival>Time) * 100` (hundredths, truncated),
flag 1, type 0. At the finish (0x882E8980 = PC 0x799E50) the player's value is the race time record x 100 (kind
0xB as kind 1), which TU4's lap timer (0x88380670) does not advance while the clock is frozen: the clock used.
0x7D5C40 sorts the entries (0x7D5AD0, compare 0x7D53F0: type 0 ascending), and up to 8 of them around the player
become result rows: entry j's values go into player slot j's records (position j, time, not disqualified); a rival
(flag != 0) gets the next dummy player slot (1..7: `'Fill remaining slots with dummy players' = true` in the mode
file makes the game logic 8 players, 0x447560) and its row (0x803400). The end screen (0x882D25B8) and the rows
(0x882E5F78) show `(int)Countdown * 100 - value` (`TEXT_DNF` when not below); the Xbox's end screen shows no
position for kind 0xB (0x882DA1D0). So the player is ranked among the rivals by time left.

The PC (`src/dlcrivals.cpp`, `src/dlcpickups.cpp`; data writes, calls of the game's own functions, vtable slots):
- Values: the overlay the PC reads for the track carries the Xbox keys (above; the DLL parses it from the merged
  resident.ark, for Quarry as well); names from `glimpse/deadline/RivalNames.txt`, else HammerTime -> HAMMER,
  LiveWire -> LIVE WIRE.
- Entries: after the event objectives screen built the race info (cEventObjectives slot 2 wrapper; 0x7D6FF0 clears
  it first), the 7 entries go in with the game's 0x7D5730 (thiscall name, flag 1, type 0, value; as the friends'
  times) and are sorted as the game does (0x7D5AD0 over the 101 entries). Not twice (by name), not with fewer than 8
  player slots or dummy slots without the records 0x799E50 writes (logged), not while the career's current event is
  a championship (0x801440 then copies all rows' positions into the season standings, `[0xD64E40]` event +0xE4 == 3).
- The player's time: the clock used, as TU4: cLaps' update (vtable 0xBA8DE8 slot 4, 0x440630) gets dt = 0 for
  player 1 in the racing frames the countdown hook held the clock (`src/dlcpickups.cpp`, `[DLC] Pickups=1`).
- Time left: the race end screen's time text (+0x814, written by 0x807730 from the value it keeps at +0x808) is
  formatted again in cRaceEnd's enter wrapper before 0x8082D0 shows it; the result rows' time records (list
  `[cRaceResults+0x4A0]` +0x2F4 + i * 0x3C: +0x321 used, +0x328 the record collection output, its +0xC cRecordTime)
  become the time left (0x429650) in cRaceResults' enter wrapper (vtable 0xC1F83C slot 2, 0x802E10), which then
  builds the texts (0x802420) and shows them (0x801500); once per finished race (cEndGame output +9). A
  disqualified row (the clock ran out) gets time 0 and the row's DNF flag (+0x323): TEXT_DNF, as TU4's row
  (0x882E8E60) when the time left is 0. The championship gate applies only with the career flag `[0xD64E40]+8`. The rows' order comes from their position records and stays.
- The standings and the persistent best use different game data. 0x7D5C40 updates race info in memory.
  `[DLC] PersonalBests=1` saves the local player's best for the original countdown and pickup layout
  using the native profile record table. The Quick Play track card and the race HUD show that
  best as time remaining. See
  [Original DLC personal bests](../../../docs/TECHNICAL.md#original-dlc-personal-bests-steam-build-385319).

### Mode slot and quick-play movie

The movie is chosen by an exe table, not by a data file. 0x479680 maps an id to a path: ids 0-5 through the table
0xD0F998 (`data|Movies/Screens/QuickPlay/FE_Race.bik` ... `FE_Eliminator.bik`, loose files under `data\movies`, not
in resident.ark), 6-20 to the track movies (0xD6FC40, `FE_QP_Track10.bik` first), 21 to `FE_Locked.bik`, above 21 a
null path. A mode's id is its index in the SinglePlayer PlayMode (0x77A9F0 stores it at +4; 0x479720 plays
`locked ? 21 : id`). Deadline goes in as the 7th SinglePlayer entry, id 6: no out-of-range access, the existing six
keep their movies, and Deadline shows `FE_QP_Track10.bik` (21 = `FE_Locked.bik` while locked). Putting it into an
existing slot would cost that mode. Up to 15 extra modes stay in the track range; a 22nd would get the null path.

`FE_DEADLINE.BIK` is not packed into the overlay: nothing in the data would request it. It is checked: Bink `i`,
256x256, 30 fps, 600 frames, flags 0, no audio, size field = file size, the same header fields as all six PC mode
movies. `build_all.py` stages it loose as `glimpse_dlc\deadline\data\movies\screens\quickplay\fe_deadline.bik`, and
the DLL shows it with `[DLC] Modes=1` (see "Quick-play movie" below).

### Mode icon (`modeicon.py`, `apply(..., mode_icon=True)`, default on; `--no-mode-icon`)

Where the PC shows a mode icon and how it picks it (static RE, Steam exe):

| screen | movie | icon chosen by | Deadline on the PC without the change |
|---|---|---|---|
| pre-race game mode instructions (the Xbox recording's start dialog: hex icon at the right, CONTINUE) | `UI/InGame/IG_GameModeInstructions.gfx` | 0x804A70 calls `SetGameMode(name, help1..5, icon)`; `icon` = the race's mode name (setup +0x988 without `.params`/`Multi`/`SplitScreen`) in lower case (`finalchamprace` -> `champrace`); the script does `mc_main.mc_eventicons.gotoAndStop(icon)` (labels match without case) | no frame `timetrialplus`: the sprite keeps its frame (ChampRace, the silver trophy, or the previous race's icon) |
| event objectives (`SetGameModeIcon`) | `UI/InGame/EventObjectives.gfx` | race type: `[[0xD719F8]+0x194C]` looked up in the table 0xC1FDC8 (9 x {u32 kind, char[0x40] label}; not found -> `bonus`), 0x8059D0..0x805B45 | kind 8 from the name (see "Race end screen"): label 8 in the table -> `bonus`; `timeTrial` once the DLL sets kind 1 |
| race results | `UI/InGame/RaceResults.gfx` | the same by kind, table 0xC1F390 (0x8032F5) | `bonus`; `timeTrial` with the DLL's kind 1 |
| career event lists (front end 0x47A9D0, in game 0x80DEC0 / 0x80F270) | FE / in-game event select | 0x4DD230: mode name compared with a fixed list -> `elimination` ... `timeTrial`, else empty | not reached (no season event uses TimeTrialPlus) |
| quick play mode list | `FE_quick_play_detailscreen.gfx` + movie | no icon (FE_event_ICONS is exported but no shape uses it); movie by id (below) | `FE_QP_Track10.bik` |

The Xbox package brings its own copies of `IG_GameModeInstructions.gfx`, `EventObjectives.gfx`, `RaceResults.gfx`
(loose, `UI/INGAME/`): each has the PC icon frames plus KingOfTheAir / NemesisRace / TimeTrialPlus (instructions)
or KOA / survivalRace / timeTrialPlus (objectives, results), showing cells of the new 256x256 atlas
`FE_DLC_event_ICONS` (`UI/INGAME/TEXTURES/INGAME4.TEXTURES`, A8R8G8B8; Deadline = the blue hexagon with the car,
cell x 13..127, y 129..255). The quick play screens are not in the package.

What goes in (data only):
- `UI/InGame/IG_GameModeInstructions.gfx`: the PC movie with one frame `TimeTrialPlus` appended to its icon sprite
  (15): every PC tag unchanged except that sprite, whose 7 frames stay; the new frame removes the last frame's
  depths and places the DLC frame's display list (mask = the PC mask shape, mapped over the 7 shared frames; the
  icon = the DLC wrapper sprite -> shape -> image `FE_DLC_event_ICONS`, copied under new ids 47-49, its matrix and
  the frame script `stop` as in the DLC). The definitions go before the sprite.
- `UI/InGame/Textures/InGame.textures` + `.filenames` (pack versions): `FE_DLC_event_ICONS_RAW` (format 0x2, the
  set's own name and order rules, key f600bddf = the DLC's) and `FE_DLC_event_ICONS`, as `../ui/flashtex.py`
  would add it, except the DDS header's flags / mip count / caps, which are taken from the set's own images of
  that format (InGame: 0x1007 / 1 / 0x1000; flashtex writes the Flash set's mip count 0). The set is the one the
  movie's `FE_event_ICONS` comes from.
- Build order: the icon goes into `InGame.textures`/`.filenames`, which `../assemble/finish_car.py` rebuilds from
  the PC copy plus the cars. `build_pack.py` runs the mode after finish_car and checks the result; any other
  order would drop the image silently, so a step that runs later must call `deadline_mode.icon_present(files)`
  (also part of `check()`).

Checks (in `check()`): the GFX is the PC movie plus the frame (ids unique and new, exports, the frame's depths
place the same matrices/clip as the DLC frame and resolve to the same image and bitmap matrix); the texture set is
the PC set plus one image whose pixels equal the DLC image converted, everything else byte-identical, the name
order kept; the size matches the GFX's image tag (256x256).

Not done by data:
- Quick-play movie `FE_DEADLINE.BIK`: done in the DLL. The movie path comes from `cNewQuickPlay` vtable 0xBAFD34
  slot 24 (+0x60, 0x479680, `thiscall(id)`, `ret 4`), called by 0x456550 (play movie id if it is not the current
  one). Ids 6-20 are the track movies, 21 FE_Locked, so the DLC modes get ids from 64 on (far above the track ids):
  `src\dlcmode.cpp`'s slot-1 wrapper (0x4867B0) sets the Deadline record's movie id (+4) besides clearing +8, and
  `src\dlcfs.cpp` swaps slot 24 for a wrapper that returns `data|Movies/Screens/QuickPlay/FE_Deadline.bik` for that
  id and calls 0x479680 otherwise. The movie is the loose file `build_all.py` stages under
  `glimpse_dlc\deadline\data\movies\screens\quickplay\` (served there by the DLL's file redirect, or below the
  game's `data` with the same layout), as the track step ships `fe_qp_track17.bik`; without it the stock movie
  stays (logged).
- Event objectives / race results (proposal for the DLL, not implemented): the label comes from the race kind (`bonus` before the
  race, `timeTrial` after it, see "Race end screen"). A hook would have to replace that label by `timeTrialPlus` for the `SetGameModeIcon` call when the mode name is
  TimeTrialPlus (and the two movies would need the frame, the same way as above). Not worked out further; whether
  a quick play race shows the event objectives screen at all was not checked.

### Unlock

0x77A9F0 marks a single-player quick-play mode locked unless an unlocked season event (12 episodes; an episode-1
event with field +0xE4 == 1, or an event that is not locked and has +0xF0 != -1) has GameMode equal to the mode's Name
(case-insensitive, 0x530970). Online modes are never locked. Tracks are unlocked by track and subtrack (0x77ACB0),
independent of the mode, so Deadline's tracks follow the season. Two DLC flags lift the lock: 0x486DE0 tests
`DLCUnlocks11/isUnlocked` or `DLCUnlocks12/isUnlocked` (tweakables registered by 0xA062D0; 0x4853B0 tests 11, 12 or
2 for tracks; the Master Unlock pack's `DLC/Unlock/Unlock.params` sets 12). The flags are copied into the profile
(0x41ACA0, restored by 0x41ACD0).

No season event uses TimeTrialPlus, so Deadline is listed but locked (TEXT_LOCKED, FE_Locked). Data-only ways
round it, not taken:
- a params file the game applies (the parser sets global tweakables) with `DLCUnlocks11/:` `isUnlocked = true;`:
  unlocks every quick-play mode and track, and the game then keeps the flag in the profile;
- Season.xml: an event's GameMode changed to TimeTrialPlus: changes the career event.

Taken: the DLL (`src/dlcmode.cpp`, `[DLC] Modes=1`) clears the lock byte of the Deadline record only. The list
object (0x486BE0: capacity 16 at +4, count +8, records +0xC, 12 bytes `{GameMode*, movie id, locked}`) is rebuilt
by 0x486D90 when the quick play screen opens (0x479B30). Readers of +8: 0x4866D0, 0x486F40 (current selection,
prompt and buttons 0x479D60), 0x486C90 (choice taken, 0x486FF0 / 0x486D30) and the item update 0x4867B0 (text
`TEXT_LOCKED`, movie `locked ? 21 : id` via 0x479720). The item update is slot 1 of the delegate
`cQuickPlayEventSelectDynamicMenuConfigObject` (vtable 0xBB2360, COL 0xC7A40C, made at 0x486C4C with the list at
+4; `ret 0xC`). The DLL swaps that slot: before the game's update every record whose GameMode name (GameMode +0xC,
the field 0x77A5F0 compares) is `TimeTrialPlus` gets +8 = 0. Checked first: COL pointer and class name, slot value
0x4867B0, the first 7 code bytes, the Steam build, and that the merged resident.ark has
`GameLogic/GameModes/TimeTrialPlus.params`. No game code byte, profile or DLC flag changes.

## Checks (all in the script)

- Archive read back with arkio: little-endian, `hsize = 0x10 + 20n`, keys sorted and unique, all stored, data as
  built; merged with the real resident.ark through `../inject/vark.py`, every test entry and base samples read
  through `GameArkReader`.
- Exactly the 36 mode entries are new or changed (16 new with the 11 pickup files, the box model and the two Frozen
  Time presets, 20 replaced with `DialsInfo.gfx`; + 2 new with the menu audio, + 1 with the TU4 rival names; 35 and
  15 with `--no-pickup-model`, 22 and 2 with `--no-pickups`, one replaced less with `--no-dlc-hud`); every other
  entry byte-identical; every new key resolves.
- Overlays: the Xbox's 7 rival times and TimeTrialPlusCountdown after the Initial Countdown block, values as in the
  Xbox overlay; the rival names file (with a TU4 table) names exactly the overlays' 7 keys. Presets: new keys,
  byte-identical to the DLC, with fade times and the Overwrite flags.
- HUD movie: the DLC file as is, the same external images and imports as the PC's, every name of the PC code, the four
  timer functions new. Menu audio: the converted files as dlcaudio.py wrote them, new keys, a FEV1 project DLCMenu
  with the five TrialPlus events, an FSB4 bank of 7 samples.
- Box model: a new key in neither archive; `pickupmodel.check` re-reads the blob (151 vertices, 72 triangles,
  slide show constants, the three DXT1 textures byte for byte against `convtex.xb_texture`).
- Pickups: each of the 11 files byte-identical to the DLC entry, a new key, parses (`pkps.py`: colours known, rows
  0-2 unit length, positions finite); 496 boxes in all.
- Mode params: key set = DLC minus the 78, every key known to a PC mode file, every value the DLC's (Type mapped,
  a PC enum value), the differences to PC TimeTrial exactly the six above, CRLF.
- HUD: byte-identical to the DLC file, 255 keys as PC TimeTrial, only the 5 countdown flags differ.
- directory.txt: one line inserted, every other byte unchanged, all 17 sections intact, GAMEMODES = PC + 1 in order.
- TrackFiltration.xml: removing the 17 inserted lines gives the PC bytes; well-formed (ElementTree); SinglePlayer =
  PC + TimeTrialPlus, the other PlayModes unchanged; GameModes = PC + 1; within the 16-entry arrays; the 11 tracks are
  the DLC's, each in TRACKS and with a PC overlay.
- Overlays: PC bytes followed by exactly the countdown block, value = Xbox countdown (+ extra), one key more.
- Dictionaries: every old entry unchanged, the 6 new keys resolve to their texts, every non-ASCII letter of the new
  texts occurs in that language's PC texts (font).
- FE_DEADLINE.BIK header as above.

## Result (2026-11-20)

`deadline_mode_test.ark`: 311 entries (297 of `deadline.ark` + 14 new keys; 7 of the 21 mode files replace pack
entries), 96,794,624 bytes, all 62 checks pass. Not tried in the game.

`deadline_v3.ark` (build_pack `--mode`, extra 50 s): 314 entries (the 300 of `deadline_v2.ark`, 293 of them
byte-identical, + 14), 99,270,656 bytes, all checks pass (59 mode checks); start times 110-120 s.
`track_out\combined_mode_v1` (assemble_track `--base deadline_v3.ark --combined dlc_combined.ark`, unchanged
script): all checks pass; `dlc_combined.ark` 476 entries (314 + 172 track keys, 10 merged), 108,224,512 bytes;
against `quarry_on_deadline_v3\glimpse_dlc\dlc_combined.ark` (cars + Quarry) it differs in exactly the 21 mode
files; the four level archives, the movie and the streamed bank are byte-identical to that run. Not tried in the game.

## Stage 2: the pickup boxes (`src/dlcpickups.cpp`, `[DLC] Pickups=1`)

`pkps.py` parses and checks the files (`python -I pkps.py <RESIDENT_DLC.ARK | file.pkps>`): XML `<Root>` /
`<Pickup pickup_colour node_id group_node_id>` / `<transform>` with `Row0..Row3` (X, Y, Z); rows 0-2 the box's axes
(row vectors), row 3 its centre. The 11 DLC files: 496 boxes, all green, one group each, up axis (0, 1, 0) within
0.3 %. The DLL's own reader gives the same 496 boxes and values (offline test against `pkps.py`), and its archive
read (`DlcOverlayRead`, stored and LZSS entries) was checked on the PC resident.ark merged with a test overlay.

What the PC has (Steam build; addresses in `src/dlcpickups.cpp`): the mode runs on the generic logic objects.
`cCountdown` (vtable 0xBA96B0, one per player, logic id 6, output +0x2C: +4 enabled, +8 start, +0xC time left,
+0x10 the value before the update) is updated by slot 4 (0x4454F0), which subtracts dt only in race state 6
(`[[this+0x30]+4]+0x10`); `cEndGame` (0x442DA0 -> 0x442780) ends the race at 0; the HUD reads the same output
(0x4CB7E0). So the PC TimeTrialPlus counts down and ends the race at 0 as it is (stage 1). The DLL swaps slots 2
(reset) and 4 (update) of that vtable: while frozen time is left the time left is written back to the value before
the game's update (the Xbox runs its countdown with dt = 0 then), the frozen time runs down by the same dt. Race:
`[0xD66B28]` +0x14 setup (+0x988 mode name, +0xE0 track, +0x1E0 subtrack, +0x9C screens), +0xE4 game logic,
+0xEC scoring managers ([1] = `cScoreDrifting`, vtable 0xC22E50, +0x40 in a drift). Car: the chassis rigid body as
in boost.cpp. Camera: `[0xD66000]` viewport manager, camera `+0x68 + i*0x2F4`, view x projection at +0x198
(standard depth 0..1). Restart: slot 2 (reset) and the change into state 6 with a full clock both re-arm.

The box model (`Generic/Models/GreenPickupBox`) is only in the DLC archive (Xbox model and shaders) and no PC code
would load it. `pickupmodel.py` converts it from the user's own DLC data into a small blob of our own
(`glimpse/pickups/GreenPickupBox.bin`, 597,808 bytes; never in the repository), which `apply()` adds by default
(new key, in neither archive; `check()` re-reads it: every vertex, triangle, constant and texture byte against the
DLC files). The DLL draws it with its own shader right after the game's tone mapping (postfx.cpp's bloom hook),
depth tested against the scene's depth surface, colour only, all state put back with a state block; within 100 m,
fading from 75 m. Without the blob it draws 1.5 m cubes as before.

### Pickup model (pickupmodel.py)

    python -I -X utf8 pickupmodel.py <RESIDENT_DLC.ARK | package dir> [<out.bin>]

- `.model`: two meshes. The token: a hexagon (corners 0.9 m from the centre, 1.72 x 1.99 m, 0.71 m thick, shallow
  pyramids on both faces) in the model's x/y plane, y up, facing +z/-z; 151 vertices of 0x30 bytes {FLOAT3
  position, FLOAT4 = normal xyz + u0, FLOAT4 = v0, alpha1 (1.0), u2, v2, FLOAT1 alpha3 (0.56)}, one strip of 191
  indices = 72 triangles. Material shader `0x58f57228` plus a depth pass (`0xacf16db3`). A second mesh, a
  1.65 x 1.53 m quad 1.30 m below the token with its normal down (G-buffer shader `0xb0ef2956`, no texture), is
  not converted.
- `.shaderinst`: `uvslideshow_2` frames 4 x 2, skip 1, speed 10; `reflect_fresnel_cheap_3` fresnel 0, fadeout 0;
  `Emissive_0_emissive` 1.0; textures `HUD_PPIcon_Detonator2_DXT5` (512x512 DXT1, 10 levels: a blue hexagon with a
  car seen from above), `cubemap_Generic` (128 cube DXT1, 8 levels) and `HUD_PPIcon_Detonator2_Slide` (1024x512
  DXT1, 11 levels: 4 x 2 cells of blue facet patterns). All gamma, default swizzle, practically no DXT1 alpha.
- Shader `0x58f57228` (`../xenos/xlate.py`): G-buffer albedo = lerp(lerp(0.5, icon, icon.a * alpha1), slide,
  slide.a * alpha3), the slide at uv2 + (column / 4, row / 2) of frame floor(7 frac(10 t / 7)) (t = frame counter /
  60): 7 frames at 10 fps, a 0.7 s cycle; light = albedo * (hemisphere ambient + emissive) + cube(reflect) *
  (1 - |n.v|). The DLL's version: the same blend in linear light (gamma 2), x 1.8 emissive, + 0.6 x the reflection,
  then a shift towards cyan (+ (0.05, 0.38, 0) x blue) and the bright parts towards white (by 0.55 x blue^2): the
  Xbox's bloom and HDR make the token a light cyan with a whitish icon in the game (matched to the video's colours),
  and our pass comes after them; gamma 2 out; faces turned away are clipped by the normal (convex, no depth write).
  The slide show texture keeps its levels down to 64 x 32 only (smaller ones would mix neighbouring cells); the
  three textures are left out of `[Graphics] TextureDetail`. With `HDR=1` the boxes are drawn once more into the
  HDR gain target with gain 1, so they stay at paper white like the HUD.
- TU4: the render 0x882AA010 draws every pickup not collected with the transform kept at +0x20 (set by vtable
  slot 8, 0x882A9020, from the file; +0x60 is the previous transform for the motion vectors, `gbuffer_MotionID`
  2): no rotation, no bobbing, no scale or fade on collection (it is just no longer drawn). The Xbox video
  agrees: the icon's axis stays put over the frames; what moves are the slide show's facets. The only other
  writer of the transform is 0x882A9A68 (a pickup message with node_id and transform, from 0x882CEC18; source not
  traced).

Blob layout: see the top of `pickupmodel.py` (header 0x60, vertices 48 bytes {position, normal, uv0, uv2, alpha1,
alpha3}, u16 triangle list, three DXT1 textures with all their levels, cube faces in D3D order).

The HUD: see "Deadline's HUD (Xbox parity)" below. With `[DLC] PickupVignette=1` (default) the Xbox's "Frozen Time"
post presets go onto the game's colour cube pass while the clock stands (`src/dlcpickups.cpp`, PickupsGradeBegin,
from postfx.cpp's wrapper of cSSColourCube's render 0x49A7A0: its block at this+0x14 has the cube blending +0x14,
input blending +0x18, gradient +0x1C..+0x34, vignette colour +0x38, U/V scale +0x48/+0x4C, inner/outer +0x50/+0x54,
the fields the presets overwrite; values blended with the camera's by the fade weight and put back after the call).
With a mode pack without the preset files a cyan glow along the screen's edges stands in (four gradient bands drawn
into the tone mapped scene with the boxes).

Not tried in the game: where the depth surface is at that point, the drift flag's owner (scoring manager 0 =
player 1 is inferred from 0x4AED34), the collision size of the car (fixed 2 x 2 x 4.8 m around the chassis'
origin; the Xbox uses the car's own box), and the look of the boxes.

## Deadline's HUD (Xbox parity)

The Xbox's Deadline HUD is TU4 code on the base game's HUD (TU4_RULES.md section 3); the PC has the same HUD
message queue, the same 2D sound call and, with the DLC's own `DialsInfo.gfx`, the same timer movie functions. The DLL
(`src/dlcpickups.cpp`, "Deadline's HUD and sounds", with `[DLC] Pickups=1`) drives them with TU4's rules; the data
comes with the mode pack (`deadline_mode.apply`, `dlc_hud` and `menu_audio`). References: the Xenia recording of
the real Xbox game (2026-11-21, 62 s, ends with a timeout) and a YouTube capture (no finished race in either).

| element | Xbox (TU4 code, video) | PC before | PC now |
|---|---|---|---|
| top centre seconds counter | none (TU4 creates `TimerOverlay.gfx` too, nothing shows it in Deadline) | red whole seconds (shown because Countdown is enabled) | hidden in a Deadline race (its movie's SetVisible, as photo mode), shown again in the next race of another mode |
| timer under the car | counts down, orange; cyan while the clock stands: `SetTimerToStoppedColor` (plain hit) / `SetTimerToStoppedColorLong` (drift hit), `PulseFrozenTimer` on a further hit, `SetTimerToRunningColor` when the stopped time runs out (0x8834C4C0) | counts down, never cyan | the same four calls (0x536AC0 on the DialsInfo layer, cHud +0x2B4) on the DLC's `DialsInfo.gfx` |
| "+0,5s" / "+2,0s" | HUD message (0x88342888: 0.5 s, type 1, style 0 = the positive message between the cyan bars at the top), `TEXT_TIMETRIALPLUS_PICKUP` / `_DRIFTPICKUP` in the game's language | our large "+0.5 s" overlay | the PC's same message queue (0x561E10 on cHud +0x28C) with the TU4 text from the dictionary; our small line only when the HUD code is not as expected |
| "DRIFT" / "KNAPP" under the timer | the game's own action texts (drift scoring is on in the mode) | ours on a drift hit, plus the game's | the game's only |
| "CLOCK STOPPED" | none | ours | removed |
| hint | `TEXT_TIMETRIALPLUS_OBJECTIVE2` as a teaching message (0x883427E0: 4 s, the neutral message), once more than 3 s into the race after hint 1 (3 s cooldown) and after 3 pickups in a row without a drift; hint 1 (after 3 s, or 3 boxes missed in a row) has no text case | none | the same rules (a missed box: within 10 m, then 15 m away without a hit), the same message (0x561DB0) |
| sounds | `DLCMenu/TrialPlus/Pickup`, `PickupWithDrift`, `Pulse`, `PulseWithDrift`, `FreezeExpired` (0x8833E098); the last five seconds `DLCMenu/TrialPlus/5`..`1` (0x88239530); the bank `Menu/DLCMenu` loaded by the level audio load for TimeTrialPlus (0x8857F188) | none | the same events through the PC's 2D sound call (0x7845E0, as the HUD's own sounds), the DLC bank converted into the pack (`Audio/Release/Menu/DLCMenu.fev/.fsb`), loaded on the audio thread at the first state message of a Deadline race (0x7F2F20, dlcmusic.cpp's message wrappers) |
| "Frozen Time" look | post presets Heavy / Subtle (DLC `PostProcessDatabase/Frozen_Time_*.params`: colour cube off, cyan vignette, fade out ~1 s; selection not traced) | cyan edge glow | the same presets on the game's colour cube / vignette pass while the clock stands (Heavy after a drift hit, Subtle for a plain one: inferred), faded out with the preset's time; the edge glow only with a pack without the presets |
| pickup token | hexagonal token, slide show | converted model | unchanged |
| race time while frozen | stands (lap timer dt 0, 0x88380670) | runs on | stands (cLaps slot 4 wrapper) |
| end screen, finished | the time left (Countdown - clock used), no position | "1." and empty time lines (see below) | the time left, no position (race kind 1 kept at the screens, time text written again) |
| end screen, timeout | NICHT IM ZIEL (the video shows the car wrecked first) | DNF | DNF (no wreck sequence) |
| rival standings | THE HAMMER .. RIGG with `(TimeTrialPlusCountdown - <Rival>Time) * 100` in the race info, ranked with the player by clock used, all rows as time left (0x882E7C58, 0x882E8980, 0x882E5F78) | none | the same entries through the game's own race info and result rows, rows as time left ("Rival standings" above); not in the pre-race list (the event objectives screen shows its list before the DLL can add them) |
| restart | boxes and logic reset | boxes and clock reset | + the HUD state (timer colour, hint counters) |

Not done, with the reason:
- The rivals in the pre-race list: 0x806820 builds the race info and shows it (0x805920) in one call; the DLL can
  only add the entries after it.
- Which Frozen Time preset when: TU4 registers both (0x88E78A58) but the code choosing one was not found; the DLL
  uses Heavy for a freeze started or extended by a drift hit, Subtle otherwise.
- The wreck sequence at a timeout (the Xenia video) is TU4 race end code.

Not tried in the game: whether the DLC `DialsInfo.gfx` looks the same as the PC's in the other modes (it replaces
the PC movie for all of them; the checks below compare its images, imports and code names), the message ids 0x9D-0x9F
in the PC queue, and the sound levels of the converted DLCMenu bank.

### Race end screen, finished race (fixed 2026-11-21)

The test of 2026-11-21 (current DLL, kind 8 -> 1 logged at the race start) ended a finished race with "1." and empty
time lines: the race end screen (cRaceEnd vtable 0xC208DC slot 2, 0x808600, the screen's enter) showed the position
and 0x799E50 wrote no time, so both read a kind other than 1. Only 0x7D5290 writes 8, called by 0x7D6FF0, which
first clears the race info (0x7D5570: kind -1) and derives the kind from the setup's mode name again; 0x7D6FF0 runs
when the event objectives screen comes up (cEventObjectives vtable 0xC201E4 slot 2, 0x806820: the pre-race screen,
the pause menu's event page) and for online races. The DLL wrote 1 only in racing frames, so a pause (or the
event objectives screen) late in the race left 8 for the end. Now both screens' enter slots are swapped (data):
after the event objectives screen has set its kind and before the race end screen reads it, 8 becomes 1 again when
the info's setup (+0x1CF8) is Deadline on one screen, offline (logged). The timeout path (DNF) is unchanged: it sets
the Disqualified record and cEndGame's +0xA in the racing frame the clock runs out, with kind 1.

## Official texts (`tu4text.py`, optional)

Title Update 4's `default.xex` has a built-in table of 75 texts for the DLC content (the car names and
descriptions `TEXT_PLACEHOLDER_NN` / `TEXT_CARDESCR_Placeholder_NN`, the mode names and help lines of Deadline,
Survival Race and Onslaught, the Deadline pickup texts, Quarry track names, loading tips 57-60, wave texts): records
of 0x20 bytes at 0x88EC80D8 (hash, language 0 = "", EN, FR, IT, DE, ES, RU; UTF-8 strings), keys only in the
static initializer 0x88E65330 (CRC-32, init -1, no final xor), lookup 0x883CD440. The texts are game data, so
nothing of them is in the repository or a release. The official texts need a TU4 text table that the release
cannot produce: it is read from a patched `SPLITSECOND1.DLL` image, made with a local development tool that is not
part of glimpse_fix (`tu4text.py extract <patched SPLITSECOND1 image> <json>`, once), and passed with
`GLIMPSE_TU4_TEXTS=<json>` (build_all.py: `--tu4-texts`). With it `deadline_mode.py` writes the official name, help
lines 1-5 (empty -> `BLANK`) and `TEXT_TIMETRIALPLUS_PICKUP/_DRIFTPICKUP/_OBJECTIVE1/_OBJECTIVE2` (the DLL's HUD
asks for them), and `../assemble/cartext.py` the official car descriptions (a no-break space becomes a space);
without it they write glimpse_fix's own texts under the same keys. The logs say which. RU is not used: the PC exe knows a RUSSIAN
dictionary, but the PC resident.ark has none.

## Limits and open points

- Without `[DLC] Pickups=1` there are no boxes: the countdown only runs down, from the Xbox start time plus
  `[DLC] DeadlineBonus`. The bonus is added only with `Pickups=0`: with `Pickups=1` and a pack built before stage 2
  (or a track without its `.pkps`, which the install log names) there are neither boxes nor the bonus, so the Xbox
  start time is hardly enough for the lap; rebuild the pack or set `Pickups=0`. Packs built with the old build_pack default
  (`--mode-extra-seconds 50`) carry the 50 s themselves: a start time above 100 s gets no DeadlineBonus (logged);
  with `Pickups=1` they give the boxes plus 50 s, so rebuild them for the Xbox times.
- Locked in quick play unless `[DLC] Modes=1` (or DLCUnlocks11/12, `UnlockAll=1`); see Unlock. The unlock is
  re-applied on every item update, so a mode list read before the first draw after a rebuild would still see it
  locked (none known: the rebuild's own 0x486C90 call ignores the result).
- Quarry is a Deadline track when the track step builds on a pack with the mode (`../assemble/assemble_track.py`,
  `deadline_track.py`): its converted overlay carries the Xbox rival times itself, its `.pkps` 42 boxes.
- A restart that does not pass the event objectives screen keeps the race info: the player's entry then holds the
  session's best run (0x7D5C40 keeps the better value), as a stock Time Trial does; the rivals stay as they are.
- Quick-play movie: `FE_DEADLINE.BIK` only with `[DLC] Modes=1` and the staged movie file (see "Mode icon");
  without the file, `FE_QP_Track10.bik`.
- Event objectives and race results show the bonus icon before the race and the Time Trial icon after it (keyed by
  the race kind, see "Race end screen"; with `[DLC] Modes=0` and `Pickups=0` the kind stays 8: no finish time).
  Screens keyed by the mode type use the TimeTrial texts (DETONATOR).
- Quick play is expected to use the variation-1 overlay; with variation 2 or more the `<sub>2` file is read and the
  mode's 30 s applies.
- Offline only: the DLC's `Network/Leaderboard_Def.xml` is not merged; the mode is not in MultiPlayer/SplitScreen.
