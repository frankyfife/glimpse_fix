// The game's calls into its Scaleform menus (Invoke and SetVariable on the GFx movie view, vtable 0xC41CF4
// slots 18 and 11). Only the vtable slots are swapped, no game code. Used by the lap choice on the track
// select (lap_menu.cpp) and, with UiLog=1 (research), logged with the movie's file name so a screen's state
// and texts can be matched to the game's code.
#include "common.h"
#include <stdio.h>

static GFxInvoke_t s_invoke;
static GFxSetVariable_t s_setVariable;
static int s_lines;
static bool s_installed;

bool MenuHookInstalled()
{
    return s_installed;
}

// for the lap menu: the original calls, so it can set its own text without passing through the hook
bool UiSetText(void* movie, const char* path, const char* text)
{
    if (!s_setVariable || !movie) return false;
    GFxValue v = {};
    v.type = GFX_STRING;
    v.str = text;
    return s_setVariable(movie, path, &v, 1);
}

static void Describe(const GFxValue* v, char* out, size_t size)
{
    __try {
        switch (v->type & 0x0F) {
        case 0: strcpy_s(out, size, "undefined"); break;
        case 1: strcpy_s(out, size, "null"); break;
        case GFX_BOOL: strcpy_s(out, size, v->boolean ? "true" : "false"); break;
        case GFX_NUMBER: _snprintf_s(out, size, _TRUNCATE, "%g", v->num); break;
        case GFX_STRING: _snprintf_s(out, size, _TRUNCATE, "\"%.80s\"", v->str ? v->str : ""); break;
        case 5: _snprintf_s(out, size, _TRUNCATE, "L\"%.80ls\"", v->str ? (const wchar_t*)v->str : L""); break;
        default: _snprintf_s(out, size, _TRUNCATE, "type %lu", v->type); break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        strcpy_s(out, size, "?");
    }
}

// a call with the same arguments as the last one logged for that movie, kind and path is skipped
static void Note(void* movie, const char* how, const char* path, const GFxValue* args, unsigned count)
{
    if (!g_cfg.uiLog || !s_installed || s_lines >= 4000 || !path || IsBadStringPtrA(path, 256)) return;
    char text[512], one[128], name[48];
    text[0] = 0;
    for (unsigned i = 0; args && i < count && i < 6; i++) {
        Describe(&args[i], one, sizeof one);
        if (i) strncat_s(text, ", ", _TRUNCATE);
        strncat_s(text, one, _TRUNCATE);
    }
    static struct { DWORD hash; DWORD args; } seen[512];
    DWORD h = 2166136261u, a = 2166136261u;
    for (const char* c = how; *c; c++) h = (h ^ (BYTE)*c) * 16777619u;
    for (const char* c = path; *c; c++) h = (h ^ (BYTE)*c) * 16777619u;
    h ^= (DWORD)(UINT_PTR)movie;
    for (const char* c = text; *c; c++) a = (a ^ (BYTE)*c) * 16777619u;
    auto& s = seen[h % 512];
    if (s.hash == h && s.args == a) return;
    s.hash = h;
    s.args = a;
    if (!ReadMovieName(movie, name, sizeof name)) strcpy_s(name, "?");
    s_lines++;
    Log("ui %s %s %s(%s)", name, how, path, text);
}

static bool __fastcall Hook_Invoke(void* movie, void*, const char* path, GFxValue* result, const GFxValue* args, unsigned count)
{
    GFxValue mine[8];
    __try {
        if (DlcTrackBestArgs(movie, path, args, count, mine)) args = mine;
        Note(movie, "invoke", path, args, count);
        LapMenuInvoke(movie, path, args, count);
        if (args != mine && LapMenuInvokeArgs(movie, path, args, count, mine)) args = mine;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return s_invoke(movie, path, result, args, count);
}

static bool __fastcall Hook_SetVariable(void* movie, void*, const char* path, const GFxValue* value, int setType)
{
    GFxValue mine;
    const char* text = NULL;
    bool substituted = false;
    __try {
        Note(movie, "set", path, value, value ? 1 : 0);
        const char* label = LapMenuLabel(movie, path, value);
        if (label) {
            mine = *value;
            mine.str = label;
            value = &mine;
            text = label;
            substituted = true;
        } else if (value && value->type == GFX_NUMBER) {
            mine = *value;
            if (LapMenuButtonX(movie, path, &mine.num)) value = &mine;
        } else if (value && value->type == GFX_STRING) {
            text = value->str;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    bool ok = s_setVariable(movie, path, value, setType);
    __try {
        if (text) LapMenuButtonTextSet(movie, path, text, substituted, ok);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return ok;
}

static void** const kMovieVtable = (void**)0x00C41CF4;

// With SteamLAN, online.cpp's Invoke wrapper holds slot 18 already (it is installed first, game.cpp): it is
// wrapped in turn, so both see every call (ours first, then online.cpp's, then the game's).
bool MenuHookReady()
{
    if (CurrentBuild() != BUILD_STEAM) return false;
    if (s_installed) return true;
    __try {
        const DWORD invoke = (DWORD)kMovieVtable[18];
        const void* online = OnlineInvokeHook();
        return ClassIs(kMovieVtable, 0x00CA2478, ".?AVGFxMovieRoot@@") &&
               (invoke == 0x0098D2E0 || (online && invoke == (DWORD)online)) && (DWORD)kMovieVtable[11] == 0x00991010 &&
               BytesMatch((void*)0x0098D2E0, "6AFF68906AB50064A100") &&
               BytesMatch((void*)0x00991010, "6AFF68E06EB50064A100");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void InstallMenuHook(bool forLaps, bool forTrackBests)
{
    if (s_installed || !(g_cfg.uiLog || forLaps || forTrackBests)) return;
    if (!MenuHookReady()) {
        Log("ui: Scaleform movie hooks unavailable on this build or configuration");
        return;
    }
    s_invoke = (GFxInvoke_t)kMovieVtable[18];
    s_setVariable = (GFxSetVariable_t)kMovieVtable[11];
    if (!WriteSlot(&kMovieVtable[11], (void*)Hook_SetVariable)) {
        Log("ui: SetVariable slot cannot be written");
        return;
    }
    if (!WriteSlot(&kMovieVtable[18], (void*)Hook_Invoke)) {
        if (!WriteSlot(&kMovieVtable[11], (void*)s_setVariable)) Log("ui: SetVariable slot could not be restored");
        Log("ui: Invoke slot cannot be written");
        return;
    }
    s_installed = true;
    Log("ui: Scaleform Invoke / SetVariable wrapped%s", g_cfg.uiLog ? ", calls logged" : "");
}

void RemoveLapMenuHook()
{
    if (!s_installed || g_cfg.uiLog) return;
    s_installed = false;
    if (!WriteSlot(&kMovieVtable[18], (void*)s_invoke)) Log("ui: Invoke slot could not be restored");
    if (!WriteSlot(&kMovieVtable[11], (void*)s_setVariable)) Log("ui: SetVariable slot could not be restored");
}
