# Online over Steam (experimental)

The game's own LAN mode, played over the internet with the players of a Steam lobby: no server, no
sign-up, no ports to open. Steam carries the data, directly or through its relays. **This is new and
needs testing: please try it and report what you find in the
[issues](https://github.com/frankyfife/glimpse_fix/issues), with `SplitSecondPad.log` (`Log=1`) from
every PC involved.**

> **Important: online play needs one file that is NOT in the download.** Copying the fix is not
> enough: put `steam_api.dll` (32 bit) next to `SplitSecond.exe` yourself. It comes with Valve's
> Steamworks SDK (free download after signing in at partner.steamgames.com), file
> `sdk/redistributable_bin/steam_api.dll` (not the 64 bit `steam_api64.dll`). Without it the game runs
> as before, but online play stays off, and with `SteamLAN=1` and `Log=1` the log says that
> `steam_api.dll` was not found. With `SteamLAN=0` the file is not looked for at all.

**What you need**

* **`steam_api.dll` next to `SplitSecond.exe`** (see above).
* `SteamLAN=1` (settings program, page *Online*) on every PC, all with the same version of the fix.
* The game started through Steam.

**Where you land** when the game starts

| Setting | You meet |
|---|---|
| Lobby name empty | everybody else with an empty name, in the public lobby "glimpse" |
| A lobby name | everybody with the same name (letters, digits, `-` and `_`); Steam friends of a member can still join through "Join game" |
| *Friends only* | your Steam friends: you join a friend's lobby if one is playing, otherwise you open one that only your friends can find |
| Steam's "Join game" on a friend | that friend's lobby, also when your game is not running yet |

Two players who open a lobby of the same name at the same moment end up together in the older one
within 15 seconds. A full lobby (8 players) makes the next player open a second one.

**Playing**

* In the game choose *LAN*, then create a game or search for one. Several games can run in one lobby.
* The player who creates a game is its host: the host's mode and track apply, and so do the host's
  settings for boost, handbrake, power bar fill, AI boost and respot time (`WreckTime`), for everyone
  in that game, and its lap pick (`LapSelect`, up to 8 laps) for Race, once every player in the lobby
  runs a glimpse_fix that takes it. A guest uses its own buttons and keys for them, also when it has them off itself; when
  the boost or handbrake button is Y, the game's camera change moves to D-pad down.
* A banner at the top left shows the lobby, the players (2/8), the game mode and whose rules apply.
  The photo mode is a free camera there; the race goes on. The lobby name and the mode are also in
  your Steam status, which your friends can see in your game info.

Online play runs on the German/English and on the Russian Steam version. **Not yet:** split screen
in an online game. Tested so far with two players, in Race and Eliminator.

![The banner in a game's lobby: lobby, players, mode and the host's rules](images/online-lobby.jpg)
