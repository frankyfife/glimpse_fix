// Split/Second controller fix - FMOD event logging (Log=1 only): name and result of every
// Event::start, to see which sounds and music the game asks for and what FMOD answers
#include "common.h"
#include <string.h>

typedef int(__stdcall* EventStart_t)(void* evt);
typedef int(__stdcall* EventGetInfo_t)(void* evt, int* index, char** name, void* info);

static void** const kIatEventStart = (void**)0x00B9B43C;    // ?start@Event@FMOD@@QAG?AW4FMOD_RESULT@@XZ

typedef int(__stdcall* GetU32_t)(void* obj, unsigned* out);
typedef int(__stdcall* GetFloat_t)(void* obj, float* out);
typedef int(__stdcall* GetPtr_t)(void* obj, void** out);
typedef int(__stdcall* CatGetInfo_t)(void* cat, int* index, char** name);

static EventStart_t s_origStart;
static EventGetInfo_t s_getInfo;
static GetU32_t s_getState;
static GetFloat_t s_getVolume, s_catGetVolume;
static GetPtr_t s_getCategory, s_catGetParent;
static CatGetInfo_t s_catGetInfo;

// music events get a state report a moment after they started
struct Tracked {
    void* evt;
    DWORD started;
    char name[48];
};
static Tracked s_tracked[8];

static bool IsMusic(const char* n)
{
    static const char* const prefixes[] = { "Music", "RaceSection", "PreRace", "BuildUp", "RACE", "Load_loop" };
    for (const char* p : prefixes)
        if (n && !strncmp(n, p, strlen(p))) return true;
    return false;
}

void AudioDiagTick()
{
    DWORD now = GetTickCount();
    for (Tracked& t : s_tracked) {
        if (!t.evt || now - t.started < 1500) continue;
        unsigned state = 0;
        float vol = -1, catVol = -1, parentVol = -1;
        void *cat = NULL, *parent = NULL;
        char *catName = NULL, *parentName = NULL;
        int idx;
        int rs = s_getState ? s_getState(t.evt, &state) : -1;
        if (s_getVolume) s_getVolume(t.evt, &vol);
        if (s_getCategory && s_getCategory(t.evt, &cat) == 0 && cat) {
            if (s_catGetVolume) s_catGetVolume(cat, &catVol);
            if (s_catGetInfo) s_catGetInfo(cat, &idx, &catName);
            if (s_catGetParent && s_catGetParent(cat, &parent) == 0 && parent) {
                if (s_catGetVolume) s_catGetVolume(parent, &parentVol);
                if (s_catGetInfo) s_catGetInfo(parent, &idx, &parentName);
            }
        }
        Log("fmod music '%s': getState=%d state=%#x volume=%.2f category '%s'=%.2f parent '%s'=%.2f", t.name, rs, state, vol,
            catName ? catName : "?", catVol, parentName ? parentName : "?", parentVol);
        t.evt = NULL;
    }
}

static int __stdcall Hook_EventStart(void* evt)
{
    int r = s_origStart(evt);
    char* name = NULL;
    int index = -1;
    if (s_getInfo) s_getInfo(evt, &index, &name, NULL);
    if (IsMusic(name)) {
        for (Tracked& t : s_tracked) {
            if (t.evt) continue;
            t.evt = evt;
            t.started = GetTickCount();
            lstrcpynA(t.name, name, sizeof(t.name));
            break;
        }
    }
    // engine and tyre loops restart constantly; log each name only until it has started 3 times
    static struct { char name[48]; int count; } seen[256];
    static int nseen;
    int i = 0;
    for (; i < nseen; i++)
        if (name && !strncmp(seen[i].name, name, sizeof(seen[i].name) - 1)) break;
    if (i == nseen && nseen < 256) {
        lstrcpynA(seen[nseen].name, name ? name : "?", sizeof(seen[0].name));
        seen[nseen++].count = 0;
    }
    if ((i < 256 && seen[i].count++ < 3) || r != 0)
        Log("fmod event start '%s' (#%d) -> %d", name ? name : "?", index, r);
    return r;
}

void InstallAudioDiagnostics()
{
    HMODULE ev = GetModuleHandleA("fmod_event.dll");
    if (!ev) return;
    void* start = (void*)GetProcAddress(ev, "?start@Event@FMOD@@QAG?AW4FMOD_RESULT@@XZ");
    void* info = (void*)GetProcAddress(ev, "?getInfo@Event@FMOD@@QAG?AW4FMOD_RESULT@@PAHPAPADPAUFMOD_EVENT_INFO@@@Z");
    if (!start || *kIatEventStart != start) {
        Log("fmod Event::start import not found");
        return;
    }
    s_origStart = (EventStart_t)start;
    s_getInfo = (EventGetInfo_t)info;
    s_getState = (GetU32_t)GetProcAddress(ev, "?getState@Event@FMOD@@QAG?AW4FMOD_RESULT@@PAI@Z");
    s_getVolume = (GetFloat_t)GetProcAddress(ev, "?getVolume@Event@FMOD@@QAG?AW4FMOD_RESULT@@PAM@Z");
    s_getCategory = (GetPtr_t)GetProcAddress(ev, "?getCategory@Event@FMOD@@QAG?AW4FMOD_RESULT@@PAPAVEventCategory@2@@Z");
    s_catGetVolume = (GetFloat_t)GetProcAddress(ev, "?getVolume@EventCategoryI@FMOD@@UAG?AW4FMOD_RESULT@@PAM@Z");
    s_catGetInfo = (CatGetInfo_t)GetProcAddress(ev, "?getInfo@EventCategoryI@FMOD@@UAG?AW4FMOD_RESULT@@PAHPAPAD@Z");
    s_catGetParent = (GetPtr_t)GetProcAddress(ev, "?getParentCategory@EventCategoryI@FMOD@@UAG?AW4FMOD_RESULT@@PAPAV12@@Z");
    Log("fmod exports: state=%p vol=%p cat=%p catvol=%p catinfo=%p parent=%p", s_getState, s_getVolume, s_getCategory,
        s_catGetVolume, s_catGetInfo, s_catGetParent);
    void* hook = (void*)Hook_EventStart;
    DWORD old;
    VirtualProtect(kIatEventStart, 4, PAGE_READWRITE, &old);
    *kIatEventStart = hook;
    VirtualProtect(kIatEventStart, 4, old, &old);
    Log("fmod event logging on");
}
