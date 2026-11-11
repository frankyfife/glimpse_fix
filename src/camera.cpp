// Split/Second PC fix - cameras: FOV factor, zoom keys, ultrawide correction, photo mode, HUD toggle
//
// The race cameras are cChaseCamera (vtable 0xC5AE50) and cBumperCamera (0xC5B1DC); the director
// copies the active one's camera state ([camera+4], cGameCameraState: +4 position, +0x14/+0x24/+0x34
// left, up and forward axis, +0x44 field of view in radians, +0x48 a second FOV kept in the same
// ratio) into the view. The game scales that state itself in horizontal split screen, at the end of
// each camera's update (0xA89240, 0xA8D9A0: FOV *= tweakable "FOV Scale"). The update is reached
// through the classes' vtable slot 5 (0xC5AE64, 0xC5B1F0); the hook calls the game's update, then
// changes the state. The next update starts from the state (the camera eases towards the speed
// dependent FOV and keeps the car's size on screen with it), so the hook first puts the game's own
// values back; otherwise the change would build up from frame to frame. No game code is changed.
//
// The view setup (0x4B8DD0) keeps the horizontal field of view whatever the screen's aspect ratio, so
// wider screens than 16:9 lose picture at the top and bottom. UltrawideFix widens the horizontal FOV
// until the vertical one matches 16:9 again (single screen only, split screen keeps the game's view).
//
// The near plane is 1 m (0x4B8DD0 takes the state's own +0x64 instead while the byte at +0x60 is set; the
// projection is built from it at 0x50C0D0). With a much wider FOV the bottom of the picture looks down at
// the road right below the camera, nearer than 1 m, and the road is cut away there. So with the FOV the near
// plane gets closer by the same ratio: the road is cut at the same place on screen as with the game's FOV.
//
// Photo mode, during a race: the game's slow motion tweakables, "Physics/Change Phys dt/Slow motion
// activated" (0xD71208) and "Slow Motion cheat speed" (0xD113B8, scales the simulation step), hold
// the world still (speed 0.000001) while the cameras keep running in real time. The race camera's
// state then gets a free position and direction (the same hook as the FOV), and the HUD's two render
// tweakables, "UI/HUD/3D Components/Render Powerbar" (0xD1BC94) and "Render Infobar" (0xD1BC9C), are
// switched off. The 2D overlays (race messages, power play prompts, timers and counters) are Scaleform
// movies on the UI layers, [[0xD64E0C]+8][id] (Hud.gfx 0x3B, TimerOverlay.gfx 100, ..., created at
// 0x55A5A0 and 0x462100); each layer's [+0x68]+4 is its GFx movie view, and every one that is shown is
// hidden through the view's own SetVisible (GFxMovieRoot vtable 0xC41CF4, +0x20, with GetVisible at +0x24).
// The pad drives the camera and the game sees it idle, and the game's sound is paused
// (audio_diag.cpp). Leaving restores all of it.
//
// Crashes and takedowns: the director switches to other cameras then (cCrashCameraMode and others,
// built from the Tripod camera classes), and it does so in real time, while the world is held still.
// So the photo mode hooks those classes' update too (vtable slot 5 of each, the same as the race
// cameras) and puts its pose on whichever camera runs; it keeps the FOV it started with. It may start
// on such a camera while a car is wrecked, within 15 s of a race camera, and ends when no camera runs
// any more, on Esc (the pause menu) and when the game loses focus. Depth of field, bloom and exposure
// for the photo are set in postfx.cpp, from the values LookPhoto keeps. Once the player's car is
// wrecked the crash sequence decides the view (see README, Known limitations).
#include "common.h"
#include <math.h>
#include <string.h>

static void** const kChaseUpdateSlot = (void**)0x00C5AE64;
static BYTE* const kChaseUpdate = (BYTE*)0x00A89240;
static void** const kBumperUpdateSlot = (void**)0x00C5B1F0;
static BYTE* const kBumperUpdate = (BYTE*)0x00A8D9A0;
static BYTE* const kSlowMotionOn = (BYTE*)0x00D71208;
static float* const kSlowMotionSpeed = (float*)0x00D113B8;
static BYTE* const kHudPowerbar = (BYTE*)0x00D1BC94;
static BYTE* const kHudInfobar = (BYTE*)0x00D1BC9C;
static void* const* const kDisplay = (void* const*)0x00D64708;      // +0x30 width, +0x34 height
static void* const* const kInGameResources = (void* const*)0x00D66B28; // +0x14 race setup, +0x9C viewports

typedef void(__thiscall* CameraUpdate_t)(void* self, void* frame);
static CameraUpdate_t s_origChase, s_origBumper;

static float s_zoom = 1.0f;       // zoom keys, on top of the INI factor
static float s_photoZoom = 1.0f;  // zoom keys in photo mode

// photo mode
static bool s_photo;              // on
static bool s_photoPose;          // the free pose has been taken from the camera
static float s_pos[3];
static float s_axis[3][3];        // left, up, forward (the state's axes)
static float s_photoFov;          // the FOV the photo mode started with
static DWORD s_raceCamera;        // tick of the last race camera update (0: none yet)
static DWORD s_anyCamera;         // tick of the last update of any hooked camera
static BYTE s_slowOn;             // the slow motion tweakables before the photo mode
static float s_slowSpeed;
static bool s_padHeld;            // after the photo mode: the game sees the pad idle until it is let go
static bool s_hudHidden;          // HUD key

static int Viewports()
{
    BYTE* res = (BYTE*)*kInGameResources;
    if (!res || IsBadReadPtr(res, 0x18)) return 1;
    BYTE* setup = *(BYTE**)(res + 0x14);
    if (!setup || IsBadReadPtr(setup, 0xA0)) return 1;
    return *(int*)(setup + 0x9C);
}

static float ScreenAspect()
{
    BYTE* d = (BYTE*)*kDisplay;
    if (!d || IsBadReadPtr(d, 0x38)) return 16.0f / 9;
    int w = *(int*)(d + 0x30), h = *(int*)(d + 0x34);
    return w > 0 && h > 0 ? (float)w / h : 16.0f / 9;
}

// the game's values of each changed state (+4 to +0x68: position, axes, both FOVs, near plane switch and
// distance) and what the hook wrote over them
enum { kPose = 4, kPoseLen = 0x64 };
struct Saved { BYTE* state; BYTE stock[kPoseLen], set[kPoseLen]; };
static Saved s_saved[32];

static Saved* Record(BYTE* state, bool add)
{
    Saved* empty = NULL;
    for (Saved& r : s_saved) {
        if (r.state == state) return &r;
        if (!r.state && !empty) empty = &r;
    }
    if (!add) return NULL;
    if (!empty) empty = &s_saved[(DWORD)(UINT_PTR)state / 16 % 32];  // full: reuse a slot
    empty->state = state;
    return empty;
}

static BYTE* State(BYTE* self)
{
    BYTE* state = *(BYTE**)(self + 4);
    return state && !IsBadReadPtr(state, 0x4C) ? state : NULL;
}

// before the update: the game's own values back, if the state still holds what the hook wrote
static void RestoreState(BYTE* self)
{
    BYTE* state = State(self);
    Saved* r = state ? Record(state, false) : NULL;
    if (!r) return;
    if (!memcmp(state + kPose, r->set, kPoseLen)) memcpy(state + kPose, r->stock, kPoseLen);
    r->state = NULL;
}

// +1 or -1: the sign of side . (up x forward), 0 for no usable axes
static int s_raceHand;
static int Handedness(const BYTE* state)
{
    const float* a = (const float*)(state + 0x14);
    const float* u = (const float*)(state + 0x24);
    const float* f = (const float*)(state + 0x34);
    float d = a[0] * (u[1] * f[2] - u[2] * f[1]) + a[1] * (u[2] * f[0] - u[0] * f[2]) + a[2] * (u[0] * f[1] - u[1] * f[0]);
    return d > 0.5f ? 1 : d < -0.5f ? -1 : 0;
}

static void ScaleFov(BYTE* state, float factor, bool ultrawide)
{
    float* fov = (float*)(state + 0x44);
    float stock = *fov;
    if (!(stock > 0.01f && stock < 3.0f)) return;
    float base = stock, f = stock * factor;
    if (ultrawide && g_cfg.ultrawideFix && Viewports() == 1) {
        float wide = ScreenAspect() / (16.0f / 9);
        if (wide > 1.01f) {
            base = 2.0f * atanf(tanf(base * 0.5f) * wide);
            f = 2.0f * atanf(tanf(f * 0.5f) * wide);
        }
    }
    f = f < 0.05f ? 0.05f : f > 2.97f ? 2.97f : f;
    base = base > 2.97f ? 2.97f : base;
    float ratio = tanf(f * 0.5f) / tanf(base * 0.5f);  // how much wider than with the ultrawide correction alone
    if (ratio > 1.001f) {
        BYTE* nearOn = state + 0x60;
        float* nearDist = (float*)(state + 0x64);
        float game = *nearOn && *nearDist > 0.0f ? *nearDist : 1.0f;
        float n = game / ratio;
        n = n < 0.05f ? 0.05f : n;
        *nearOn = 1;
        *nearDist = n;
        static bool logged;
        if (!logged) Log("camera fov: view %.2f times wider, near plane %.2f m", ratio, *nearDist);
        logged = true;
    }
    if (f == stock) return;
    fov[0] = f;
    fov[1] *= f / stock;
}

// Camera shake: the race cameras' own shake (bumps, collisions) shows in their rotation from frame to
// frame. CameraShake below 1 passes the forward and up axis of the chase and bumper camera through a
// low pass (time constant up to 0.12 s): quick shaking is filtered, turns follow with little delay.
// It is applied after the game's update and taken back before the next one (RestoreState), like the
// FOV, so the camera's own easing never sees it. A camera cut (a big jump or a pause) starts it anew.
struct Smooth { BYTE* state; float fwd[3], up[3]; LONGLONG t; };
static Smooth s_smooth[8];

static void Norm3(float* v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static float Dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void Cross3(const float* a, const float* b, float* out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void SmoothShake(BYTE* state)
{
    if (g_cfg.cameraShake >= 0.999f) return;
    float tau = (1.0f - g_cfg.cameraShake) * 0.12f;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    Smooth* s = NULL;
    for (Smooth& e : s_smooth)
        if (e.state == state) s = &e;
    bool fresh = !s;
    if (!s) {
        s = &s_smooth[(DWORD)(UINT_PTR)state / 16 % 8];
        s->state = state;
    }
    float* left = (float*)(state + 0x14);
    float* up = (float*)(state + 0x24);
    float* fwd = (float*)(state + 0x34);
    float dt = fresh ? 1.0f : (float)(now.QuadPart - s->t) / freq.QuadPart;
    s->t = now.QuadPart;
    if (dt > 0.2f || Dot3(fwd, s->fwd) < 0.87f || Dot3(up, s->up) < 0.87f) {  // new, after a pause or a cut
        memcpy(s->fwd, fwd, sizeof(s->fwd));
        memcpy(s->up, up, sizeof(s->up));
        return;
    }
    float a = 1.0f - expf(-dt / tau);
    for (int i = 0; i < 3; i++) {
        s->fwd[i] += (fwd[i] - s->fwd[i]) * a;
        s->up[i] += (up[i] - s->up[i]) * a;
    }
    Norm3(s->fwd);
    Norm3(s->up);
    // square again, each axis on the side the game's own points to
    float l[3], u[3];
    Cross3(s->up, s->fwd, l);
    Norm3(l);
    if (Dot3(l, left) < 0) { l[0] = -l[0]; l[1] = -l[1]; l[2] = -l[2]; }
    Cross3(s->fwd, l, u);
    Norm3(u);
    if (Dot3(u, up) < 0) { u[0] = -u[0]; u[1] = -u[1]; u[2] = -u[2]; }
    memcpy(left, l, sizeof(l));
    memcpy(up, u, sizeof(u));
    memcpy(fwd, s->fwd, sizeof(s->fwd));
}

// ExplosionShakeOff=1: the game's own switch "AreaOfEffects/DebugTrigger/DisallowAllCameraShake"
// (0xD8B4CC), which the camera shake controller (0x827FA0) checks to drop a running shake. Written only
// when it is clear (levels may load it), not every frame; Log=1 counts how often the game clears it.
void ApplyExplosionShake()
{
    BYTE* flag = (BYTE*)0x00D8B4CC;
    static unsigned cleared;
    static DWORD lastLog;
    if (!*flag) {
        *flag = 1;
        cleared++;
    }
    DWORD now = GetTickCount();
    if (g_cfg.log && now - lastLog > 10000) {
        if (cleared) Log("explosion shake switch set %u times in %u s", cleared, lastLog ? (now - lastLog) / 1000 : 0);
        cleared = 0;
        lastLog = now;
    }
}

// after the update: FOV of the race cameras, the free pose in photo mode (for every camera, so the
// view stays put when the director switches to a crash camera)
static void ChangeState(BYTE* self, bool raceCamera)
{
    BYTE* state = State(self);
    if (!state) return;
    BYTE stock[kPoseLen];
    memcpy(stock, state + kPose, kPoseLen);
    // which way the first axis points next to up and forward: the race cameras' is the photo mode's own
    // (left), some crash cameras have it the other way round (right)
    int hand = Handedness(state);
    if (raceCamera && hand) s_raceHand = hand;
    bool mirrored = hand && s_raceHand && hand != s_raceHand;
    if (raceCamera) ScaleFov(state, g_cfg.fov * s_zoom, true);
    if (raceCamera && !s_photo) SmoothShake(state);
    if (s_photo) {
        if (!raceCamera) ScaleFov(state, 1.0f, true);  // the ultrawide correction the race cameras already have
        float* pos = (float*)(state + 4);
        float fov = *(float*)(state + 0x44);
        if (!s_photoPose) {
            memcpy(s_pos, pos, sizeof(s_pos));
            for (int i = 0; i < 3; i++) memcpy(s_axis[i], state + 0x14 + i * 0x10, sizeof(s_axis[i]));
            if (mirrored)
                for (int i = 0; i < 3; i++) s_axis[0][i] = -s_axis[0][i];
            s_photoFov = fov;
            s_photoPose = true;
        }
        static bool logged;
        if (mirrored && !logged) Log("photo mode: a camera with its side axis to the right");
        logged = logged || mirrored;
        memcpy(pos, s_pos, sizeof(s_pos));
        for (int i = 0; i < 3; i++) memcpy(state + 0x14 + i * 0x10, s_axis[i], sizeof(s_axis[i]));
        if (mirrored)
            for (int i = 0; i < 3; i++) ((float*)(state + 0x14))[i] = -s_axis[0][i];
        // the FOV the photo mode started with, whichever camera runs now
        if (fov > 0.01f) ScaleFov(state, s_photoFov * s_photoZoom / fov, false);
    }
    if (!memcmp(stock, state + kPose, kPoseLen)) return;
    Saved* r = Record(state, true);
    memcpy(r->stock, stock, kPoseLen);
    memcpy(r->set, state + kPose, kPoseLen);
}

static void __fastcall Hook_ChaseUpdate(BYTE* self, void*, void* frame)
{
    s_raceCamera = s_anyCamera = GetTickCount() | 1;
    RestoreState(self);
    s_origChase(self, frame);
    ChangeState(self, true);
}

static void __fastcall Hook_BumperUpdate(BYTE* self, void*, void* frame)
{
    s_raceCamera = s_anyCamera = GetTickCount() | 1;
    RestoreState(self);
    s_origBumper(self, frame);
    ChangeState(self, true);
}

// the other camera classes (crash, takedown, replay and post race cameras are made of them), all with
// the update in vtable slot 5 and the state at +4 (cCameraState, 0x74 bytes, the same layout); only the
// photo mode changes their state
struct OtherCamera { DWORD vtable; DWORD update; const char* prologue; int len; const char* name; };
static const OtherCamera kOtherCameras[] = {
    { 0x00C5B3DC, 0x00A92520, "\x81\xEC\x18\x01\x00\x00", 6, "bonnet" },
    { 0x00C5B418, 0x00A93760, "\x81\xEC\xA8\x00\x00\x00", 6, "velocity based" },
    { 0x00C535B0, 0x00A62540, "\x81\xEC\xB4\x00\x00\x00", 6, "free" },
    { 0x00C535D8, 0x00A62E10, "\x83\xEC\x30\x56\x8B\xF1", 6, "fixed to world" },
    { 0x00C53630, 0x00A630E0, "\x81\xEC\x94\x00\x00\x00", 6, "fixed to spline" },
    { 0x00C53660, 0x00A63A40, "\x81\xEC\xEC\x00\x00\x00", 6, "fixed to subject" },
    { 0x00C536EC, 0x00A643E0, "\x83\xB9\x2C\x01\x00\x00\x00", 7, "dynamic behaviour" },
};
enum { kOtherCount = sizeof(kOtherCameras) / sizeof(kOtherCameras[0]) };
static CameraUpdate_t s_origOther[kOtherCount];
static bool s_otherSeen[kOtherCount];  // logged in this photo mode

template <int N> static void __fastcall Hook_OtherUpdate(BYTE* self, void*, void* frame)
{
    s_anyCamera = GetTickCount() | 1;
    if (s_photo && !s_otherSeen[N]) {
        s_otherSeen[N] = true;
        Log("photo mode: %s camera runs", kOtherCameras[N].name);
    }
    RestoreState(self);
    s_origOther[N](self, frame);
    ChangeState(self, false);
}
static void* const kOtherHooks[kOtherCount] = {
    (void*)Hook_OtherUpdate<0>, (void*)Hook_OtherUpdate<1>, (void*)Hook_OtherUpdate<2>, (void*)Hook_OtherUpdate<3>,
    (void*)Hook_OtherUpdate<4>, (void*)Hook_OtherUpdate<5>, (void*)Hook_OtherUpdate<6>,
};

static bool HookSlot(void** slot, BYTE* fn, const char* prologue, int len, void* hook, void** orig)
{
    if (*slot != fn || memcmp(fn, prologue, len)) return false;
    *orig = fn;
    WriteMemory(slot, &hook, sizeof(hook));
    return true;
}

// Respot: every car's GameLogicNew::cRespot component (vtable 0xBA7C38) decides each frame in its update
// (slot 4, 0x43C2E0, thiscall with the time step) whether the car is put back on the track: when it is
// wrecked (state 1 of the player's race state, [[+0x30]+0xA4+[0xD02BB4]*4]+0x2C, +4) or forced by one of
// many flags, and the wreck time or the crash flags have run out (0x43A420, 0x439D80). The crash camera
// runs in real time, so the respot came while the world was held still. The hook leaves the update out:
// in photo mode for every car, so wrecks stay where they are; with WreckTime for the human player
// ([[+0x30]+0x80+[0xD02D78]*4]+0xC) until that many seconds after the wreck.
static void** const kRespotUpdateSlot = (void**)(0x00BA7C38 + 0x10);
static BYTE* const kRespotUpdate = (BYTE*)0x0043C2E0;
typedef void(__thiscall* RespotUpdate_t)(void* self, float dt);
static RespotUpdate_t s_origRespot;
static DWORD s_wreckSeen;  // tick when a wrecked car was last seen (crash and takedown cameras run then)

// a car's race state (0 racing, 1 wrecked, 2 being respotted, -1 unreadable) and whether a human drives it
static int RaceState(BYTE* respot, bool* human)
{
    __try {
        BYTE* car = *(BYTE**)(respot + 0x30);
        BYTE* who = *(BYTE**)(car + 0x80 + *(int*)0x00D02D78 * 4);
        *human = who[0xC] != 0;
        BYTE* state = *(BYTE**)(*(BYTE**)(car + 0xA4 + *(int*)0x00D02BB4 * 4) + 0x2C);
        return *(int*)(state + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// WreckTime: when each human player's car was wrecked
static bool HoldRespot(BYTE* respot, int state)
{
    static struct { BYTE* respot; DWORD start; } wrecks[4];
    DWORD now = GetTickCount();
    int i = 0;
    while (i < 4 && wrecks[i].respot != respot) i++;  // this car's entry
    if (i == 4) {
        if (state != 1) return false;
        for (i = 0; i < 4 && wrecks[i].respot; i++) {  // else a free one
        }
        if (i == 4) i = (int)((UINT_PTR)respot / 16 % 4);  // full: reuse one
    }
    if (state != 1) {
        if (wrecks[i].respot == respot) wrecks[i].respot = NULL;
        return false;
    }
    if (wrecks[i].respot != respot) {
        wrecks[i].respot = respot;
        wrecks[i].start = now;
        Log("wreck: respot after %.0f s", g_cfg.wreckTime);
    }
    return now - wrecks[i].start < (DWORD)(g_cfg.wreckTime * 1000.0f);
}

static void __fastcall Hook_RespotUpdate(BYTE* self, void*, float dt)
{
    bool human = false;
    int state = RaceState(self, &human);
    if (state == 1 || state == 2) s_wreckSeen = GetTickCount() | 1;
    if (s_photo) {
        static DWORD logged;
        if (GetTickCount() - logged > 5000) Log("photo mode: respot held");
        logged = GetTickCount();
        return;
    }
    if (g_cfg.wreckTime > 0.0f && human && state >= 0 && HoldRespot(self, state)) return;
    s_origRespot(self, dt);
}

void PatchCamera()
{
    if (g_cfg.photoMode || g_cfg.wreckTime > 0.0f) {
        if (HookSlot(kRespotUpdateSlot, kRespotUpdate, "\xA0\xDC\x66\xD0\x00\x83\xEC\x14", 8, (void*)Hook_RespotUpdate,
                     (void**)&s_origRespot))
            Log("respot hooked (photo mode %d, wreck time %.0f s)", g_cfg.photoMode, g_cfg.wreckTime);
        else
            Log("respot not recognised");
    }
    // the race cameras also tell UnlockDecals that a single player race runs (SinglePlayerRace)
    bool race = g_cfg.fov != 1.0f || g_cfg.ultrawideFix || g_cfg.zoomKeys || g_cfg.photoMode || g_cfg.unlockDecals ||
                g_cfg.cameraShake < 0.999f;
    if (race) {
        bool chase = HookSlot(kChaseUpdateSlot, kChaseUpdate, "\x81\xEC\x44\x01\x00\x00", 6, (void*)Hook_ChaseUpdate,
                              (void**)&s_origChase);
        bool bumper = HookSlot(kBumperUpdateSlot, kBumperUpdate, "\x81\xEC\xEC\x00\x00\x00", 6, (void*)Hook_BumperUpdate,
                               (void**)&s_origBumper);
        Log("camera fov: chase %s, bumper %s, factor %.2f, ultrawide %d, zoom keys %d, shake %.2f", chase ? "hooked" : "not recognised",
            bumper ? "hooked" : "not recognised", g_cfg.fov, g_cfg.ultrawideFix, g_cfg.zoomKeys, g_cfg.cameraShake);
    }
    if (g_cfg.explosionShakeOff) Log("explosion camera shake off");
    if (g_cfg.photoMode) {
        int hooked = 0;
        for (int i = 0; i < kOtherCount; i++) {
            const OtherCamera& c = kOtherCameras[i];
            if (HookSlot((void**)(c.vtable + 0x14), (BYTE*)c.update, c.prologue, c.len, kOtherHooks[i], (void**)&s_origOther[i]))
                hooked++;
            else
                Log("photo mode: %s camera not recognised", c.name);
        }
        Log("photo mode: key 0x%02X and the pad's Back button during a race, %d of %d other cameras hooked",
            g_cfg.photoModeKey, hooked, (int)kOtherCount);
    }
}

// the game sees the pad idle in photo mode and afterwards until it is let go
bool CameraBlocksPad()
{
    return s_photo || s_padHeld;
}

// a race camera ran within the last half second (in a race, not paused)
static bool InRace()
{
    return s_raceCamera && GetTickCount() - (s_raceCamera & ~1u) < 500;
}

// some camera ran within the last half second while a car is wrecked, within 15 s of a race camera
// (a crash or takedown camera during the race, not the cameras of the pause menu, the finish or the
// frontend)
static bool AnyCamera()
{
    return s_anyCamera && GetTickCount() - (s_anyCamera & ~1u) < 500;
}

static bool InRaceScene()
{
    DWORD now = GetTickCount();
    return AnyCamera() && s_raceCamera && now - (s_raceCamera & ~1u) < 15000 && s_wreckSeen &&
           now - (s_wreckSeen & ~1u) < 1000;
}

static bool GameHasFocus()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static bool KeyDown(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000); }

// key or pad button pressed since the last call
static bool Pressed(bool down, bool& last)
{
    bool hit = down && !last;
    last = down;
    return hit;
}

static void SetHud(bool visible)
{
    *kHudPowerbar = visible;
    *kHudInfobar = visible;
}

// the 2D overlays: every UI layer in [[0xD64E0C]+8][id] (+4 id, +0x68 holder whose +4 is the GFx movie
// view, a GFxMovieRoot with vtable 0xC41CF4: +0x20 SetVisible(bool), +0x24 GetVisible(), both for _root)
typedef void(__thiscall* GFxSetVisible_t)(void* movie, bool visible);
typedef bool(__thiscall* GFxGetVisible_t)(void* movie);
enum { kLayers = 0x80 };
static void* const kMovieRootVtable = (void*)0x00C41CF4;
static void* s_hiddenMovie[kLayers];  // the movies photo mode hid, shown again when it ends

static void* LayerMovie(int id)
{
    __try {
        BYTE* ui = *(BYTE**)0x00D64E0C;
        BYTE** slots = ui ? *(BYTE***)(ui + 8) : NULL;
        BYTE* layer = slots ? slots[id] : NULL;
        if (!layer || *(int*)(layer + 4) != id) return NULL;
        BYTE* holder = *(BYTE**)(layer + 0x68);
        void* movie = holder ? *(void**)(holder + 4) : NULL;
        return movie && *(void**)movie == kMovieRootVtable ? movie : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

// hides every overlay that is shown (again each frame, for overlays that come up during photo mode);
// shows only those again that it hid
static void SetOverlays(bool visible)
{
    for (int id = 0; id < kLayers; id++) {
        void* movie = LayerMovie(id);
        void** vt = movie ? *(void***)movie : NULL;
        if (visible) {
            if (movie && movie == s_hiddenMovie[id]) ((GFxSetVisible_t)vt[0x20 / 4])(movie, true);
            s_hiddenMovie[id] = NULL;
        } else if (movie && ((GFxGetVisible_t)vt[0x24 / 4])(movie)) {
            ((GFxSetVisible_t)vt[0x20 / 4])(movie, false);
            if (s_hiddenMovie[id] != movie) Log("photo mode: overlay layer 0x%02X hidden", id);
            s_hiddenMovie[id] = movie;
        }
    }
}

static float Stick(SHORT v)
{
    float f = v / 32767.0f;
    if (f < -1.0f) f = -1.0f;
    float a = fabsf(f);
    return a < 0.2f ? 0.0f : (f > 0 ? 1.0f : -1.0f) * (a - 0.2f) / 0.8f;
}

static void Rotate(float* v, const float* axis, float angle)
{
    // Rodrigues, axis of unit length
    float c = cosf(angle), s = sinf(angle);
    float d = (v[0] * axis[0] + v[1] * axis[1] + v[2] * axis[2]) * (1 - c);
    float x = v[0] * c + (axis[1] * v[2] - axis[2] * v[1]) * s + axis[0] * d;
    float y = v[1] * c + (axis[2] * v[0] - axis[0] * v[2]) * s + axis[1] * d;
    float z = v[2] * c + (axis[0] * v[1] - axis[1] * v[0]) * s + axis[2] * d;
    v[0] = x; v[1] = y; v[2] = z;
}

static void Normalise(float* v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

// free camera: left stick / WASD move, right stick / IJKL look, triggers / Q E down and up,
// A / Shift faster, LB RB / zoom keys zoom, Y + LB RB / U O tilt
static void MovePhoto(float dt, const XINPUT_GAMEPAD& pad, bool padOk)
{
    if (!s_photoPose) return;
    float fwd = 0, side = 0, up = 0, yaw = 0, pitch = 0, roll = 0, zoom = 0;
    if (padOk) {
        side += Stick(pad.sThumbLX);
        fwd += Stick(pad.sThumbLY);
        yaw += Stick(pad.sThumbRX);
        pitch += Stick(pad.sThumbRY);
        up += (pad.bRightTrigger - pad.bLeftTrigger) / 255.0f;
        float& shoulders = (pad.wButtons & XINPUT_GAMEPAD_Y) ? roll : zoom;  // Y held: LB RB tilt
        if (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) shoulders -= 1;
        if (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) shoulders += 1;
    }
    if (GameHasFocus()) {
        fwd += KeyDown('W') - KeyDown('S');
        side += KeyDown('D') - KeyDown('A');
        up += KeyDown('E') - KeyDown('Q');
        yaw += KeyDown('L') - KeyDown('J');
        pitch += KeyDown('I') - KeyDown('K');
        roll += KeyDown('O') - KeyDown('U');
        zoom += KeyDown(g_cfg.zoomOutKey) - KeyDown(g_cfg.zoomInKey);
        if (KeyDown(g_cfg.zoomResetKey)) s_photoZoom = 1.0f;
    }
    bool fast = (padOk && (pad.wButtons & XINPUT_GAMEPAD_A)) || (GameHasFocus() && KeyDown(VK_SHIFT));
    float speed = (fast ? 80.0f : 8.0f) * dt;
    static const float kUp[3] = { 0.0f, 1.0f, 0.0f };
    for (int i = 0; i < 3; i++) {
        s_pos[i] += s_axis[2][i] * fwd * speed - s_axis[0][i] * side * speed;  // the first axis points left
        s_pos[i] += kUp[i] * up * speed;
    }
    // yaw about the world's up axis, pitch about the camera's side axis
    for (int i = 0; i < 3; i++) Rotate(s_axis[i], kUp, -yaw * 1.5f * dt);
    float pivot[3] = { s_axis[0][0], s_axis[0][1], s_axis[0][2] };
    Rotate(s_axis[1], pivot, -pitch * 1.2f * dt);
    Rotate(s_axis[2], pivot, -pitch * 1.2f * dt);
    // tilt about the view axis, positive to the right
    float view[3] = { s_axis[2][0], s_axis[2][1], s_axis[2][2] };
    Rotate(s_axis[0], view, roll * 0.8f * dt);
    Rotate(s_axis[1], view, roll * 0.8f * dt);
    // keep the axes square: forward, then left, then up (Gram-Schmidt)
    Normalise(s_axis[2]);
    float d = s_axis[0][0] * s_axis[2][0] + s_axis[0][1] * s_axis[2][1] + s_axis[0][2] * s_axis[2][2];
    for (int i = 0; i < 3; i++) s_axis[0][i] -= d * s_axis[2][i];
    Normalise(s_axis[0]);
    float a = s_axis[1][0] * s_axis[2][0] + s_axis[1][1] * s_axis[2][1] + s_axis[1][2] * s_axis[2][2];
    float b = s_axis[1][0] * s_axis[0][0] + s_axis[1][1] * s_axis[0][1] + s_axis[1][2] * s_axis[0][2];
    for (int i = 0; i < 3; i++) s_axis[1][i] -= a * s_axis[2][i] + b * s_axis[0][i];
    Normalise(s_axis[1]);
    s_photoZoom *= expf(0.7f * zoom * dt);
    s_photoZoom = s_photoZoom < 0.2f ? 0.2f : s_photoZoom > 3.0f ? 3.0f : s_photoZoom;
}

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// the look: d-pad / arrow keys up and down focus distance, left and right depth of field strength;
// with X / Ctrl held up and down bloom, left and right exposure; left stick click / Backspace resets
static void LookPhoto(float dt, const XINPUT_GAMEPAD& pad, bool padOk)
{
    float upDown = 0, leftRight = 0;
    bool alt = false, reset = false;
    if (padOk) {
        upDown += !!(pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) - !!(pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
        leftRight += !!(pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) - !!(pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
        alt = (pad.wButtons & XINPUT_GAMEPAD_X) != 0;
        reset = (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    }
    if (GameHasFocus()) {
        upDown += KeyDown(VK_UP) - KeyDown(VK_DOWN);
        leftRight += KeyDown(VK_RIGHT) - KeyDown(VK_LEFT);
        alt = alt || KeyDown(VK_CONTROL);
        reset = reset || KeyDown(VK_BACK);
    }
    if (reset) {
        g_photoBloom = g_photoExposure = 1.0f;
        g_photoDof = 0.0f;
        g_photoFocus = 15.0f;
    } else if (alt) {
        // bloom from 1 down to off (below a tenth) and up to 4 times, exposure a quarter to 4 times
        if (upDown) {
            float b = g_photoBloom < 0.1f ? 0.1f : g_photoBloom;
            b = Clamp(b * expf(0.8f * upDown * dt), 0.0f, 4.0f);
            g_photoBloom = upDown < 0 && b < 0.1f ? 0.0f : b;
        }
        g_photoExposure = Clamp(g_photoExposure * expf(0.5f * leftRight * dt), 0.25f, 4.0f);
    } else {
        g_photoFocus = Clamp(g_photoFocus * expf(0.8f * upDown * dt), 1.0f, 2000.0f);
        if (upDown && g_photoDof <= 0.0f) g_photoDof = 0.5f;  // the focus only shows with depth of field
        g_photoDof = Clamp(g_photoDof + 0.8f * leftRight * dt, 0.0f, 1.0f);
    }
    static float logged[4];
    float now[4] = { g_photoFocus, g_photoDof, g_photoBloom, g_photoExposure };
    if (!upDown && !leftRight && !reset && memcmp(now, logged, sizeof(now))) {
        memcpy(logged, now, sizeof(now));
        Log("photo look: focus %.1f m, depth of field %.2f, bloom %.2f, exposure %.2f", now[0], now[1], now[2], now[3]);
    }
}

static void SetPhoto(bool on, const char* why)
{
    if (on == s_photo) return;
    Log("photo mode %s (%s)", on ? "on" : "off", why);
    s_photo = on;
    // the look stays for the next photo; outside the photo mode the game's own
    static float look[4] = { 1.0f, 1.0f, 0.0f, 15.0f };
    float* const values[4] = { &g_photoBloom, &g_photoExposure, &g_photoDof, &g_photoFocus };
    for (int i = 0; i < 4; i++) {
        if (on) *values[i] = look[i];
        else {
            look[i] = *values[i];
            *values[i] = i < 2 ? 1.0f : i == 2 ? 0.0f : 15.0f;
        }
    }
    if (on) {
        memset(s_otherSeen, 0, sizeof(s_otherSeen));
        s_photoPose = false;
        s_photoZoom = 1.0f;
        s_slowOn = *kSlowMotionOn;
        s_slowSpeed = *kSlowMotionSpeed;
        *kSlowMotionSpeed = 0.000001f;
        *kSlowMotionOn = 1;
        SetHud(false);
        SetOverlays(false);
        PauseGameSound(true);
    } else {
        *kSlowMotionOn = s_slowOn;
        *kSlowMotionSpeed = s_slowSpeed;
        PauseGameSound(false);
        s_padHeld = true;
        SetHud(!s_hudHidden);
        SetOverlays(true);
    }
}

bool SinglePlayerRace() { return InRace() && Viewports() < 2; }

// per input frame: zoom keys, HUD key, photo mode
void CameraTick()
{
    static LONGLONG last;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = last ? (float)(now.QuadPart - last) / freq.QuadPart : 0.0f;
    last = now.QuadPart;
    if (dt > 0.1f) dt = 0.1f;

    // the first pad that is connected
    XINPUT_STATE st = {};
    bool padOk = false;
    for (DWORD i = 0; i < XUSER_MAX_COUNT && !padOk && pXInputGetState; i++)
        padOk = PadSlotConnected(i) && pXInputGetState(i, &st) == ERROR_SUCCESS;
    const XINPUT_GAMEPAD& pad = st.Gamepad;
    bool focus = GameHasFocus();

    if (g_cfg.photoMode) {
        // a race camera or a crash camera, on a single screen (player 1 only)
        bool race = (InRace() || InRaceScene()) && Viewports() < 2;
        static bool lastKey, lastBack, lastB, lastEsc;
        bool key = Pressed(focus && KeyDown(g_cfg.photoModeKey), lastKey);
        bool back = Pressed(padOk && (pad.wButtons & XINPUT_GAMEPAD_BACK), lastBack);
        bool b = Pressed(padOk && (pad.wButtons & XINPUT_GAMEPAD_B), lastB);
        // Esc opens the pause menu and the game may pause without focus: the photo mode ends first,
        // so it does not hide the menu
        bool esc = Pressed(focus && KeyDown(VK_ESCAPE), lastEsc);
        if (s_photo) {
            const char* why = key ? "key" : back ? "back" : b ? "b" : esc ? "esc" : !focus ? "focus lost" :
                              !AnyCamera() ? "cameras gone" : NULL;
            if (why) SetPhoto(false, why);
            else {
                MovePhoto(dt, pad, padOk);
                LookPhoto(dt, pad, padOk);
                SetOverlays(false);
            }
        } else if ((key || back) && race) {
            SetPhoto(true, key ? "key" : "back");
        }
        if (s_padHeld && (!padOk || (!pad.wButtons && pad.bLeftTrigger < 30 && pad.bRightTrigger < 30 &&
                                     !Stick(pad.sThumbLX) && !Stick(pad.sThumbLY) && !Stick(pad.sThumbRX) &&
                                     !Stick(pad.sThumbRY))))
            s_padHeld = false;
    }

    if (g_cfg.hudKey) {
        static bool lastHud;
        if (Pressed(focus && KeyDown(g_cfg.hudKey), lastHud) && !s_photo) {
            s_hudHidden = !s_hudHidden;
            SetHud(!s_hudHidden);
            Log(s_hudHidden ? "HUD off" : "HUD on");
        }
    }

    if (!g_cfg.zoomKeys || !focus || s_photo) return;
    float before = s_zoom;
    if (KeyDown(g_cfg.zoomInKey)) s_zoom *= expf(-0.7f * dt);
    if (KeyDown(g_cfg.zoomOutKey)) s_zoom *= expf(0.7f * dt);
    if (KeyDown(g_cfg.zoomResetKey)) s_zoom = 1.0f;
    s_zoom = s_zoom < 0.2f ? 0.2f : s_zoom > 3.0f ? 3.0f : s_zoom;
    static float logged = 1.0f;
    if (s_zoom != before && fabsf(s_zoom - logged) > 0.05f) {
        logged = s_zoom;
        Log("camera zoom %.2f", s_zoom);
    }
}
