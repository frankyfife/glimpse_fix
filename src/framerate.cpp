// Split/Second controller fix - 60 fps.
//
// The game simulates in fixed 1/60 s ticks but renders every 1/30 s and passes two ticks per
// frame (main loop 0x558880). For 60 fps the loop gets a 1/60 s period and one tick per frame, so
// physics, Havok and race logic keep their exact step. The only per-frame code that assumes a
// 1/30 s frame is the AI steering controller (0x524AC0, once per car per frame); it gets the new
// frame time. The other users of the shared 1/30 constant are look-ahead grids and data scales.
#include "common.h"
#include <stdlib.h>
#include <string.h>

static float s_framePeriod = 1.0f / 60;  // read by the patched instructions
static float s_frameDt = 1.0f / 60;
static void* s_sleepFn;                  // replaces the Sleep import the limiter calls through

struct OperandPatch {
    DWORD instr;       // instruction address
    const char* bytes; // expected opcode bytes, hex, up to the operand
};

// instructions reading [0xBA0C14] (1/30) that become per-frame values
static const OperandPatch kLoopPeriod[] = {
    { 0x005588C4, "F30F100D" },  // movss xmm1, [period]   accumulator compare
    { 0x00558906, "D905" },      // fld [period]           sleep length
    { 0x0055894F, "F30F5C05" },  // subss xmm0, [period]   accumulator after sleeping
};
static const OperandPatch kAiFrameDt[] = {
    { 0x00524B17, "D905" },      // fld [dt]
    { 0x00524B7F, "F30F590D" },  // mulss xmm1, [dt]  max steering change per frame
    { 0x00524C66, "D905" },      // fld [dt]
};

static bool Matches(DWORD at, const char* hex)
{
    const BYTE* p = (const BYTE*)at;
    for (; hex[0] && hex[1]; hex += 2, p++) {
        char b[3] = { hex[0], hex[1], 0 };
        if (*p != (BYTE)strtoul(b, NULL, 16)) return false;
    }
    return true;
}

static void Write(DWORD at, const void* src, size_t len)
{
    DWORD old;
    VirtualProtect((void*)at, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)at, src, len);
    VirtualProtect((void*)at, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, len);
}

static bool PatchOperand(const OperandPatch& p, const void* target)
{
    size_t n = strlen(p.bytes) / 2;
    if (!Matches(p.instr, p.bytes) || *(DWORD*)(p.instr + n) != 0x00BA0C14) return false;
    DWORD addr = (DWORD)target;
    Write(p.instr + n, &addr, 4);
    return true;
}

// Sleep(ms) with sub-millisecond accuracy: high resolution waitable timer, then a short spin
static void WINAPI PreciseSleep(DWORD ms)
{
    static LARGE_INTEGER freq;
    static HANDLE timer;
    if (!freq.QuadPart) {
        QueryPerformanceFrequency(&freq);
        timer = CreateWaitableTimerExW(NULL, NULL, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    }
    LARGE_INTEGER start, now;
    QueryPerformanceCounter(&start);
    LONGLONG end = start.QuadPart + freq.QuadPart * ms / 1000;
    if (ms > 1 && timer) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(ms - 1) * 10000;  // relative, 100 ns units
        if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
    } else if (ms > 1) {
        Sleep(ms - 1);
    }
    do {
        YieldProcessor();
        QueryPerformanceCounter(&now);
    } while (now.QuadPart < end);
}

void PatchFrameRate()
{
    if (g_cfg.frameRate != 60) return;
    // main loop: period, ticks per period, and the limiter's Sleep
    if (!Matches(0x005588E6, "83C602") || !Matches(0x0055895C, "BE02000000") || !Matches(0x005588CC, "8B1D20B1B900")) {
        Log("main loop not recognised, frame rate left at 30");
        return;
    }
    for (const OperandPatch& p : kLoopPeriod) {
        if (!Matches(p.instr, p.bytes)) {
            Log("main loop not recognised, frame rate left at 30");
            return;
        }
    }
    for (const OperandPatch& p : kLoopPeriod) PatchOperand(p, &s_framePeriod);
    static const BYTE addOne[3] = { 0x83, 0xC6, 0x01 };
    static const BYTE movOne[5] = { 0xBE, 0x01, 0x00, 0x00, 0x00 };
    Write(0x005588E6, addOne, sizeof(addOne));
    Write(0x0055895C, movOne, sizeof(movOne));
    s_sleepFn = (void*)PreciseSleep;
    DWORD slot = (DWORD)&s_sleepFn;
    Write(0x005588CC + 2, &slot, 4);

    int ai = 0;
    for (const OperandPatch& p : kAiFrameDt) ai += PatchOperand(p, &s_frameDt);
    Log("60 fps: main loop patched, %d/3 AI frame steps", ai);
}
