// Split/Second PC fix - [DLC] Pickups=1 (experimental): the green pickup boxes of the converted DLC game mode
// Deadline (TimeTrialPlus, stage 2; stage 1 is the data-only countdown of tools\dlc\mode\deadline_mode.py and
// dlcmode.cpp). As on the Xbox (TU4, tools\dlc\mode\TU4_RULES.md): driving through a green box stops the clock
// for 0.5 s, 2.0 s while drifting (frozen += 0.5 x 4); the stopped time stacks and runs down in game time;
// restarting the race puts every box back and clears it.
//
// Where the PC game is (SplitSecond.exe, Steam build, read-only Ghidra project and disassembly):
// - The mode. Stage 1 gives TimeTrialPlus the PC type 'Time Trial' with /RaceSetup/Logic/Countdown Enabled and
//   'End Condition Countdown': the PC's generic logic objects run it. cCountdown (GameLogicNew, vtable 0xBA96B0,
//   one per player, created by 0x4454A0 which sets its logic id [0xD03C74] = 6, so the player's slot
//   player+0xA4+6*4) has its output at +0x2C (cCountdownOutput: +4 enabled, +8 start value, +0xC time left,
//   +0x10 the value before this frame's update, +0x14 bonus). Its update, vtable slot 4 (0x4454F0, __thiscall
//   (dt), ret 4), copies Enabled, and only while the player's race state [[this+0x30]+4]+0x10 is 6 (racing)
//   subtracts dt and adds the wreck/overtake/barrel/nemesis bonuses (all off in this mode), clamped 0..3600. Slot 2
//   (0x445400) is the reset: time left = start value. cEndGame (vtable 0xBA9034, update 0x442DA0) ends the race
//   through 0x442780 when Enabled and time left <= 0. The HUD reads the same output (0x4CB7E0: +8 and +0xC).
//   So the clock is stopped by a data write: after the game's update, while frozen time is left, time left is
//   set back to the value before the update (as the Xbox runs its countdown with dt = 0 then, 0x8837F2D8).
//   Both slots of the vtable are swapped (data in .rdata); no code byte changes.
// - The race: [0xD66B28] cInGameResources +0x14 race setup (+0xE0 track name, inline; +0x1E0 subtrack, char*;
//   +0x988 game mode name, char*; +0x9C viewports; the same fields 0x793550 / 0x790D90 / 0x4CBC60 read),
//   +0xE4 cGameLogic (+0x1C players, the human first), +0xEC eight scoring managers (one per player slot,
//   0x4AED18: +4 count, +8 scorers; [1] is cScoreDrifting, vtable 0xC22E50, made by 0x7AFCF0). The car:
//   player +0xAC cVehicleState +0x40 vehicles [+4] vehicle +0x584 chassis hkpRigidBody (+0xE0/+0xF0/+0x100
//   rotation columns, +0x100 forward, +0x110 position), as boost.cpp.
// - Drifting: cScoreDrifting's update (slot 2, 0x824690) starts a drift when its input's drift flag (+0xA4) is
//   set and keeps "in a drift" at scorer+0x40 until it ends; that byte of player 1's scorer is read. Should
//   the scorer not be there, a slip angle of the chassis (velocity against heading, above 15 degrees at more
//   than 12 m/s) stands in (logged).
// - The camera: the viewport manager [0xD66000], current viewport index +0xC, its cameras at +0x68 + i*0x2F4
//   (0x794350); a camera keeps view (+0xD8), projection (+0x158, 0x50AAF0: depth 0..1, 2/w, 2/h) and view x
//   projection (+0x198, 0x49F350, row vectors), copied in by 0x794B60 / 0x483C20 every frame (0x4B8DD0).
// - Drawing: the Xbox draws Generic/Models/GreenPickupBox (in the DLC archive only, Xbox model and shaders;
//   the PC data has none and no PC code loads them). tools\dlc\mode\pickupmodel.py converts it from the user's
//   own DLC data into a blob of our own (glimpse/pickups/GreenPickupBox.bin in the mode pack: a hexagonal token
//   of 151 vertices and 72 triangles, its icon, slide show and cube map as DXT1), which is drawn here with our
//   own shader (pickups.hlsl PickupModelVS/PS, the Xbox shader 0x58f57228's look). The TU4 code (render
//   0x882AA010 for every pickup not collected) draws the box with the transform the pickup keeps from the file
//   (+0x20, set once by vtable slot 8, 0x882A9020): no rotation or bobbing; the only animation is the shader's
//   slide show. A collected box is not drawn any more. Without the blob (a pack built before) the boxes are our
//   own 1.5 m cubes with glowing edges. One static vertex and index buffer, one draw per box within 100 m
//   (fading from 75 m, as the Xbox's Pickups/Render tweakables). They go into the tone mapped picture right
//   after the game's tone mapping (postfx.cpp's bloom hook, the first pass of the post processing chain
//   0x493BF0 that the scene render 0x455E30 calls after the deferred and forward passes, before the HUD): the
//   colour grading, motion blur and SMAA after it treat them as part of the scene. Depth tested against the scene's depth
//   surface (the one set when the post processing starts, or the last one of the picture's size the game
//   set), no depth write, colour only (the target's alpha stays the game's); every device state is captured
//   and put back with a state block. With HDR=1 the same draws go once more into hdr.cpp's gain target with
//   gain 1 (PickupGainPS): the HDR output boosts only pixels that still show the scene, so the boxes stay at
//   paper white like the HUD instead of taking the boost of the bright sky behind them.
// - The data: Environments/Levels/<track>/Subtracks/<sub>/Pickups/<track>.pkps (the Xbox's path), from the
//   merged resident.ark (deadline_mode.py puts the 11 DLC files there); read once per race. Parser below,
//   tools\dlc\mode\pkps.py is the reference.
// - The HUD (see "Deadline's HUD and sounds" below): as TU4 does, through the PC's own HUD message queue, the
//   DLC's DialsInfo.gfx timer functions and the DLC's DLCMenu sounds (both put into the mode pack by
//   deadline_mode.py), the game's 2D sound call and the dictionary's TU4 texts; the top centre seconds counter
//   (TimerOverlay.gfx) is hidden. Only when that code is not as expected (or the HUD is not found) a small
//   "+0,5s" line is drawn on the back buffer at Present (overlay.cpp, kFeedbackY). While the clock stands
//   ([DLC] PickupVignette, default on) TU4's "Frozen Time" post presets from the mode pack go onto the game's own
//   colour cube / vignette pass (PickupsGradeBegin, see "the Frozen Time post presets"); with a pack without them
//   a faint cyan glow along the screen's edges is drawn into the tone mapped picture with the boxes (EdgeGlow).
// - The end screen (DeadlineResult, Hook_RaceEndEnter): race kind 1 instead of 8, a timeout as DNF; the race time
//   stands while the clock is frozen (Hook_LapsUpdate), the rivals and the time left are dlcrivals.cpp's.
// Steam build only; every address is checked before use. Inert outside a Deadline race on one screen.
#include "common.h"
#include <d3d9.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "pickup_VS.h"  // build\, compiled from src\pickups.hlsl by build.bat
#include "pickup_PS.h"
#include "pickup_ModelVS.h"
#include "pickup_ModelPS.h"
#include "pickup_GainPS.h"

bool DlcOverlayRead(const char* path, std::vector<unsigned char>* out);  // dlcfs.cpp

// ---------------------------------------------------------------- game addresses (Steam build)
static void** const kCountdownVtable = (void**)0x00BA96B0;
static const DWORD kCountdownCol = 0x00C77774;
static const char kCountdownClass[] = ".?AVcCountdown@GameLogicNew@@";
static const DWORD kCountdownReset = 0x00445400;   // slot 2
static const DWORD kCountdownUpdate = 0x004454F0;  // slot 4
static const DWORD* const kCountdownId = (const DWORD*)0x00D03C74;  // the logic id 0x4454A0 gives it (6)
static BYTE* const* const kInGameResources = (BYTE* const*)0x00D66B28;
static BYTE* const* const kViewportManager = (BYTE* const*)0x00D66000;
static const DWORD kGameLogicVt = 0x00BA9A74, kPlayerVt = 0x00BA63D4, kVehicleStateVt = 0x00BA7378,
                   kRigidBodyVt = 0x00BE9C90, kDriftScorerVt = 0x00C22E50;
static float* const kInitialCountdown = (float*)0x00D0B7E0;  // /RaceSetup/Logic/Countdown 'Initial Countdown'
// the race result (see DeadlineResult)
static BYTE* const* const kRaceInfo = (BYTE* const*)0x00D719F8;  // +0x194C the race kind (0x7D5290)
static const DWORD kRaceKindOff = 0x194C;
static const int kKindTimeTrial = 1, kKindBonus = 8;  // 0x7D5290: "TIMETRIAL" -> 1, unknown names -> 8
static const DWORD* const kEndGameId = (const DWORD*)0x00D03700;  // cEndGame's logic id (9, 0x4425F1)
static const DWORD kEndGameVt = 0x00BA9034, kEndGameOutVt = 0x00BA9070;
static const DWORD* const kRecordCollectionId = (const DWORD*)0x00D03C78;  // cRecordCollection's logic id (0, 0x446281)
static const DWORD kRecordCollectionVt = 0x00BA97D4, kRecordCollectionOutVt = 0x00BA9810, kRecordIntVt = 0x00BA6738;
static const char kModeName[] = "TimeTrialPlus";
static const char kModeFile[] = "GameLogic/GameModes/TimeTrialPlus.params";

// ---------------------------------------------------------------- rules (Xbox TU4, Deadline's values)
static const float kFreezeBonus = 0.5f;      // 'Freeze pickup time bonus' in TimeTrialPlus.params
static const float kDriftMultiplier = 4.0f;  // 'Pickup Drift Multiplier'
static const float kBoxHalf = 0.75f;         // collision and drawn box: 1.5 m cube centred on the transform
static const float kCarHalf[3] = { 1.0f, 1.0f, 2.4f };  // side, up, forward: about the cars' size
static const float kDrawDist = 100.0f, kFadeDist = 75.0f;
static const DWORD kCollectAnimMs = 300;

enum Colour { GREEN, YELLOW, RED, WHITE, BLACK, COLOURS };
static const char* const kColourNames[COLOURS] = { "green", "yellow", "red", "white", "black" };

struct Pickup {
    float c[3];        // centre
    float ax[3][3];    // rows 0..2 of the transform: the box's axes
    BYTE colour;
    volatile BYTE collected;
    BYTE nearBy;       // the car came within 10 m (TU4 slot 10: leaving to 15 m without it is a miss)
    DWORD collectTick;
};
static const int kMaxPickups = 512;
// two buffers: LoadTrack (game logic thread) fills the one not in use, so the render thread never reads boxes
// while they are written; it takes buffer and count together from s_drawState (one read)
static Pickup s_pkBuf[2][kMaxPickups];
static Pickup* s_pk = s_pkBuf[0];  // the boxes in use (game logic thread)
static volatile LONG s_count;      // their count (game logic thread), published after the array is filled
static volatile LONG s_drawState;  // for the render thread: bit 16 the buffer, bits 0..15 the count
static char s_loaded[160];         // "<track>/<sub>" of the boxes in s_pk ("" = none)

typedef void(__thiscall* CountdownUpdate_t)(void* self, float dt);
typedef void(__thiscall* CountdownReset_t)(void* self, DWORD arg);
static CountdownUpdate_t s_origUpdate;
static CountdownReset_t s_origReset;
static bool s_hooked;

// race state (game logic thread)
static void* s_cd;                 // player 1's cCountdown seen last
static bool s_needEval = true;     // decide again whether this is a Deadline race
static bool s_lapsHooked;
static volatile bool s_active;     // a Deadline race with boxes
static int s_lastState = -1;
static volatile float s_frozen;    // seconds the clock stays stopped
static volatile DWORD s_lastUpdate;  // GetTickCount of the last countdown update in an active race
static float s_carPos[3];
static int s_hits, s_driftHits;
static bool s_slipLogged, s_scorerLogged;
static bool s_resetPending;        // the reset slot ran for player 1's countdown since the last start
static volatile bool s_deadline;   // the race is Deadline (with or without boxes)
static float s_bonusWritten = -1;  // DeadlineBonus: the Initial Countdown value written last
static char s_bonusRace[96];       // and the race it was written for ("<track>/<sub>")
static bool s_bonusOldPackLogged;
static bool s_resultCode;          // the code DeadlineResult relies on is as expected (checked once)
static bool s_result;              // this race: Deadline on one screen, offline
static bool s_timedOut;            // the clock ran out in this race
static bool s_kindLogged;          // the race kind change was logged for this race
static bool s_endLogged;
// this frame's pickups (Collide) for the HUD rules: a green box hit, hit while drifting, one missed
static bool s_framePicked, s_frameDrift, s_frameMissed;
static void HudRaceStart();        // the HUD state of a new or restarted race (below)
static volatile LONG s_cdSeq, s_heldSeq;  // player 1's countdown updates in an active race / the last one that held the clock
static volatile float s_startValue;       // the countdown's start value of this race (cCountdownOutput +8)
static DWORD s_lapsHeld;                  // racing frames cLaps' race time was held this race (logged)
static bool s_gradeOn;                    // the Frozen Time presets were read: the grade instead of the edge glow
static volatile DWORD s_gradeStartTick, s_gradeEndTick;  // the clock stopped / runs again (0 = not in this race)
static void LapsInstall();         // the race time stands while the clock is frozen (below)
static void GradeLoad();           // the "Frozen Time" post presets (below)
static void GradeFrame(float prev, float now);
// dlcrivals.cpp: the Xbox's rival standings and the time left on the end screens
bool RivalsInstall();
bool DeadlineResultsNow(float* start);
void RivalsObjectives(BYTE* info);
void RivalsRaceEnd(BYTE* screen);
void RivalsRaceStart();

// ---------------------------------------------------------------- game data, all reads guarded
static bool Is(const BYTE* p, DWORD vt) { return p && *(const DWORD*)p == vt; }

static BYTE* Ptr(const BYTE* p, DWORD off) { return p ? *(BYTE* const*)(p + off) : NULL; }

static BYTE* Resources() { return *kInGameResources; }

static BYTE* Player1(BYTE* res)
{
    BYTE* logic = Ptr(res, 0xE4);
    if (!Is(logic, kGameLogicVt)) return NULL;
    int n = *(const int*)(logic + 0x18);
    if (n < 1 || n > 16) return NULL;
    BYTE* p = Ptr(Ptr(logic, 0x1C), 0);
    return Is(p, kPlayerVt) ? p : NULL;
}

static BYTE* Chassis(BYTE* player)
{
    BYTE* state = Ptr(player, 0xAC);
    if (!Is(state, kVehicleStateVt)) return NULL;
    BYTE* body = Ptr(Ptr(Ptr(state, 0x40), 4), 0x584);
    return Is(body, kRigidBodyVt) ? body : NULL;
}

// the countdown is player 1's (its slot in the player's logic list)
static bool IsPlayer1Countdown(void* self, BYTE** playerOut)
{
    __try {
        BYTE* player = Ptr((BYTE*)self, 0x30);
        BYTE* p1 = Player1(Resources());
        DWORD id = *kCountdownId;
        if (!player || player != p1 || id > 63 || Ptr(player, 0xA4 + id * 4) != self) return false;
        *playerOut = player;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int RaceState(BYTE* player)
{
    BYTE* s = Ptr(player, 4);
    return s ? *(const int*)(s + 0x10) : -1;
}

static bool Same(const char* a, const char* b, size_t max)
{
    for (size_t i = 0; i < max; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
        if (!x) return true;
    }
    return false;
}

// the race setup's mode name is the params file name ("TimeTrialPlus.params", seen in game); the bare name is
// accepted too. Case-insensitive; reads at most sizeof(kModeName) + sizeof(".params") characters
static bool IsDeadlineName(const char* s)
{
    static const char kExt[] = ".params";
    const size_t n = sizeof(kModeName) - 1;
    for (size_t i = 0; i < n; i++) {
        char x = s[i], y = kModeName[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return !s[n] || Same(s + n, kExt, sizeof(kExt));
}

static void CopyName(char* out, size_t n, const char* s)
{
    size_t i = 0;
    for (; s && s[i] && i + 1 < n; i++) out[i] = (s[i] >= 32 && s[i] < 127) ? s[i] : '?';
    out[i] = 0;
}

// player 1 is drifting: the game's drift scorer, else the chassis' slip angle
static bool Drifting(BYTE* res, BYTE* body)
{
    __try {
        BYTE* mgr = Ptr(res, 0xEC);
        int n = mgr ? *(const int*)(mgr + 4) : 0;
        BYTE* scorer = n >= 2 && n <= 64 ? Ptr(Ptr(mgr, 8), 4) : NULL;
        if (Is(scorer, kDriftScorerVt)) {
            if (!s_scorerLogged && g_cfg.log) Log("DLC pickups: drifting from the game's drift scorer (%p)", scorer);
            s_scorerLogged = true;
            return scorer[0x40] != 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (!s_slipLogged) Log("DLC pickups: the drift scorer is not where expected, drifting is taken from the slip angle");
    s_slipLogged = true;
    const float* v = (const float*)(body + 0x1A0);
    const float* f = (const float*)(body + 0x100);
    float vh = sqrtf(v[0] * v[0] + v[2] * v[2]), fh = sqrtf(f[0] * f[0] + f[2] * f[2]);
    if (vh < 12.0f || fh < 0.5f) return false;
    float cosA = (v[0] * f[0] + v[2] * f[2]) / (vh * fh);
    return cosA < 0.9659f;  // more than 15 degrees
}

// ---------------------------------------------------------------- .pkps
static const char* Attr(const char* p, const char* end, const char* name, char* out, size_t n)
{
    size_t len = strlen(name);
    for (const char* q = p; q + len + 2 < end; q++) {
        if (memcmp(q, name, len) || q[len] != '=' || q[len + 1] != '"') continue;
        const char* v = q + len + 2;
        size_t i = 0;
        while (v + i < end && v[i] != '"' && i + 1 < n) {
            out[i] = v[i];
            i++;
        }
        out[i] = 0;
        return v + i;
    }
    return NULL;
}

static bool RowOf(const char* p, const char* end, int row, float* xyz)
{
    char tag[8];
    sprintf_s(tag, "<Row%d", row);
    const char* r = strstr(p, tag);
    if (!r || r >= end) return false;
    const char* e = strchr(r, '>');
    if (!e || e > end) return false;
    static const char* const axes[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; i++) {
        char v[40];
        if (!Attr(r, e, axes[i], v, sizeof(v))) return false;
        char* stop = NULL;
        xyz[i] = (float)strtod(v, &stop);
        if (stop == v || !(xyz[i] > -20000.0f && xyz[i] < 20000.0f)) return false;
    }
    return true;
}

// the file's pickups into out (kMaxPickups); returns the count, -1 on a format error
static int ParsePkps(std::vector<unsigned char>& data, Pickup* out)
{
    data.push_back(0);
    const char* s = (const char*)data.data();
    if (!strstr(s, "<Root")) return -1;
    int n = 0;
    for (const char* p = strstr(s, "<Pickup "); p; p = strstr(p + 1, "<Pickup ")) {
        const char* end = strstr(p, "</Pickup>");
        if (!end) return -1;
        if (n >= kMaxPickups) break;
        Pickup& k = out[n];
        char colour[16];
        if (!Attr(p, end, "pickup_colour", colour, sizeof(colour))) return -1;
        k.colour = COLOURS;
        for (int c = 0; c < COLOURS; c++)
            if (Same(colour, kColourNames[c], sizeof(colour))) k.colour = (BYTE)c;
        float rows[4][3];
        for (int r = 0; r < 4; r++)
            if (!RowOf(p, end, r, rows[r])) return -1;
        if (k.colour == COLOURS) continue;  // the Xbox's "none": not loaded
        memcpy(k.ax, rows, sizeof(k.ax));
        memcpy(k.c, rows[3], sizeof(k.c));
        k.collected = 0;
        k.nearBy = 0;
        k.collectTick = 0;
        n++;
    }
    return n;
}

// ---------------------------------------------------------------- the box model (tools\dlc\mode\pickupmodel.py)
// Read once, on the game logic thread with the first track's boxes (not while drawing); the render thread makes
// its buffers and textures from it at the first draw (and again for a new device; the blob is kept for that).
static const char kModelPath[] = "glimpse/pickups/GreenPickupBox.bin";
static const DWORD kModelHeader = 0x60, kModelStride = 48, kRoles = 3;  // icon, cube map, slide show
struct ModelTex {
    DWORD kind, fourcc, w, h, levels, off, size, role;
};
struct ModelInfo {
    DWORD nv, ni, vo, io;
    ModelTex tex[kRoles];  // by role
    float frames[2], skip, speed, emissive;
};
static std::vector<unsigned char> s_modelBlob;
static ModelInfo s_model;
static volatile LONG s_modelState;  // 0 not read yet, 1 read and valid (for the render thread), 2 none

static DWORD Dxt1Size(DWORD w, DWORD h, DWORD levels)
{
    DWORD n = 0;
    for (DWORD l = 0; l < levels; l++) {
        DWORD lw = w >> l ? w >> l : 1, lh = h >> l ? h >> l : 1;
        n += ((lw + 3) / 4) * ((lh + 3) / 4) * 8;
    }
    return n;
}

// the same checks as pickupmodel.parse
static bool ParseModel(const std::vector<unsigned char>& b, ModelInfo* m)
{
    const size_t n = b.size();
    if (n < kModelHeader || memcmp(b.data(), "GPKM", 4)) return false;
    DWORD h[10];
    memcpy(h, b.data(), sizeof(h));
    const DWORD nt = h[8], to = h[9];
    m->nv = h[3];
    m->ni = h[4];
    m->vo = h[6];
    m->io = h[7];
    if (h[1] != 1 || h[2] != kModelHeader || h[5] != kModelStride || !m->nv || m->nv > 0x10000 || !m->ni ||
        m->ni % 3 || m->ni > 0x30000 || m->vo > n || (ULONGLONG)m->nv * kModelStride > n - m->vo ||
        m->io > n || 2ull * m->ni > n - m->io || nt != kRoles || to > n || 0x20ull * nt > n - to)
        return false;
    const WORD* idx = (const WORD*)(b.data() + m->io);
    for (DWORD i = 0; i < m->ni; i++)
        if (idx[i] >= m->nv) return false;
    float c[5];
    memcpy(c, b.data() + 0x28, sizeof(c));
    m->frames[0] = c[0];
    m->frames[1] = c[1];
    m->skip = c[2];
    m->speed = c[3];
    m->emissive = c[4];
    if (!(c[0] >= 1 && c[0] <= 16 && c[1] >= 1 && c[1] <= 16 && c[2] >= 0 && c[2] < c[0] * c[1] && c[3] >= 0 &&
          c[3] <= 120 && c[4] >= 0 && c[4] <= 16))
        return false;
    bool seen[kRoles] = {};
    for (DWORD i = 0; i < nt; i++) {
        ModelTex t;
        memcpy(&t, b.data() + to + 0x20 * i, sizeof(t));
        if (t.kind > 1 || t.fourcc != MAKEFOURCC('D', 'X', 'T', '1') || !t.w || t.w > 4096 || !t.h || t.h > 4096 ||
            !t.levels || t.levels > 13 || (t.kind && t.w != t.h) || t.role >= kRoles || seen[t.role] ||
            t.kind != (t.role == 1 ? 1u : 0u) || t.size != Dxt1Size(t.w, t.h, t.levels) * (t.kind ? 6 : 1) ||
            t.off > n || t.size > n - t.off)
            return false;
        seen[t.role] = true;
        m->tex[t.role] = t;
    }
    return true;
}

static void LoadModel()
{
    if (s_modelState) return;
    std::vector<unsigned char> b;
    ModelInfo m = {};
    bool have = false, ok = false;
    try {
        have = DlcOverlayRead(kModelPath, &b);
        ok = have && ParseModel(b, &m);
    } catch (...) {
        ok = false;
    }
    if (!ok) {
        Log("DLC pickups: %s %s: the boxes are drawn as cubes (rebuild the mode pack with tools\\dlc\\mode\\deadline_mode.py "
            "for the Xbox model)", kModelPath, have ? "is not a model blob as expected" : "not in the merged resident.ark");
        InterlockedExchange(&s_modelState, 2);
        return;
    }
    s_model = m;
    s_modelBlob.swap(b);
    Log("DLC pickups: box model %s: %lu vertices, %lu triangles, textures %lux%lu, %lu cube, %lux%lu; slide show %.0fx%.0f "
        "at %.0f fps", kModelPath, m.nv, m.ni / 3, m.tex[0].w, m.tex[0].h, m.tex[1].w, m.tex[2].w, m.tex[2].h, m.frames[0],
        m.frames[1], m.speed);
    InterlockedExchange(&s_modelState, 1);  // published after the blob and its description
}

// the boxes of this track (once per track and subtrack)
static bool LoadTrack(const char* track, const char* sub)
{
    LoadModel();
    char key[160];
    sprintf_s(key, "%s/%s", track, sub);
    if (s_loaded[0] && Same(key, s_loaded, sizeof(key))) return s_count > 0;
    InterlockedExchange(&s_count, 0);  // nothing is tested while the boxes change
    InterlockedExchange(&s_drawState, 0);  // nor drawn
    s_loaded[0] = 0;
    char path[320];
    sprintf_s(path, "Environments/Levels/%s/Subtracks/%s/Pickups/%s.pkps", track, sub, track);
    std::vector<unsigned char> data;
    const int next = s_pk == s_pkBuf[0] ? 1 : 0;  // the buffer the render thread is not reading
    int n;
    try {
        if (!DlcOverlayRead(path, &data)) {
            Log("DLC pickups: %s not in the merged resident.ark (rebuild the mode pack with tools\\dlc\\mode\\deadline_mode.py), "
                "no boxes on this track", path);
            n = 0;
        } else if ((n = ParsePkps(data, s_pkBuf[next])) < 0) {
            Log("DLC pickups: %s is not a pickup file as expected, no boxes", path);
            n = 0;
        }
    } catch (...) {
        n = 0;
    }
    strcpy_s(s_loaded, key);
    s_pk = s_pkBuf[next];
    int green = 0;
    for (int i = 0; i < n; i++) green += s_pk[i].colour == GREEN;
    InterlockedExchange(&s_drawState, (LONG)(next << 16 | n));  // buffer and count in one write, after the boxes
    InterlockedExchange(&s_count, n);
    Log("DLC pickups: %s: %d boxes (%d green)", path, n, green);
    return n > 0;
}

static void ArmAll(const char* why)
{
    for (int i = 0; i < s_count; i++) {
        s_pk[i].collected = 0;
        s_pk[i].nearBy = 0;
        s_pk[i].collectTick = 0;
    }
    s_frozen = 0.0f;
    s_gradeStartTick = s_gradeEndTick = 0;
    s_lapsHeld = 0;
    HudRaceStart();
    if (g_cfg.log && s_active) Log("DLC pickups: %d boxes armed, clock running (%s; %d hit, %d while drifting before)",
                                   (int)s_count, why, s_hits, s_driftHits);
    s_hits = s_driftHits = 0;
}

// Deadline on one screen, outside an online session, with boxes for this track
static void Evaluate(BYTE* res)
{
    bool active = false;
    char mode[48] = "", track[64] = "", sub[16] = "";
    int viewports = 0;
    __try {
        BYTE* setup = Ptr(res, 0x14);
        if (setup) {
            CopyName(mode, sizeof(mode), *(const char* const*)(setup + 0x988));
            CopyName(track, sizeof(track), (const char*)(setup + 0xE0));
            CopyName(sub, sizeof(sub), *(const char* const*)(setup + 0x1E0));
            viewports = *(const int*)(setup + 0x9C);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mode[0] = 0;
    }
    s_deadline = IsDeadlineName(mode);
    s_result = s_resultCode && s_deadline && viewports == 1 && !OnlineSession();
    if (s_deadline && g_cfg.dlcPickups) {
        if (viewports != 1 || OnlineSession()) {
            Log("DLC pickups: Deadline with %d screens%s, no boxes", viewports, OnlineSession() ? " online" : "");
        } else if (track[0] && sub[0]) {
            __try {
                active = LoadTrack(track, sub);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                active = false;
                InterlockedExchange(&s_count, 0);
                InterlockedExchange(&s_drawState, 0);
                s_loaded[0] = 0;
                Log("DLC pickups: reading the pickup file failed, no boxes");
            }
        }
    }
    if (active != s_active || g_cfg.log)
        Log("DLC pickups: race %s %s/%s: %s", mode[0] ? mode : "(no mode)", track, sub, active ? "boxes on" : "off");
    s_active = active;
}

// [DLC] DeadlineBonus (Pickups=0): seconds added to Deadline's start time, since without the boxes the Xbox start
// times leave hardly enough time for a lap. Added to the countdown tweakable itself: the reset (0x445400) copies
// it into time left and the start value, every update (0x4454F0) copies it into the start value again, so the
// HUD's share of time used (0x4CB7E0, (start - left) / start) and the timer (0x4C5110) stay consistent. A restart
// does not load the params again, so the value written is remembered with the track and subtrack and not added
// twice; a new race loads the track's own value and gets it again (a race on another track whose own value equals
// the one written, e.g. 60 + 5 = docks A's 65, is told apart by the track). A start time above 100 s means the pack
// was built with the old 50 extra seconds (110-120 s): nothing is added then.
static bool BonusWanted() { return !g_cfg.dlcPickups && g_cfg.deadlineBonus > 0.0f; }

static bool ModeIsDeadline(BYTE* res)
{
    __try {
        BYTE* setup = Ptr(res, 0x14);
        const char* name = setup ? *(const char* const*)(setup + 0x988) : NULL;
        return name && IsDeadlineName(name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// "<track>/<sub>" of the race setup, "" when unreadable
static void RaceName(BYTE* res, char* out, size_t n)
{
    char track[64] = "", sub[16] = "";
    __try {
        BYTE* setup = Ptr(res, 0x14);
        if (setup) {
            CopyName(track, sizeof(track), (const char*)(setup + 0xE0));
            CopyName(sub, sizeof(sub), *(const char* const*)(setup + 0x1E0));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        track[0] = 0;
    }
    sprintf_s(out, n, "%s/%s", track, sub);
}

// the start time ApplyBonus gives a Deadline race whose pack start time is base (dlcrivals.cpp measures the rivals'
// clock used against it): + DeadlineBonus in whole seconds with Pickups=0, nothing for a pack that carries it already
float DeadlineStartFor(float base)
{
    if (!BonusWanted() || !(base > 0.0f && base <= 100.0f)) return base;
    const float v = base + floorf(g_cfg.deadlineBonus + 0.5f);  // whole seconds: the time left is shown from (int)start
    return v > 3600.0f ? 3600.0f : v;
}

// out: player 1's countdown output when the reset has already copied the value (else NULL)
static void ApplyBonus(float* out)
{
    float base = *kInitialCountdown;
    char race[96];
    RaceName(Resources(), race, sizeof(race));
    if ((base == s_bonusWritten && !strcmp(race, s_bonusRace)) || !(base > 0.0f && base < 3600.0f)) return;
    if (base > 100.0f) {  // the Xbox times are 60-70 s; an old pack carries + 50 s already
        if (!s_bonusOldPackLogged)
            Log("DLC modes: Deadline start time %.1f s: the pack already carries extra seconds, DeadlineBonus not added", base);
        s_bonusOldPackLogged = true;
        return;
    }
    const float v = DeadlineStartFor(base);
    *kInitialCountdown = v;
    s_bonusWritten = v;
    strcpy_s(s_bonusRace, race);
    if (out) {
        out[3] += v - base;
        if (out[3] > 3600.0f) out[3] = 3600.0f;
    }
    Log("DLC modes: Deadline start time %.1f s + %.1f s ([DLC] DeadlineBonus, Pickups=0)", base, v - base);
}

// ---------------------------------------------------------------- per frame (game logic thread)
static float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// the car's position (the boxes fade with the distance from it), every update of an active race
static void NoteCar(BYTE* player)
{
    BYTE* body = Chassis(player);
    if (body) memcpy(s_carPos, body + 0x110, sizeof(s_carPos));
}

static void Collide(BYTE* res, BYTE* player)
{
    BYTE* body = Chassis(player);
    if (!body) return;
    const float* pos = (const float*)(body + 0x110);
    const float* c0 = (const float*)(body + 0xE0);
    const float* c1 = (const float*)(body + 0xF0);
    const float* fwd = (const float*)(body + 0x100);
    // up is the column nearer the world's up (boost.cpp learns it from the yaw; here only the box matters)
    const float* up = fabsf(c1[1]) >= fabsf(c0[1]) ? c1 : c0;
    const float* side = up == c1 ? c0 : c1;
    int n = s_count;
    bool drift = false, driftKnown = false;
    for (int i = 0; i < n; i++) {
        Pickup& k = s_pk[i];
        if (k.collected) continue;
        float d[3] = { k.c[0] - pos[0], k.c[1] - pos[1], k.c[2] - pos[2] };
        const float d2 = Dot(d, d);
        // a missed green box (TU4 0x882A8AF0): within 10 m, then 15 m away again without being hit
        if (k.colour == GREEN) {
            if (d2 < 100.0f) k.nearBy = 1;
            else if (k.nearBy && d2 > 225.0f) {
                k.nearBy = 0;
                s_frameMissed = true;
            }
        }
        if (d2 > 49.0f) continue;  // 7 m
        if (fabsf(Dot(d, side)) > kCarHalf[0] + kBoxHalf || fabsf(Dot(d, up)) > kCarHalf[1] + kBoxHalf ||
            fabsf(Dot(d, fwd)) > kCarHalf[2] + kBoxHalf)
            continue;
        k.collectTick = GetTickCount();
        k.collected = 1;
        if (k.colour != GREEN) continue;  // yellow/white/black are only hidden, red has no clock effect (TU4)
        if (!driftKnown) {
            drift = Drifting(res, body);
            driftKnown = true;
        }
        float bonus = kFreezeBonus * (drift ? kDriftMultiplier : 1.0f);
        s_frozen += bonus;
        s_hits++;
        if (drift) s_driftHits++;
        s_framePicked = true;
        if (drift) s_frameDrift = true;
        if (g_cfg.log)
            Log("DLC pickups: box %d hit%s, +%.1f s, clock stopped for %.2f s", i, drift ? " while drifting" : "", bonus,
                (float)s_frozen);
    }
}

// The race result. The PC derives a "race kind" from the mode's name (0x7D5290 into [0xD719F8]+0x194C: QuickRace /
// ChampRace 0, TimeTrial 1, Elimination 2, HelicopterSurvival 3, HelicopterRevenge 4, Nemesis 5 and 6, any other
// name 7 or 8), not from the params' Type: TimeTrialPlus gets 8 ("bonus"). At the end of the race 0x799E50 hands
// the player's time to the race end screen (UI/InGame/RaceEnd.gfx, 0x807730, the only writer of its time text
// +0x814: "%.2d:%.2d:%.2d", or "" when the player is disqualified) and to the results list only for kinds 0, 1, 3, 4
// and 5; the screen (0x808600, jump table 0x8087F8) shows the time line in quick play for every kind but 2 and the
// position for 0, 2 and the rest. So a Deadline race ended with an empty (or a previous race's) time line and a
// position (the "2." of the test). While a Deadline race is on, kind 8 is set to 1, the Time Trial's: the screen
// shows a time and no position (as the Xbox for its kind 0xB), the results list the times with the Time Trial icon
// and column; dlcrivals.cpp shows them as the time left and adds the Xbox's rivals. The kind is read only by the screens and their lists
// (0x7D5D70 / 0x7D6770 before the race; 0x799E50, 0x7D5C40 and the reads at 0x80054B, 0x8015B5, 0x8031F8, 0x8059BB,
// 0x806246 after it); the best time lookup (0x795E10, key "<track>_<sub>_TimeTrialPlus") finds nothing and nothing
// is saved by it. The event objectives screen sets kind 8 again (see Hook_RaceEndEnter): both screens keep it at 1.
// When the clock runs out (the race is then ended by cEndGame, 0x442780) the player is marked as not finished, with
// the game's own two flags:
// - the Disqualified record (record 12 of the record collection, logic id [0xD03C78] = 0, output +0x34 -> the
//   cRecordInt at cPlayerStateOutput +0xC, value +0xC). cPlayerState writes it only to 1 (online rules, 0x43E9ED /
//   0x43EA67, and 0x43E7B0) and its reset (0x43E770, slot 2) puts it back to 0 on a restart. 0x799E50 then calls
//   0x807730 with "disqualified": the end screen's time text is emptied (no stale time from a run before), no best
//   time check, and 0x7D5C40 leaves the player's row alone;
// - cEndGame's output +0xA (set by the game itself for the online modes, cleared by its reset 0x442520), which
//   0x79A210 passes on to the end screen (+0x89C: ShowDNF instead of the position).
// The results table still lists the run's time: its DNF text (0x802420) also needs the row's online-only flag +0x323.
static bool MarkDisqualified(BYTE* player)
{
    DWORD id = *kRecordCollectionId;
    BYTE* rc = id < 64 ? Ptr(player, 0xA4 + id * 4) : NULL;
    BYTE* o = Is(rc, kRecordCollectionVt) ? Ptr(rc, 0x2C) : NULL;
    BYTE* rec = Is(o, kRecordCollectionOutVt) ? Ptr(o, 0x34) : NULL;
    if (!Is(rec, kRecordIntVt) || *(const DWORD*)(rec + 4) != 12) return false;
    *(int*)(rec + 0xC) = 1;  // as the record's setter 0x429790: value, valid, changed
    rec[8] = 1;
    rec[9] = 1;
    return true;
}

static void DeadlineResult(BYTE* player, float before, const float* out)
{
    BYTE* info = *kRaceInfo;
    if (!info || s_timedOut) return;
    int* kind = (int*)(info + kRaceKindOff);
    if (*kind == kKindBonus) {
        *kind = kKindTimeTrial;
        if (!s_kindLogged && g_cfg.log) Log("DLC modes: Deadline: race kind 8 -> 1 (Time Trial), for the finish time on the end screen");
        s_kindLogged = true;
    }
    if (((const BYTE*)out)[4] && out[3] <= 0.0f && before > 0.0f) {
        s_timedOut = true;
        DWORD id = *kEndGameId;
        BYTE* eg = id < 64 ? Ptr(player, 0xA4 + id * 4) : NULL;
        BYTE* o = Is(eg, kEndGameVt) ? Ptr(eg, 0x2C) : NULL;
        if (Is(o, kEndGameOutVt)) o[0xA] = 1;
        bool dq = MarkDisqualified(player);
        Log("DLC modes: Deadline: the clock ran out, the player did not finish (end game flag %s, disqualified record %s)",
            Is(o, kEndGameOutVt) ? "set" : "not found", dq ? "set" : "not found");
    }
}

// The kind set while racing did not reach the end screen of a finished race (the test of 2026-11-21: "1." and no
// time): only 0x7D5290 writes kind 8, called by 0x7D6FF0, which first clears the race info (0x7D5570: kind -1) and
// then derives the kind from the setup's mode name again. 0x7D6FF0 runs when the event objectives screen comes up
// (cEventObjectives vtable 0xC201E4 slot 2, 0x806820: the pre-race screen and the pause menu's event page) and for
// online races (0x4CB3F0); so after any of these the kind is 8 again until the next racing frame of the countdown
// hook, and a screen entered meanwhile (or a race ended before another racing frame) sees 8: the race end screen
// (cRaceEnd vtable 0xC208DC slot 2, 0x808600: position for 8, no time) and 0x799E50 (no time text for 8). Both
// screens' slot 2 (enter, thiscall, no arguments) are swapped: after the event objectives screen has set its kind,
// and before the race end screen reads it, kind 8 becomes 1 again when the info's setup (+0x1CF8, set by 0x7D6FF0)
// is Deadline on one screen, offline. The event objectives screen itself keeps the kind it was entered with.
typedef void(__thiscall* ScreenEnter_t)(void* self);
static ScreenEnter_t s_origObjectivesEnter, s_origRaceEndEnter;
static void** const kObjectivesVt = (void**)0x00C201E4;  // cEventObjectives (COL 0xC979C8), slot 2 = 0x806820
static void** const kRaceEndVt = (void**)0x00C208DC;     // cRaceEnd (COL 0xC98258), slot 2 = 0x808600

static bool InfoIsDeadline(BYTE* info)
{
    __try {
        BYTE* setup = Ptr(info, 0x1CF8);
        if (!setup) return false;
        const int type = *(const int*)setup;  // 6 and 7: online (0x7D5880)
        const char* name = *(const char* const*)(setup + 0x988);
        return type != 6 && type != 7 && *(const int*)(setup + 0x9C) == 1 && name && IsDeadlineName(name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void KindForDeadline(const char* where)
{
    BYTE* info = *kRaceInfo;
    if (!info || OnlineSession() || !InfoIsDeadline(info)) return;
    int was;
    __try {  // the race info as InfoIsDeadline reads it: guarded the same way
        int* kind = (int*)(info + kRaceKindOff);
        was = *kind;
        if (was == kKindBonus) *kind = kKindTimeTrial;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (g_cfg.log || was == kKindBonus)
        Log("DLC modes: Deadline: %s: race kind %d%s", where, was, was == kKindBonus ? " -> 1 (Time Trial)" : "");
}

static void __fastcall Hook_ObjectivesEnter(void* self, void*)
{
    s_origObjectivesEnter(self);
    KindForDeadline("event objectives screen");
    BYTE* info = *kRaceInfo;
    if (info && !OnlineSession() && InfoIsDeadline(info)) RivalsObjectives(info);  // the race info is built now
}

static void __fastcall Hook_RaceEndEnter(void* self, void*)
{
    if (s_resultCode) KindForDeadline("race end screen");
    // the time text as the time left. self is cRaceEnd + 4 (this vtable's COL offset): the game's enter goes back to
    // the object with lea edi, [esi-4] (0x808622), and 0x807730 / 0x8082D0 use +0x808 and +0x814 from there
    DlcRecordsRaceEnd((BYTE*)self - 4);
    RivalsRaceEnd((BYTE*)self - 4);
    s_origRaceEndEnter(self);
}

// The record uses the active race, with the original countdown and both timing hooks.
bool DeadlineRecordRules(const BYTE* setup, int countdown)
{
    float start = 0.0f;
    return s_active && s_lapsHooked && !s_timedOut && Ptr(Resources(), 0x14) == setup &&
           DeadlineResultsNow(&start) && start == (float)countdown;
}

// a finished Deadline race on one screen, offline, with the Time Trial kind: the results show the time left of
// *start (dlcrivals.cpp, screen enters). Finished: player 1's cEndGame output +8, set by 0x442590 when the race ends
// (finish or the clock run out) and cleared by the reset 0x442520 (+9 is set for one update only: 0x442DB8 clears it)
bool DeadlineResultsNow(float* start)
{
    __try {
        BYTE* info = *kRaceInfo;
        if (!s_resultCode || !info || OnlineSession() || !InfoIsDeadline(info) ||
            *(const int*)(info + kRaceKindOff) != kKindTimeTrial || !(s_startValue > 0.0f))
            return false;
        BYTE* player = Player1(Resources());
        DWORD id = *kEndGameId;
        BYTE* eg = player && id < 64 ? Ptr(player, 0xA4 + id * 4) : NULL;
        BYTE* o = Is(eg, kEndGameVt) ? Ptr(eg, 0x2C) : NULL;
        if (!Is(o, kEndGameOutVt) || !o[8]) return false;
        *start = s_startValue;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// slot 2 of a screen's vtable -> hook (data in .rdata); orig gets the game's function first
static bool SwapEnter(void** vt, DWORD col, const char* cls, DWORD fn, const char* code, size_t n, void* hook,
                      ScreenEnter_t* orig)
{
    __try {
        if (!ClassIs(vt, col, cls) || (DWORD)vt[2] != fn || memcmp((const void*)fn, code, n)) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    DWORD old;
    if (!VirtualProtect(&vt[2], 4, PAGE_READWRITE, &old)) return false;
    *orig = (ScreenEnter_t)vt[2];
    InterlockedExchangePointer(&vt[2], hook);
    VirtualProtect(&vt[2], 4, old, &old);
    return true;
}

bool InstallDlcRaceEnd()
{
    if (s_origRaceEndEnter) return true;
    return SwapEnter(kRaceEndVt, 0x00C98258, ".?AVcRaceEnd@InGameUI@@", 0x00808600,
                     "\x83\xEC\x24\x53\x55\x56\x57\x68", 8,
                     (void*)Hook_RaceEndEnter, &s_origRaceEndEnter);
}


// ---------------------------------------------------------------- Deadline's HUD and sounds, as on the Xbox
// TU4 (TU4_RULES.md section 3) drives its HUD from the frozen time: 0x882609A8 turns the pickup state into events.
// The timer under the car (DialsInfo.gfx's large timer, the element at cHud+0x124 -> +0x104 on the Xbox) gets
// SetTimerToStoppedColor / SetTimerToStoppedColorLong / PulseFrozenTimer / SetTimerToRunningColor (0x8834C4C0, no
// arguments); the sound handler 0x8833E098 plays DLCMenu/TrialPlus/<event> through the 2D sound call the PC's HUD
// uses too (PC 0x7845E0 + 0x790B90, a message to the audio thread); the text handler 0x8834D428 puts
// TEXT_TIMETRIALPLUS_PICKUP / _DRIFTPICKUP into the HUD's message queue (0x88342888 -> 0x8833E7B8: 0.5 s, type 1,
// style 0, the positive message between the cyan bars at the top) and TEXT_TIMETRIALPLUS_OBJECTIVE2 as a teaching
// message (0x883427E0: 4 s, 1, 1, the neutral one); OBJECTIVE1's event has no text case. The countdown's last five
// whole seconds play DLCMenu/TrialPlus/5..1 (0x88239530). The PC has the same message code (0x561E10 = 0x88342888,
// 0x561DB0 = 0x883427E0, 0x5619E0 = 0x8833E7B8; the queue at cHud+0x28C, updated by 0x564230) and the same sound
// call; the DLC's DialsInfo.gfx (put into the mode pack in place of the PC's, which is byte-identical to the Xbox
// base game's) brings the four timer functions, and the DLC's DLCMenu bank (converted, Audio/Release/Menu) the
// sounds, which dlcmusic.cpp loads on the audio thread as TU4's level audio load does for TimeTrialPlus.
// - cHud: [0xD66B24] is cInGame (InGameLoadState 0x413E10 sets it with [0xD66B28]); its HUD manager is at +0x20
//   (0x4B4D88), each viewport's cHud at manager +0xC + i*4 (0x4C8573); cHud +0x28C the message queue (whose +8 is
//   the cHud again, 0x564234), +0x2B4 the DialsInfo layer, +0x2BC the TimerOverlay layer, +0x2C0 the viewport
//   (0x55A5A0). A layer's +0x68 is its movie holder (holder +4 the GFx movie, vtable 0xC41CF4), on which 0x536AC0
//   invokes a movie function, as the HUD's own calls do.
// - Everything runs on the game logic thread (the countdown update), which also runs the HUD (0x4B2670 calls the
//   logic and the HUD manager 0x4C83E0).
// - The top centre countdown (UI/HUD/TimerOverlay.gfx: the whole seconds left, because Countdown is enabled; TU4
//   creates it too, but the Xbox's Deadline shows no such element) is hidden in a Deadline race through its movie's
//   SetVisible (GFxMovieRoot +0x20, +0x24 GetVisible), as photo mode hides the overlays (camera.cpp), and shown
//   again when a race of another mode starts.
static BYTE* const* const kInGame = (BYTE* const*)0x00D66B24;
static BYTE* const* const kUi = (BYTE* const*)0x00D64E0C;  // +8: the UI layers [id]
static void* const* const kDictionary = (void* const*)0x00D64E28;
static const DWORD kMovieRootVt = 0x00C41CF4;
static const int kLayerDialsInfo = 0x40, kLayerTimerOverlay = 100;  // one screen: viewport 0 (0x55A5A0)
typedef const char*(__thiscall* DictLookup_t)(void* dict, const char* key);                           // 0xA15BF0
typedef void(__thiscall* HudMessage_t)(void* queue, const char* text, int id, int teaching, int type, int style,
                                       float seconds);                                                // 0x561E10, ret 0x18
typedef void(__thiscall* HudTeaching_t)(void* queue, const char* text, int id, int type, int style, float seconds);  // 0x561DB0
typedef void*(__cdecl* Sound2D_t)(void* handle, const char* name, const void* pos, int play, int owner);  // 0x7845E0
typedef void(__fastcall* SoundRelease_t)(void* handle);                                               // 0x790B90
typedef int(__thiscall* MovieInvoke_t)(void* holder, const char* name, void* args, int n);            // 0x536AC0, ret 0xC
typedef void(__thiscall* GFxSetVisible_t)(void* movie, bool visible);
typedef bool(__thiscall* GFxGetVisible_t)(void* movie);
static const DictLookup_t kDictLookup = (DictLookup_t)0x00A15BF0;
static const HudMessage_t kHudMessage = (HudMessage_t)0x00561E10;
static const HudTeaching_t kHudTeaching = (HudTeaching_t)0x00561DB0;
static const Sound2D_t kSound2D = (Sound2D_t)0x007845E0;
static const SoundRelease_t kSoundRelease = (SoundRelease_t)0x00790B90;
static const MovieInvoke_t kMovieInvoke = (MovieInvoke_t)0x00536AC0;
// the message ids TU4 gives them (its HUD event numbers; the PC's own events end at 0x98)
static const int kMsgObjective2 = 0x9D, kMsgPickup = 0x9E, kMsgDriftPickup = 0x9F;

struct CodeBytes {
    DWORD addr;
    const char* bytes;
    size_t n;
};
static const CodeBytes kHudCode[] = {
    { 0x00413F15, "\x8B\x4E\x18\xA3\x28\x6B\xD6\x00\xB8\x14\x6B\xD6\x00\x89\x0D\x24\x6B\xD6\x00", 19 },  // [0xD66B24]
    { 0x004B4D88, "\x8D\x4B\x20", 3 },                // the HUD manager at cInGame + 0x20
    { 0x004C8573, "\x8B\x74\xAF\x0C", 4 },            // its cHud per viewport at +0xC
    { 0x00564234, "\x8B\x46\x08", 3 },                // the message queue's +8 (its cHud)
    { 0x0055A8F8, "\x8B\x8E\x8C\x02\x00\x00", 6 },    // cHud + 0x28C the message queue
    { 0x0055A6E3, "\x89\x86\xB4\x02\x00\x00", 6 },    // + 0x2B4 the DialsInfo layer
    { 0x0055A847, "\x89\x86\xBC\x02\x00\x00", 6 },    // + 0x2BC the TimerOverlay layer
    { 0x00A15BF0, "\x56\x8B\xF1\x83\x3E\x00\x75\x2E", 8 },
    { 0x00561E10, "\x53\x8B\x5C\x24\x0C\x55\x56\x57\x8B\xF1", 10 },
    { 0x00561DB0, "\x56\x57\x8B\xF1\x33\xFF\x39\x7E\x10", 9 },
    { 0x005619E0, "\x8B\x44\x24\x04\x56\x50\x68\xA8", 8 },
    { 0x007845E0, "\x51\x80\x3D\xD4\xD9\xD7\x00\x00", 8 },
    { 0x00790B90, "\x56\x8B\xF1\x8B\x0E\x85\xC9", 7 },
    { 0x00536AC0, "\x83\xEC\x10\x8B\x54\x24\x1C\x8B\x49\x04", 10 },
};
static bool s_hudCode;            // the code above is as expected (checked once at install)
static bool s_timerStopped;       // our timer colour state (TU4 keeps it in the element, +0x240)
static float s_raceTime;          // racing seconds of this run (the hints' "after 3 s")
static float s_hintCooldown;      // TU4 HUD +0x344
static int s_missStreak, s_plainStreak;   // +0x36C misses in a row, +0x370 pickups without a drift in a row
static bool s_hint1Shown, s_hint2Shown;   // +0x374, +0x375
static int s_pickups, s_driftPickups, s_misses;  // +0x360, +0x364, +0x368 (logged)
static void* s_hiddenTimer;       // the TimerOverlay movie we hid
static volatile DWORD s_msgTick;  // GetTickCount of the last pickup line we draw ourselves (0 = none)
static char s_fallbackText[48];   // and its text (written and read under s_fallbackLock: game and render thread)
static SRWLOCK s_fallbackLock = SRWLOCK_INIT;
static bool s_hudLogged[4];

static bool CodeAsExpected(const CodeBytes* c, size_t n)
{
    __try {
        for (size_t i = 0; i < n; i++)
            if (memcmp((const void*)c[i].addr, c[i].bytes, c[i].n)) {
                Log("DLC modes: Deadline HUD: code at 0x%08lX not as expected", c[i].addr);
                return false;
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// player 1's cHud (one screen), checked against the layers it owns; NULL when there is none
static BYTE* Hud()
{
    __try {
        BYTE* game = *kInGame;
        BYTE* hud = game ? Ptr(game, 0x20 + 0xC) : NULL;
        BYTE* ui = *kUi;
        BYTE** layers = ui ? *(BYTE***)(ui + 8) : NULL;
        if (!hud || !layers || *(const int*)(hud + 0x2C0) != 0) return NULL;
        BYTE* queue = Ptr(hud, 0x28C);
        if (!queue || Ptr(queue, 8) != hud || Ptr(hud, 0x2B4) != layers[kLayerDialsInfo]) return NULL;
        return hud;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

// a layer's movie holder (for 0x536AC0) and movie (GFxMovieRoot), or NULL
static BYTE* LayerHolder(BYTE* layer, void** movie)
{
    BYTE* holder = layer ? Ptr(layer, 0x68) : NULL;
    void* m = holder ? *(void**)(holder + 4) : NULL;
    if (!m || *(DWORD*)m != kMovieRootVt) return NULL;
    if (movie) *movie = m;
    return holder;
}

static void LogOnce(int i, const char* what, const char* detail)
{
    if (s_hudLogged[i]) return;
    s_hudLogged[i] = true;
    Log("DLC modes: Deadline HUD: %s%s", what, detail ? detail : "");
}

// the top centre countdown: hidden in a Deadline race, shown again otherwise (only the movie we hid)
static void TimerOverlayVisible(bool deadline)
{
    if (!deadline && !s_hiddenTimer) return;
    __try {
        BYTE* ui = *kUi;
        BYTE** layers = ui ? *(BYTE***)(ui + 8) : NULL;
        BYTE* layer = layers ? layers[kLayerTimerOverlay] : NULL;
        void* movie = NULL;
        if (!layer || *(const int*)(layer + 4) != kLayerTimerOverlay || !LayerHolder(layer, &movie)) movie = NULL;
        void** vt = movie ? *(void***)movie : NULL;
        if (deadline) {
            if (movie && ((GFxGetVisible_t)vt[0x24 / 4])(movie)) {
                ((GFxSetVisible_t)vt[0x20 / 4])(movie, false);
                if (s_hiddenTimer != movie) Log("DLC modes: Deadline: the top centre countdown (TimerOverlay.gfx) hidden");
                s_hiddenTimer = movie;
            }
        } else if (s_hiddenTimer) {
            if (movie == s_hiddenTimer) ((GFxSetVisible_t)vt[0x20 / 4])(movie, true);
            s_hiddenTimer = NULL;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_hiddenTimer = NULL;
    }
}

// a DialsInfo movie function without arguments (the DLC movie's timer colours)
static void TimerCall(const char* fn)
{
    BYTE* hud = Hud();
    BYTE* holder = NULL;
    __try {
        holder = hud ? LayerHolder(Ptr(hud, 0x2B4), NULL) : NULL;
        if (holder) kMovieInvoke(holder, fn, NULL, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        holder = NULL;
    }
    if (!holder) LogOnce(0, "no DialsInfo movie for the timer colour, the timer keeps its colour", NULL);
    else if (g_cfg.log) Log("DLC modes: Deadline HUD: DialsInfo %s()", fn);
}

// (an event the audio thread does not find is dropped there: 0x8A4B80 releases the sound and returns 0)
static void PlaySound2D(const char* name)
{
    if (!s_hudCode || !DeadlineMenuWanted()) return;
    __try {
        alignas(8) BYTE handle[16] = {};
        kSound2D(handle, name, NULL, 1, 0);
        kSoundRelease(handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogOnce(1, "the 2D sound call failed", NULL);
    }
}

// a dictionary text (UTF-8) into out; false when the key is not there (0xA15BF0 then gives the key back)
static bool Text(const char* key, char* out, size_t n)
{
    const char* t = NULL;
    __try {
        void* dict = *kDictionary;
        t = dict ? kDictLookup(dict, key) : NULL;
        if (t && *t && strcmp(t, key)) {
            strncpy_s(out, n, t, _TRUNCATE);  // a longer dictionary text is cut, never the end of the process
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

// "+0.5s" / "+2.0s" in the game's language (TEXT_TIMETRIALPLUS_PICKUP / _DRIFTPICKUP), else our own
static void PickupText(bool drift, char* out, size_t n)
{
    if (!Text(drift ? "TEXT_TIMETRIALPLUS_DRIFTPICKUP" : "TEXT_TIMETRIALPLUS_PICKUP", out, n))
        sprintf_s(out, n, "+%.1fs", drift ? kFreezeBonus * kDriftMultiplier : kFreezeBonus);
}

static bool HudText(int id, bool teaching, float seconds, int style)
{
    BYTE* hud = s_hudCode ? Hud() : NULL;
    if (!hud) {
        LogOnce(2, "no HUD message queue, ", "the DLL draws the pickup line itself");
        return false;
    }
    char text[256];
    if (id == kMsgObjective2) {
        // a pack without the TU4 texts (every build from a dump with the released tools): the fix's own wording,
        // not the game's text, so the hint still comes up
        if (!Text("TEXT_TIMETRIALPLUS_OBJECTIVE2", text, sizeof(text)))
            strcpy_s(text, "DRIFT THROUGH A PICKUP: THE CLOCK STOPS FOUR TIMES AS LONG");
    } else {
        PickupText(id == kMsgDriftPickup, text, sizeof(text));
    }
    __try {
        BYTE* queue = Ptr(hud, 0x28C);
        if (teaching) kHudTeaching(queue, text, id, 1, style, seconds);
        else kHudMessage(queue, text, id, 0, 1, style, seconds);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogOnce(3, "the HUD message call failed", NULL);
        return false;
    }
    return true;
}

static void HudRaceStart()
{
    s_framePicked = s_frameDrift = s_frameMissed = false;
    if (s_timerStopped) TimerCall("SetTimerToRunningColor");
    s_timerStopped = false;
    s_raceTime = s_hintCooldown = 0.0f;
    s_missStreak = s_plainStreak = 0;
    s_hint1Shown = s_hint2Shown = false;
    s_pickups = s_driftPickups = s_misses = 0;
    s_msgTick = 0;
}

// once per racing frame of a Deadline race with boxes, after the clock: prev / now = the frozen time before this
// frame's hits and after this frame's run-down (TU4 state +0x10 / +0x0C)
static void HudFrame(float prev, float now, float dt)
{
    const bool picked = s_framePicked, drift = s_frameDrift, missed = s_frameMissed;
    s_framePicked = s_frameDrift = s_frameMissed = false;
    s_raceTime += dt;
    if (s_hintCooldown > 0.0f) s_hintCooldown -= dt;
    // the hints (0x882609A8)
    if (picked) s_pickups++;
    if (missed) s_misses++;
    if (drift) s_driftPickups++;
    if (picked) {
        s_missStreak = 0;
        s_plainStreak = drift ? 0 : s_plainStreak + 1;
    }
    if (missed) s_missStreak++;
    const bool after3 = s_raceTime > 3.0f;
    const bool first = !s_hint1Shown && after3, second = s_hint1Shown && !s_hint2Shown && after3;
    const bool misses = s_missStreak >= 3 && missed, plain = s_plainStreak >= 3 && picked;
    if ((first || misses) && s_hintCooldown <= 0.0f) {  // OBJECTIVE1: TU4 shows no text for it
        s_hintCooldown = 3.0f;
        s_missStreak = 0;
        s_hint1Shown = true;
        if (g_cfg.log) Log("DLC modes: Deadline hint 1 (%s, no text on the Xbox)", misses ? "3 boxes missed" : "race start");
    }
    if ((second || plain) && s_hintCooldown <= 0.0f) {
        s_hintCooldown = 3.0f;
        s_plainStreak = 0;
        s_hint2Shown = true;
        bool shown = HudText(kMsgObjective2, true, 4.0f, 1);
        if (g_cfg.log) Log("DLC modes: Deadline hint 2 (%s)%s", plain ? "3 boxes without a drift" : "after hint 1",
                           shown ? "" : ": not shown (no HUD message queue)");
    }
    // the timer, the sound and the "+x,xs" message
    int msg = 0;
    if (prev <= 0.0f && now > 0.0f) {
        const bool lng = now > 1.0f;
        TimerCall(lng ? "SetTimerToStoppedColorLong" : "SetTimerToStoppedColor");
        s_timerStopped = true;
        PlaySound2D(lng ? "DLCMenu/TrialPlus/PickupWithDrift" : "DLCMenu/TrialPlus/Pickup");
        msg = lng ? kMsgDriftPickup : kMsgPickup;
    } else if (prev > 0.0f && now > prev) {
        const bool lng = now - 1.0f > prev;
        TimerCall("PulseFrozenTimer");
        PlaySound2D(lng ? "DLCMenu/TrialPlus/PulseWithDrift" : "DLCMenu/TrialPlus/Pulse");
        msg = lng ? kMsgDriftPickup : kMsgPickup;
    } else if (prev > 0.0f && now <= 0.0f) {
        if (s_timerStopped) TimerCall("SetTimerToRunningColor");
        s_timerStopped = false;
        PlaySound2D("DLCMenu/TrialPlus/FreezeExpired");
    }
    if (msg) {
        if (HudText(msg, false, 0.5f, 0)) {
            s_msgTick = 0;
        } else {  // our own line at Present (PickupsPresent)
            char text[sizeof(s_fallbackText)];
            PickupText(msg == kMsgDriftPickup, text, sizeof(text));
            AcquireSRWLockExclusive(&s_fallbackLock);  // PickupsPresent copies it on the render thread
            memcpy(s_fallbackText, text, sizeof(text));
            ReleaseSRWLockExclusive(&s_fallbackLock);
            s_msgTick = GetTickCount();
        }
    }
}

// the countdown's last five whole seconds (0x88239530): a voice each time the clock passes 5, 4, 3, 2, 1
static void CountdownVoice(float before, float now)
{
    const int s = (int)before;
    if (s >= 1 && s <= 5 && now <= (float)s && (float)s < before) {
        char name[32];
        sprintf_s(name, "DLCMenu/TrialPlus/%d", s);
        PlaySound2D(name);
    }
}

static void __fastcall Hook_CountdownUpdate(void* self, void*, float dt)
{
    BYTE* player = NULL;
    if (!IsPlayer1Countdown(self, &player)) {
        s_origUpdate(self, dt);
        return;
    }
    float* out = NULL;
    float before = 0.0f, frozenPrev = 0.0f;
    int state = -1;
    BYTE* res = NULL;
    __try {
        res = Resources();
        state = RaceState(player);
        out = (float*)Ptr((BYTE*)self, 0x2C);
        if (out) before = out[3];
        if (self != s_cd) {
            s_cd = self;
            s_needEval = true;
        }
        // into the racing state with a full clock: a start or a restart (whether or not the reset came first);
        // without the reset only while nothing was hit yet (a box hit in the first racing frame holds the clock full)
        bool start = state == 6 && s_lastState != 6 && out && fabsf(out[3] - out[2]) < 0.01f &&
                     (s_resetPending || (s_frozen <= 0.0f && s_hits == 0));
        if (s_needEval || start) {
            s_needEval = false;
            if (start) s_resetPending = false;
            s_timedOut = s_kindLogged = s_endLogged = false;
            RivalsRaceStart();
            Evaluate(res);
            ArmAll(start ? "race start" : "new race");
            if (start && s_deadline && BonusWanted()) ApplyBonus(out);  // in case no reset brought it
        }
        frozenPrev = s_frozen;
        if (s_active) {  // drawn from the intro on; tested only while racing
            NoteCar(player);
            if (state == 6) Collide(res, player);
            s_lastUpdate = GetTickCount();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = NULL;
    }
    if (s_hudCode) TimerOverlayVisible(s_result);  // Deadline has no top centre countdown
    const int prev = s_lastState;
    s_lastState = state;
    s_origUpdate(self, dt);
    if (!out) return;
    __try {
        if (state == 6) {
            s_startValue = out[2];
            if (prev != 6 && s_active && s_lapsHooked) DlcRecordsDeadlineHud(res, s_startValue);
            if (s_active) InterlockedIncrement(&s_cdSeq);
            if (s_active && s_frozen > 0.0f && ((const BYTE*)out)[4]) {
                if (out[3] < before) out[3] = before;  // the clock does not run this frame (Xbox: dt = 0)
                float f = s_frozen - dt;
                s_frozen = f > 0.0f ? f : 0.0f;
                InterlockedExchange(&s_heldSeq, s_cdSeq);  // cLaps holds the race time for this frame too
            }
            if (s_active) GradeFrame(frozenPrev, s_frozen);
            if (s_active && s_hudCode) HudFrame(frozenPrev, s_frozen, dt);
            if (s_result && s_hudCode && ((const BYTE*)out)[4]) CountdownVoice(before, out[3]);
            if (s_result) DeadlineResult(player, before, out);  // after the stopped clock is put back
        } else if (prev == 6 && s_result && !s_timedOut && !s_endLogged) {
            s_endLogged = true;
            Log("DLC modes: Deadline: out of the racing state (state %d) with %.2f s left of %.2f s, race time held "
                "for %lu frames", state, out[3], out[2], s_lapsHeld);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void __fastcall Hook_CountdownReset(void* self, void*, DWORD arg)
{
    if (BonusWanted() && ModeIsDeadline(Resources())) ApplyBonus(NULL);  // before the reset copies the value
    s_origReset(self, arg);
    BYTE* player = NULL;
    if (IsPlayer1Countdown(self, &player)) {
        s_needEval = true;  // the next update decides about the race and arms the boxes
        s_resetPending = true;
        s_startValue = 0.0f;
        s_frozen = 0.0f;
        s_timedOut = s_kindLogged = s_endLogged = false;
        for (int i = 0; i < s_count; i++) s_pk[i].collected = 0;
    }
}

// ---------------------------------------------------------------- the race time while the clock is frozen
// TU4's lap timer (0x88380670) adds dt = 0 to the race time while the pickup logic holds the clock (pickup delta <
// 0), so a Deadline time is the clock used, start - time left, which the results rank against the rivals'
// (dlcrivals.cpp). The PC's cLaps (GameLogicNew, vtable 0xBA8DE8 COL 0xC77258, update slot 4 = 0x440630,
// thiscall(dt), ret 4) uses dt only there (the race time record at output +0x30 = its value +0x3C + dt, and the
// wrecked-time sum +0x40); the lap times come from the race's own clock. The wrapper passes dt = 0 for player 1's
// cLaps in a racing frame in which the countdown hook held the clock (its sequence number; whichever of the two
// updates comes first in a frame, the held frames match up to the frame of a hit and of the end of a freeze, which
// cancel out). Every other cLaps, mode and frame goes to the game's update as it is.
typedef void(__thiscall* LapsUpdate_t)(void* self, float dt);
static LapsUpdate_t s_origLaps;
static void** const kLapsVtable = (void**)0x00BA8DE8;
static LONG s_lapsSeq;      // the countdown sequence number the laps update saw last

static void __fastcall Hook_LapsUpdate(void* self, void*, float dt)
{
    if (s_active && s_lastState == 6 && dt > 0.0f) {
        const LONG held = s_heldSeq, seq = s_cdSeq;
        bool hold = false;
        if (held && held == seq && GetTickCount() - s_lastUpdate < 500) {
            __try {
                hold = Ptr((BYTE*)self, 0x30) == Player1(Resources());
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                hold = false;
            }
        }
        if (hold && s_lapsSeq != seq) {  // once per countdown frame
            s_lapsSeq = seq;
            s_lapsHeld++;
            s_origLaps(self, 0.0f);
            return;
        }
    }
    s_origLaps(self, dt);
}

static void LapsInstall()
{
    __try {
        const DWORD* col = (const DWORD*)kLapsVtable[-1];
        const char* name = (const char*)(col[3] + 8);
        if ((DWORD)col != 0x00C77258 || col[0] != 0 || IsBadStringPtrA(name, 32) ||
            strcmp(name, ".?AVcLaps@GameLogicNew@@") || (DWORD)kLapsVtable[4] != 0x00440630 ||
            memcmp((const void*)0x00440630, "\x51\xA0\xB4\xB6\xD6\x00\x56\x8B\xF1\x57", 10) ||
            memcmp((const void*)0x004407A2, "\xF3\x0F\x10\x47\x3C\xF3\x0F\x58\x44\x24\x18", 11)) {  // race time = +0x3C + dt
            Log("DLC pickups: the lap timer is not as expected, the race time runs on while the clock is frozen");
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    DWORD old;
    if (!VirtualProtect(&kLapsVtable[4], 4, PAGE_READWRITE, &old)) return;
    s_origLaps = (LapsUpdate_t)kLapsVtable[4];
    InterlockedExchangePointer(&kLapsVtable[4], (void*)Hook_LapsUpdate);
    VirtualProtect(&kLapsVtable[4], 4, old, &old);
    s_lapsHooked = true;
    Log("DLC pickups: lap timer hooked (vtable 0xBA8DE8 slot 4): the race time stands while the clock is frozen (TU4)");
}

// ---------------------------------------------------------------- the "Frozen Time" post presets
// TU4 adds two states to the post process database (0x88E78A58: "Frozen Time Heavy", "Frozen Time Subtle"; the PC's
// table 0xD6FCB8 has 36 fixed states and no free one) whose parameters the Deadline package brings as
// PostProcessDatabase/Frozen_Time_Heavy.params / _Subtle.params (TU4's loader 0x884D3308 reads
// Resident_DLC_Redirect|PostProcessDatabase/<name with _>.params). Both overwrite the colour cube configuration
// (cube off: Enabled false, Blending 0, Input Blending 1; here input blending 0) and the vignette (cyan, 0 0.78 1; Heavy inner 2.03 outer
// 4.97 scale 6.26 x 6.07, Subtle 1.82 / 6.37 / 6.49 x 6.22), fade in 0 s, out about 1 s; nothing else. These are
// the parameters of the PC's colour cube pass (cSSColourCube, vtable 0xBB4040, render slot 2 = 0x49A7A0,
// postfx.cpp's Hook_GradeRender; its block at this+0x14: +0x14 cube blending, +0x18 input blending, +0x1C..+0x34
// gradient, +0x38 vignette colour, +0x48 U scale, +0x4C V scale, +0x50 inner, +0x54 outer, as the camera block at
// +0xA4.. 0x550FE0 registers). So while the clock stands the pass gets the preset's values for its call (blended
// with the camera's by the fade weight) and its own values back after it: the game's own vignette and grading do
// the look, before the HUD and the HDR gain, like on the Xbox. deadline_mode.py puts the two files into the mode
// pack as they are (new keys, the PC never asks for them). Which preset when is not traced in TU4 (inferred): Heavy
// for a freeze started or extended by a drift hit (the HUD's "Long" stopped colour), Subtle for a plain one. With
// the presets the edge glow is not drawn; [DLC] PickupVignette=0 turns both off. Without the files: the edge glow.
struct GradePreset {
    bool ok;
    float fadeIn, fadeOut;
    bool cube, vignette, gradient;            // 'Overwrite ... Configuration'
    bool cubeEnabled;
    float cubeBlend, cubeInput;               // +0x14, +0x18
    float grad[7];                            // r g b (+0x1C..), alpha +0x2C, height +0x30, power +0x34
    float vig[7];                             // r g b (+0x38..), U +0x48, V +0x4C, inner +0x50, outer +0x54
};
static GradePreset s_preset[2];               // 0 Subtle, 1 Heavy
static volatile LONG s_gradeHeavy;
static const char* const kPresetPath[2] = { "PostProcessDatabase/Frozen_Time_Subtle.params",
                                            "PostProcessDatabase/Frozen_Time_Heavy.params" };

static bool ParsePreset(std::vector<unsigned char>& d, GradePreset* p)
{
    d.push_back(0);
    memset(p, 0, sizeof(*p));
    int got = 0;  // bit per value found
    enum { ROOT, CUBE, GRAD, VIG, OTHER } sec = OTHER;
    char* ctx = NULL;
    for (char* ln = strtok_s((char*)&d[0], "\r\n", &ctx); ln; ln = strtok_s(NULL, "\r\n", &ctx)) {
        if (ln[0] == '/') {
            const char* s = strstr(ln, "/PostProcessDatabase/Frozen Time ");
            const char* rest = s ? strchr(s + 33, '/') : NULL;
            sec = !s ? OTHER : !rest ? ROOT : !strcmp(rest, "/ColorCube:") ? CUBE : !strcmp(rest, "/Gradient:") ? GRAD
                : !strcmp(rest, "/Vignette:") ? VIG : OTHER;
            continue;
        }
        const char* q1 = strchr(ln, '\'');
        const char* q2 = q1 ? strchr(q1 + 1, '\'') : NULL;
        const char* eq = q2 ? strchr(q2, '=') : NULL;
        if (!eq || sec == OTHER) continue;
        char key[64];
        size_t kl = q2 - q1 - 1;
        if (kl >= sizeof(key)) continue;
        memcpy(key, q1 + 1, kl);
        key[kl] = 0;
        const char* v = eq + 1;
        while (*v == ' ') v++;
        const bool b = !strncmp(v, "true", 4);
        const float f = (float)atof(v);
        struct K {
            int sec;
            const char* key;
            float* f;
            bool* b;
        };
        const K keys[] = {
            { ROOT, "b; Fade In Time", &p->fadeIn, NULL }, { ROOT, "c; Fade Out Time", &p->fadeOut, NULL },
            { ROOT, "e; Overwrite Color Cube Configuration", NULL, &p->cube },
            { ROOT, "i; Overwrite Vignette Configuration", NULL, &p->vignette },
            { ROOT, "h; Overwrite Gradient Configuration", NULL, &p->gradient },
            { CUBE, "a; Enabled", NULL, &p->cubeEnabled }, { CUBE, "b; Color Cube Blending", &p->cubeBlend, NULL },
            { CUBE, "c; Color Cube Input Blending", &p->cubeInput, NULL },
            { GRAD, "f; Gradient Colour_r", &p->grad[0], NULL }, { GRAD, "f; Gradient Colour_g", &p->grad[1], NULL },
            { GRAD, "f; Gradient Colour_b", &p->grad[2], NULL }, { GRAD, "a; Gradient Alpha", &p->grad[3], NULL },
            { GRAD, "a; Gradient Height", &p->grad[4], NULL }, { GRAD, "a; Gradient Power", &p->grad[5], NULL },
            { VIG, "f; Vignette Colour_r", &p->vig[0], NULL }, { VIG, "f; Vignette Colour_g", &p->vig[1], NULL },
            { VIG, "f; Vignette Colour_b", &p->vig[2], NULL }, { VIG, "a; Vignette U Scale", &p->vig[3], NULL },
            { VIG, "a; Vignette V Scale", &p->vig[4], NULL }, { VIG, "a; Vignette Inner Distance", &p->vig[5], NULL },
            { VIG, "a; Vignette Outer Distance", &p->vig[6], NULL },
        };
        for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
            if (keys[i].sec == sec && !strcmp(keys[i].key, key)) {
                if (keys[i].f) *keys[i].f = f;
                else *keys[i].b = b;
                got |= 1 << i;
            }
    }
    const int need = (1 << 21) - 1;  // every key above
    p->ok = got == need && p->fadeIn >= 0.0f && p->fadeIn <= 30.0f && p->fadeOut >= 0.0f && p->fadeOut <= 30.0f;
    return p->ok;
}

static void GradeLoad()
{
    if (!g_cfg.pickupVignette) {
        Log("DLC pickups: PickupVignette=0: no Frozen Time look");
        return;
    }
    bool ok = true;
    for (int i = 0; i < 2; i++) {
        std::vector<unsigned char> d;
        if (!DlcOverlayRead(kPresetPath[i], &d) || d.empty() || !ParsePreset(d, &s_preset[i])) {
            Log("DLC pickups: %s %s, the Frozen Time look is the DLL's edge glow", kPresetPath[i],
                d.empty() ? "not in the mode pack (built before)" : "not as expected");
            ok = false;
            break;
        }
        const GradePreset& p = s_preset[i];
        Log("DLC pickups: %s: fade in %.2f s, out %.2f s; colour cube %s (on %d, blending %.2f, input %.2f), vignette %s "
            "(%.2f %.2f %.2f, scale %.2f x %.2f, inner %.2f, outer %.2f), gradient %s",
            kPresetPath[i], p.fadeIn, p.fadeOut, p.cube ? "overwritten" : "kept", p.cubeEnabled, p.cubeBlend, p.cubeInput,
            p.vignette ? "overwritten" : "kept", p.vig[0], p.vig[1], p.vig[2], p.vig[3], p.vig[4], p.vig[5], p.vig[6],
            p.gradient ? "overwritten" : "kept");
    }
    // without the hook on the game's colour cube pass (PatchMask, or the pass was not recognised) the presets would
    // never show: the DLL's edge glow stays the look then
    if (ok && !GradePassHooked()) {
        Log("DLC pickups: the game's colour grading pass is not hooked, the Frozen Time look is the DLL's edge glow");
        ok = false;
    }
    s_gradeOn = ok;
    if (ok) Log("DLC pickups: the Frozen Time look is TU4's post presets on the game's colour cube pass (no edge glow)");
}

// game logic thread, racing frames of an active race: prev / now the frozen time before the hits / after the run-down
// (HudFrame's events: a freeze starts long or plain, a hit extends it, it runs out)
static void GradeFrame(float prev, float now)
{
    if (!s_gradeOn) return;
    if (prev <= 0.0f && now > 0.0f) {
        InterlockedExchange(&s_gradeHeavy, now > 1.0f);
        s_gradeStartTick = GetTickCount() | 1;
        s_gradeEndTick = 0;
    } else if (prev > 0.0f && now - 1.0f > prev) {
        InterlockedExchange(&s_gradeHeavy, 1);
    } else if (prev > 0.0f && now <= 0.0f) {
        s_gradeEndTick = GetTickCount() | 1;
    }
}

// render thread: the preset and its weight now (0 = none)
static float GradeWeight(int* which)
{
    if (!s_gradeOn || !s_active || s_lastState != 6) return 0.0f;
    const DWORD now = GetTickCount();
    if (now - s_lastUpdate >= 500) return 0.0f;
    const int w = s_gradeHeavy ? 1 : 0;
    const GradePreset& p = s_preset[w];
    *which = w;
    const DWORD st = s_gradeStartTick, en = s_gradeEndTick;
    float in = 1.0f;
    if (st && p.fadeIn > 0.0f) {
        const float t = (now - st) / 1000.0f;
        in = t < p.fadeIn ? t / p.fadeIn : 1.0f;
    }
    if (s_frozen > 0.0f) return st ? in : 1.0f;
    if (!en) return 0.0f;
    const float t = (now - en) / 1000.0f;
    if (p.fadeOut <= 0.0f || t >= p.fadeOut) return 0.0f;
    return in * (1.0f - t / p.fadeOut);
}

// postfx.cpp's colour cube pass wrapper: the preset's values for the call (saved: the block's +0x14..+0x58)
bool PickupsGradeBegin(unsigned char* params, float* saved)
{
    int which = 0;
    const float w = GradeWeight(&which);
    if (w <= 0.0f || !params) return false;
    const GradePreset& p = s_preset[which];
    float* f = (float*)(params + 0x14);
    memcpy(saved, f, 0x44);
    auto mix = [w](float a, float b) { return a + (b - a) * w; };
    if (p.cube) {
        // +0x14 picks between the two cube textures (0 = the first; the crash camera's states use 1), +0x18 how much
        // of the cubed colour replaces the input (1 everywhere, the pause menu 0.9). The PC's pass has no switch of
        // its own for 'Enabled' (its state flag would also drop the vignette): the cube off is taken as no cubed
        // colour at all (inferred from these values)
        if (p.cubeEnabled) {
            f[0] = mix(f[0], p.cubeBlend);
            f[1] = mix(f[1], p.cubeInput);
        } else {
            f[1] = mix(f[1], 0.0f);
        }
    }
    if (p.gradient) {
        for (int i = 0; i < 3; i++) f[2 + i] = mix(f[2 + i], p.grad[i]);  // +0x1C..+0x24
        for (int i = 0; i < 3; i++) f[6 + i] = mix(f[6 + i], p.grad[3 + i]);  // +0x2C..+0x34
    }
    if (p.vignette) {
        for (int i = 0; i < 3; i++) f[9 + i] = mix(f[9 + i], p.vig[i]);  // +0x38..+0x40
        for (int i = 0; i < 4; i++) f[13 + i] = mix(f[13 + i], p.vig[3 + i]);  // +0x48..+0x54
    }
    static bool logged;
    if (!logged) {
        logged = true;
        Log("DLC pickups: Frozen Time %s on the colour cube pass (weight %.2f)", which ? "Heavy" : "Subtle", w);
    }
    return true;
}

void PickupsGradeEnd(unsigned char* params, const float* saved) { memcpy(params + 0x14, saved, 0x44); }

static bool CountdownClassMatches()
{
    const DWORD* col = (const DWORD*)kCountdownCol;
    if (col[0] != 0) return false;
    const char* name = (const char*)(col[3] + 8);
    return !IsBadStringPtrA(name, sizeof(kCountdownClass)) && !strcmp(name, kCountdownClass);
}

// [DLC] Pickups=1 on the Steam build and a merged overlay that brings the mode: only then the D3D hooks and
// resources for the boxes go in (diag.cpp, main.cpp). Decided once, at the first call: the overlay is merged while
// the DLL loads (DlcInstall), before the D3D hooks are installed
bool PickupsWanted()
{
    static int s_wanted = -1;
    if (s_wanted < 0) s_wanted = g_cfg.dlcPickups && CurrentBuild() == BUILD_STEAM && DlcOverlayHas(kModeFile);
    return s_wanted != 0;
}

// Deadline's sounds: Pickups=1 on the Steam build and the merged overlay brings the converted DLCMenu bank
// (deadline_mode.py with the menu audio); decided once, after DlcInstall merged the overlay
bool DeadlineMenuWanted()
{
    static int s_wanted = -1;
    if (s_wanted < 0)
        s_wanted = g_cfg.dlcPickups && CurrentBuild() == BUILD_STEAM && DlcOverlayHas(kModeFile) &&
                   DlcOverlayHas("Audio/Release/Menu/DLCMenu.fev") && DlcOverlayHas("Audio/Release/Menu/DLCMenu.fsb");
    return s_wanted != 0;
}

// the race setup's mode is Deadline (the audio thread asks; read only)
bool DeadlineRaceNow() { return ModeIsDeadline(Resources()); }

// with [DLC] Modes=1 (DeadlineBonus) or Pickups=1
void DlcInstallPickups()
{
    if (!g_cfg.dlcPickups && !g_cfg.dlcModes) return;
    if (CurrentBuild() != BUILD_STEAM) {
        Log("DLC pickups: not on this build (%s), no boxes and no DeadlineBonus", BuildName());
        return;
    }
    if (!DlcOverlayHas(kModeFile)) {
        if (g_cfg.dlcPickups)
            Log("DLC pickups: no overlay in use brings %s (needs [DLC] Overlays=1 and the mode pack), no boxes", kModeFile);
        return;
    }
    if (g_cfg.dlcPickups && !DlcOverlayHas("Environments/Levels/docks/Subtracks/A/Pickups/docks.pkps"))
        Log("DLC pickups: the mode pack has no .pkps files (built before stage 2): rebuild it with "
            "tools\\dlc\\mode\\deadline_mode.py; tracks without their file get no boxes");
    __try {
        if ((DWORD)kCountdownVtable[-1] != kCountdownCol || !CountdownClassMatches() ||
            (DWORD)kCountdownVtable[2] != kCountdownReset || (DWORD)kCountdownVtable[4] != kCountdownUpdate ||
            memcmp((const void*)kCountdownUpdate, "\xA0\x44\xBC\xD6\x00\x0F\x57\xD2", 8) ||
            memcmp((const void*)kCountdownReset, "\x0F\xB6\x15\x44\xBC\xD6\x00\x8B\x41\x2C", 10) ||
            memcmp((const void*)0x004454B1, "\xC7\x05\x74\x3C\xD0\x00\x06\x00\x00\x00", 10)) {
            Log("DLC pickups: the countdown logic is not as expected, no boxes");
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    DWORD old;
    if (!VirtualProtect(&kCountdownVtable[2], 12, PAGE_READWRITE, &old)) return;
    // the originals are stored before the slots point at the hooks
    s_origReset = (CountdownReset_t)kCountdownVtable[2];
    s_origUpdate = (CountdownUpdate_t)kCountdownVtable[4];
    InterlockedExchangePointer(&kCountdownVtable[2], (void*)Hook_CountdownReset);
    InterlockedExchangePointer(&kCountdownVtable[4], (void*)Hook_CountdownUpdate);
    VirtualProtect(&kCountdownVtable[2], 12, old, &old);
    s_hooked = true;
    // the race result (DeadlineResult): the kind's writer (unknown names -> 7 or 8) and its reader at the race end,
    // cEndGame's logic id and the reset that clears its "not finished" flag, the record collection's logic id and
    // the reset that clears the Disqualified record
    __try {
        s_resultCode = !memcmp((const void*)0x007D53D5, "\xF7\xD8\x1B\xC0\xF7\xD8\x83\xC0\x07\x89\x86\x4C\x19\x00\x00", 15) &&
                       !memcmp((const void*)0x00799E73, "\xA1\xF8\x19\xD7\x00\x8B\x80\x4C\x19\x00\x00", 11) &&
                       !memcmp((const void*)0x004425F1, "\xC7\x05\x00\x37\xD0\x00\x09\x00\x00\x00", 10) &&
                       !memcmp((const void*)0x00442536, "\x88\x50\x0A", 3) &&
                       (DWORD)((void**)kEndGameVt)[2] == 0x00442520 &&
                       !memcmp((const void*)0x00446281, "\xC7\x05\x78\x3C\xD0\x00\x00\x00\x00\x00", 10) &&
                       (DWORD)((void**)0x00BA89B4)[2] == 0x0043E770 &&  // cPlayerState's reset clears Disqualified
                       !memcmp((const void*)0x0043E770, "\x51\x53\x56\x8B\x71\x2C\x33\xDB", 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_resultCode = false;
    }
    if (!s_resultCode) Log("DLC modes: the race end code is not as expected, Deadline's end screen stays stock (no time)");
    else Log("DLC modes: Deadline's end screen shows the finish time (race kind Time Trial), a run out of time as DNF");
    if (s_resultCode) {
        const bool a = SwapEnter(kObjectivesVt, 0x00C979C8, ".?AVcEventObjectives@InGameUI@@", 0x00806820,
                                 "\x83\xEC\x18\x56\x8B\xF1\x8B\x46\x68", 9, (void*)Hook_ObjectivesEnter, &s_origObjectivesEnter);
        const bool b = InstallDlcRaceEnd();
        Log("DLC modes: Deadline's race kind kept at the screens: event objectives %s, race end %s (vtables 0xC201E4 / "
            "0xC208DC slot 2)", a ? "hooked" : "NOT as expected", b ? "hooked" : "NOT as expected");
    }
    // Deadline's HUD as on the Xbox (HudFrame): timer colours, sounds, the "+x,xs" and hint messages, no top countdown
    s_hudCode = CodeAsExpected(kHudCode, sizeof(kHudCode) / sizeof(kHudCode[0]));
    // the top countdown is hidden for s_result races, which need the race end code too
    Log("DLC modes: Deadline HUD (%s): %s%s", s_hudCode ? "game code as expected" : "game code NOT as expected",
        s_hudCode && s_resultCode ? "no top countdown, " : s_hudCode ? "the top countdown stays (race end code not as expected), " : "",
        s_hudCode ? "timer colours (DLC DialsInfo.gfx), DLCMenu sounds, pickup and hint messages"
                  : "the top countdown stays, the DLL draws the pickup line itself");
    if (s_resultCode) RivalsInstall();  // the Xbox's rival standings and its time left on the end screens
    if (g_cfg.dlcPickups) {
        LapsInstall();     // the race time stands while the clock is frozen (TU4)
        GradeLoad();       // the "Frozen Time" post presets
    }
    if (g_cfg.dlcPickups)
        Log("DLC pickups: countdown hooked (vtable 0xBA96B0 slots 2 and 4), Deadline's boxes stop the clock "
            "(%.1f s, x%.0f drifting), Xbox start times", kFreezeBonus, kDriftMultiplier);
    else
        Log("DLC modes: countdown hooked (vtable 0xBA96B0 slots 2 and 4), no boxes (Pickups=0): Deadline's start "
            "times + %.0f s (DeadlineBonus)", floorf(g_cfg.deadlineBonus + 0.5f));
}

// ---------------------------------------------------------------- drawing (render thread)
static IDirect3DDevice9* s_dev;
static IDirect3DVertexShader9* s_vs;
static IDirect3DPixelShader9* s_ps;
static IDirect3DPixelShader9* s_gps;  // HDR=1: gain 1 where a box is (NULL: not made)
static IDirect3DVertexDeclaration9* s_decl;
static IDirect3DVertexBuffer9* s_vb;  // managed: survive a Reset
static IDirect3DIndexBuffer9* s_ib;
static IDirect3DStateBlock9* s_state;  // released before a Reset
static bool s_ready, s_failed;
// the box model (managed pool: kept over a Reset)
static IDirect3DVertexShader9* s_mvs;
static IDirect3DPixelShader9* s_mps;
static IDirect3DVertexDeclaration9* s_mdecl;
static IDirect3DVertexBuffer9* s_mvb;
static IDirect3DIndexBuffer9* s_mib;
static IDirect3DBaseTexture9* s_mtex[kRoles];
static bool s_modelReady, s_modelTried;
static bool s_feedbackReady;  // the feedback's fonts, glyphs and state block made (PickupsPresent)
static DWORD s_feedbackTry;   // GetTickCount of the last try
static DWORD s_eyeFallbacks;  // frames the camera position was taken from the car (for the log)
static IDirect3DSurface9* s_entryDs;   // bound when the post processing began (one reference, this frame)
static bool s_drawing;                 // our own SetDepthStencilSurface calls are not noted

// the last depth surfaces the game set, one reference each (the scene's has the picture's size)
struct DepthSeen {
    IDirect3DSurface9* s;
    UINT w, h;
    D3DMULTISAMPLE_TYPE ms;
    DWORD stamp;  // when the game set it last
};
static const int kDepthSeen = 3;
static DepthSeen s_depth[kDepthSeen];
static DWORD s_depthStamp;

static bool WantDraw()
{
    return s_ready && s_active && (s_drawState & 0xFFFF) > 0 && GetTickCount() - s_lastUpdate < 500;
}

static void DropDeviceObjects();  // below

void PickupsDeviceCreated(IDirect3DDevice9* dev)
{
    if (!PickupsWanted() || !dev) return;
    if (s_dev && dev != s_dev) {  // the game made a new device: nothing of the old one may be used with it
        Log("DLC pickups: a new Direct3D device, the boxes' resources are made again for it");
        DropDeviceObjects();
    }
    if (s_dev || s_failed) return;
    struct V {
        float x, y, z, w, u, v;
    };
    V verts[24];
    WORD idx[36];
    for (int f = 0; f < 6; f++) {  // face f: axis f / 2, side +1 or -1
        int a = f / 2, b = (a + 1) % 3, c = (a + 2) % 3;
        float s = (f & 1) ? -1.0f : 1.0f;
        static const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int k = 0; k < 4; k++) {
            float p[3];
            p[a] = s;
            p[b] = uv[k][0] * 2 - 1;
            p[c] = uv[k][1] * 2 - 1;
            V& v = verts[f * 4 + k];
            v.x = p[0];
            v.y = p[1];
            v.z = p[2];
            v.w = 1;
            v.u = uv[k][0];
            v.v = uv[k][1];
        }
        static const WORD q[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k = 0; k < 6; k++) idx[f * 6 + k] = (WORD)(f * 4 + q[k]);
    }
    static const D3DVERTEXELEMENT9 decl[] = {
        { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END(),
    };
    void* p = NULL;
    bool ok = SUCCEEDED(dev->CreateVertexShader((const DWORD*)g_pickupVS, &s_vs)) &&
              SUCCEEDED(dev->CreatePixelShader((const DWORD*)g_pickupPS, &s_ps)) &&
              SUCCEEDED(dev->CreateVertexDeclaration(decl, &s_decl)) &&
              SUCCEEDED(dev->CreateVertexBuffer(sizeof(verts), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &s_vb, NULL)) &&
              SUCCEEDED(s_vb->Lock(0, 0, &p, 0)) && p;
    if (ok) {
        memcpy(p, verts, sizeof(verts));
        s_vb->Unlock();
        p = NULL;
        ok = SUCCEEDED(dev->CreateIndexBuffer(sizeof(idx), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &s_ib, NULL)) &&
             SUCCEEDED(s_ib->Lock(0, 0, &p, 0)) && p;
        if (ok) {
            memcpy(p, idx, sizeof(idx));
            s_ib->Unlock();
        }
    }
    if (!ok) {
        Log("DLC pickups: shaders or buffers not created, the boxes are not drawn (the clock still stops)");
        s_failed = true;
        s_dev = dev;  // not ready; what was made is released, and tried again, with a new device (DropDeviceObjects)
        return;
    }
    if (FAILED(dev->CreatePixelShader((const DWORD*)g_pickupGainPS, &s_gps))) s_gps = NULL;  // only for HDR=1
    s_dev = dev;
    s_ready = true;
}

// one DXT1 level from the blob into a locked surface (rows of 4 x 4 blocks)
static void CopyDxt1(const D3DLOCKED_RECT& lr, const unsigned char* src, DWORD w, DWORD h)
{
    const DWORD rowBytes = ((w + 3) / 4) * 8, rows = (h + 3) / 4;
    for (DWORD r = 0; r < rows; r++) memcpy((BYTE*)lr.pBits + r * lr.Pitch, src + r * rowBytes, rowBytes);
}

// maxLevels: the slide show's cells (256 texels, the token's part of them 20 texels from the border) would bleed
// into each other in the smallest levels: at most the levels down to 64 x 32
static IDirect3DBaseTexture9* MakeTexture(IDirect3DDevice9* dev, const ModelTex& t, const unsigned char* data, DWORD maxLevels)
{
    if (t.kind == 0) {
        IDirect3DTexture9* tex = NULL;
        const DWORD levels = t.levels < maxLevels ? t.levels : maxLevels;
        if (FAILED(dev->CreateTexture(t.w, t.h, levels, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &tex, NULL)) || !tex) return NULL;
        for (DWORD l = 0; l < levels; l++) {
            DWORD w = t.w >> l ? t.w >> l : 1, h = t.h >> l ? t.h >> l : 1;
            D3DLOCKED_RECT lr;
            if (FAILED(tex->LockRect(l, &lr, NULL, 0))) {
                tex->Release();
                return NULL;
            }
            CopyDxt1(lr, data, w, h);
            tex->UnlockRect(l);
            data += Dxt1Size(w, h, 1);
        }
        return tex;
    }
    IDirect3DCubeTexture9* cube = NULL;
    if (FAILED(dev->CreateCubeTexture(t.w, t.levels, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &cube, NULL)) || !cube) return NULL;
    for (DWORD f = 0; f < 6; f++)  // +X, -X, +Y, -Y, +Z, -Z: the blob's order is D3D's
        for (DWORD l = 0; l < t.levels; l++) {
            DWORD e = t.w >> l ? t.w >> l : 1;
            D3DLOCKED_RECT lr;
            if (FAILED(cube->LockRect((D3DCUBEMAP_FACES)f, l, &lr, NULL, 0))) {
                cube->Release();
                return NULL;
            }
            CopyDxt1(lr, data, e, e);
            cube->UnlockRect((D3DCUBEMAP_FACES)f, l);
            data += Dxt1Size(e, e, 1);
        }
    return cube;
}

static void DropModel()
{
    for (DWORD i = 0; i < kRoles; i++) {
        if (s_mtex[i]) {
            TextureDetailForget(s_mtex[i]);  // diag.cpp must not keep excluding whatever comes at this address next
            s_mtex[i]->Release();
        }
        s_mtex[i] = NULL;
    }
    if (s_mib) s_mib->Release();
    if (s_mvb) s_mvb->Release();
    if (s_mdecl) s_mdecl->Release();
    if (s_mps) s_mps->Release();
    if (s_mvs) s_mvs->Release();
    s_mib = NULL;
    s_mvb = NULL;
    s_mdecl = NULL;
    s_mps = NULL;
    s_mvs = NULL;
}

// render thread, once the game logic thread has read the blob: buffers, textures and shaders of the model
static void CreateModel()
{
    if (s_modelTried || s_modelState != 1) return;
    s_modelTried = true;
    IDirect3DDevice9* dev = s_dev;
    const ModelInfo& m = s_model;
    const unsigned char* b = s_modelBlob.data();
    static const D3DVERTEXELEMENT9 decl[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
        { 0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 32, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
        { 0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },
        D3DDECL_END(),
    };
    void* p = NULL;
    bool ok = SUCCEEDED(dev->CreateVertexShader((const DWORD*)g_pickupModelVS, &s_mvs)) &&
              SUCCEEDED(dev->CreatePixelShader((const DWORD*)g_pickupModelPS, &s_mps)) &&
              SUCCEEDED(dev->CreateVertexDeclaration(decl, &s_mdecl)) &&
              SUCCEEDED(dev->CreateVertexBuffer(m.nv * kModelStride, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &s_mvb, NULL)) &&
              SUCCEEDED(s_mvb->Lock(0, 0, &p, 0)) && p;
    if (ok) {
        memcpy(p, b + m.vo, m.nv * kModelStride);
        s_mvb->Unlock();
        p = NULL;
        ok = SUCCEEDED(dev->CreateIndexBuffer(m.ni * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &s_mib, NULL)) &&
             SUCCEEDED(s_mib->Lock(0, 0, &p, 0)) && p;
        if (ok) {
            memcpy(p, b + m.io, m.ni * 2);
            s_mib->Unlock();
        }
    }
    for (DWORD i = 0; ok && i < kRoles; i++) ok = (s_mtex[i] = MakeTexture(dev, m.tex[i], b + m.tex[i].off, i == 2 ? 5 : 16)) != NULL;
    for (DWORD i = 0; ok && i < kRoles; i++) TextureDetailExclude(s_mtex[i]);  // [Graphics] TextureDetail: not ours
    if (!ok) {
        DropModel();
        Log("DLC pickups: the box model's buffers, textures or shaders were not created, the boxes are drawn as cubes");
    } else {
        Log("DLC pickups: box model ready (%lu vertices, %lu triangles, 3 DXT1 textures, managed)", m.nv, m.ni / 3);
    }
    s_modelReady = ok;
    // the blob stays (read only from here on): a device made anew gets the model from it again (DropDeviceObjects)
}

// the camera's position: from its view matrix (rotation rows 0..2 and translation row 3, row vectors), checked
// against the view x projection (the eye projects to w = 0)
static bool CameraEye(const float* vpCols, float* eye)
{
    __try {
        BYTE* mgr = *kViewportManager;
        if (!mgr) return false;
        DWORD i = *(const DWORD*)(mgr + 0xC);
        if (i > 3) return false;
        const float* v = (const float*)(mgr + 0x68 + i * 0x2F4 + 0xD8);
        for (int k = 0; k < 16; k++)
            if (!(v[k] > -1e7f && v[k] < 1e7f)) return false;
        for (int a = 0; a < 3; a++) eye[a] = -(v[12] * v[a * 4] + v[13] * v[a * 4 + 1] + v[14] * v[a * 4 + 2]);
        const float* w = vpCols + 12;  // column 3: clip w
        float cw = w[0] * eye[0] + w[1] * eye[1] + w[2] * eye[2] + w[3];
        float scale = fabsf(w[0]) + fabsf(w[1]) + fabsf(w[2]);
        return scale > 0 && fabsf(cw) < 0.05f * scale;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void DropDepth()
{
    for (DepthSeen& d : s_depth) {
        if (d.s) d.s->Release();
        d = DepthSeen();
    }
    if (s_entryDs) s_entryDs->Release();
    s_entryDs = NULL;
}

void PickupsDeviceLost()
{
    s_feedbackReady = false;  // overlay.cpp's fonts and state block are put back or made again: warm them up again
    s_feedbackTry = GetTickCount() - 1000;
    if (s_state) s_state->Release();
    s_state = NULL;
    DropDepth();
}

// everything made on s_dev (a new device replaces it, PickupsDeviceCreated): released, made again for the new one;
// the model blob is kept for that
static void DropDeviceObjects()
{
    PickupsDeviceLost();
    DropModel();
    IUnknown* const objs[] = { s_vs, s_ps, s_gps, s_decl, s_vb, s_ib };
    for (IUnknown* o : objs)
        if (o) o->Release();
    s_vs = NULL;
    s_ps = s_gps = NULL;
    s_decl = NULL;
    s_vb = NULL;
    s_ib = NULL;
    s_ready = s_failed = false;
    s_modelReady = s_modelTried = false;
    s_dev = NULL;
}

// only while boxes are drawn: no reference to the game's surfaces is kept outside a Deadline race
void PickupsDepthSet(IDirect3DSurface9* ds)
{
    if (!ds || s_drawing || !WantDraw()) return;
    s_depthStamp++;
    for (DepthSeen& d : s_depth)
        if (d.s == ds) {
            d.stamp = s_depthStamp;
            return;
        }
    D3DSURFACE_DESC desc;
    if (FAILED(ds->GetDesc(&desc))) return;
    // the sun shadow atlas (three cascades one above the other, diag.cpp) and small targets are never the scene's
    if (desc.Height == 3 * desc.Width || desc.Width < 320) return;
    DepthSeen* oldest = &s_depth[0];
    for (DepthSeen& e : s_depth)
        if (oldest->s && (!e.s || e.stamp < oldest->stamp)) oldest = &e;
    DepthSeen& d = *oldest;
    if (d.s) d.s->Release();
    d.stamp = s_depthStamp;
    ds->AddRef();
    d.s = ds;
    d.w = desc.Width;
    d.h = desc.Height;
    d.ms = desc.MultiSampleType;
}

void PickupsBeforePost()
{
    if (!WantDraw()) return;
    if (s_entryDs) s_entryDs->Release();
    s_entryDs = NULL;
    s_dev->GetDepthStencilSurface(&s_entryDs);
}

static bool Fits(IDirect3DSurface9* ds, const D3DSURFACE_DESC& rt)
{
    D3DSURFACE_DESC d;
    return ds && SUCCEEDED(ds->GetDesc(&d)) && d.Width == rt.Width && d.Height == rt.Height &&
           d.MultiSampleType == rt.MultiSampleType;
}

// the camera the frame is drawn with: view x projection of the current viewport (as columns)
static bool CameraMatrix(float* vp)
{
    __try {
        BYTE* mgr = *kViewportManager;
        if (!mgr) return false;
        DWORD i = *(const DWORD*)(mgr + 0xC);
        if (i > 3) return false;
        const float* m = (const float*)(mgr + 0x68 + i * 0x2F4 + 0x198);
        float sum = 0;
        for (int k = 0; k < 16; k++) {
            if (!(m[k] > -1e9f && m[k] < 1e9f)) return false;
            sum += fabsf(m[k]);
        }
        if (sum == 0) return false;
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) vp[c * 4 + r] = m[r * 4 + c];  // columns, for dot(c[i], p)
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SkipOnce(const char* why)
{
    static const char* last;
    if (why != last) Log("DLC pickups: boxes not drawn: %s", why);
    last = why;
}

// [DLC] PickupVignette: while the clock stands, a cyan glow along the picture's edges (the Xbox's "Frozen Time"
// look), following the stopped time left and fading out in its last 0.2 s. Drawn into the tone mapped scene with the
// boxes, before the HUD copy of hdr.cpp: with HDR=1 it is part of the picture the gain applies to (drawn at Present it
// would have cut the boost off along the edges). Four gradient bands from the edges to 22 % of the height inwards,
// no full-screen pass; fixed function, inside the boxes' state block.
static float EdgeStrength()
{
    if (!g_cfg.pickupVignette || s_gradeOn || s_lastState != 6) return 0.0f;  // the presets replace it
    const float f = s_frozen;
    if (f <= 0.0f) return 0.0f;
    float strength = 0.6f + 0.2f * f;
    if (strength > 1.0f) strength = 1.0f;
    return strength * (f < 0.2f ? f / 0.2f : 1.0f);
}

static void EdgeGlow(IDirect3DDevice9* dev, const D3DVIEWPORT9& vp, float strength)
{
    struct V {
        float x, y, z, w;
        D3DCOLOR c;
    };
    int a = (int)(0.5f * strength * 255.0f + 0.5f);
    const D3DCOLOR o = (D3DCOLOR)((a > 255 ? 255 : a) << 24) | 0x0890B0, in = 0x0890B0;
    const float x0 = vp.X - 0.5f, y0 = vp.Y - 0.5f, x1 = x0 + vp.Width, y1 = y0 + vp.Height, b = vp.Height * 0.22f;
    const V ol = { x0, y0, 0, 1, o }, orr = { x1, y0, 0, 1, o }, obl = { x0, y1, 0, 1, o }, obr = { x1, y1, 0, 1, o };
    const V il = { x0 + b, y0 + b, 0, 1, in }, ir = { x1 - b, y0 + b, 0, 1, in }, ibl = { x0 + b, y1 - b, 0, 1, in },
            ibr = { x1 - b, y1 - b, 0, 1, in };
    const V band[24] = { ol, orr, il, il, orr, ir, obl, ibl, obr, obr, ibl, ibr,
                         ol, il, obl, obl, il, ibl, orr, obr, ir, ir, obr, ibr };
    dev->SetVertexShader(NULL);
    dev->SetPixelShader(NULL);
    dev->SetTexture(0, NULL);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 7);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 8, band, sizeof(band[0]));
}

void PickupsAfterToneMap()
{
    if (!WantDraw()) {  // outside a Deadline race nothing of ours stays on the card
        if (s_state || s_entryDs || s_depth[0].s || s_depth[1].s || s_depth[2].s) PickupsDeviceLost();
        return;
    }
    IDirect3DDevice9* dev = s_dev;
    IDirect3DSurface9 *rt = NULL, *cur = NULL, *rts[4] = {};
    D3DSURFACE_DESC rd = {};
    float vp[16];
    dev->GetRenderTarget(0, &rt);
    if (!rt || FAILED(rt->GetDesc(&rd))) {
        SkipOnce("no render target");
    } else if (!CameraMatrix(vp)) {
        SkipOnce("no camera matrix");
    } else {
        IDirect3DSurface9* ds = Fits(s_entryDs, rd) ? s_entryDs : NULL;
        DWORD best = 0;
        if (!ds)  // else the one of the picture's size the game set last
            for (const DepthSeen& d : s_depth)
                if (d.s && d.w == rd.Width && d.h == rd.Height && d.ms == rd.MultiSampleType && (!ds || d.stamp > best)) {
                    ds = d.s;
                    best = d.stamp;
                }
        if (!ds) {
            SkipOnce("no depth surface of the picture's size");
        } else if (!s_state && FAILED(dev->CreateStateBlock(D3DSBT_ALL, &s_state))) {
            SkipOnce("no state block");
        } else if (FAILED(s_state->Capture())) {  // the game's state could not be put back: nothing drawn
            SkipOnce("the state block did not capture");
        } else {
            static bool logged;
            if (!logged) {
                logged = true;
                D3DSURFACE_DESC dd = {};
                ds->GetDesc(&dd);
                Log("DLC pickups: drawing into %ux%u format %d with depth %ux%u format %d (%s)", rd.Width, rd.Height,
                    rd.Format, dd.Width, dd.Height, dd.Format, ds == s_entryDs ? "set at the post processing" : "set earlier");
            }
            dev->GetDepthStencilSurface(&cur);
            for (DWORD i = 1; i < 4; i++) dev->GetRenderTarget(i, &rts[i]);
            D3DVIEWPORT9 view;
            dev->GetViewport(&view);
            SaveStage0Tracking();
            s_drawing = true;
            for (DWORD i = 1; i < 4; i++)
                if (rts[i]) dev->SetRenderTarget(i, NULL);
            if (cur != ds) dev->SetDepthStencilSurface(ds);
            D3DVIEWPORT9 v = view;
            v.MinZ = 0;
            v.MaxZ = 1;
            dev->SetViewport(&v);
            dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
            dev->SetRenderState(D3DRS_COLORWRITEENABLE, 7);  // the target's alpha stays the game's
            dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
            dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
            dev->SetRenderState(D3DRS_DEPTHBIAS, 0);
            dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0);
            dev->SetRenderState(D3DRS_CLIPPING, TRUE);
            CreateModel();  // once, after the game logic thread has read the blob
            const bool model = s_modelReady;
            DWORD now = GetTickCount();
            if (model) {
                float eye[4] = { s_carPos[0], s_carPos[1] + 3.0f, s_carPos[2], 1 };
                static bool eyeLogged;
                if (!CameraEye(vp, eye)) {
                    s_eyeFallbacks++;
                    eye[0] = s_carPos[0];
                    eye[1] = s_carPos[1] + 3.0f;
                    eye[2] = s_carPos[2];
                    if (!eyeLogged) Log("DLC pickups: camera position not taken from its view matrix, the car's is used");
                    eyeLogged = true;
                }
                // the slide show: frames x * y cells less the skipped ones, speed frames per second (Xbox: 4 x 2 - 1 at 10)
                const ModelInfo& m = s_model;
                DWORD fx = (DWORD)m.frames[0], fy = (DWORD)m.frames[1], skip = (DWORD)m.skip;
                DWORD frames = fx * fy > skip ? fx * fy - skip : 1;
                DWORD frame = (DWORD)((ULONGLONG)now * (DWORD)(m.speed * 1000.0f) / 1000000ull) % frames;
                const float slide[4] = { (frame % fx) / m.frames[0], (frame / fx) / m.frames[1], 0, 0 };
                static const float cyan[4] = { 0.05f, 0.38f, 0.0f, 0.55f };  // matched to the Xbox video's token
                dev->SetVertexDeclaration(s_mdecl);
                dev->SetStreamSource(0, s_mvb, 0, kModelStride);
                dev->SetStreamSourceFreq(0, 1);
                dev->SetIndices(s_mib);
                dev->SetVertexShader(s_mvs);
                dev->SetPixelShader(s_mps);
                dev->SetVertexShaderConstantF(0, vp, 4);
                dev->SetVertexShaderConstantF(8, eye, 1);
                dev->SetVertexShaderConstantF(9, slide, 1);
                dev->SetPixelShaderConstantF(2, cyan, 1);
                for (DWORD i = 0; i < kRoles; i++) {
                    dev->SetTexture(i, s_mtex[i]);
                    dev->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                    dev->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
                    dev->SetSamplerState(i, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
                    dev->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                    dev->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                    dev->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
                    dev->SetSamplerState(i, D3DSAMP_MAXMIPLEVEL, 0);
                    dev->SetSamplerState(i, D3DSAMP_MIPMAPLODBIAS, 0);
                    dev->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE);
                }
            } else {
                dev->SetVertexDeclaration(s_decl);
                dev->SetStreamSource(0, s_vb, 0, 24);
                dev->SetStreamSourceFreq(0, 1);
                dev->SetIndices(s_ib);
                dev->SetVertexShader(s_vs);
                dev->SetPixelShader(s_ps);
                dev->SetVertexShaderConstantF(0, vp, 4);
                const float width[4] = { 0.14f, 0, 0, 0 };
                dev->SetPixelShaderConstantF(2, width, 1);
            }
            float pulse = 0.85f + 0.15f * sinf((now % 100000) * 0.004f);
            static const float tint[COLOURS][3] = {
                { 0.15f, 0.95f, 0.25f }, { 1.0f, 0.85f, 0.1f }, { 1.0f, 0.15f, 0.1f }, { 0.95f, 0.95f, 0.95f }, { 0.1f, 0.1f, 0.1f },
            };
            // the model's colour per box colour: the Xbox draws every colour with its own model; only green is in
            // the Deadline files, the others get the green one tinted
            static const float modelTint[COLOURS][4] = {
                { 1, 1, 1, 0 }, { 2.0f, 1.4f, 0.2f, 0 }, { 2.2f, 0.4f, 0.3f, 0 }, { 1.6f, 1.4f, 1.2f, 0 }, { 0.3f, 0.3f, 0.3f, 0 },
            };
            static WORD shown[kMaxPickups];  // the boxes drawn this frame and their scale and opacity, for the gain pass
            static float shownScale[kMaxPickups], shownFade[kMaxPickups];
            const LONG drawState = s_drawState;  // the buffer and its count together (LoadTrack)
            const Pickup* const pk = s_pkBuf[(drawState >> 16) & 1];
            int n = drawState & 0xFFFF, drawn = 0;
            for (int i = 0; i < n; i++) {
                const Pickup& k = pk[i];
                float scale = 1.0f, fade = 1.0f;
                if (k.collected) {  // the Xbox hides it at once; ours grows and fades for a moment
                    DWORD age = now - k.collectTick;
                    if (!k.collectTick || age >= kCollectAnimMs) continue;
                    float t = age / (float)kCollectAnimMs;
                    scale = 1.0f + 0.6f * t;
                    fade = 1.0f - t;
                }
                float d[3] = { k.c[0] - s_carPos[0], k.c[1] - s_carPos[1], k.c[2] - s_carPos[2] };
                float dist = sqrtf(Dot(d, d));
                if (dist >= kDrawDist) continue;
                if (dist > kFadeDist) fade *= (kDrawDist - dist) / (kDrawDist - kFadeDist);
                const float axes[12] = { k.ax[0][0], k.ax[0][1], k.ax[0][2], 0, k.ax[1][0], k.ax[1][1], k.ax[1][2], 0,
                                         k.ax[2][0], k.ax[2][1], k.ax[2][2], 0 };
                const int col = k.colour < COLOURS ? k.colour : 0;
                if (model) {
                    const float box[4] = { k.c[0], k.c[1], k.c[2], scale };  // the model in metres, on the transform
                    const float look[4] = { 1.8f * s_model.emissive, 0.6f, fade, 0 };
                    dev->SetVertexShaderConstantF(4, box, 1);
                    dev->SetVertexShaderConstantF(5, axes, 3);
                    dev->SetPixelShaderConstantF(0, look, 1);
                    dev->SetPixelShaderConstantF(1, modelTint[col], 1);
                    dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, s_model.nv, 0, s_model.ni / 3);
                } else {
                    const float box[4] = { k.c[0], k.c[1], k.c[2], kBoxHalf * scale };
                    const float* t = tint[col];
                    const float face[4] = { t[0], t[1], t[2], 0.30f * fade * pulse };
                    const float edge[4] = { 0.5f + 0.5f * t[0], 0.5f + 0.5f * t[1], 0.5f + 0.5f * t[2], 0.90f * fade };
                    dev->SetVertexShaderConstantF(4, box, 1);
                    dev->SetVertexShaderConstantF(5, axes, 3);
                    dev->SetPixelShaderConstantF(0, face, 1);
                    dev->SetPixelShaderConstantF(1, edge, 1);
                    dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 24, 0, 12);
                }
                shown[drawn] = (WORD)i;
                shownScale[drawn] = scale;
                shownFade[drawn] = fade;
                drawn++;
            }
            // HDR=1: the gain the HDR output gives the scene's highlights was computed before the boxes; where a box
            // is it becomes 1 (the HUD's), so the boxes stay at paper white instead of taking the boost of the sky
            // behind them. The same draws into the gain target (same size, our depth test), gain 1, no blending.
            IDirect3DSurface9* gain = drawn && model && s_gps ? HdrGainTarget() : NULL;  // the cubes are see-through: not
            D3DSURFACE_DESC gd = {}, dd = {};
            if (gain && SUCCEEDED(gain->GetDesc(&gd)) && SUCCEEDED(ds->GetDesc(&dd)) && gd.Width == rd.Width &&
                gd.Height == rd.Height && gd.MultiSampleType == dd.MultiSampleType && SUCCEEDED(dev->SetRenderTarget(0, gain))) {
                dev->SetViewport(&v);
                dev->SetPixelShader(s_gps);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
                for (int j = 0; j < drawn; j++) {
                    const Pickup& k = pk[shown[j]];
                    const float axes[12] = { k.ax[0][0], k.ax[0][1], k.ax[0][2], 0, k.ax[1][0], k.ax[1][1], k.ax[1][2], 0,
                                             k.ax[2][0], k.ax[2][1], k.ax[2][2], 0 };
                    const float box[4] = { k.c[0], k.c[1], k.c[2], model ? shownScale[j] : kBoxHalf * shownScale[j] };
                    const float fade[4] = { shownFade[j], 0, 0, 0 };
                    dev->SetVertexShaderConstantF(4, box, 1);
                    dev->SetVertexShaderConstantF(5, axes, 3);
                    dev->SetPixelShaderConstantF(3, fade, 1);
                    if (model) dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, s_model.nv, 0, s_model.ni / 3);
                    else dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 24, 0, 12);
                }
                dev->SetRenderTarget(0, rt);
                static bool gainLogged;
                if (!gainLogged) Log("DLC pickups: HDR: gain 1 written where the boxes are (%ux%u format %d)", gd.Width, gd.Height, gd.Format);
                gainLogged = true;
            }
            const float glow = EdgeStrength();
            if (glow > 0.0f) EdgeGlow(dev, v, glow);  // render target 0 is the tone mapped picture again
            s_state->Apply();
            RestoreStage0Tracking();
            if (cur != ds) dev->SetDepthStencilSurface(cur);
            for (DWORD i = 1; i < 4; i++)
                if (rts[i]) dev->SetRenderTarget(i, rts[i]);
            dev->SetViewport(&view);
            s_drawing = false;
            static DWORD lastLog;
            if (g_cfg.log && now - lastLog > 15000) {
                lastLog = now;
                Log("DLC pickups: %d of %d boxes drawn, clock stopped for %.2f s, camera position from the car %lu times",
                    drawn, n, (float)s_frozen, s_eyeFallbacks);
            }
        }
    }
    if (cur) cur->Release();
    for (DWORD i = 1; i < 4; i++)
        if (rts[i]) rts[i]->Release();
    if (rt) rt->Release();
    if (s_entryDs) s_entryDs->Release();
    s_entryDs = NULL;
}

// Our own pickup line, only when the HUD's message queue is not reachable (HudText failed: s_msgTick set): what the
// Xbox's message shows, the "+0,5s" / "+2,0s" of TEXT_TIMETRIALPLUS_PICKUP / _DRIFTPICKUP in the game's language
// (prepared on the game thread, s_fallbackText), medium size (a 30th of the height, as the Xbox's message line)
// between two thin cyan bars in the upper middle, 0.5 s (the Xbox's message time) plus a 0.2 s fade. The Xbox has no
// "CLOCK STOPPED" line (the stopped clock shows in the timer's colour), and "DRIFT" under the timer is the game's own
// drift action text, so neither is drawn. Colours stay below white: the HDR output leaves these pixels at paper
// white. The edge glow is drawn into the scene (EdgeGlow). Placement: from 21.5 % of the height down (the upper bar
// at 20 %), where the Xbox video has its message (bars at 22 and 32 %), above the car (from about 50 %); the
// TimerOverlay.gfx countdown that ends at 17 % is hidden in Deadline.
static const float kFeedbackY = 0.215f;
void PickupsPresent(IDirect3DDevice9* dev)
{
    const DWORD now = GetTickCount();
    if (!s_active || now - s_lastUpdate >= 500) return;
    // the font made before the first hit (not in the frame of the hit), and one invisible draw: the first text draw
    // on the device has the runtime put its own functions back into the device's vtable (the log's "slot N holds
    // the runtime's function again", seen at the first hit and at the first panel of overlay.cpp). Tried again
    // (once a second) while there is no back buffer or font yet, and after a Reset (PickupsDeviceLost).
    if (!s_feedbackReady && now - s_feedbackTry >= 1000) {
        s_feedbackTry = now;
        if (OverlayPreloadFeedback(dev, L"+0123456789.,sseg")) {
            s_feedbackReady = true;
            const char* warm = "+0,5s";
            const DWORD clear = 0;
            const int mid = 2;
            OverlayDrawFeedback(dev, &warm, &clear, &mid, 1, kFeedbackY, 0, 0);
        }
    }
    const DWORD hit = s_msgTick, since = now - hit;
    if (!hit || s_lastState != 6 || since >= 700) return;  // not over the pause menu or the results
    const float a = since < 500 ? 1.0f : (700 - since) / 200.0f;
    auto argb = [](float f, DWORD rgb) {
        int v = (int)(f * 255.0f + 0.5f);
        return (DWORD)((v < 0 ? 0 : v > 255 ? 255 : v) << 24) | (rgb & 0xFFFFFF);
    };
    char text[sizeof(s_fallbackText)];
    AcquireSRWLockShared(&s_fallbackLock);  // not torn by the next hit's text (game thread)
    memcpy(text, s_fallbackText, sizeof(text));
    ReleaseSRWLockShared(&s_fallbackLock);
    text[sizeof(text) - 1] = 0;
    const char* line = text;
    const DWORD color = argb(a, since < 120 ? 0xE8F4F8 : 0x70E0F0);  // a white flash first, then cyan
    const int size = 2;
    OverlayDrawFeedback(dev, &line, &color, &size, 1, kFeedbackY, argb(0.85f * a, 0x30C0E0), 0);
}
