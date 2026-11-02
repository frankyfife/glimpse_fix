// Split/Second PC fix - 60 fps.
//
// The game simulates in fixed 1/60 s ticks but its main loop (0x558880) renders every 1/30 s and
// passes two ticks to each frame: it adds the measured frame time to an accumulator (0xD747F4), takes
// two ticks per 1/30 s in it, sleeps away the rest and clamps the ticks to "max ticks" (0xD199D8, 10).
// Patching the loop's code sets off SecuROM's integrity checks (about 6.5 minutes after launch), so
// the loop is driven through its own variables instead, all of them read only by the loop:
//  - after every presented frame we wait for the next 1/60 s ourselves (high resolution timer),
//  - "max ticks" is set to the number of 1/60 s steps that have passed (1, more after a slow frame),
//  - the accumulator is topped up so the loop always finds at least that many ticks and never sleeps,
//  - its tick counter (0xD747F0, Sleep(5) once it reaches 5) is reset.
// Physics, Havok and race logic keep their exact step. The AI player update (cAIPlayer, vtable slot
// 0xBC0BC0) assumes a 1/30 s frame (its steering controller 0x524AC0 limits the steering change per
// call by 1/30 s), so it keeps its 30 Hz rhythm: each AI car is updated every other frame and drives
// on its last inputs in between, exactly as the game does at 30 fps with two ticks per update.
#include "common.h"
#include <stdlib.h>
#include <string.h>

static int* const kMaxTicks = (int*)0x00D199D8;       // tweakable, read once per loop pass
static float* const kAccumulator = (float*)0x00D747F4;
static int* const kTickCounter = (int*)0x00D747F0;
static const float kGamePeriod = 1.0f / 30;          // [0xBA0C14], left as it is
static void** const kAiUpdateSlot = (void**)0x00BC0BC0;
static BYTE* const kAiUpdate = (BYTE*)0x0051A8B0;

static bool s_active;

static bool Matches(DWORD at, const char* hex)
{
    const BYTE* p = (const BYTE*)at;
    for (; hex[0] && hex[1]; hex += 2, p++) {
        char b[3] = { hex[0], hex[1], 0 };
        if (*p != (BYTE)strtoul(b, NULL, 16)) return false;
    }
    return true;
}

// waits until the performance counter reaches 'until': high resolution waitable timer, then a short spin
static void WaitUntil(LONGLONG until, LONGLONG freq)
{
    static HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                                 TIMER_ALL_ACCESS);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG left = until - now.QuadPart;
    if (left > freq / 1000 && timer) {
        LARGE_INTEGER due;
        due.QuadPart = -(left * 10000000 / freq - 10000);  // relative, 100 ns units, 1 ms early
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
    }
    do {
        YieldProcessor();
        QueryPerformanceCounter(&now);
    } while (now.QuadPart < until);
}

// called after every Present, i.e. once per main loop pass
void FramePace()
{
    if (!s_active) return;
    static LARGE_INTEGER freq;
    static LONGLONG last, sim;  // time of the last pass, simulated time not yet handed out (counter units)
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    const LONGLONG step = freq.QuadPart / 60;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (last && now.QuadPart < last + step) {
        WaitUntil(last + step, freq.QuadPart);
        QueryPerformanceCounter(&now);
    }
    LONGLONG elapsed = last ? now.QuadPart - last : step;
    last = now.QuadPart;
    if (elapsed > freq.QuadPart / 4) elapsed = step;  // loading or a stall: don't catch up
    sim += elapsed;
    int ticks = (int)(sim / step);
    if (ticks < 1) ticks = 1;
    if (ticks > 4) ticks = 4;
    sim -= ticks * step;
    if (sim < 0) sim = 0;
    if (sim > step) sim = step;

    *kMaxTicks = ticks;
    *kAccumulator = kGamePeriod * ((ticks + 1) / 2);
    *kTickCounter = 0;
}

typedef void(__thiscall* AiUpdate_t)(void* self, void* a, void* b);
static AiUpdate_t s_origAiUpdate;

// An AI car's track cursor, read by its update and by another cAIPlayer method (vtable 0xBC0BF8, every
// pass): [[[ai+0x18]]+0x28]+0x50, +4 = current track section. Restarting a race sets the section to NULL;
// the next update finds it again. That update must not be the skipped one, or the other method reads
// [NULL+4] and the game crashes (0xA463B6).
static bool TrackSectionMissing(void* ai)
{
    BYTE* p = *(BYTE**)((BYTE*)ai + 0x18);
    if (!p || IsBadReadPtr(p, 4) || !(p = *(BYTE**)p) || IsBadReadPtr(p + 0x28, 4)) return false;
    p = *(BYTE**)(p + 0x28);
    if (!p || IsBadReadPtr(p + 0x54, 4)) return false;
    return *(void**)(p + 0x54) == NULL;  // cursor at +0x50, section at cursor +4
}

static void __fastcall Hook_AiUpdate(void* self, void*, void* a, void* b)
{
    // per AI car: run every other call
    static void* cars[16];
    static unsigned skip;  // bit i: car i skips its next call
    int i = 0;
    while (i < 16 && cars[i] && cars[i] != self) i++;
    if (i == 16) {  // a new race: forget the old cars
        memset(cars, 0, sizeof(cars));
        skip = 0;
        i = 0;
    }
    cars[i] = self;
    unsigned bit = 1u << i;
    skip ^= bit;
    if (!(skip & bit)) {
        if (!TrackSectionMissing(self)) return;
        skip |= bit;  // race restarted: run now, the rhythm continues from here
    }
    s_origAiUpdate(self, a, b);
}

void PatchFrameRate()
{
    if (g_cfg.frameRate != 60) return;
    // the loop must be the one described above
    if (!Matches(0x005588A9, "8B2DD899D100") || !Matches(0x005588B4, "F30F10052023D700F30F5805F447D700") ||
        !Matches(0x005588E1, "A1F047D70083C602") || !Matches(0x00558969, "83F8057C04")) {
        Log("main loop not recognised, frame rate left at 30");
        return;
    }
    if (!(g_cfg.patchMask & PATCH_D3D)) {
        Log("60 fps needs the Direct3D hooks, frame rate left at 30");
        return;
    }
    if (*kAiUpdateSlot != kAiUpdate || !Matches((DWORD)kAiUpdate, "83EC1053")) {
        Log("AI update not recognised, frame rate left at 30");
        return;
    }
    s_origAiUpdate = (AiUpdate_t)kAiUpdate;
    void* hook = (void*)Hook_AiUpdate;
    WriteMemory(kAiUpdateSlot, &hook, sizeof(hook));
    s_active = true;
    Log("60 fps: main loop paced after Present, AI updates at 30 Hz");
}
