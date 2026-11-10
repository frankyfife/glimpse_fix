// Split/Second PC fix - boost (X or BoostKey held), full power bar (LS+RS or PowerBarFillKey) for player 1,
// boost for the AI cars
//
// All of it works on game data reached from the in-game resources global, no code is touched:
//   [0xD66B28] cInGameResources  +0xE4 cGameLogic (vtable 0xBA9A74): +0x18 player count, +0x1C players,
//                                +0x58 local (human) players, they come first
//   player i cPlayerLogic (0xBA63D4): +0xE8 cPowerBar (0xBA8644) +0x2C output (0xBA8680), +8 power 0..100
//                                     +0xAC cVehicleState (0xBA7378) +0x40 vehicle list, +4 + i*4 vehicle i
//   vehicle +0x584 chassis hkpRigidBody (0xBE9C90): +0x100 forward axis, +0x110 position,
//                  +0x1A0 linear velocity, +0x1B0 angular velocity
// The power bar update (0x43D0F0) adds its gain to the output value and clamps it to 100, so setting the
// value is enough. A boost adds speed along the chassis' forward axis while the car is moving forward
// and the game is not paused (the chassis moved since the last frame).
#include "common.h"
#include <math.h>
#include <string.h>

static void* const* const kInGameResources = (void* const*)0x00D66B28;

static bool IsA(void* p, DWORD vtable)
{
    return p && !IsBadReadPtr(p, 4) && *(DWORD*)p == vtable;
}

static BYTE* Field(void* p, DWORD offset)
{
    BYTE* q = p ? *(BYTE**)((BYTE*)p + offset) : NULL;
    return q && !IsBadReadPtr(q, 0x200) ? q : NULL;
}

static BYTE* GameLogic()
{
    BYTE* res = (BYTE*)*kInGameResources;
    if (!res || IsBadReadPtr(res, 0x100)) return NULL;
    BYTE* logic = Field(res, 0xE4);
    return IsA(logic, 0x00BA9A74) && *(int*)(logic + 0x18) >= 1 && *(int*)(logic + 0x18) <= 16 ? logic : NULL;
}

static BYTE* Player(BYTE* logic, int i)
{
    BYTE* players = Field(logic, 0x1C);
    BYTE* p = players && !IsBadReadPtr(players, (i + 1) * 4) ? Field(players, i * 4) : NULL;
    return IsA(p, 0x00BA63D4) ? p : NULL;
}

static float* PowerValue(BYTE* player)
{
    BYTE* bar = Field(player, 0xE8);
    if (!IsA(bar, 0x00BA8644)) return NULL;
    BYTE* out = Field(bar, 0x2C);
    return IsA(out, 0x00BA8680) ? (float*)(out + 8) : NULL;
}

// chassis of car i (the vehicle list holds the cars in player order)
static BYTE* Chassis(BYTE* player, int i)
{
    BYTE* state = Field(player, 0xAC);
    if (!IsA(state, 0x00BA7378)) return NULL;
    BYTE* vehicles = Field(state, 0x40);
    BYTE* vehicle = vehicles ? Field(vehicles, 4 + i * 4) : NULL;
    BYTE* body = vehicle && !IsBadReadPtr(vehicle, 0x600) ? Field(vehicle, 0x584) : NULL;
    return IsA(body, 0x00BE9C90) && !IsBadWritePtr(body + 0x1A0, 0x20) ? body : NULL;
}

static float Dot(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// adds up to accel * dt along the chassis heading, capped at the boost top speed; false if the car is
// not driving forward
static bool Push(BYTE* body, float accel, float dt)
{
    float* fwd = (float*)(body + 0x100);
    float* vel = (float*)(body + 0x1A0);
    float speed = sqrtf(Dot(vel, vel));
    if (Dot(vel, fwd) <= 5.0f || speed >= g_cfg.boostMaxSpeed) return false;
    float dv = accel * dt;
    if (speed + dv > g_cfg.boostMaxSpeed) dv = g_cfg.boostMaxSpeed - speed;
    vel[0] += fwd[0] * dv;
    vel[1] += fwd[1] * dv;
    vel[2] += fwd[2] * dv;
    return true;
}

static WORD PadButtons()
{
    WORD buttons = 0;
    XINPUT_STATE st;
    // polling empty slots is slow, so only the ones the pad wrapper has seen connected
    for (DWORD i = 0; i < XUSER_MAX_COUNT && pXInputGetState; i++)
        if (PadSlotConnected(i) && pXInputGetState(i, &st) == ERROR_SUCCESS) buttons |= st.Gamepad.wButtons;
    return buttons;
}

// The keyboard keys count as the pad buttons they stand for, while the game window has the focus.
// Their defaults, left Shift and left Ctrl, are bound in none of the game's input presets (0xCFABF8).
static WORD KeyButtons()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return 0;
    WORD buttons = 0;
    if (g_cfg.boostKey && (GetAsyncKeyState(g_cfg.boostKey) & 0x8000)) buttons |= XINPUT_GAMEPAD_X;
    if (g_cfg.powerFillKey && (GetAsyncKeyState(g_cfg.powerFillKey) & 0x8000))
        buttons |= XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
    return buttons;
}

// ---------------------------------------------------------------- AI boost (experimental)

// Each AI car has a boost tank and boosts in bursts while it is behind player 1 or not far ahead, so
// it closes gaps rather than running away. AIBoostLevel sets how much and how often; with
// AIBoostStraightOnly it only boosts on a straight at speed (little yaw rate, fast forward motion),
// AIBoostUnlimited never empties the tank. AIBoostChaos is level 3, unlimited, anywhere, and also
// while far ahead of player 1.
struct AiBoostLevel {
    float strength;            // multiple of BoostAcceleration
    float tank;                // seconds of boost per full tank
    float refill;              // seconds to refill an empty tank
    float rate;                // bursts started per second of eligible driving (on average)
    float burstMin, burstMax;  // burst length, s
    float maxAhead;            // no boost when further ahead of player 1 than this, m
};
static const AiBoostLevel kAiLevels[3] = {
    { 0.8f, 3.0f, 20.0f, 0.5f, 0.5f, 1.5f, 30.0f },   // 1 easy
    { 1.0f, 5.0f, 15.0f, 1.0f, 1.0f, 2.5f, 60.0f },   // 2 normal
    { 1.3f, 8.0f, 10.0f, 2.0f, 1.5f, 3.5f, 150.0f },  // 3 hard
};

struct AiBoostState {
    BYTE* body;
    float tank;   // seconds of boost left
    float burst;  // seconds left in the current burst
    float lastPos[3];
};
static AiBoostState s_ai[16];

static unsigned Random()
{
    static unsigned seed = GetTickCount();
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

// Chaos mode also gives the AI an endless power bar: the game's own cheat switch
// "SSAI/Debug Cheats/AI Full Power Bar" (0xD72534), read by every AI player update (0x51AC16).
static BYTE* const kAiFullPowerBar = (BYTE*)0x00D72534;

static void AiBoost(BYTE* logic, BYTE* player0, BYTE* body0, float dt)
{
    if (g_cfg.aiBoostChaos) *kAiFullPowerBar = 1;
    int count = *(int*)(logic + 0x18);
    int humans = *(int*)(logic + 0x58);
    if (humans < 1 || humans >= count) humans = 1;
    const AiBoostLevel& lv = kAiLevels[g_cfg.aiBoostLevel - 1];
    for (int i = humans; i < count && i < 16; i++) {
        AiBoostState& a = s_ai[i];
        BYTE* body = Chassis(player0, i);
        if (!body) continue;
        if (body != a.body) {  // new race or new car
            memset(&a, 0, sizeof(a));
            a.body = body;
            a.tank = lv.tank;
        }
        float* pos = (float*)(body + 0x110);
        bool moved = pos[0] != a.lastPos[0] || pos[1] != a.lastPos[1] || pos[2] != a.lastPos[2];
        memcpy(a.lastPos, pos, sizeof(a.lastPos));
        if (!moved) continue;  // paused
        a.tank += lv.tank / lv.refill * dt;
        if (a.tank > lv.tank || g_cfg.aiBoostUnlimited) a.tank = lv.tank;

        float* fwd = (float*)(body + 0x100);
        float* vel = (float*)(body + 0x1A0);
        float* spin = (float*)(body + 0x1B0);
        float forward = Dot(vel, fwd);
        bool drivable = g_cfg.aiBoostStraightOnly ? fabsf(spin[1]) < 0.15f && forward > 30.0f : forward > 15.0f;
        float ahead = 0.0f;  // how far the AI car is ahead of player 1 along its own heading
        if (body0) {
            float* p0 = (float*)(body0 + 0x110);
            float d[3] = { pos[0] - p0[0], pos[1] - p0[1], pos[2] - p0[2] };
            ahead = Dot(d, fwd);
        }
        bool wanted = drivable && (ahead < lv.maxAhead || g_cfg.aiBoostChaos);
        if (a.burst > 0.0f) {
            if (!wanted || a.tank <= 0.0f) {
                a.burst = 0.0f;
                continue;
            }
        } else {
            if (!wanted || a.tank < 0.5f || (Random() % 1000) >= (unsigned)(dt * lv.rate * 1000.0f)) continue;
            a.burst = lv.burstMin + (lv.burstMax - lv.burstMin) * (Random() % 1000) / 1000.0f;
            if (g_cfg.log) Log("ai boost: car %d, %.1f s, tank %.1f s, %.0f m ahead", i, a.burst, a.tank, ahead);
        }
        if (Push(body, g_cfg.boostAcceleration * lv.strength, dt)) a.tank -= dt;
        a.burst -= dt;
    }
}

// ---------------------------------------------------------------- per frame

static float s_boostBlend;  // 0..1, eases the motion blur in and out

float BoostBlurScale()
{
    return 1.0f + (g_cfg.boostMotionBlur - 1.0f) * s_boostBlend;
}

void BoostTick()
{
    static DWORD lastTick;
    static WORD lastButtons;
    static float lastPos[3];
    DWORD now = GetTickCount();
    float dt = lastTick ? (now - lastTick) / 1000.0f : 0.0f;
    lastTick = now;
    if (dt > 0.1f) dt = 0.1f;

    // the photo mode drives the camera with the pad, Shift and Ctrl: no boost or power bar meanwhile
    WORD buttons = CameraBlocksPad() ? 0 : PadButtons() | KeyButtons();
    WORD pressed = buttons & ~lastButtons;
    lastButtons = buttons;
    bool boosting = false;

    __try {
        BYTE* logic = GameLogic();
        BYTE* player = logic ? Player(logic, 0) : NULL;
        if (player) {
            const WORD fill = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
            if (g_cfg.powerFill && (buttons & fill) == fill && (pressed & fill)) {
                if (float* power = PowerValue(player)) {
                    *power = 100.0f;
                    Log("power bar filled");
                }
            }
            BYTE* body = Chassis(player, 0);
            if (body && g_cfg.boost) {
                float* pos = (float*)(body + 0x110);
                bool moved = pos[0] != lastPos[0] || pos[1] != lastPos[1] || pos[2] != lastPos[2];
                memcpy(lastPos, pos, sizeof(lastPos));
                if ((buttons & XINPUT_GAMEPAD_X) && moved) boosting = Push(body, g_cfg.boostAcceleration, dt);
            }
            if (g_cfg.aiBoost && (g_cfg.patchMask & PATCH_AIBOOST)) AiBoost(logic, player, body, dt);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        boosting = false;
    }

    // blur reaches full strength in 0.25 s and fades out over 0.5 s
    if (boosting) s_boostBlend = s_boostBlend + dt * 4.0f > 1.0f ? 1.0f : s_boostBlend + dt * 4.0f;
    else s_boostBlend = s_boostBlend - dt * 2.0f < 0.0f ? 0.0f : s_boostBlend - dt * 2.0f;
}
