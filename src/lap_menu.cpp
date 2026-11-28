// Laps per race (Steam build): the game's own lap override tweakables, and the lap choice on the track select.
//
// LapSelect uses the game's resolved difficulty action on offline Quick Play Race's track select (one
// screen or split screen, the same screen class). The count is saved as Laps. cLaps::Reset consumes the
// choice for the committed track, with the game's override flag and count restored after each reset.
// In a SteamLAN session the session's laps (the host's pick, online.cpp's rules) are applied the same way
// to every network Race, on every PC: the game sends no lap count. Only setups that used changed laps
// receive the results key substitution and finish-card notice. See docs/TECHNICAL.md.
//
// The default shown is the track's own count, read from the game's data through the game's own file object
// (0x403010 opens a path in its packages, the stream's vtable +0x38 gives the size and +0x20 reads, an error
// is flagged at stream +0x10, 0x403080 closes; its XML loader at 0x546468 uses it the same way):
// GameLogic/Tracks/<location>/Params/RaceSetupOverlay_<subtrack>.params, 'Default Number of Laps' (the
// overlay loader 0x790D90 builds that path; the track select's TEXT_<location>_<subtrack> names both; no
// line: the tweakable's own 3). QuickRace.params sets no count of its own.
#include "common.h"
#include <stdio.h>

static BYTE* const kLapOverride = (BYTE*)0x00D6B6B5;  // "Override Default Number of Laps"
static int* const kLapCount = (int*)0x00D090CC;       // "Number of Laps"
// The button goes up to 9: the HUD shows one digit (DialsInfo.gfx's lap counter has a 30 px laps_total_txt, which
// SetNumLaps writes, right before the closing bracket, and laps_current_txt runs into the "/" with two; the game
// itself never goes past 8). Laps up to kMaxIniLaps can be set in the INI or the settings program, which say so.
static const int kMaxLaps = 9;
static const int kMaxIniLaps = 16;

static bool s_installed;     // all LapSelect hooks are in place (the lap choice on the track select)
static bool s_raceHooks;     // cLaps::Reset and cRaceResults::Update are wrapped (LapSelect or SteamLAN)
static bool s_changedNetwork; // s_changedSetup is a SteamLAN race that got the session's laps
static int s_changedLaps;    // the laps that setup got
static char s_mode[32];      // the GameMode name of the quick play mode last highlighted (QuickRace, ...)
static char s_menuEntry[32]; // the main menu entry last highlighted (TEXT_QUICKPLAY, TEXT_SEASON, ...)
static char s_track[48];     // the track last highlighted on the track select (TEXT_Downtown_A ...)
static const BYTE* s_screen; // the owner of the active Quick Play mode list
static char s_committedTrack[48];
static int s_pickLaps;
static BYTE* s_changedSetup; // the setup that consumed changed laps
static bool s_onTrackSelect;
static void* s_promptBar;    // the prompt bar movie (it gets SetTitleText)
static char s_gameLabel[32]; // the game's last difficulty label (TEXT_EASY ...), put back when the screen is left
static bool s_labelShown;    // our label is on the button now
static char s_label[32];
static char s_shown[32];     // our label as it is on the button now
static char s_entryLabel[32];  // the label chosen when the screen opened, used until the bar has settled
static bool s_entryPending;    // the screen is open, its first label waits for the highlighted track
static bool s_lapsUnsaved;    // the last pick is not in the INI yet (it was busy)
static void SaveLaps(DWORD wait);

// ---- race setup and records

typedef void(__thiscall* LapsReset_t)(void* self, void* context);
typedef void(__thiscall* ResultsUpdate_t)(void* self, float dt);
typedef bool(__thiscall* LeaderboardExists_t)(const void* owner, const char* key);
typedef void(__cdecl* NormalizeKey_t)(char* key);

static void** const kQuickPlayVtable = (void**)0x00BAFD34;
static void** const kLapsVtable = (void**)0x00BA8DE8;
static void** const kResultsVtable = (void**)0x00C1F83C;
static LapsReset_t s_origLapsReset;
static ResultsUpdate_t s_origResultsUpdate;
static const BYTE* const* const kRecordsOwner = (const BYTE* const*)0x00D64E38;
static const char kNoRecordMode[] = "LapSelect.params";
static const char kNoRecordSuffix[] = "LapSelect";

static const char* TrackKey(const char* name)
{
    return !_strnicmp(name, "TEXT_", 5) ? name + 5 : name;
}

// cRaceSetup can be reused. Match the committed track and the actual race kind as well as any saved pointer.
// One screen or split screen (+0x9C, the viewports: 1 or 2).
static bool RaceSetupMatches(const BYTE* setup)
{
    __try {
        if (!setup || !s_committedTrack[0]) return false;
        const BYTE* career = *(const BYTE* const*)0x00D64E40;
        int type = *(const int*)setup;
        int viewports = *(const int*)(setup + 0x9C);
        const char* mode = *(const char* const*)(setup + 0x988);
        const char* sub = *(const char* const*)(setup + 0x1E0);
        char track[48];
        if (!career || career[8] || type == 6 || type == 7 || setup[0x9B2] || (viewports != 1 && viewports != 2) ||
            !mode || _stricmp(mode, "QuickRace.params") || !sub ||
            _snprintf_s(track, _TRUNCATE, "%s_%s", (const char*)(setup + 0xE0), sub) < 0)
            return false;
        return !_stricmp(track, s_committedTrack);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A network Race (setup kinds 6 and 7, the game's multiplayer setups; 0x43FFCB caps their laps at 8) in a
// SteamLAN session.
static bool NetworkRaceSetup(const BYTE* setup)
{
    __try {
        if (!setup) return false;
        const int type = *(const int*)setup;
        const char* mode = *(const char* const*)(setup + 0x988);
        return (type == 6 || type == 7) && mode && !_stricmp(mode, "QuickRace.params");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ChangedSetupMatches(const BYTE* setup)
{
    return s_changedNetwork ? NetworkRaceSetup(setup) : RaceSetupMatches(setup);
}

// a pick that the race did not take, once per setup (the log shows which part of the setup differs)
static void NoteSetupMismatch(const BYTE* setup)
{
    static const BYTE* noted;
    if (!g_cfg.log || setup == noted) return;
    noted = setup;
    __try {
        const char* mode = *(const char* const*)(setup + 0x988);
        const char* sub = *(const char* const*)(setup + 0x1E0);
        Log("laps: %d picked for %s, but this race is %s_%s, kind %d, %d viewport(s), mode %s, flag %d: the game's laps",
            s_pickLaps, s_committedTrack, (const char*)(setup + 0xE0), sub ? sub : "?", *(const int*)setup,
            *(const int*)(setup + 0x9C), mode ? mode : "?", setup[0x9B2]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// The game lays out at most 100 catalogue pointers at owner+0x134 and checks the key at 0x788C40.
// Form the same trimmed key as 0x8029C8..0x8029E0 and use its normalizer and lookup.
static bool ReplacementKeyAbsent(const BYTE* setup)
{
    __try {
        const BYTE* owner = *kRecordsOwner;
        if (!owner || *(const DWORD*)(owner + 0x2C4) > 100) return false;
        const char* sub = *(const char* const*)(setup + 0x1E0);
        char key[128];
        if (!sub || _snprintf_s(key, _TRUNCATE, "%s_%s_%s", (const char*)(setup + 0xE0), sub, kNoRecordSuffix) < 0)
            return false;
        ((NormalizeKey_t)0x00530D40)(key);
        return !((LeaderboardExists_t)0x00788C40)(owner, key);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static BYTE* LapsSetup(BYTE* self)
{
    __try {
        const BYTE* player = *(const BYTE* const*)(self + 0x30);
        const BYTE* race = player ? *(const BYTE* const*)(player + 4) : NULL;
        return race ? *(BYTE* const*)(race + 0x20) : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

static bool ReadLapPair(BYTE* flag, int* count, int* stock)
{
    __try {
        *flag = *kLapOverride;
        *count = *kLapCount;
        *stock = *flag ? *count : *(const int*)0x00D36E30;
        return *flag <= 1 && *count >= 0 && *count <= 4096 && *stock > 0 && *stock <= 4096;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void __fastcall Hook_LapsReset(BYTE* self, void*, void* context)
{
    BYTE* setup = s_raceHooks ? LapsSetup(self) : NULL;
    int want = 0;
    bool network = false;
    if (setup && OnlineSession()) {
        // a SteamLAN session: the session's laps, on every PC the same (the game caps them at 8 after the override)
        if (g_cfg.sessionLaps > 0 && NetworkRaceSetup(setup)) {
            want = g_cfg.sessionLaps > 8 ? 8 : g_cfg.sessionLaps;
            network = true;
        }
    } else if (setup && s_installed && s_pickLaps) {
        if (RaceSetupMatches(setup)) want = s_pickLaps;
        else NoteSetupMismatch(setup);
    }
    BYTE flag;
    int count, stock;
    if (!want || !ReadLapPair(&flag, &count, &stock) || stock == want || !ReplacementKeyAbsent(setup)) {
        s_origLapsReset(self, context);
        return;
    }
    // Reset consumes this pair synchronously at 0x43FFAF. It is called for each car and again on Restart.
    // Restore both values together before leaving; the menu's request stays armed until it is left.
    __try {
        *kLapCount = want;
        *kLapOverride = 1;
        s_origLapsReset(self, context);
    } __finally {
        *kLapOverride = flag;
        *kLapCount = count;
    }
    if (s_changedSetup != setup) {
        if (network) Log("laps: %d per race in the LAN session (stock %d, the session's rules)", want, stock);
        else Log("laps: %d per race (stock %d, %s)", want, stock, s_committedTrack);
    }
    s_changedSetup = setup;
    s_changedNetwork = network;
    s_changedLaps = want;
}

static BYTE* ResultsSetup(BYTE* self)
{
    __try {
        BYTE* results = *(BYTE**)(self + 0x4A0);
        return results ? *(BYTE**)results : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

static bool SubstituteRecordMode(BYTE* setup, const char** original)
{
    __try {
        const char** mode = (const char**)(setup + 0x988);
        *original = *mode;
        *mode = kNoRecordMode;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void RestoreRecordMode(BYTE* setup, const char* original)
{
    __try {
        const char** mode = (const char**)(setup + 0x988);
        if (*mode == kNoRecordMode) *mode = original;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lap choice: could not restore the race setup mode");
    }
}

static void __fastcall Hook_ResultsUpdate(BYTE* self, void*, float dt)
{
    BYTE* setup = s_changedSetup;
    BYTE* resultsSetup = setup ? ResultsSetup(self) : NULL;
    if (!setup || (resultsSetup && resultsSetup != setup)) {
        s_origResultsUpdate(self, dt);
        return;
    }

    // Stock advances the movie at 0x802B12, then checks the current result at 0x802B69.
    // Protect the whole call so a callback cannot move the store ahead of the rejection.
    // The writer rejects unknown keys at 0x795B88, before changing the profile or holder.
    const char* original;
    if (!ChangedSetupMatches(setup) || !ReplacementKeyAbsent(setup) || !SubstituteRecordMode(setup, &original)) {
        static bool logged;
        if (!logged) Log("lap choice: results held because the no-record key could not be verified");
        logged = true;
        return;
    }
    __try {
        s_origResultsUpdate(self, dt);
    } __finally {
        RestoreRecordMode(setup, original);
    }
}

static bool RaceHooksReady()
{
    __try {
        if (!ClassIs(kLapsVtable, 0x00C77258, ".?AVcLaps@GameLogicNew@@") ||
            (DWORD)kLapsVtable[2] != 0x0043FF40 ||
            !ClassIs(kResultsVtable, 0x00C972C0, ".?AVcRaceResults@InGameUI@@") ||
            (DWORD)kResultsVtable[3] != 0x00802AF0)
            return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return BytesMatch((void*)0x0043FF40, "83EC080F57C05355568BF1578B7E2C") &&
           BytesMatch((void*)0x0043FFA5, "E8061A350033DB894500381DB5B6D60074098B0DCC90D000") &&
           BytesMatch((void*)0x0043FFC0, "8B56308B42048B40208B0083F806") &&
           BytesMatch((void*)0x00428445, "8B0E8B018B500853FFD2") &&  // pushes the reset context
           BytesMatch((void*)0x004400E4, "5F5E5D5B83C408C20400") &&
           BytesMatch((void*)0x007919B0, "A1306ED300C3") &&
           BytesMatch((void*)0x00802AF0, "6AFF686861B600") &&
           BytesMatch((void*)0x00802B69, "8BBEA0040000807F3000742D8B5718") &&
           BytesMatch((void*)0x00802B97, "8B8EA0040000E8CEFCFFFF") &&
           BytesMatch((void*)0x0080296A, "55E880B8F8FF8BF88B068B80E0010000") &&
           BytesMatch((void*)0x0080299E, "8B0605880900008B00") &&
           BytesMatch((void*)0x008029D0, "6AF98D4C2410E8C5CAD2FF8B54240C52") &&
           BytesMatch((void*)0x00802A40, "8B0D384ED600505281C1E0020000E80D31F9FF") &&
           BytesMatch((void*)0x00795B60, "53568B74240C85F68BD9") &&
           BytesMatch((void*)0x00788C40, "53558B6C240C85ED8BD9") &&
           BytesMatch((void*)0x00530D40, "53558B6C240C575533DB");
}

// ---- the game's data

struct GameFile { void* vt; BYTE* stream; BYTE owns; BYTE pad[3]; };
typedef void*(__thiscall* GameFileOpen_t)(GameFile* f, const char* path);
typedef void(__thiscall* GameFileClose_t)(GameFile* f);
typedef unsigned(__thiscall* StreamSize_t)(void* stream);
typedef unsigned(__thiscall* StreamRead_t)(void* stream, void* buf, unsigned size);
static bool s_fileApi;  // the file object's code is as expected (checked at install)

// Resident paths use ArkLoader, with WinFileLoader for loose files. The file wrapper also calls
// slots 2 and 0 when it closes either stream.
static bool ReadStreamKnown(void** vt)
{
    if (vt == (void**)0x00BA03A4)
        return ClassIs(vt, 0x00C73238, ".?AVArkLoader@@") &&
               (DWORD)vt[0] == 0x004018F0 && (DWORD)vt[2] == 0x00572440 &&
               (DWORD)vt[8] == 0x005739A0 && (DWORD)vt[14] == 0x00573A00 &&
               BytesMatch(vt[8], "8B490C8B018B4020") && BytesMatch(vt[14], "568BF18B4E0C8B01");
    if (vt == (void**)0x00BA07C4)
        return ClassIs(vt, 0x00C73824, ".?AVWinFileLoader@@") &&
               (DWORD)vt[0] == 0x004023F0 && (DWORD)vt[2] == 0x005727F0 &&
               (DWORD)vt[8] == 0x00572850 && (DWORD)vt[14] == 0x005727B0 &&
               BytesMatch(vt[8], "538B5C240C85DB55") && BytesMatch(vt[14], "8B91A401000085D2");
    return false;
}

// a file of the game's packages as text, 0-terminated; false when it is not there, too big or not read whole
static bool ReadGameFile(const char* path, char* buf, unsigned cap)
{
    if (!s_fileApi) return false;
    GameFile f = {};
    bool ok = false;
    __try {
        ((GameFileOpen_t)0x00403010)(&f, path);
        void** vt = f.stream ? *(void***)f.stream : NULL;
        if (f.stream && !ReadStreamKnown(vt)) {
            Log("lap choice: unknown track file stream %p", (void*)vt);
            s_fileApi = false;
            return false;
        }
        if (vt && *(DWORD*)(f.stream + 0x10) == 0) {
            unsigned size = ((StreamSize_t)vt[0x38 / 4])(f.stream);
            if (size && size < cap) {
                memset(buf, 0, size + 1);  // nothing of an earlier file can be parsed
                ((StreamRead_t)vt[0x20 / 4])(f.stream, buf, size);
                ok = *(DWORD*)(f.stream + 0x10) == 0;  // the stream flags a failed or short read
            }
        }
        ((GameFileClose_t)0x00403080)(&f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("lap choice: reading %s failed, defaults no longer read", path);
        s_fileApi = false;
        return false;
    }
    return ok;
}

// the value of 'key' = <number> in a params text; -1 when not there
static int ParamValue(const char* text, const char* key)
{
    char pat[64];
    sprintf_s(pat, "'%s' = ", key);
    const char* at = strstr(text, pat);
    return at ? atoi(at + strlen(pat)) : -1;
}

// the track's own count from its overlay params; 0 = not readable
static int GameLaps(const char* track)
{
    static char text[256 * 1024];
    char location[48], path[160];
    strncpy_s(location, track, _TRUNCATE);
    char* sub = strrchr(location, '_');  // TEXT_<location>_<subtrack>: the subtrack is the last part
    if (!sub) return 0;
    *sub++ = 0;
    sprintf_s(path, "Resident(noassert)|GameLogic/Tracks/%s/Params/RaceSetupOverlay_%s.params", location, sub);
    if (!ReadGameFile(path, text, sizeof text)) return 0;
    int n = ParamValue(text, "Default Number of Laps");
    return n >= 1 && n <= 64 ? n : 3;  // no line: the tweakable's own value
}

// the highlighted track's own lap count, 0 = not known (read once per track)
static int DefaultLaps()
{
    if (!s_track[0]) return 0;
    const char* t = TrackKey(s_track);
    static struct { char track[48]; int laps; } cache[64];
    static int cached;
    for (int i = 0; i < cached; i++)
        if (!strcmp(cache[i].track, t)) return cache[i].laps;
    int n = GameLaps(t);
    if (cached < 64) {
        strcpy_s(cache[cached].track, t);
        cache[cached++].laps = n;
    }
    if (n) Log("lap choice: %s is %d laps in the game's data", t, n);
    else Log("lap choice: %s: no lap count in the game's data, shown as ?", t);
    return n;
}

// ---- the label

// Only quick play's Race picks laps: Elimination ends with the field (its 8 laps are a ceiling), Detonator
// is one timed lap, Survival, Air Strike and Air Revenge have no difficulty button to take over, and the DLC
// modes keep their own rules. Reached from the main menu's Quick Play or Split Screen entry (both open the same
// quick play screen, cNewQuickPlay); LAN and online pick on their own screens (a SteamLAN session takes the
// host's pick, see the top).
static bool RaceMode()
{
    return !_stricmp(s_mode, "QuickRace") &&
           (!strcmp(s_menuEntry, "TEXT_QUICKPLAY") || !strcmp(s_menuEntry, "TEXT_SPLIT_SCREEN"));
}

// dlcmode.cpp's mode list hook: the GameMode name of the highlighted quick play mode ("" when not readable)
void LapMenuModeHighlighted(const BYTE* screen, const char* gameMode)
{
    if (!s_installed) return;
    if (strcmp(s_mode, gameMode) && g_cfg.log) Log("lap choice: mode %s highlighted", gameMode[0] ? gameMode : "(unknown)");
    s_screen = screen;
    strncpy_s(s_mode, gameMode, _TRUNCATE);
}

static bool TrackUnlocked()
{
    __try {
        return s_screen && *(void***)(s_screen) == kQuickPlayVtable &&
               *(const DWORD*)(s_screen + 0x240) == 1 &&
               !((bool(__thiscall*)(const void*))0x004866A0)(s_screen + 0x140);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// the lap choice owns the difficulty button right now: on Race's track select, with the prompt bar it saw
// that screen's title on still being the game's prompt bar
static bool Active()
{
    if (!s_installed || !s_fileApi || !s_onTrackSelect || !s_promptBar || !RaceMode() || !TrackUnlocked()) return false;
    void* now = PromptBarMovieNow();
    if (now == s_promptBar) return true;
    static bool logged;
    if (!logged) Log("lap choice: the prompt bar changed without a new title (%p, now %p), the button left alone", s_promptBar, now);
    logged = true;
    return false;
}

static const char* const kButtonPaths[2] = { "main.bar_mc.buttonY_mc.text_txt.text", "main.barskew_mc.buttonY_mc.text_txt.text" };

static bool IsButtonPath(const char* path)
{
    return path && (!strcmp(path, kButtonPaths[0]) || !strcmp(path, kButtonPaths[1]));
}

static bool IsDifficulty(const char* key)
{
    return key && (!strcmp(key, "TEXT_EASY") || !strcmp(key, "TEXT_MEDIUM") || !strcmp(key, "TEXT_HARD"));
}

static const char* Label()
{
    int def = DefaultLaps();
    char d[4] = "?";
    if (def) sprintf_s(d, "%d", def);
    // the number that changes goes last, so nothing on the button moves with it
    if (g_cfg.laps) sprintf_s(s_label, "LAPS (DEFAULT %s): %d", d, g_cfg.laps);
    else sprintf_s(s_label, "LAPS (DEFAULT %s): %s", d, d);
    return s_label;
}

// true when the button took it
// our label (ours) or the game's own text back on the button; the state follows only a write that took
static bool ShowLabel(const char* text, bool ours)
{
    bool ok = UiSetText(s_promptBar, kButtonPaths[0], text);
    if (ok) {
        strcpy_s(s_shown, ours ? text : "");
        s_labelShown = ours;
    }
    UiSetText(s_promptBar, kButtonPaths[1], text);
    return ok;
}

// The game places the button by its text width (it sets main.bar_mc.buttonY_mc._x, animated), so a new
// label would slide it. The place it takes for the first label on the screen is kept for every later one.
static const char kButtonX[] = "main.bar_mc.buttonY_mc._x";
static bool s_pinned;
static double s_buttonX;
static bool s_haveX;
static DWORD s_enteredAt;  // the bar slides in with the screen: the place is taken once that is over
static DWORD s_lastXAt;    // the game's last move of the button
static bool s_relabelPending;

// the slide-in is over: the game has not moved the button for a while (or never does)
static bool Settled()
{
    DWORD now = GetTickCount();
    if (!s_haveX) return now - s_enteredAt > 1500;
    return now - s_enteredAt > 800 && now - s_lastXAt > 150;
}

// a new label while the bar still slides in would move the button's final place: it waits for the slide
static void Relabel()
{
    if (!Active()) {
        s_relabelPending = false;
        return;
    }
    if (!strcmp(Label(), s_shown)) {
        s_relabelPending = false;
        return;
    }
    if (!Settled()) {
        s_relabelPending = true;
        return;
    }
    if (s_haveX) s_pinned = true;
    s_relabelPending = !ShowLabel(s_label, true);  // not taken: tried again next frame
}

// The screen's first label. The game names the highlighted track right after the title, in the same
// frame, so the label waits for it and opens with the track's default instead of "?".
static void ShowEntry()
{
    s_entryPending = false;
    strcpy_s(s_entryLabel, Label());
    s_relabelPending = !ShowLabel(s_entryLabel, true);  // not taken: tried again once the bar has settled
}

// every frame: a label held back during the slide-in goes up once the bar has settled
void LapMenuTick()
{
    if (s_entryPending && Active() && GetTickCount() - s_enteredAt > 100) ShowEntry();
    if (s_relabelPending && Settled()) Relabel();
    if (s_lapsUnsaved) SaveLaps(0);
}

bool LapMenuButtonX(void* movie, const char* path, double* x)
{
    if (!Active() || movie != s_promptBar || !path || strcmp(path, kButtonX)) return false;
    if (!s_pinned) {
        if (!s_haveX || *x != s_buttonX) s_lastXAt = GetTickCount();
        s_buttonX = *x;
        s_haveX = true;
        return false;
    }
    *x = s_buttonX;
    return true;
}

// the game's SetVariable on the prompt bar's button text: on Race's track select its difficulty label is
// replaced by the label on the button now (a change goes through Relabel, which waits for the slide-in).
// Nothing is recorded here; LapMenuButtonTextSet does that once the write is known to have taken.
const char* LapMenuLabel(void* movie, const char* path, const GFxValue* value)
{
    if (!s_installed || movie != s_promptBar || !IsButtonPath(path) || !value || value->type != GFX_STRING ||
        IsBadStringPtrA(value->str, 32) || !IsDifficulty(value->str))
        return NULL;
    strcpy_s(s_gameLabel, value->str);  // the game's own label, to put back when the screen is left
    if (!Active()) return NULL;
    if (s_entryPending) strcpy_s(s_entryLabel, Label());  // the track is not named yet: the best label so far
    // what is on the button, else the label the screen opened with; a newer one goes up through Relabel,
    // which pins the button first
    return !strncmp(s_shown, "LAPS", 4) ? s_shown : s_entryLabel;
}

// after the game's write of the button text: ours (substituted) or its own
void LapMenuButtonTextSet(void* movie, const char* path, const char* text, bool substituted, bool ok)
{
    if (!s_installed || !ok || movie != s_promptBar || !path || strcmp(path, kButtonPaths[0])) return;
    if (substituted) {
        s_labelShown = true;
        if (text != s_shown) strcpy_s(s_shown, text);
    } else {
        s_labelShown = false;  // the game's own text is on the button now
        s_shown[0] = 0;
    }
}

// ---- the screens

static bool StringArg(const GFxValue* args, unsigned count, unsigned i)
{
    return args && i < count && args[i].type == GFX_STRING && args[i].str && !IsBadStringPtrA(args[i].str, 64);
}

static bool BoolArg(const GFxValue* args, unsigned count, unsigned i)
{
    return args && i < count && args[i].type == 2 && *(const BYTE*)&args[i].num;
}

static bool MovieIs(void* movie, const char* name)
{
    char got[48];
    return ReadMovieName(movie, got, sizeof got) && !_stricmp(got, name);
}

// the main menu's entries (table 0xD6FBE0) and its profile submenu: other TEXT_ keys (an untranslated mode name,
// for one) leave the entry as it is
static bool IsMainMenuEntry(const char* key)
{
    static const char* const kEntries[] = { "TEXT_SEASON", "TEXT_QUICKPLAY", "TEXT_ONLINE", "TEXT_LAN", "TEXT_PROFILE",
                                            "TEXT_SPLIT_SCREEN", "TEXT_OPTIONS", "TEXT_CONTROLS", "TEXT_EXTRAS", "TEXT_QUIT" };
    for (const char* e : kEntries)
        if (!strcmp(key, e)) return true;
    return false;
}

static void ClearRaceChoice()
{
    s_committedTrack[0] = 0;
    s_pickLaps = 0;
    s_changedSetup = NULL;
}

// the game's Invoke: the highlighted mode and track, and the prompt bar's title (the screen)
void LapMenuInvoke(void* movie, const char* path, const GFxValue* args, unsigned count)
{
    if (!s_installed || !path) return;
    if (!strcmp(path, "SetTrack")) {  // FE_quick_play_DetailScreen: (name, ?, highlighted)
        if (StringArg(args, count, 0) && BoolArg(args, count, 2) && MovieIs(movie, "FE_quick_play_DetailScreen.gfx")) {
            strncpy_s(s_track, args[0].str, _TRUNCATE);
            if (s_entryPending && Active()) ShowEntry();
            else Relabel();
        }
        return;
    }
    if (!strcmp(path, "SetActive")) {  // FE_MediaModule03: (name, highlighted, ...): the main menu's are TEXT_ keys
        if (StringArg(args, count, 0) && BoolArg(args, count, 1) && IsMainMenuEntry(args[0].str) &&
            MovieIs(movie, "FE_MediaModule03_Movie.gfx")) {
            if (strcmp(s_menuEntry, args[0].str) && g_cfg.log) Log("lap choice: menu entry %s highlighted", args[0].str);
            strncpy_s(s_menuEntry, args[0].str, _TRUNCATE);
            if (!RaceMode()) ClearRaceChoice();
        }
        return;
    }
    if (strcmp(path, "main.SetTitleText") || !StringArg(args, count, 0) || !MovieIs(movie, "Prompt_Bar.gfx")) return;
    const char* title = args[0].str;
    bool track = !strcmp(title, "TEXT_CHOOSE_TRACK");
    if (!strcmp(title, "TEXT_FE_main_menu_NAME")) {  // back in the main menu: season next, maybe
        ClearRaceChoice();
        s_screen = NULL;
        s_mode[0] = 0;
    }
    if (!strcmp(title, "TEXT_CHOOSE_GAMEMODE")) ClearRaceChoice();
    bool newBar = movie != s_promptBar;
    if (track == s_onTrackSelect && !(track && newBar)) {
        if (newBar) s_gameLabel[0] = 0;  // that label belonged to the old bar
        s_promptBar = movie;
        return;
    }
    if (!track) {
        // A car-select transition commits the pending track, not the lap override itself.
        ClearRaceChoice();
        if (RaceMode() && !strcmp(title, "TEXT_FE_vehicle_select_NAME") && g_cfg.laps && DefaultLaps()) {
            strcpy_s(s_committedTrack, TrackKey(s_track));
            s_pickLaps = g_cfg.laps;
        }
        s_onTrackSelect = false;
        s_relabelPending = s_entryPending = false;
        // the game's label goes back on the bar that showed ours, only while that is still the game's bar
        if (s_labelShown && s_gameLabel[0] && !newBar && s_promptBar == PromptBarMovieNow())
            ShowLabel(s_gameLabel, false);  // the next screen sets its own button text if it has one
        else
            s_labelShown = false;
        s_promptBar = movie;
        if (newBar) s_gameLabel[0] = 0;  // that label belonged to the old bar
        return;
    }
    // a track select opens (or opens again on a new bar): everything about the old bar is dropped
    if (newBar) s_gameLabel[0] = 0;
    s_promptBar = movie;
    s_onTrackSelect = true;
    ClearRaceChoice();
    s_track[0] = 0;
    s_pinned = s_haveX = s_relabelPending = s_entryPending = false;
    s_labelShown = false;
    s_shown[0] = 0;
    s_enteredAt = GetTickCount();
    if (!RaceMode()) return;  // the difficulty stays on the button
    s_entryPending = true;  // ShowEntry, once SetTrack names the highlighted track
}

// ---- the finish card
//
// The race end screen (RaceEnd.gfx) shows "NEW PERSONAL BEST" under the time when cRaceEnd +0x810 is 1 (set by
// 0x807730 from the time and the stored best, before the record is written; read at 0x80856A), passing the key
// TEXT_NEW_PERSONAL_BEST, or "", with the time to the movie (queued by 0x4DE530, run through Invoke). A race with
// a picked lap count writes no record (Hook_ResultsUpdate), so that line says so instead.
static const char kNoRecordText[] = "NO RECORD: LAPS CHANGED";

// true when out[] (room for count values) holds the arguments to use instead
bool LapMenuInvokeArgs(void* movie, const char* path, const GFxValue* args, unsigned count, GFxValue* out)
{
    if (!s_changedSetup || !args || count > 8) return false;
    int at = -1;
    __try {
        for (unsigned i = 0; i < count; i++)
            if (args[i].type == GFX_STRING && args[i].str && !strcmp(args[i].str, "TEXT_NEW_PERSONAL_BEST")) at = (int)i;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (at < 0 || !MovieIs(movie, "RaceEnd.gfx") || !ChangedSetupMatches(s_changedSetup)) return false;
    memcpy(out, args, count * sizeof(GFxValue));
    out[at].str = kNoRecordText;
    Log("lap choice: finish card: \"%s\" instead of the new personal best (%s, %d laps)", kNoRecordText,
        path ? path : "?", s_changedLaps);
    return true;
}

// ---- the button

// The pick goes into the INI as Laps. glimpse_config.exe rewrites the whole INI; a named mutex keeps its
// read-change-replace and this one-key write apart. Without the mutex nothing is written: the pick stays in
// memory and LapMenuTick tries again (without waiting) until it is saved.
static void SaveLaps(DWORD wait)
{
    static HANDLE iniLock;
    static DWORD lastAttempt;
    DWORD now = GetTickCount();
    if (!wait && now - lastAttempt < 1000) return;
    lastAttempt = now;
    if (!iniLock) iniLock = CreateMutexA(NULL, FALSE, "Local\\GlimpseFixIni");
    DWORD w = iniLock ? WaitForSingleObject(iniLock, wait) : WAIT_FAILED;
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        if (w == WAIT_FAILED) {
            DWORD error = GetLastError();
            if (iniLock) CloseHandle(iniLock);
            iniLock = NULL;
            if (wait) Log("lap choice: could not acquire the INI lock (error %lu), will retry", error);
        } else if (wait) {
            Log("lap choice: the INI is busy, will retry");
        }
        return;
    }
    char ini[MAX_PATH], text[8];
    ModulePath(ini, "SplitSecondPad.ini");
    sprintf_s(text, "%d", g_cfg.laps);
    WritePrivateProfileStringA(NULL, NULL, NULL, ini);  // drop Windows' cached copy: the file as the tool left it
    if (WritePrivateProfileStringA("Gameplay", "Laps", text, ini)) s_lapsUnsaved = false;
    else if (wait) Log("lap choice: could not write the INI (error %lu), will retry", GetLastError());
    WritePrivateProfileStringA(NULL, NULL, NULL, ini);  // on disk before the lock is released
    ReleaseMutex(iniLock);
}

// 1, 2, ... 10, 1 in plain order; the game's own count stands for itself (Laps=0, the game's default).
// Unknown default: the game's count, 1 ... 9, the game's count again.
static void NextLaps()
{
    int def = DefaultLaps();
    if (def) {
        int cur = g_cfg.laps ? g_cfg.laps : def;
        int next = cur >= kMaxLaps ? 1 : cur + 1;
        g_cfg.laps = next == def ? 0 : next;
    } else {
        g_cfg.laps = g_cfg.laps >= kMaxLaps ? 0 : g_cfg.laps + 1;
    }
    s_lapsUnsaved = true;
    SaveLaps(200);
    Relabel();
    if (g_cfg.laps) Log("lap choice: %d laps", g_cfg.laps);
    else Log("lap choice: the game's laps (%d)", def);
}

// cNewQuickPlay::OnInput receives the game's resolved actions. The difficulty action (0x21)
// is already mapped to byte 3 by 0x4D628E. Leave the packet alone outside this screen and action.
typedef void(__thiscall* QuickPlayInput_t)(void* self, BYTE* actions);
typedef bool(__thiscall* QuickPlayCheck_t)(const void* self);
static QuickPlayInput_t s_origQuickPlayInput;

static BYTE TakeDifficultyAction(const BYTE* self, BYTE* actions)
{
    if (self != s_screen || !Active()) return 0;
    __try {
        // The original takes Select and Back before difficulty. Leave those combinations intact.
        if (!actions || !actions[3] || actions[0] || actions[8] || actions[1] || actions[9] ||
            !((QuickPlayCheck_t)0x00479C70)(self)) return 0;
        BYTE value = actions[3];
        actions[3] = 0;
        return value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static void __fastcall Hook_QuickPlayInput(BYTE* self, void*, BYTE* actions)
{
    BYTE difficulty = TakeDifficultyAction(self, actions);
    if (!difficulty) {
        s_origQuickPlayInput(self, actions);
        return;
    }
    __try {
        NextLaps();
        s_origQuickPlayInput(self, actions);
    } __finally {
        actions[3] = difficulty;
    }
}

static bool QuickPlayInputReady()
{
    __try {
        return ClassIs(kQuickPlayVtable, 0x00C79BD0, ".?AVcNewQuickPlay@Frontend@@") &&
               (DWORD)kQuickPlayVtable[11] == 0x0047A170 &&
               BytesMatch((void*)0x0047A170, "83EC0C56578B7C2418578BF1E82FD8FDFF") &&
               BytesMatch((void*)0x0047A1A8, "807F03000F84990000008BCEE8B7FAFFFF") &&
               BytesMatch((void*)0x00479C70, "6AFF682860B30064A1") &&
               BytesMatch((void*)0x004866A0, "8B0183786000740C8B50608B40") &&
               BytesMatch((void*)0x004D628E, "884603");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool LapMenuInstalled()
{
    return s_installed;
}

bool LapRaceHooksInstalled()
{
    return s_raceHooks;
}

// what this player's SteamLAN rules offer as a host: its lap pick, only with the lap choice in place
int HostLaps()
{
    return s_installed && s_raceHooks && g_cfg.laps > 0 && g_cfg.laps <= kMaxIniLaps ? g_cfg.laps : 0;
}

// The race hooks (cLaps::Reset, cRaceResults::Update) go in with LapSelect=1 and with SteamLAN=1: a guest applies
// the session's laps also without the lap choice of its own. The lap choice (the track select's button) only with
// LapSelect=1; when it cannot go in, the race hooks stay only for SteamLAN.
void InstallLapMenu()
{
    const bool lan = g_cfg.steamLan && (g_cfg.patchMask & PATCH_ONLINE);
    if (!g_cfg.lapSelect && !lan) return;
    if (CurrentBuild() != BUILD_STEAM || !(g_cfg.patchMask & PATCH_LAPS)) {
        Log("lap choice: disabled by the build or PatchMask");
        return;
    }
    if (!RaceHooksReady()) {
        Log("lap choice: race setup or results signature mismatch, no lap count applied");
        return;
    }
    s_origResultsUpdate = (ResultsUpdate_t)kResultsVtable[3];
    s_origLapsReset = (LapsReset_t)kLapsVtable[2];
    if (!WriteSlot(&kResultsVtable[3], (void*)Hook_ResultsUpdate)) {
        Log("lap choice: results slot could not be written, no lap count applied");
        return;
    }
    if (!WriteSlot(&kLapsVtable[2], (void*)Hook_LapsReset)) {
        if (!WriteSlot(&kResultsVtable[3], (void*)s_origResultsUpdate)) Log("lap choice: results slot could not be restored");
        Log("lap choice: lap slot could not be written, no lap count applied");
        return;
    }
    s_raceHooks = true;
    if (lan) Log("laps: in a SteamLAN session the session's Race laps apply on this PC (the host's pick)");

    const char* why = NULL;
    if (!g_cfg.lapSelect)
        why = "";
    else if (!PadsWrapped() && !FrameHookInstalled())
        why = "no per-frame caller for prompt and save retries";
    else if (!BytesMatch((void*)0x00469CB0, "A10C4ED600578BF98B4808") ||
             !BytesMatch((void*)0x00403010, "6AFF6838EEB200") || !BytesMatch((void*)0x00403080, "568BF18B4E04"))
        why = "prompt bar or file API signature mismatch";
    else if (!QuickPlayInputReady() || !MenuHookReady() || !QuickPlayModeListReady() ||
             !BytesMatch((void*)0x004867D1, "8B47048B401485C0") || !BytesMatch((void*)0x004867DF, "83B84002000000"))
        why = "input or menu signature mismatch";
    if (!why) {
        s_origQuickPlayInput = (QuickPlayInput_t)kQuickPlayVtable[11];
        if (WriteSlot(&kQuickPlayVtable[11], (void*)Hook_QuickPlayInput)) {
            InstallMenuHook(true);
            // Install the shared mode wrapper last, so failure needs no rollback of that slot.
            if (MenuHookInstalled() && HookQuickPlayModeList()) {
                s_fileApi = true;
                s_installed = true;
                Log("lap choice: Quick Play Race difficulty action picks laps, one screen and split screen (%d now, "
                    "0 = the game's)", g_cfg.laps);
                return;
            }
            RemoveLapMenuHook();
            if (!WriteSlot(&kQuickPlayVtable[11], (void*)s_origQuickPlayInput)) Log("lap choice: input slot could not be restored");
        }
        why = "prerequisite hook could not be installed";
    }
    if (*why) Log("lap choice: %s, no lap choice on the track select", why);
    if (lan) return;  // the session's laps still apply
    s_raceHooks = false;
    if (!WriteSlot(&kLapsVtable[2], (void*)s_origLapsReset)) Log("lap choice: lap slot could not be restored");
    if (!WriteSlot(&kResultsVtable[3], (void*)s_origResultsUpdate)) Log("lap choice: results slot could not be restored");
}
