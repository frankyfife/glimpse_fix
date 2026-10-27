// Split/Second controller fix - call counters for timing research (Profile=1 only)
#include "common.h"
#include <string.h>

struct CountSite {
    DWORD addr;
    int prologue;     // bytes moved into the stub, whole instructions without relative operands
    const char* name;
    volatile LONG count;
};

static CountSite s_sites[] = {
    { 0x00558880, 5, "frame", 0 },
    { 0x00417940, 6, "race update", 0 },
    { 0x004B4F90, 7, "sim update", 0 },
    { 0x0051A8B0, 8, "ai driver", 0 },
    { 0x00524AC0, 5, "ai steer 524ac0", 0 },
    { 0x00528D10, 8, "ai 528d10", 0 },
    { 0x00529000, 6, "ai 529000", 0 },
    { 0x00859EF0, 6, "859ef0", 0 },
    { 0x00833A60, 6, "833a60", 0 },
};

void InstallProfiler()
{
    BYTE* stubs = (BYTE*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stubs) return;
    for (CountSite& s : s_sites) {
        BYTE* at = (BYTE*)s.addr;
        BYTE* p = stubs;
        p[0] = 0xF0; p[1] = 0xFF; p[2] = 0x05;  // lock inc dword ptr [count]
        *(volatile LONG**)(p + 3) = &s.count;
        p += 7;
        memcpy(p, at, s.prologue);
        p += s.prologue;
        p[0] = 0xE9;
        *(DWORD*)(p + 1) = (DWORD)(at + s.prologue) - (DWORD)(p + 5);
        p += 5;

        BYTE jmp[16];
        memset(jmp, 0x90, sizeof(jmp));
        jmp[0] = 0xE9;
        *(DWORD*)(jmp + 1) = (DWORD)stubs - (DWORD)(at + 5);
        DWORD old;
        VirtualProtect(at, s.prologue, PAGE_EXECUTE_READWRITE, &old);
        memcpy(at, jmp, s.prologue);
        VirtualProtect(at, s.prologue, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, s.prologue);
        stubs = p;
    }
    Log("profiler installed");
}

// called once per input frame; dumps and resets the counters every 300 frames
void ProfilerTick()
{
    static int frames;
    if (++frames < 300) return;
    frames = 0;
    char line[512];
    int n = 0;
    for (CountSite& s : s_sites) n += wsprintfA(line + n, "%s=%ld ", s.name, InterlockedExchange(&s.count, 0));
    Log("per 300 input frames: %s", line);
}
