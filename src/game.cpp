// Split/Second controller fix - patches into SplitSecond.exe (Steam build, image base 0x400000)
#include "common.h"
#include <intrin.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Addresses in the decrypted image. Each one is checked against the expected bytes first.
static BYTE* const kPadPresets = (BYTE*)G(0x00CFABF8);          // per-device binding presets, preset 0 = Xbox 360 pad
static BYTE* const kPromptBarSetButtons = (BYTE*)G(0x004DADE0);  // cLayer_PromptBar: sets PC key icons + key names
static void** const kIatXInputSetState = (void**)G(0x00B9B370);

static const int kPresetEntries = 57;  // 5 dwords each: action, key, source index, source type, pov angle
enum { SRC_BUTTON = 0, SRC_AXIS = 1, SRC_POV = 2 };
enum { AXIS_ZPOS = 4, AXIS_ZNEG = 5, AXIS_RZPOS = 10 };
enum { ACTION_THROTTLE = 0x29, ACTION_BRAKE = 0x2A };

bool BytesMatch(const void* at, const char* hex)
{
    BYTE want[64];
    size_t n = 0;
    for (; hex[0] && hex[1] && n < sizeof(want); hex += 2) {
        char b[3] = { hex[0], hex[1], 0 };
        want[n++] = (BYTE)strtoul(b, NULL, 16);
    }
    return CodeMatches(at, want, n);
}

bool CodeMatches(const void* at, const void* expected, size_t n)
{
    BYTE want[64];
    if (n > sizeof(want)) return false;
    memcpy(want, expected, n);
    // addresses inside the expected bytes (operands) are where this build has them
    for (size_t i = 0; i + 4 <= n; i++) {
        DWORD v = *(DWORD*)(want + i), mapped;
        if (v >= 0x00401000 && MapEmbedded(v, &mapped)) {
            *(DWORD*)(want + i) = mapped;
            i += 3;
        }
    }
    if (!at) return false;
    const BYTE* p = (const BYTE*)at;
    __try {
        for (size_t i = 0; i < n; i++)
            if (p[i] != want[i]) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// one pointer slot (vtable or import entry); false, with nothing written, when the page cannot be made writable
bool WriteSlot(void** slot, void* value)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *slot = value;
    if (!VirtualProtect(slot, sizeof(void*), old, &old))
        Log("slot %p written, its page protection could not be put back (error %lu)", (void*)slot, GetLastError());
    return true;
}

void WriteMemory(void* at, const void* src, size_t len)
{
    DWORD old;
    VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(at, src, len);
    VirtualProtect(at, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, len);
}

// ---------------------------------------------------------------- triggers

static void PatchTriggerPreset()
{
    DWORD* e = (DWORD*)kPadPresets;
    DWORD* throttle = NULL;
    DWORD* brake = NULL;
    for (int i = 0; i < kPresetEntries; i++, e += 5) {
        if (e[0] == ACTION_THROTTLE && e[3] == SRC_AXIS && e[2] == AXIS_ZNEG) throttle = e;
        if (e[0] == ACTION_BRAKE && e[3] == SRC_AXIS && e[2] == AXIS_ZPOS) brake = e;
    }
    if (!throttle || !brake) {
        Log("trigger preset not found, leaving bindings alone");
        return;
    }
    // LT already sits on Z+ (our Z axis carries only LT); move the throttle from Z- to RZ+ (RT)
    DWORD rz = AXIS_RZPOS;
    WriteMemory(&throttle[2], &rz, sizeof(rz));
    Log("360 preset: throttle -> RZ+, brake stays Z+");
}

// ---------------------------------------------------------------- binding names

// {code, name} tables behind the controls screen; a name without a translation is shown as is
struct InputName {
    DWORD code;
    const char* name;
};
static InputName* const kJoyAxisNames = (InputName*)G(0x00CFA5E0);    // JOY_AXIS_XPOS .. JOY_AXIS_SLIDER1NEG
static InputName* const kJoyButtonNames = (InputName*)G(0x00CFA6E0);  // JOY_BUTTON_1 .. JOY_BUTTON_16

static bool s_knownBuild;

bool KnownBuild() { return s_knownBuild; }

void PatchPadNames()
{
    if (!s_knownBuild || !(g_cfg.patchMask & PATCH_PADNAMES)) return;
    static const char* const buttons[] = { "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS", "RS" };
    // DirectInput order: X+ X- Y+ Y- Z+ Z- RX+ RX- RY+ RY- RZ+ RZ-; Y grows downwards
    const char* axes[12] = { "LS Right", "LS Left", "LS Down", "LS Up", "LT", "-", "RS Right", "RS Left", "RS Down", "RS Up", "RT", "-" };
    if (!g_cfg.separateTriggers) {
        axes[5] = "RT";   // both triggers on Z
        axes[10] = "-";
    }
    if (!BytesMatch(kJoyButtonNames[0].name, "4A4F595F425554544F4E5F3100") ||  // "JOY_BUTTON_1"
        !BytesMatch(kJoyAxisNames[0].name, "4A4F595F415849535F58504F5300")) {  // "JOY_AXIS_XPOS"
        Log("binding name tables not found");
        return;
    }
    for (int i = 0; i < 10; i++) {
        if (kJoyButtonNames[i].code != (DWORD)i) return;
        WriteMemory(&kJoyButtonNames[i].name, &buttons[i], sizeof(char*));
    }
    for (int i = 0; i < 12; i++) {
        if (kJoyAxisNames[i].code != (DWORD)i) return;
        WriteMemory(&kJoyAxisNames[i].name, &axes[i], sizeof(char*));
    }
    Log("pad binding names patched");
}

// ---------------------------------------------------------------- streaming pool

// Sector streaming pool in MB (tweakable "Sectors/Streaming/d; Memory Pool MB", 57 = the console
// budget). Once it is full the streamer evicts the high mips and props and the world falls back
// to blurry or flat textures. Level data rewrites the tweakable before the pool is created
// (0x7C6140, read at 0x7C654F). Patching that read set off SecuROM's integrity checks, so instead the
// tweakable's record ([0xD389E4]: vtable 0xBB2498, +4 variable, +8/+0xC range) is pointed at a
// variable of ours: the level's value lands there, the real one keeps the configured size.
// More pool needs the 4 GB address space (LAA).
static int* const kStreamPoolMB = (int*)G(0x00D389E0);
static void* const* const kStreamPoolRecord = (void* const*)G(0x00D389E4);
static int s_streamPoolMB;       // configured size
static int s_levelStreamPoolMB;  // what the level asked for, for the log

static void RaiseStreamPool()
{
    if (g_cfg.streamPoolMB <= 0) return;
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    if (ms.ullTotalVirtual <= 0x80000000ull) {
        Log("no large address space (exe without LAA flag), stream pool stays at %d MB", *kStreamPoolMB);
        return;
    }
    DWORD* record = (DWORD*)*kStreamPoolRecord;
    if (!record || IsBadWritePtr(record, 16) || record[0] != G(0x00BB2498) || record[1] != (DWORD)kStreamPoolMB) {
        Log("stream pool tweakable not recognised, left alone");
        return;
    }
    s_streamPoolMB = g_cfg.streamPoolMB;
    s_levelStreamPoolMB = *kStreamPoolMB;
    record[1] = (DWORD)&s_levelStreamPoolMB;
    if (*kStreamPoolMB < s_streamPoolMB) *kStreamPoolMB = s_streamPoolMB;
    Log("stream pool %d MB", *kStreamPoolMB);
}

// ---------------------------------------------------------------- powerplay icon size

// When the HUD loads its parameters (0x4C9AF0) the PC port multiplies the powerplay icon scales by
// render width / 1280, but the icon renderer (0x55E740) already sizes icons by the screen width.
// Above 1280 pixels the icons grow twice: 1.5x at 1080p, 3x at 4K. The scales are divided back
// in the loaded data, once per load. Patching the multiplication in the code instead set off
// SecuROM's integrity checks (they stop freeing their VM contexts and the game hangs minutes later).
static void* const* const kHud = (void* const*)G(0x00D66B24);  // HUD, its parameter block at +0x20

// The route change markers (red and blue arrows, two slots drawn by 0x4C7910) pass width * 2.25/1280
// as their size to the same renderer, so they grow with the width squared. The factor is a constant
// only this code reads (0xBB8494); it is set to 2.25 / width, which gives the console's 2.25.
static float* const kRouteMarkerScale = (float*)G(0x00BB8494);

static void FixRouteMarkerSize(int width)
{
    static int s_width;
    static bool s_known = *kRouteMarkerScale == 2.25f / 1280.0f;
    if (!s_known || width == s_width) return;
    s_width = width;
    float scale = 2.25f / width;
    WriteMemory(kRouteMarkerScale, &scale, sizeof(scale));
    Log("route marker scale for width %d", width);
}

static void FixPowerplayIconSize()
{
    static BYTE* s_params;
    static float s_written[2];
    BYTE* hud = (BYTE*)*kHud;
    if (!hud) return;
    BYTE* p = hud + 0x20;
    float* scales = (float*)(p + 0x214);  // +0x214, +0x218
    int width = *(int*)(p + 0x2B0);
    if (width <= 0) return;
    if (g_cfg.patchMask & PATCH_ROUTEMARKER) FixRouteMarkerSize(width);
    if (p == s_params && scales[0] == s_written[0] && scales[1] == s_written[1]) return;  // done
    float factor = width / 1280.0f;
    s_params = p;
    s_written[0] = scales[0] / factor;
    s_written[1] = scales[1] / factor;
    scales[0] = s_written[0];
    scales[1] = s_written[1];
    Log("powerplay icon scales %.3f %.3f (width %d)", s_written[0], s_written[1], width);
}

// ---------------------------------------------------------------- LOD distances

// Tweakables with their stock values: environment LOD switch and fade distances (normal and
// split screen) and the particle LOD distances, in metres. The low LOD distance is left alone:
// beyond it each level hands over to its backdrop models, and pushing it out makes both draw at
// once (far buildings flicker). The game streams the more detailed sectors further out, which the
// bigger streaming pool absorbs.
static const struct { float* var; float stock; } kLodDistances[] = {
    { (float*)G(0x00D38A90), 150.0f },  // EnvironmentSS/LODs/a; High LOD Draw Dist
    { (float*)G(0x00D38A98), 300.0f },  // b; Mid LOD Draw Dist
    { (float*)G(0x00D38AA8), 30.0f },   // e; High LOD Fade Range
    { (float*)G(0x00D38AB8), 60.0f },   // Split Screen High LOD Draw Dist
    { (float*)G(0x00D38AC0), 120.0f },  // Split Screen Mid LOD Draw Dist
    { (float*)G(0x00D38AD0), 20.0f },   // Split Screen High LOD Fade Range
    { (float*)G(0x00D373DC), 150.0f },  // Particles/Min LOD Distance
    { (float*)G(0x00D373FC), 200.0f },  // Particles/Further LOD Distance
    { (float*)G(0x00D3742C), 100.0f },  // Particles/Split Screen/f; Min LOD Distance
    { (float*)G(0x00D193F0), 500.0f },  // Debris/FadeInDist
    { (float*)G(0x00D389F0), 400.0f },  // Sectors/Streaming/e; Max Sector Stream Dist
    { (float*)G(0x00D9AFD4), 0.0f },    // Sectors/Entities/a; Default Entity Stream Dist (set at run time)
    { (float*)G(0x00D5FDDC), 400.0f },  // Sectors/Entities/b; Max Entity Stream Dist
    { (float*)G(0x00D40A84), 5.0f },    // powerplays/DrawDists/Auto Size Scale (assets without a set distance:
    { (float*)G(0x00D40A8C), 200.0f },  // powerplays/DrawDists/Auto Offset      size * scale + offset, 0x813797)
};

static float s_lodWritten[sizeof(kLodDistances) / sizeof(kLodDistances[0])];

// Levels load their own LOD distances, so every value that no longer holds what we wrote is
// taken as the new base and scaled again. Called once per input frame.
static void ScaleLodDistances()
{
    if (g_cfg.lodDistanceScale == 1.0f) return;
    for (size_t i = 0; i < sizeof(kLodDistances) / sizeof(kLodDistances[0]); i++) {
        float v = *kLodDistances[i].var;
        if (v == s_lodWritten[i] || v <= 0 || v > 100000.0f) continue;
        s_lodWritten[i] = v * g_cfg.lodDistanceScale;
        // the mid LOD must still switch before the low LOD (0xD38AA0 / split screen 0xD38AC8)
        if (kLodDistances[i].var == (float*)G(0x00D38A98) && s_lodWritten[i] > *(float*)G(0x00D38AA0) * 0.9f)
            s_lodWritten[i] = *(float*)G(0x00D38AA0) * 0.9f;
        if (kLodDistances[i].var == (float*)G(0x00D38AC0) && s_lodWritten[i] > *(float*)G(0x00D38AC8) * 0.9f)
            s_lodWritten[i] = *(float*)G(0x00D38AC8) * 0.9f;
        *kLodDistances[i].var = s_lodWritten[i];
        if (i == 0) Log("LOD distances rescaled (high %.0f -> %.0f)", v, s_lodWritten[i]);
        if (kLodDistances[i].var == (float*)G(0x00D389F0) || kLodDistances[i].var == (float*)G(0x00D5FDDC) ||
            kLodDistances[i].var == (float*)G(0x00D9AFD4))
            Log("stream distance %08X: %.0f -> %.0f", (DWORD)kLodDistances[i].var, v, s_lodWritten[i]);
    }
}

// Powerplay assets (terminal facade, radar tower, cranes, ...) get their draw distance per level
// from Params/PP_DrawDists_<level>_<track>.params. The records are a list: [0xD8B384] -> node,
// node+0 = next, the list ends at 0xD8B388; node+0x10 = name ("powerplays/DrawDists/<level>/<asset>"),
// node+0x90 = draw distance in metres (0 = derived from the asset size). The asset test (0x813730)
// reads the record every time, so scaling it in place is enough.
static void ScalePowerplayDrawDists()
{
    static struct { DWORD node; float written; } s_done[1024];
    static int s_doneCount;
    if (g_cfg.lodDistanceScale == 1.0f) return;
    int scaled = 0;
    __try {
        DWORD node = *(DWORD*)G(0x00D8B384);
        for (int n = 0; node && node != G(0x00D8B388) && n < 4096; n++, node = *(DWORD*)node) {
            float* dist = (float*)(node + 0x90);
            if (*dist <= 0 || *dist > 100000.0f) continue;
            int i = 0;
            while (i < s_doneCount && s_done[i].node != node) i++;
            if (i < s_doneCount && s_done[i].written == *dist) continue;  // already scaled
            if (i == s_doneCount) {
                if (s_doneCount == 1024) continue;
                s_done[s_doneCount++].node = node;
            }
            *dist *= g_cfg.lodDistanceScale;
            s_done[i].written = *dist;
            scaled++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (scaled) Log("powerplay draw distances: %d scaled", scaled);
}

// ---------------------------------------------------------------- unlocks

// The game's own switches, read whenever a menu asks whether something is locked; nothing is written
// to the profile. UI tweakables (as the QA menu 0x4DC520 sets them): all game modes and tracks in
// quick play (0x4851C0), episodes, events and vehicles. PCcheat tweakables: the extras the PC port
// unlocked with cheat codes, among them the Expressway track (Downtown route B, 0x4851C0).
static BYTE* const kUnlockAll[] = {
    (BYTE*)G(0x00D6FC54),  // UI/All Gamemodes Unlocked
    (BYTE*)G(0x00D6FBA0),  // UI/Episode Select/All Episodes Unlocked
    (BYTE*)G(0x00D7D978),  // UI/Events Select/All Events Unlocked
    (BYTE*)G(0x00D6F8F0),  // UI/Vehicle Select/All Vehicles Unlocked
};
static const size_t kAllEpisodesIndex = 1;
static int s_suspendAllEpisodes;     // scoped suspend depth during Hook_RaceResultsInput
static bool s_episodeSelectSuspend;  // latched from cSeasonEpisodeSelect::OnEnter until cInGameMovieState::OnEnter
static BYTE* const kBonusContent = (BYTE*)G(0x00D6C49C);  // PCcheat;Cobretti SlipStream .. Euro Livery Pack, 8 bytes apart
static const int kBonusCount = 10;

// Decals are the achievements: cAnchoviesPC (vtable 0xC4AAC4) at [0xD64E00]+0x208 keeps one byte per
// achievement at [+0x2C] (count [+0x38], 47), saved with the profile. Entry 0 (the last decal) stands for all
// the others; the game's own check for it (vtable +0x18, 0xA17E10) does not run on every unlock, so it is
// asked for last through the same switch. Decal id 47 is the Pure one, the profile's HasPureSave byte at
// [0xD64E6C] (the game sets it when a Pure save is found in AppData). Achievements 41..46 are only given in
// the results of ranked online races (0x802870, behind the online check 0x800980); all others come from
// the single player conditions. Mode 2 asks only for those six, and for entry 0 once all others are there.
// The game's own debug unlock is used: tweakables Anchovies/UnlockAnchovy (0xD71954) and UnlockAnchovieID (0xD11B6C),
// read by the race update (0x4BF180, single player races) which clears the switch and unlocks through the
// game's normal path (0xA17F90; only with a signed in profile, [+4] and [+0x3C]). One achievement per
// frame, and only while a single player race runs: the flags are set before the profile is loaded, and the
// game saves the profile when the race ends. Before the first one the profile files are copied to a backup folder.
static BYTE* const kUnlockAnchovy = (BYTE*)G(0x00D71954);
static int* const kUnlockAnchovyId = (int*)G(0x00D11B6C);
static const int kOnlineFirst = 41, kOnlineLast = 46;

static void BackupSaves()
{
    char dir[MAX_PATH], from[MAX_PATH], to[MAX_PATH];
    if (FAILED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, dir))) return;
    strcat_s(dir, "\\Disney Interactive Studios\\Split Second\\");
    sprintf_s(to, "%sglimpse_backup", dir);
    if (!CreateDirectoryA(to, NULL)) {
        Log("decals: profile backup %s exists, kept", to);  // the one from before the first unlock
        return;
    }
    sprintf_s(from, "%s*.sav", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(from, &fd);
    int copied = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            sprintf_s(from, "%s%s", dir, fd.cFileName);
            sprintf_s(to, "%sglimpse_backup\\%s", dir, fd.cFileName);
            if (CopyFileA(from, to, TRUE)) copied++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    Log("decals: %d profile files backed up to %sglimpse_backup", copied, dir);
}

static void UnlockDecals()
{
    static bool backedUp, done, pureLogged;
    static int asked = -1;
    BYTE* pure = *(BYTE**)G(0x00D64E6C);
    if (pure && !*pure) {  // the game clears it again when it looks for Pure saves, so it is kept set
        *pure = 1;
        if (!pureLogged) Log("decals: Pure decal unlocked");
        pureLogged = true;
    }
    if (done || *kUnlockAnchovy || !SinglePlayerRace()) return;  // or the game has not taken the last request yet
    // SinglePlayerRace counts one screen while the in-game resources are unreadable (a split screen load);
    // the front end's race setup (0x453300 returns 0xD6EE78) has the screen count the game's consumer
    // 0x4BF180 checks (+0x9C > 1: no unlock). Steam build only: the address is not in build_ru.inc.
    if (CurrentBuild() == BUILD_STEAM) {
        const int* screens = (const int*)(0x00D6EE78 + 0x9C);
        if (IsBadReadPtr(screens, sizeof(int)) || *screens > 1) return;
    }
    BYTE* mgr = *(BYTE**)G(0x00D64E00);
    BYTE* ach = mgr ? mgr + 0x208 : NULL;
    if (!ach || IsBadReadPtr(ach, 0x40) || *(DWORD*)ach != G(0x00C4AAC4) || !ach[4] || !ach[0x3C]) return;
    BYTE* unlocked = *(BYTE**)(ach + 0x2C);
    int count = *(int*)(ach + 0x38);
    if (count < 2 || count > 64 || IsBadReadPtr(unlocked, count)) return;
    bool onlineOnly = g_cfg.unlockDecals == 2;
    int next = -1, missing = 0;
    for (int i = 1; i < count; i++) {
        if (unlocked[i]) continue;
        bool online = i >= kOnlineFirst && i <= kOnlineLast;
        if (next < 0 && (!onlineOnly || online)) next = i;
        missing++;
    }
    if (next < 0 && !missing && !unlocked[0]) next = 0;
    if (next < 0) {
        done = true;
        if (missing) Log("decals: online achievements unlocked, %d others still to earn", missing);
        else Log("decals: all %d achievements unlocked", count);
        return;
    }
    if (!backedUp) {
        backedUp = true;
        BackupSaves();
    }
    if (next != asked) Log("decals: unlocking achievement %d", next);
    asked = next;
    *kUnlockAnchovyId = next;
    *kUnlockAnchovy = 1;
}

static bool AllEpisodesSuspended() { return s_suspendAllEpisodes > 0 || s_episodeSelectSuspend; }

static bool InSeasonEpisodeCompleted()
{
    if (CurrentBuild() != BUILD_STEAM) return false;
    __try {
        const BYTE* seasonMgr = *(const BYTE* const*)0x00D64E40;
        return seasonMgr && seasonMgr[0x08] && seasonMgr[0x16];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void ApplyUnlocks()
{
    static bool logged;
    if (g_cfg.unlockDecals) UnlockDecals();
    if (g_cfg.unlockAll) {
        if (s_episodeSelectSuspend && !InSeasonEpisodeCompleted()) {
            s_episodeSelectSuspend = false;
            Log("unlocks: season episode select ended before movie state, restored all-episodes flag");
        }
        for (size_t i = 0; i < sizeof(kUnlockAll) / sizeof(kUnlockAll[0]); i++) {
            if (i == kAllEpisodesIndex && AllEpisodesSuspended()) continue;
            *kUnlockAll[i] = 1;
        }
    }
    if (g_cfg.bonusContent)
        for (int i = 0; i < kBonusCount; i++) kBonusContent[i * 8] = 1;
    if (!logged && (g_cfg.unlockAll || g_cfg.bonusContent)) {
        logged = true;
        Log("unlocks: all=%d bonus=%d", g_cfg.unlockAll, g_cfg.bonusContent);
    }
}

// UnlockAll=1 sets UI/Episode Select/All Episodes Unlocked (0xD6FBA0), making cEpisode::IsUnlocked
// (0x7749F0) return true for all 12 episodes. After winning an episode's Elite Race,
// cRaceResults::OnInput (0x801EC0) checks cSeason::CountLockedEpisodes (0x7754D0 -> 0x7749F0) at
// 0x8020B1 / 0x80215F: seeing 0 locked episodes, it skips cSeasonEpisodeSelect (layer 0x63, which
// unlocks the chosen next episode via 0x772B00 and advances season->currentEpisode) and enters
// cInGameMovieState (0x417370). On Episode 1 (currentEpisode == 0), 0x416EB0 formats NT_Episode1.bik
// (which does not exist; only NT_Episode2..12.bik ship), Bink enters error state 12 (0x57AF0C), and
// 0x4B5D4B only finishes on state 9, soft-locking on a black screen under NextTimeOverlay.gfx.
//
// Steam build only: suspend 0xD6FBA0 during cRaceResults::OnInput (when in Season mode after an
// episode completion) and hold it at 0 from cSeasonEpisodeSelect::OnEnter until
// cInGameMovieState::OnEnter restores it. cSeasonEpisodeSelect::OnInput (0x80BC30) is intentionally
// not wrapped because its call tree enters SecuROM's VM via 0x772B00 -> 0x774A10 and save writes
// 0x5905D0 -> 0x590C50/0x590E90. Also guard cInGameMovieState::OnEnter against missing
// NT_Episode%d.bik if all 12 episodes were already unlocked in the save before Episode 1's Elite
// Race was beaten.
typedef void(__thiscall* SeasonScreenInput_t)(void* self, void* input);
typedef void(__thiscall* SeasonScreenEnter_t)(void* self);
static SeasonScreenInput_t s_origRaceResultsInput;
static SeasonScreenEnter_t s_origEpisodeSelectEnter;
static SeasonScreenEnter_t s_origMovieStateEnter;

struct ScopedSuspendAllEpisodes {
    BYTE* flag;
    BYTE prev;
    explicit ScopedSuspendAllEpisodes(bool active = true)
        : flag(active ? kUnlockAll[kAllEpisodesIndex] : NULL), prev(flag ? *flag : 0)
    {
        if (flag) {
            s_suspendAllEpisodes++;
            *flag = 0;
        }
    }
    ~ScopedSuspendAllEpisodes()
    {
        if (flag) {
            s_suspendAllEpisodes--;
            if (!AllEpisodesSuspended()) *flag = prev;
        }
    }
};

struct ScopedClearByte {
    BYTE* ptr;
    BYTE prev;
    explicit ScopedClearByte(BYTE* p) : ptr(p), prev(p ? *p : 0)
    {
        if (ptr) *ptr = 0;
    }
    ~ScopedClearByte()
    {
        if (ptr) *ptr = prev;
    }
};

static int ReadEpisodeChoiceCount(const void* self)
{
    __try {
        return *(const int*)((const BYTE*)self + 0x220);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static BYTE* MissingNextTimeFlag(const BYTE* state, int* epOut)
{
    __try {
        BYTE* seasonMgr = *(BYTE* const*)0x00D64E40;
        // The debug tweakable at 0xD66AD4 (default 0) can also enter 0x416EB0; intentionally out of scope.
        if (!seasonMgr || !seasonMgr[0x08] || state[0x10] || !seasonMgr[0x16]) return NULL;
        int curEp = *(const int*)(seasonMgr + 0x0C);
        // 0x416EB0 opens NT_Episode(curEp + 1).bik; only NT_Episode2..12.bik exist.
        if (curEp >= 1 && curEp <= 11) return NULL;
        *epOut = curEp;
        return seasonMgr + 0x16;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

static void __fastcall Hook_RaceResultsInput(void* self, void*, void* input)
{
    ScopedSuspendAllEpisodes suspend(InSeasonEpisodeCompleted());
    s_origRaceResultsInput(self, input);
}

static void __fastcall Hook_EpisodeSelectEnter(void* self, void*)
{
    s_episodeSelectSuspend = true;
    *kUnlockAll[kAllEpisodesIndex] = 0;
    s_origEpisodeSelectEnter(self);
    Log("unlocks: season episode select opened with real save progression (%d choices)",
        ReadEpisodeChoiceCount(self));
}

static void __fastcall Hook_InGameMovieStateEnter(void* self, void*)
{
    if (s_episodeSelectSuspend) {
        s_episodeSelectSuspend = false;
        if (!AllEpisodesSuspended()) *kUnlockAll[kAllEpisodesIndex] = 1;
    }
    int missingEp = -1;
    BYTE* epDone = MissingNextTimeFlag((const BYTE*)self, &missingEp);
    if (epDone)
        Log("unlocks: skipped missing Next Time movie for episode %d (NT_Episode%d.bik)",
            missingEp + 1, missingEp + 1);
    ScopedClearByte suppress(epDone);
    s_origMovieStateEnter(self);
}

static bool CareerUnlockHooksMatch()
{
    __try {
        void** resultsVt = (void**)0x00C1F83C;
        void** epSelectVt = (void**)0x00C21014;
        void** movieStateVt = (void**)0x00BA3518;
        if (!ClassIs(resultsVt, 0x00C972C0, ".?AVcRaceResults@InGameUI@@") ||
            resultsVt[16] != (void*)0x00801EC0 ||
            !ClassIs(epSelectVt, 0x00C98B94, ".?AVcSeasonEpisodeSelect@InGameUI@@") ||
            epSelectVt[2] != (void*)0x0080BEB0 ||
            !ClassIs(movieStateVt, 0x00C745A4, ".?AVcInGameMovieState@GameStates@@") ||
            movieStateVt[1] != (void*)0x00417370)
            return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return BytesMatch((void*)0x00801EC0, "83EC28578BF9837F58020F85C9030000") &&
           BytesMatch((void*)0x008020AA, "385E16741D8B0EE81A34F7FF") &&
           BytesMatch((void*)0x0080BEB0, "558BEC83E4F86AFF68BF42B40064A100") &&
           BytesMatch((void*)0x0080BF0D, "E88E95F6FF8BF8568BCBE82493F6FF8BC8E8CD8AF6FF") &&
           BytesMatch((void*)0x00417370, "64A1000000006AFF6843FDB200506489") &&
           BytesMatch((void*)0x00417438, "A1404ED60080780800") &&
           BytesMatch((void*)0x0041744B, "807E10008B480C751780781600");
}

static void InstallUnlockAllCareerFix()
{
    if (!g_cfg.unlockAll) return;
    if (CurrentBuild() != BUILD_STEAM) {
        Log("unlocks: career episode progression fix is Steam build only, not the Russian depot (%s)",
            BuildName());
        return;
    }
    if (!CareerUnlockHooksMatch()) {
        Log("unlocks: career episode progression vtables mismatch, left as is");
        return;
    }
    struct HookSlot {
        void** slot;
        void* hook;
        void** orig;
    };
    HookSlot hooks[3] = {
        { (void**)0x00C1F87C, (void*)Hook_RaceResultsInput, (void**)&s_origRaceResultsInput },
        { (void**)0x00C2101C, (void*)Hook_EpisodeSelectEnter, (void**)&s_origEpisodeSelectEnter },
        { (void**)0x00BA351C, (void*)Hook_InGameMovieStateEnter, (void**)&s_origMovieStateEnter },
    };
    int installed = 0;
    for (; installed < 3; installed++) {
        if (!SwapSlot(hooks[installed].slot, hooks[installed].hook, hooks[installed].orig)) break;
    }
    if (installed < 3) {
        for (int i = installed - 1; i >= 0; i--) {
            if (!WriteSlot(hooks[i].slot, *hooks[i].orig))
                Log("unlocks: career episode progression rollback failed for slot %d", i);
            *hooks[i].orig = NULL;
        }
        Log("unlocks: career episode progression slot %d cannot be written, rolled back", installed);
        return;
    }
    Log("unlocks: career episode progression hooked (cRaceResults, cSeasonEpisodeSelect, cInGameMovieState)");
}

// ---------------------------------------------------------------- rumble

static XInputSetState_t s_gameXInputSetState;

static DWORD WINAPI Hook_XInputSetState(DWORD slot, XINPUT_VIBRATION* vib)
{
    if (slot >= (DWORD)g_cfg.virtualPads || !PadSlotConnected((int)slot)) return ERROR_DEVICE_NOT_CONNECTED;
    XINPUT_VIBRATION v = *vib;
    if (g_cfg.rumbleScale != 1.0f) {
        float l = v.wLeftMotorSpeed * g_cfg.rumbleScale, r = v.wRightMotorSpeed * g_cfg.rumbleScale;
        v.wLeftMotorSpeed = (WORD)(l > 65535 ? 65535 : l);
        v.wRightMotorSpeed = (WORD)(r > 65535 ? 65535 : r);
    }
    if (TestFeed* feed = GetTestFeed()) {
        feed->rumble[slot] = v;
        return ERROR_SUCCESS;
    }
    return s_gameXInputSetState(slot, &v);
}

static void HookRumble()
{
    HMODULE x13 = GetModuleHandleA("xinput1_3.dll");
    void* expected = x13 ? (void*)GetProcAddress(x13, "XInputSetState") : NULL;
    if (!expected || *kIatXInputSetState != expected) {
        Log("XInputSetState import not where expected, rumble left as is");
        return;
    }
    s_gameXInputSetState = (XInputSetState_t)expected;
    void* hook = (void*)Hook_XInputSetState;
    WriteMemory(kIatXInputSetState, &hook, sizeof(hook));
    Log("rumble hooked");
}

// ---------------------------------------------------------------- intro and cutscene skip

// The boot movies (data/movies/bootup: publisher logo, intro) are played with Bink through the import
// table. With SkipIntro=1 they are opened normally (so Bink sets up its sound as usual) but never
// waited for or decoded, so the game ends them at once, as when the player skips them. Replacing or
// shortening the movie files instead made the engine sound choppy for the rest of the session.
//
// CutsceneSkip=1 does the same for the season's cutscenes on request: the first press of A, B, X, Y,
// Start, Enter or Space shows "Press again to skip" for 3 seconds, a second press within them marks the
// movie as at its last frame, and the game's movie loop ends it as if it had played to the end. The boot
// movies, which a press ends in the game anyway, end on the first press, without the prompt (#24).
static void** const kIatBinkOpen = (void**)G(0x00B9B39C);
static void** const kIatBinkWait = (void**)G(0x00B9B3B0);
static void** const kIatBinkDoFrame = (void**)G(0x00B9B380);
typedef void*(__stdcall* BinkOpen_t)(const char* name, unsigned flags);
typedef int(__stdcall* BinkWait_t)(void* bink);
typedef int(__stdcall* BinkDoFrame_t)(void* bink);
static BinkDoFrame_t s_binkDoFrame;
static BinkOpen_t s_binkOpen;
static BinkWait_t s_binkWait;
static void* s_skipped;  // the boot movie being skipped
static void* volatile s_cutscene;       // the cutscene playing that may be skipped
static volatile bool s_cutsceneBoot;    // it is a boot movie: the first press skips it
static volatile DWORD s_promptUntil;    // GetTickCount until which the skip prompt shows
static WORD s_skipHeld;                 // buttons and keys held at the last check (edges only count)
static volatile DWORD s_cutsceneFrame;  // GetTickCount of the cutscene's last BinkDoFrame

// The movie loop (0x57B4EA) waits with BinkWait for the next frame's time, decodes it with BinkDoFrame
// and ends the movie when LastFrameNum == Frames. For a skipped boot movie it neither waits nor decodes:
// the movie is marked as at its last frame, so the loop ends it on its first pass.
static int __stdcall Hook_BinkWait(void* bink)
{
    return bink && bink == s_skipped ? 0 : s_binkWait(bink);
}

// A, B, X, Y and Start of every pad, Enter and Space while the game has the focus (bit 0 and 1)
static WORD SkipInput()
{
    WORD held = 0;
    XINPUT_STATE st;
    for (DWORD i = 0; i < XUSER_MAX_COUNT && pXInputGetState; i++)
        if (PadSlotConnected(i) && pXInputGetState(i, &st) == ERROR_SUCCESS)
            held |= st.Gamepad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y |
                                           XINPUT_GAMEPAD_START);
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (fg) GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000) held |= 1;
        if (GetAsyncKeyState(VK_SPACE) & 0x8000) held |= 2;
    }
    return held;
}

static int __stdcall Hook_BinkDoFrame(void* bink)
{
    unsigned* h = (unsigned*)bink;  // HBINK: Width, Height, Frames, FrameNum, LastFrameNum
    if (bink && bink == s_cutscene) {
        s_cutsceneFrame = GetTickCount();
        WORD held = SkipInput(), pressed = held & ~s_skipHeld;
        s_skipHeld = held;
        if (pressed) {
            DWORD now = GetTickCount();
            if (s_cutsceneBoot || (s_promptUntil && (int)(s_promptUntil - now) > 0)) {
                Log("%s skipped at frame %u of %u", s_cutsceneBoot ? "boot movie" : "cutscene", h[3], h[2]);
                s_cutscene = NULL;
                s_promptUntil = 0;
                h[3] = h[4] = h[2];
                return 0;
            }
            s_promptUntil = now + 3000;
        }
        int r = s_binkDoFrame(bink);
        if (h[4] >= h[2]) s_cutscene = NULL, s_promptUntil = 0;  // played to the end
        return r;
    }
    if (!bink || bink != s_skipped) return s_binkDoFrame(bink);
    h[3] = h[4] = h[2];
    return 0;
}

// the skip prompt, bottom right, fading out over its last half second (diag.cpp's Present hook)
void MoviePresent(IDirect3DDevice9* dev)
{
    DWORD until = s_promptUntil, now = GetTickCount();
    if (!until || !s_cutscene) return;
    // Bink close is not hooked here: a cutscene the game ended itself has no frames any more
    if (now - s_cutsceneFrame > 250) return;
    int left = (int)(until - now);
    if (left <= 0) return;
    const char* text = "Press again to skip";
    const DWORD white = 0xFFEBEBEE;
    OverlayDraw(dev, &text, &white, 1, OVERLAY_BOTTOM_RIGHT, left < 500 ? left / 500.0f : 1.0f);
}

static bool Contains(const char* s, const char* part)
{
    for (size_t n = strlen(part); *s; s++)
        if (!_strnicmp(s, part, n)) return true;
    return false;
}

// SkipIntro=1: the publisher logo (bootup/disbr.bik), 2: also the game intro (bootup/intro.bik)
static bool IsBootMovie(const char* name)
{
    if (!Contains(name, "bootup")) return false;
    return Contains(name, "disbr") ? g_cfg.skipIntro >= 1 : g_cfg.skipIntro >= 2;
}

// the cutscenes: the boot movies, the season's "Coming up" and "Next time" episodes, the end sequence
// and the demo movies; not the menu backgrounds (they loop) or the tutorial clips
static bool IsCutscene(const char* name)
{
    return Contains(name, "bootup") || Contains(name, "comingup") || Contains(name, "nexttime") ||
           Contains(name, "endsequence") || Contains(name, "ss_demo");
}

static void* __stdcall Hook_BinkOpen(const char* name, unsigned flags)
{
    void* bink = s_binkOpen(name, flags);
    if (bink == s_skipped) s_skipped = NULL;
    if (bink == s_cutscene) s_cutscene = NULL, s_promptUntil = 0;
    if (bink && name && g_cfg.skipIntro > 0 && IsBootMovie(name)) {
        s_skipped = bink;
        Log("intro skip: %s (%u frames)", name, ((unsigned*)bink)[2]);
    } else if (bink && name && g_cfg.cutsceneSkip && IsCutscene(name)) {
        s_skipHeld = SkipInput();  // a button still held from the menu does not count
        s_promptUntil = 0;
        s_cutsceneBoot = Contains(name, "bootup");
        s_cutscene = bink;
        Log("cutscene: %s (%u frames), skipped on a %s press", name, ((unsigned*)bink)[2], s_cutsceneBoot ? "first" : "second");
    }
    return bink;
}

bool IntroSkipBinkHook(const void* fn)
{
    return fn == (void*)Hook_BinkOpen || fn == (void*)Hook_BinkWait || fn == (void*)Hook_BinkDoFrame;
}

static void HookIntroSkip()
{
    HMODULE bink = GetModuleHandleA("binkw32.dll");
    void* open = bink ? (void*)GetProcAddress(bink, "_BinkOpen@8") : NULL;
    void* wait = bink ? (void*)GetProcAddress(bink, "_BinkWait@4") : NULL;
    void* doFrame = bink ? (void*)GetProcAddress(bink, "_BinkDoFrame@4") : NULL;
    // UltrawideFix=2 may have put its own hooks there first (it hooks at device creation): they call on to
    // Bink, so the intro skip goes in front of them
    if (!open || !wait || !doFrame || (*kIatBinkOpen != open && !UltrawideBinkHook(*kIatBinkOpen)) ||
        *kIatBinkWait != wait || (*kIatBinkDoFrame != doFrame && !UltrawideBinkHook(*kIatBinkDoFrame))) {
        Log("Bink imports not where expected, intro and cutscenes left as is");
        return;
    }
    s_binkOpen = (BinkOpen_t)*kIatBinkOpen;
    s_binkWait = (BinkWait_t)wait;
    s_binkDoFrame = (BinkDoFrame_t)*kIatBinkDoFrame;
    void* hook = (void*)Hook_BinkOpen;
    WriteMemory(kIatBinkOpen, &hook, sizeof(hook));
    hook = (void*)Hook_BinkWait;
    WriteMemory(kIatBinkWait, &hook, sizeof(hook));
    hook = (void*)Hook_BinkDoFrame;
    WriteMemory(kIatBinkDoFrame, &hook, sizeof(hook));
    Log("%s on", g_cfg.skipIntro > 0 && g_cfg.cutsceneSkip ? "intro and cutscene skip"
                 : g_cfg.skipIntro > 0                  ? "intro skip"
                                                        : "cutscene skip");
}

// ---------------------------------------------------------------- prompt bar glyphs

static bool s_padGlyphsShown;
static DWORD s_lastPadActivity, s_lastKbmActivity;
static BYTE s_lastKeys[256];

static const char* const kIconPaths[5] = {
    "main.bar_mc.buttonA_mc.icon_mc.gotoAndStop",
    "main.bar_mc.buttonB_mc.icon_mc.gotoAndStop",
    "main.bar_mc.buttonX_mc.icon_mc.gotoAndStop",
    "main.bar_mc.buttonY_mc.icon_mc.gotoAndStop",
    "main.bar_mc.buttonSTART_mc.icon_mc.gotoAndStop",
};
// frame labels inside Resident|UI/CommonUI/Prompt_Bar.gfx
static const char* const kXenonFrames[5] = { "Xenon_A", "Xenon_B", "Xenon_X", "Xenon_Y", "Xenon_Start" };
static const char* const kPs3Frames[5] = { "PS3_Cross", "PS3_Circle", "PS3_Square", "PS3_Triangle", "PS3_Start" };
static const char kPcFrame[] = "PC_GenKeyLong";
// key captions SetButtons writes into the PC frames, fixed per button
static const char* const kCaptionPaths[5] = {
    "main.bar_mc.buttonA_mc.icon_mc.keyText_txt.text",
    "main.bar_mc.buttonB_mc.icon_mc.keyText_txt.text",
    "main.bar_mc.buttonX_mc.icon_mc.keyText_txt.text",
    "main.bar_mc.buttonY_mc.icon_mc.keyText_txt.text",
    "main.bar_mc.buttonSTART_mc.icon_mc.keyText_txt.text",
};
static const char* const kCaptionKeys[5] = { "KEY_RETURN", "KEY_ESCAPE", "KEY_SPACE", "KEY_TAB", "KEY_END" };

// The game's own copies of the icon paths (only SetButtons uses them). While pad glyphs are on,
// their last letter is changed so SetButtons' gotoAndStop calls miss and leave our frames alone:
// some screens (main menu) call SetButtons every frame, after our per-frame update.
static char* const kGameIconPaths[5] = { (char*)G(0x00BBAC7C), (char*)G(0x00BBAC50), (char*)G(0x00BBAC24),
                                         (char*)G(0x00BBABF8), (char*)G(0x00BBABC8) };

static bool GameIconPathsKnown()
{
    for (int i = 0; i < 5; i++)
        if (strcmp(kGameIconPaths[i], kIconPaths[i])) return false;
    return true;
}

static void MuteGameIconPaths(bool mute)
{
    for (char* path : kGameIconPaths) {
        char* last = path + strlen(path) - 1;
        char c = mute ? 'q' : 'p';  // gotoAndStop / gotoAndStoq
        if (*last != c) WriteMemory(last, &c, 1);
    }
}

static void* PromptBarMovie(void* self)
{
    BYTE* layer = *(BYTE**)((BYTE*)self + 4 + 0x68);
    return layer ? *(void**)(layer + 4) : NULL;
}

static void SetPromptIcons(void* self, bool pad)
{
    void* movie = PromptBarMovie(self);
    if (!movie) return;
    GFxInvoke_t invoke = (GFxInvoke_t)(*(void***)movie)[0x48 / 4];
    const char* const* frames = g_cfg.glyphs == GLYPHS_AUTO_PS ? kPs3Frames : kXenonFrames;
    for (int i = 0; i < 5; i++) {
        GFxValue arg = {}, result = {};
        arg.type = GFX_STRING;
        arg.str = pad ? frames[i] : kPcFrame;
        result.type = GFX_RESULT_STRING;
        invoke(movie, kIconPaths[i], &result, &arg, 1);
    }
    if (pad) return;
    // the key frames come back without captions
    GFxSetVariable_t setVariable = (GFxSetVariable_t)(*(void***)movie)[0x2C / 4];
    for (int i = 0; i < 5; i++) {
        GFxValue value = {};
        value.type = GFX_STRING;
        value.str = kCaptionKeys[i];
        setVariable(movie, kCaptionPaths[i], &value, 1);
    }
}

static bool WantPadGlyphs()
{
    return g_cfg.glyphs != GLYPHS_KEYBOARD && s_lastPadActivity && s_lastPadActivity >= s_lastKbmActivity;
}

// The prompt bar is a single layer, reached the way the game's own screens do (0x469CB0):
// [[[0xD64E0C]+8]] - 4. Code hooks on SetButtons (0x4DADE0) set off SecuROM's integrity checks, so
// instead of reacting to SetButtons the pad frames are applied every frame while the pad is in use.
static void* PromptBar()
{
    __try {
        BYTE* ui = *(BYTE**)G(0x00D64E0C);
        if (!ui) return NULL;
        BYTE** slot = *(BYTE***)(ui + 8);
        BYTE* layer = slot ? *slot : NULL;
        return layer ? layer - 4 : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

// the prompt bar's movie as the game holds it right now (NULL when there is none)
void* PromptBarMovieNow()
{
    void* bar = PromptBar();
    if (!bar) return NULL;
    __try {
        return PromptBarMovie(bar);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

static void UpdatePromptBar(bool pad)
{
    if (pad != s_padGlyphsShown) MuteGameIconPaths(pad);
    void* bar = PromptBar();
    if (!bar) return;
    if (pad) {
        SetPromptIcons(bar, true);  // a freshly loaded bar starts on the key frames
    } else if (s_padGlyphsShown) {
        SetPromptIcons(bar, false);
    }
    if (pad != s_padGlyphsShown) {
        s_padGlyphsShown = pad;
        Log("glyphs -> %s", pad ? "pad" : "keyboard");
    }
}

// RTTI: vt[-1] is the complete object locator (+0 signature 0, +0xC the type descriptor, its name at +8)
bool ClassIs(void** vt, DWORD col, const char* name)
{
    const DWORD* c = (const DWORD*)vt[-1];
    if ((DWORD)c != col || c[0] != 0) return false;
    const char* n = (const char*)(c[3] + 8);
    return !IsBadStringPtrA(n, 64) && !strcmp(n, name);
}

static bool s_promptBarKnown;

static void CheckPromptBar()
{
    s_promptBarKnown = BytesMatch(kPromptBarSetButtons, "81ECA4000000535556") &&
                       BytesMatch((void*)G(0x00469CB0), "A10C4ED600578BF98B4808") && GameIconPathsKnown();
    Log(s_promptBarKnown ? "prompt bar found" : "prompt bar code not recognised, glyphs disabled");
}

// ---------------------------------------------------------------- title screen text

// Dictionary lookup, key -> localised UTF-8 text (or a copy of the key). Called directly, not
// hooked: the dictionary object is the global the game's own callers pass (0x518416).
static BYTE* const kTextLookup = (BYTE*)G(0x00A15BF0);
typedef const char*(__thiscall* TextLookup_t)(void* dict, const char* key);
static TextLookup_t s_origLookup;

static void* s_dict;            // dictionary object, [0xD64E28]
static char* s_pressStart;      // TEXT_PRESS_START inside the dictionary
static char s_pressStartOrig[256], s_pressStartPad[256];
static bool s_pressStartIsPad;

// The flash title screen reads the dictionary text directly, so the entry itself is rewritten:
// "ZUM STARTEN ENTER DRUECKEN" -> "ZUM STARTEN START DRUECKEN", with the localised name of the
// Enter key (KEY_RETURN) as the word to replace.
// false while the dictionary is not loaded yet
static bool PreparePressStart()
{
    char* text = (char*)s_origLookup(s_dict, "TEXT_PRESS_START");
    if (!text || !strcmp(text, "TEXT_PRESS_START")) return false;  // missing entries come back as the key
    lstrcpynA(s_pressStartOrig, text, sizeof(s_pressStartOrig));
    char enter[64];
    lstrcpynA(enter, s_origLookup(s_dict, "KEY_RETURN"), sizeof(enter));
    size_t n = strlen(enter);
    for (const char* p = s_pressStartOrig; n && *p; p++) {
        if (_strnicmp(p, enter, n) == 0 && n >= 5) {  // rewritten in place, so it must not grow
            _snprintf_s(s_pressStartPad, _TRUNCATE, "%.*s%s%s", (int)(p - s_pressStartOrig), s_pressStartOrig, "START", p + n);
            s_pressStart = text;
            Log("title text: '%s' -> '%s'", s_pressStartOrig, s_pressStartPad);
            return true;
        }
    }
    Log("title text: '%s' has no '%s', unchanged", s_pressStartOrig, enter);
    return true;
}

static void UpdatePressStart(bool pad)
{
    if (!s_pressStart || pad == s_pressStartIsPad) return;
    strcpy_s(s_pressStart, strlen(s_pressStartOrig) + 1, pad ? s_pressStartPad : s_pressStartOrig);
    s_pressStartIsPad = pad;
}

static void CheckTitleText()
{
    if (!BytesMatch(kTextLookup, "568BF1833E00752E") || !BytesMatch((void*)G(0x00518416), "8B0D284ED600")) {
        Log("text lookup not recognised, title text unchanged");
        return;
    }
    s_origLookup = (TextLookup_t)kTextLookup;
    Log("title text lookup found");
}

void NotePadActivity()
{
    s_lastPadActivity = GetTickCount() | 1;
}

// SecuROM VM: ten interpreter contexts, pointers at 0x1E0266A, +0x24 == 0x66666666 while in use.
// A hang after the finish line was the game thread waiting for a free context with all ten taken.
// Log=1 logs every change of the in-use count to see when and from which entry they pile up.
static int CountVmContexts(DWORD* kinds)
{
    int used = 0;
    __try {
        for (int i = 0; i < 10; i++) {
            BYTE* c = *(BYTE**)(0x01E0266A + i * 4);  // SecuROM's own data, only known on BUILD_STEAM
            kinds[i] = 0;
            if (c && *(DWORD*)(c + 0x24) == 0x66666666) {
                kinds[i] = *(DWORD*)(c + 0x18);
                used++;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return used;
}

static void WatchVmContexts()
{
    static int lastUsed;
    static unsigned frame;
    frame++;
    DWORD kinds[10];
    int used = CountVmContexts(kinds);
    if (used == lastUsed) return;
    lastUsed = used;
    char list[10 * 9 + 1] = "";
    for (int i = 0; i < 10; i++)
        if (kinds[i]) wsprintfA(list + lstrlenA(list), " %X", kinds[i] & 0xFFFF);
    Log("securom vm: %d of 10 contexts in use at frame %u (%lu ms):%s", used, frame, GetTickCount(), list);
}

// Opponent car detail (CarDetail): the PC release loads Generic/Params/PlayerLOD_ps3.params, the PS3's car
// LOD tunables, where at most 3 opponents get the mid detail level (the rest the far one) and every opponent
// switches to the far level at 100 m. The Xbox 360's file in the same archive (PlayerLOD_xenon.params) has
// 31 and 200 m; with only the PS3's numbers the opponents visibly pop between detail levels. 1 writes the
// Xbox 360's numbers, 2 also gives up to 7 opponents (all of them) the near level and moves its switch from
// 20 to 40 m. Plain tunables in .data (PlayerLOD/01..04, registered at 0xB807B4), written again whenever the
// game loads the params file; the idea came from the Back on Air project.
static void ApplyCarDetail()
{
    if (!g_cfg.carDetail) return;
    int* nearLods = (int*)G(0x00D37C78);       // PlayerLOD/01;MaxAINearLODs
    int* midLods = (int*)G(0x00D37C80);        // PlayerLOD/02;MaxAIMidLODs
    float* nearToMid = (float*)G(0x00D37C88);  // PlayerLOD/03;AINearToMid
    float* midToFar = (float*)G(0x00D37C90);   // PlayerLOD/04;AIMidToFar
    // only values that look like the tunables (not loaded yet, or another layout: left alone)
    if (*midLods < 0 || *midLods > 31 || *nearLods < 0 || *nearLods > 31 || !(*midToFar > 0 && *midToFar < 10000) ||
        !(*nearToMid > 0 && *nearToMid < 10000))
        return;
    bool changed = false;
    if (*midLods < 31) *midLods = 31, changed = true;
    if (*midToFar < 200.0f) *midToFar = 200.0f, changed = true;
    if (g_cfg.carDetail == 2) {
        if (*nearLods < 7) *nearLods = 7, changed = true;
        if (*nearToMid < 40.0f) *nearToMid = 40.0f, changed = true;
    }
    if (changed && g_cfg.log) {
        static int logged;
        if (logged++ < 5)
            Log("car detail %s: near %d up to %.0f m, mid %d up to %.0f m", g_cfg.carDetail == 2 ? "high" : "as the Xbox 360",
                *nearLods, *nearToMid, *midLods, *midToFar);
    }
}

// Sun shadow distance (ShadowDistance): at the High and Very High quality levels in single player the
// game takes its sun shadow cascades from the global Lighting/CSM_Max settings (0x88A3E0 picks them,
// 0x894B60 copies them): shadows reach 300 m and fade out over the last 100 m. Both are scaled here;
// split screen and the Low and Medium levels keep their per-level settings. Plain tunables in .data
// (registered at 0xB8904C / 0xB890B0), re-scaled when anything loads new values into them.
static void ScaleShadowDistance()
{
    if (g_cfg.shadowDistance == 1.0f) return;
    float* const kVars[2] = {
        (float*)G(0x00D47874),  // Lighting/CSM_Max/Settings/c; range
        (float*)G(0x00D47864),  // Lighting/CSM_Max/Settings/a; fadeout range
    };
    static float s_written[2];
    for (int i = 0; i < 2; i++) {
        float v = *kVars[i];
        if (v == s_written[i] || !(v > 0 && v < 100000.0f)) continue;
        s_written[i] = v * g_cfg.shadowDistance;
        *kVars[i] = s_written[i];
        if (g_cfg.log) Log("shadow %s %.0f -> %.0f m", i ? "fadeout range" : "range", v, s_written[i]);
    }
}

static decltype(&PeekMessageA) s_peekMessage;
static const void* s_framePeekReturn;
static const BYTE* const kInputUpdateAnchor = (const BYTE*)G(0x00558969);

static BOOL WINAPI Hook_PeekMessageA(LPMSG msg, HWND wnd, UINT min, UINT max, UINT remove)
{
    if (_ReturnAddress() == s_framePeekReturn) {
        RefreshPadSlots();  // no virtual device polls the XInput slots: their connection state and pad use
        OnInputFrame();
    }
    return s_peekMessage(msg, wnd, min, max, remove);
}

static const BYTE* Rel32CallTarget(const BYTE* call)
{
    return call + 5 + *(const LONG*)(call + 1);
}

// Without wrapped pads (VirtualPads=0 or no XInput DLL) no virtual device is polled, so the per-frame extras
// (OnInputFrame) run from the first PeekMessageA in the game's input update instead. The main loop calls this
// update once per pass, before simulation and rendering at both frame rates.
static bool s_frameHooked;

bool FrameHookInstalled()
{
    return s_frameHooked;
}

static void HookFrameWithoutPads()
{
    if (!BytesMatch(kInputUpdateAnchor + 0x22, "8B0D") || !BytesMatch(kInputUpdateAnchor + 0x28, "E8") ||
        !BytesMatch(kInputUpdateAnchor + 0x2D, "8B178B4214568BCFFFD0")) {
        Log("frame hook: main loop signature mismatch, hook skipped");
        return;
    }
    // written out in each check, so tools/buildmap.py can follow the call
    if (!BytesMatch(Rel32CallTarget(kInputUpdateAnchor + 0x28), "83EC20538B1D") ||
        !BytesMatch(Rel32CallTarget(kInputUpdateAnchor + 0x28) + 0x0A, "566A016A006A006A008D44241C508BF1FFD385C0")) {
        Log("frame hook: input update signature mismatch, hook skipped");
        return;
    }
    const BYTE* update = Rel32CallTarget(kInputUpdateAnchor + 0x28);
    void** slot = *(void***)(update + 6);  // mov ebx, [PeekMessageA import slot]
    void* peek = (void*)GetProcAddress(GetModuleHandleA("user32.dll"), "PeekMessageA");
    if (!peek || *slot != peek) {
        Log("frame hook: PeekMessageA import not where expected, hook skipped");
        return;
    }
    s_peekMessage = (decltype(&PeekMessageA))peek;
    s_framePeekReturn = update + 0x1C;
    void* hook = (void*)Hook_PeekMessageA;
    WriteMemory(slot, &hook, sizeof(hook));
    s_frameHooked = *slot == hook;
    if (s_frameHooked) Log("frame hook: per-frame extras run from the input update (pads not wrapped)");
    else Log("frame hook: input update slot could not be written");
}

// Graphics quality limit (QualityCap): the game sets its quality level (0xD0F7E8, 0 Low .. 3 Very High) only
// through one function (0x471530), which the options menu calls with the level it shows and saves, and
// which clamps that level to the range of the level's tweakable (registered at 0xB6B2F0 with 0..3, the
// object at [0xD0F7EC], minimum at +8, maximum at +0xC). Lowering the tweakable's maximum caps every level
// the game sets from then on, while the level the menu keeps and saves stays the player's own; a level
// already set above the cap is lowered once. Data only: the object is on the heap, the code is untouched.
// Called before the game makes its device (so the renderer starts at the capped level) and every frame.
void ApplyQualityCap()
{
    if (g_cfg.qualityCap >= 3 || CurrentBuild() == BUILD_UNKNOWN || !(g_cfg.patchMask & PATCH_LOD)) return;
    BYTE* tw = *(BYTE**)G(0x00D0F7EC);
    if (!tw || IsBadReadPtr(tw, 0x10)) return;  // not registered yet
    int* lo = (int*)(tw + 8);
    int* hi = (int*)(tw + 0xC);
    if (*lo != 0 || (*hi != 3 && *hi != g_cfg.qualityCap)) {
        static bool logged;
        if (!logged) Log("graphics quality limit: the level's range is %d..%d, not the expected 0..3, left alone", *lo, *hi);
        logged = true;
        return;
    }
    if (*hi != g_cfg.qualityCap) {
        *hi = g_cfg.qualityCap;
        Log("graphics quality limit: at most level %d", g_cfg.qualityCap);
    }
    int* level = (int*)G(0x00D0F7E8);
    if (*level > g_cfg.qualityCap) {
        Log("graphics quality %d -> %d (limit)", *level, g_cfg.qualityCap);
        *level = g_cfg.qualityCap;
    }
}

void OnInputFrame()
{
    CheckD3DHooks();
    if (g_cfg.steamLan) OnlineFrame();  // before the extras below read their settings
    if (s_knownBuild && (g_cfg.patchMask & PATCH_LOD)) {
        static unsigned frame;
        ScaleLodDistances();
        ApplyCarDetail();
        ScaleShadowDistance();
        if (frame++ % 30 == 0) ScalePowerplayDrawDists();
        // Precomputed visibility: each track sector lists what can be seen from it, and the rest is
        // skipped, so buildings pop in at sector borders whatever their distance. The game's own
        // debug switches turn the lists off; then streaming distance, LOD distances and the frustum
        // decide. Sectors/Visibility/Disable Powerplay Visibility (0x8137FB), Disable Sector
        // Visibility (environment sectors, 0x7C1CC2) and Disable Visibility Map (entities, 0xAE6720).
        if (g_cfg.popInFix) {
            *(BYTE*)G(0x00D8B368) = 1;
            *(BYTE*)G(0x00D82788) = 1;
            *(BYTE*)G(0x00D9AFCC) = 1;
        }
        // Shadows=0: the game's own switch Lighting/Debug/Cast shadows - Disable all shadows (registered at
        // 0xB6DB50), which every shadow caster pass (cars, track, physics and pose objects) checks
        // before it draws into the shadow maps; with nothing in them nothing is shadowed
        if (!g_cfg.shadows) *(BYTE*)G(0x00D716EC) = 1;
        ApplyQualityCap();
    }
    if (s_knownBuild && g_cfg.fixIconSize && (g_cfg.patchMask & PATCH_ICONSIZE)) FixPowerplayIconSize();
    if (s_knownBuild && (g_cfg.patchMask & PATCH_UNLOCK)) ApplyUnlocks();
    if (s_knownBuild) LapMenuTick();
    if (s_knownBuild && (g_cfg.patchMask & PATCH_FLARES)) ApplyLensFlare();
    if (s_knownBuild && g_cfg.explosionShakeOff && (g_cfg.patchMask & PATCH_CAMERA)) ApplyExplosionShake();
    if (s_knownBuild && (g_cfg.boost || g_cfg.powerFill || g_cfg.aiBoost || g_cfg.handbrake) && (g_cfg.patchMask & PATCH_BOOST)) BoostTick();
    if (s_knownBuild && (g_cfg.zoomKeys || g_cfg.photoMode || g_cfg.hudKey) && (g_cfg.patchMask & PATCH_CAMERA)) CameraTick();
    if (g_cfg.log && s_knownBuild && CurrentBuild() == BUILD_STEAM) WatchVmContexts();
    if (g_cfg.log && (g_cfg.patchMask & PATCH_SAVEDIAG)) WatchSaveFiles();
    if (g_cfg.log && s_knownBuild) {
        // what the tweakables and the renderer look like while playing (they may be reloaded per level)
        static DWORD last;
        DWORD now = GetTickCount();
        if (now - last > 15000) {
            last = now;
            DlcLogStats();
            BYTE* r = *(BYTE**)G(0x00D64708);
            BYTE* rr = r ? *(BYTE**)(r + 4) : NULL;
            Log("state: quality=%d internal=%dx%d screen=%dx%d LOD high=%.0f mid=%.0f low=%.0f pool level=%d used=%d",
                *(int*)G(0x00D0F7E8),
                rr ? *(int*)(rr + 0x4C) : -1, rr ? *(int*)(rr + 0x50) : -1, *(int*)G(0x00D56648), *(int*)G(0x00D5664C),
                *(float*)G(0x00D38A90), *(float*)G(0x00D38A98), *(float*)G(0x00D38AA0), s_levelStreamPoolMB,
                *kStreamPoolMB);
        }
    }
    if (s_knownBuild) {
        OpponentEnginesTick();
        OpponentEngineVolumeTick();
    }
    // [DLC] Music only with a merged overlay that brings Quarry's level audio (the only level it swaps the music
    // for); otherwise the base game's audio thread is left alone. Decided once: the overlay is merged at DLL load
    // The same two messages load Deadline's sounds (the DLC's DLCMenu bank) when a Deadline race starts
    static int s_musicWanted = -1, s_menuWanted = -1;
    if (s_musicWanted < 0) {
        s_musicWanted = g_cfg.dlcMusic && g_cfg.dlcOverlays && g_cfg.dlcTracks &&
                        DlcOverlayHas("Audio/Locales/quarry/quarry_Audio.xml");
        s_menuWanted = DeadlineMenuWanted();
    }
    if (s_musicWanted || s_menuWanted)  // until its two audio thread messages are wrapped (dlcmusic.cpp)
        DlcMusicTick(s_musicWanted != 0, s_menuWanted != 0);
    // Log=1: the game polls its input but presents no frames (window in the background, device lost, a hang
    // in the drawing): when that starts and ends, with the foreground window's owner
    if (g_cfg.log && g_lastPresent) {
        static bool stopped;
        DWORD since = GetTickCount() - g_lastPresent;
        if (!stopped && since > 2000) {
            stopped = true;
            DWORD pid = 0;
            HWND fg = GetForegroundWindow();
            GetWindowThreadProcessId(fg, &pid);
            char title[64] = "";
            // another program's window only: GetWindowText on a window of this process sends it a message and
            // would wait for the very thread that may hang
            if (pid != GetCurrentProcessId()) GetWindowTextA(fg, title, sizeof(title));
            Log("no frame for %lu ms while the game runs; foreground window: %s ('%s')", since,
                pid == GetCurrentProcessId() ? "the game" : "another program", title);
        } else if (stopped && since < 500) {
            stopped = false;
            Log("frames again");
        }
    }
    if (g_cfg.glyphs == GLYPHS_KEYBOARD) return;

    // a controller that is already connected counts as the active device until a key is pressed
    static bool first = true;
    if (first) {
        first = false;
        for (int i = 0; i < XUSER_MAX_COUNT && !s_lastPadActivity; i++)
            if (PadSlotConnected(i)) s_lastPadActivity = 1;
    }

    // keyboard / mouse button presses switch the glyphs back to keys; modifiers, Windows keys,
    // F keys and Print are left out, as overlay hotkeys (screenshots, recording) use them
    BYTE keys[256];
    if (GetKeyboardState(keys)) {
        for (int vk = 1; vk < 256; vk++) {
            bool hotkey = (vk >= VK_SHIFT && vk <= VK_MENU) || (vk >= VK_LSHIFT && vk <= VK_RMENU) ||
                          vk == VK_LWIN || vk == VK_RWIN || vk == VK_SNAPSHOT || (vk >= VK_F1 && vk <= VK_F24) ||
                          (vk >= 0xC3 && vk <= 0xDA);  // VK_GAMEPAD_*: Windows' own key events for the pad
            if (!hotkey && (keys[vk] & 0x80) && !(s_lastKeys[vk] & 0x80)) {
                s_lastKbmActivity = GetTickCount() | 1;
                break;
            }
        }
        memcpy(s_lastKeys, keys, sizeof(keys));
    }

    bool want = WantPadGlyphs();
    static bool pressStartPrepared;
    if (s_origLookup && !pressStartPrepared) {
        s_dict = *(void**)G(0x00D64E28);
        if (s_dict) pressStartPrepared = PreparePressStart();
    }
    UpdatePressStart(want);
    if (s_promptBarKnown) UpdatePromptBar(want);
    if (TestFeed* feed = GetTestFeed()) feed->glyphsPad = s_padGlyphsShown;
}

// The finish card has its own prompt timer, but its input handler also requires
// TransInFinished. Hold the original update during the entering state so Continue
// appears only once input is accepted.
using RaceEndUpdate = void (__thiscall*)(BYTE*, float);
static RaceEndUpdate s_raceEndUpdate;

namespace {
constexpr unsigned kCardState = 0x58;
constexpr unsigned kCardMovieHolder = 0x68;  // c2DFlashLayer's movie, used by 0x4D5480
constexpr unsigned kCardRaceSetup = 0x38C;
constexpr unsigned kCardPromptTime = 0x390;
constexpr unsigned kCardPromptShown = 0x394;
constexpr unsigned kCardOutsideSeason = 0x7FC;  // cRaceEnd::Enter sets it from the inverse career flag
constexpr unsigned kSetupViewports = 0x9C;
constexpr unsigned kSetupDemo = 0x9B2;  // the results prompt offers Quit Demo
constexpr int kEntering = 1;
constexpr int kOnlineRace = 6;
constexpr int kOnlineChampionship = 7;
constexpr float kQuickPlayEntrance = 3.0f;
constexpr double kCreditsActiveFrame = 77;
constexpr double kBeforeTransitionFinished = 199;
}

static bool OwnCode(const void* p)
{
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    HMODULE m = NULL, self = NULL;
    return GetModuleHandleExA(flags, (LPCSTR)p, &m) && GetModuleHandleExA(flags, (LPCSTR)&OwnCode, &self) && m == self;
}

static void ShortenQuickPlayFinish(BYTE* card)
{
    __try {
        BYTE* holder = *(BYTE**)(card + kCardMovieHolder);
        void* movie = holder ? *(void**)(holder + 4) : NULL;
        if (!movie || *(void***)movie != (void**)0x00C41CF4) return;
        void** vtable = *(void***)movie;
        auto get = (GFxGetVariable_t)vtable[0x30 / 4];
        GFxValue current = {};
        if (!get(movie, &current, "mcMain._currentframe") || current.type != GFX_NUMBER ||
            !(current.num >= kCreditsActiveFrame && current.num < kBeforeTransitionFinished))
            return;
        char name[32];
        if (!ReadMovieName(movie, name, sizeof name) || _stricmp(name, "RaceEnd.gfx")) return;

        // The next normal movie advance runs the stock frame-200 callback.
        GFxValue frame = {};
        frame.type = GFX_NUMBER;
        frame.num = kBeforeTransitionFinished;
        auto invoke = (GFxInvoke_t)vtable[0x48 / 4];
        if (invoke(movie, "mcMain.gotoAndPlay", NULL, &frame, 1))
            Log("finish card: Quick Play entrance shortened from frame %.0f", current.num);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Leave the movie's original entrance to finish.
    }
}

static void __fastcall Hook_RaceEndUpdate(BYTE* card, void*, float delta)
{
    const BYTE* race = *(const BYTE**)(card + kCardRaceSetup);
    const int type = race ? *(const int*)race : -1;
    if (card[kCardPromptShown] || !race || race[kSetupDemo] || *(const int*)(race + kSetupViewports) != 1 ||
        type == kOnlineRace || type == kOnlineChampionship || OnlineSession()) {
        s_raceEndUpdate(card, delta);
        return;
    }

    int state = *(const int*)(card + kCardState);
    if (state == kEntering) {
        float& elapsed = *(float*)(card + kCardPromptTime);
        elapsed += delta;
        // A hitch can leave the movie behind the timer. Try again as it advances.
        if (card[kCardOutsideSeason] && elapsed >= kQuickPlayEntrance) ShortenQuickPlayFinish(card);
        return;
    }
    s_raceEndUpdate(card, delta);
}

static void InstallRaceEndTiming()
{
    if (!g_cfg.raceEndTiming) return;
    if (CurrentBuild() != BUILD_STEAM) {
        Log("finish card: Steam build only, not the Russian depot (%s)", BuildName());
        return;
    }
    void** slot = (void**)0x00C208EC;
    if (!BytesMatch((void*)0x00C208D8, "5882C900") ||  // cRaceEnd RTTI
        !BytesMatch(slot, "C0888000") ||
        !BytesMatch((void*)0x00C2091C, "40898000")) {
        Log("finish card: cRaceEnd vtable mismatch");
        return;
    }
    if (!BytesMatch((void*)0x008088C0, "80B994030000007565F30F10442404F30F588190030000F30F1181900300000F2F0580FAD300")) {
        Log("finish card: cRaceEnd::Update bytes mismatch");
        return;
    }
    if (!BytesMatch((void*)0x00808940, "578BF980BF94030000000F84B1000000837F58020F85A70000008B878C0300005333DB39989C000000")) {
        Log("finish card: cRaceEnd input handler bytes mismatch");
        return;
    }
    if (!BytesMatch((void*)0x00808679, "385D08C6442412010F94C03AC38886FC070000")) {
        Log("finish card: cRaceEnd::Enter bytes mismatch");
        return;
    }
    if (!BytesMatch((void*)0x00C41D24, "70A99800")) {  // GFxMovieRoot::GetVariable
        Log("finish card: GFxMovieRoot::GetVariable slot mismatch");
        return;
    }
    // SteamLAN (online.cpp) or another glimpse_fix hook may already wrap GFxMovieRoot::Invoke.
    void* const* invokeSlot = (void* const*)0x00C41D3C;
    void* curInvoke = IsBadReadPtr(invokeSlot, 4) ? NULL : *invokeSlot;
    if (!curInvoke || (curInvoke != (void*)0x0098D2E0 && !OwnCode(curInvoke))) {
        Log("finish card: GFxMovieRoot::Invoke slot mismatch (%p)", curInvoke);
        return;
    }
    // A replacement movie can move its callback while keeping the same name and frame count.
    if (!ResidentEntryMatches("UI/InGame/RaceEnd.gfx", 27516, 0x46B24DB4)) {
        Log("finish card: stock RaceEnd.gfx not found, using game timing");
        return;
    }
    if (SwapSlot(slot, (void*)Hook_RaceEndUpdate, (void**)&s_raceEndUpdate))
        Log("finish card: Continue follows transition completion");
    else
        Log("finish card: update slot cannot be written");
}

void ApplyGamePatches()
{
    if (CurrentBuild() == BUILD_UNKNOWN || !BytesMatch((void*)G(0x0040F0F0), "5355568BB16CCF0000")) {
        Log("unknown SplitSecond.exe build, no patches applied");
        return;
    }
    if (!BuildMapComplete()) {
        Log("SplitSecond.exe build (%s): address table incomplete, no patches applied", BuildName());
        return;
    }
    s_knownBuild = true;
    Log("SplitSecond.exe build recognised (%s)", BuildName());
    const unsigned mask = g_cfg.patchMask;
    if (!PadsWrapped()) HookFrameWithoutPads();
    if (g_cfg.separateTriggers && (mask & PATCH_TRIGGERS)) PatchTriggerPreset();
    if (mask & PATCH_RUMBLE) HookRumble();
    if ((g_cfg.skipIntro > 0 || g_cfg.cutsceneSkip) && (mask & PATCH_INTRO)) HookIntroSkip();
    if (mask & PATCH_PROMPTBAR) CheckPromptBar();
    if (g_cfg.glyphs != GLYPHS_KEYBOARD && (mask & PATCH_TITLETEXT)) CheckTitleText();
    if (mask & PATCH_FRAMERATE) PatchFrameRate();
    if (mask & PATCH_STREAMPOOL) RaiseStreamPool();
    if (mask & PATCH_MOTIONBLUR) PatchMotionBlur();
    if (mask & PATCH_VIGNETTE) PatchVignette();
    if (mask & PATCH_FLARES) PatchBloomAndFlares();
    if (mask & PATCH_CAMERA) PatchCamera();
    if (mask & PATCH_CAMERA) PatchDepthOfField();
    if (mask & PATCH_RACEEND) InstallRaceEndTiming();
    if (g_cfg.textureLoadFix && (mask & PATCH_TEXLOAD)) InstallTextureLoadFix();
    if (mask & PATCH_UNLOCK) InstallUnlockAllCareerFix();
    if (g_cfg.steamLan && (mask & PATCH_ONLINE)) OnlineStart();
    CheckOpponentEngineBanks();
    HookSoundBanks();
    HookVoiceDspCreation();
    HookReverbLog();
    InstallMenuHook();
    InstallLapMenu();
    if ((g_cfg.log || g_cfg.photoMode || g_cfg.opponentEngines) && (mask & PATCH_AUDIODIAG))
        InstallAudioDiagnostics();
}
