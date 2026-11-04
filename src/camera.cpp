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
// Photo mode, during a race: the game's slow motion tweakables, "Physics/Change Phys dt/Slow motion
// activated" (0xD71208) and "Slow Motion cheat speed" (0xD113B8, scales the simulation step), hold
// the world almost still (speed 0.0001) while the cameras keep running in real time. The race camera's
// state then gets a free position and direction (the same hook as the FOV), and the HUD's two render
// tweakables, "UI/HUD/3D Components/Render Powerbar" (0xD1BC94) and "Render Infobar" (0xD1BC9C), are
// switched off. The race messages and power play prompts are Scaleform, player 1's HUD movie
// Resident|UI/HUD/Hud.gfx: UI layer 0x3B (created at 0x55A5A0), [[0xD64E0C]+8][0x3B], a HUD movie object
// (vtable 0xBD0E1C) whose [+0x68]+4 is the GFx movie view; its _root._visible is set through the view's
// own SetVariable, the way the prompt bar icons are set (game.cpp). The pad drives the camera and the game sees it idle, and the game's sound is paused
// (audio_diag.cpp). Leaving restores all of it.
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
static DWORD s_raceCamera;        // tick of the last race camera update (0: none yet)
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

// the game's values of each changed state (+4 to +0x4C: position, axes, both FOVs) and what the hook
// wrote over them
enum { kPose = 4, kPoseLen = 0x48 };
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

static void ScaleFov(BYTE* state, float factor, bool ultrawide)
{
    float* fov = (float*)(state + 0x44);
    float stock = *fov;
    if (!(stock > 0.01f && stock < 3.0f)) return;
    float f = stock * factor;
    if (ultrawide && g_cfg.ultrawideFix && Viewports() == 1) {
        float wide = ScreenAspect() / (16.0f / 9);
        if (wide > 1.01f) f = 2.0f * atanf(tanf(f * 0.5f) * wide);
    }
    f = f < 0.05f ? 0.05f : f > 2.97f ? 2.97f : f;
    if (f == stock) return;
    fov[0] = f;
    fov[1] *= f / stock;
}

// after the update: FOV of the race cameras, the free pose in photo mode
static void ChangeState(BYTE* self, bool raceCamera)
{
    BYTE* state = State(self);
    if (!state) return;
    BYTE stock[kPoseLen];
    memcpy(stock, state + kPose, kPoseLen);
    if (raceCamera) ScaleFov(state, g_cfg.fov * s_zoom, true);
    if (s_photo) {
        float* pos = (float*)(state + 4);
        if (!s_photoPose) {
            memcpy(s_pos, pos, sizeof(s_pos));
            for (int i = 0; i < 3; i++) memcpy(s_axis[i], state + 0x14 + i * 0x10, sizeof(s_axis[i]));
            s_photoPose = true;
        }
        memcpy(pos, s_pos, sizeof(s_pos));
        for (int i = 0; i < 3; i++) memcpy(state + 0x14 + i * 0x10, s_axis[i], sizeof(s_axis[i]));
        ScaleFov(state, s_photoZoom, !raceCamera);
    }
    if (!memcmp(stock, state + kPose, kPoseLen)) return;
    Saved* r = Record(state, true);
    memcpy(r->stock, stock, kPoseLen);
    memcpy(r->set, state + kPose, kPoseLen);
}

static void __fastcall Hook_ChaseUpdate(BYTE* self, void*, void* frame)
{
    s_raceCamera = GetTickCount() | 1;
    RestoreState(self);
    s_origChase(self, frame);
    ChangeState(self, true);
}

static void __fastcall Hook_BumperUpdate(BYTE* self, void*, void* frame)
{
    s_raceCamera = GetTickCount() | 1;
    RestoreState(self);
    s_origBumper(self, frame);
    ChangeState(self, true);
}

static bool HookSlot(void** slot, BYTE* fn, const char* prologue, int len, void* hook, void** orig)
{
    if (*slot != fn || memcmp(fn, prologue, len)) return false;
    *orig = fn;
    WriteMemory(slot, &hook, sizeof(hook));
    return true;
}

void PatchCamera()
{
    bool race = g_cfg.fov != 1.0f || g_cfg.ultrawideFix || g_cfg.zoomKeys || g_cfg.photoMode;
    if (race) {
        bool chase = HookSlot(kChaseUpdateSlot, kChaseUpdate, "\x81\xEC\x44\x01\x00\x00", 6, (void*)Hook_ChaseUpdate,
                              (void**)&s_origChase);
        bool bumper = HookSlot(kBumperUpdateSlot, kBumperUpdate, "\x81\xEC\xEC\x00\x00\x00", 6, (void*)Hook_BumperUpdate,
                               (void**)&s_origBumper);
        Log("camera fov: chase %s, bumper %s, factor %.2f, ultrawide %d, zoom keys %d", chase ? "hooked" : "not recognised",
            bumper ? "hooked" : "not recognised", g_cfg.fov, g_cfg.ultrawideFix, g_cfg.zoomKeys);
    }
    if (g_cfg.photoMode) Log("photo mode: key 0x%02X and the pad's Back button during a race", g_cfg.photoModeKey);
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

static void* HudMovie()
{
    __try {
        BYTE* ui = *(BYTE**)0x00D64E0C;
        BYTE** slots = ui ? *(BYTE***)(ui + 8) : NULL;
        BYTE* hud = slots ? slots[0x3B] : NULL;
        if (!hud || *(DWORD*)hud != 0x00BD0E1C || *(int*)(hud + 4) != 0x3B) return NULL;
        BYTE* holder = *(BYTE**)(hud + 0x68);
        return holder ? *(void**)(holder + 4) : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

// the race messages and power play prompts (Hud.gfx)
static void SetHudMessages(bool visible)
{
    void* movie = HudMovie();
    if (!movie) {
        Log("photo mode: HUD movie not found");
        return;
    }
    GFxSetVariable_t setVariable = (GFxSetVariable_t)(*(void***)movie)[0x2C / 4];
    GFxValue value = {};
    value.type = GFX_NUMBER;
    value.num = visible ? 1.0 : 0.0;
    setVariable(movie, "_root._visible", &value, 0);
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

static void SetPhoto(bool on, const char* why)
{
    if (on == s_photo) return;
    Log("photo mode %s (%s)", on ? "on" : "off", why);
    s_photo = on;
    if (on) {
        s_photoPose = false;
        s_photoZoom = 1.0f;
        s_slowOn = *kSlowMotionOn;
        s_slowSpeed = *kSlowMotionSpeed;
        *kSlowMotionSpeed = 0.0001f;
        *kSlowMotionOn = 1;
        SetHud(false);
        SetHudMessages(false);
        PauseGameSound(true);
    } else {
        *kSlowMotionOn = s_slowOn;
        *kSlowMotionSpeed = s_slowSpeed;
        PauseGameSound(false);
        s_padHeld = true;
        SetHud(!s_hudHidden);
        SetHudMessages(true);
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
        bool race = InRace();
        static bool lastKey, lastBack, lastB;
        bool key = Pressed(focus && KeyDown(g_cfg.photoModeKey), lastKey);
        bool back = Pressed(padOk && (pad.wButtons & XINPUT_GAMEPAD_BACK), lastBack);
        bool b = Pressed(padOk && (pad.wButtons & XINPUT_GAMEPAD_B), lastB);
        if (s_photo) {
            if (key || back || b || !race) SetPhoto(false, key ? "key" : back ? "back" : b ? "b" : "race camera gone");
            else MovePhoto(dt, pad, padOk);
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
