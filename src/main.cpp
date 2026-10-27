// Split/Second controller fix - dinput8.dll proxy entry points, config and logging
#include "common.h"
#include <stdio.h>
#include <share.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

Config g_cfg;
XInputGetState_t pXInputGetState;
XInputSetState_t pXInputSetState;

static HMODULE s_self;
static HMODULE s_realDinput;
static CRITICAL_SECTION s_logLock;
static FILE* s_log;

typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
typedef HRESULT(WINAPI* DllCanUnloadNow_t)();
typedef HRESULT(WINAPI* DllGetClassObject_t)(REFCLSID, REFIID, LPVOID*);
typedef HRESULT(WINAPI* DllRegisterServer_t)();
typedef LPCDIDATAFORMAT(WINAPI* GetdfDIJoystick_t)();

void ModulePath(char* out, const char* file)
{
    GetModuleFileNameA(s_self, out, MAX_PATH);
    char* slash = strrchr(out, '\\');
    strcpy_s(slash ? slash + 1 : out, MAX_PATH - (slash ? (slash + 1 - out) : 0), file);
}

void Log(const char* fmt, ...)
{
    if (!s_log) return;
    EnterCriticalSection(&s_logLock);
    va_list ap;
    va_start(ap, fmt);
    fprintf(s_log, "[%8lu] ", GetTickCount());
    vfprintf(s_log, fmt, ap);
    fputc('\n', s_log);
    fflush(s_log);
    va_end(ap);
    LeaveCriticalSection(&s_logLock);
}

static float IniFloat(const char* ini, const char* key, float def)
{
    char buf[32], defs[32];
    sprintf_s(defs, "%g", def);
    GetPrivateProfileStringA("Controller", key, defs, buf, sizeof(buf), ini);
    return (float)atof(buf);
}

static void LoadConfig()
{
    char ini[MAX_PATH];
    ModulePath(ini, "SplitSecondPad.ini");
    g_cfg.virtualPads = GetPrivateProfileIntA("Controller", "VirtualPads", 4, ini);
    if (g_cfg.virtualPads < 0) g_cfg.virtualPads = 0;
    if (g_cfg.virtualPads > XUSER_MAX_COUNT) g_cfg.virtualPads = XUSER_MAX_COUNT;
    g_cfg.hideXInputDevices = GetPrivateProfileIntA("Controller", "HideXInputFromDirectInput", 1, ini) != 0;
    g_cfg.separateTriggers = GetPrivateProfileIntA("Controller", "SeparateTriggers", 1, ini) != 0;
    g_cfg.stickDeadzone = IniFloat(ini, "StickDeadzone", 0.15f);
    g_cfg.triggerDeadzone = IniFloat(ini, "TriggerDeadzone", 0.05f);
    g_cfg.rumbleScale = IniFloat(ini, "RumbleStrength", 1.0f);

    char glyphs[32];
    GetPrivateProfileStringA("Controller", "ButtonGlyphs", "xbox", glyphs, sizeof(glyphs), ini);
    if (!_stricmp(glyphs, "playstation") || !_stricmp(glyphs, "ps"))
        g_cfg.glyphs = GLYPHS_AUTO_PS;
    else if (!_stricmp(glyphs, "keyboard") || !_stricmp(glyphs, "off"))
        g_cfg.glyphs = GLYPHS_KEYBOARD;
    else
        g_cfg.glyphs = GLYPHS_AUTO_XBOX;

    g_cfg.log = GetPrivateProfileIntA("Controller", "Log", 0, ini) != 0;
    g_cfg.testFeed = GetPrivateProfileIntA("Controller", "TestFeed", 0, ini) != 0;
    g_cfg.profile = GetPrivateProfileIntA("Controller", "Profile", 0, ini) != 0;
    g_cfg.frameRate = GetPrivateProfileIntA("Game", "FrameRate", 60, ini) == 30 ? 30 : 60;
    if (g_cfg.log) {
        char path[MAX_PATH];
        ModulePath(path, "SplitSecondPad.log");
        s_log = _fsopen(path, "w", _SH_DENYWR);
    }
    g_cfg.streamPoolMB = GetPrivateProfileIntA("Game", "StreamPoolMB", 512, ini);
    g_cfg.fixIconSize = GetPrivateProfileIntA("Game", "FixPowerplayIconSize", 1, ini) != 0;
    {
        char buf[32];
        GetPrivateProfileStringA("Game", "MotionBlurStrength", "1.0", buf, sizeof(buf), ini);
        g_cfg.motionBlurStrength = (float)atof(buf);
    }
    Log("frame rate %d, stream pool %d MB", g_cfg.frameRate, g_cfg.streamPoolMB);
    Log("config: pads=%d hide=%d triggers=%d stickdz=%.2f trigdz=%.2f rumble=%.2f glyphs=%d",
        g_cfg.virtualPads, g_cfg.hideXInputDevices, g_cfg.separateTriggers, g_cfg.stickDeadzone,
        g_cfg.triggerDeadzone, g_cfg.rumbleScale, g_cfg.glyphs);
}

static HMODULE LoadSystemDll(const char* name)
{
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    if (!n || n + strlen(name) + 2 > MAX_PATH) return NULL;
    strcat_s(path, "\\");
    strcat_s(path, name);
    return LoadLibraryA(path);
}

static bool LoadXInput()
{
    static const char* dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (const char* name : dlls) {
        HMODULE h = LoadSystemDll(name);
        if (!h) continue;
        pXInputGetState = (XInputGetState_t)GetProcAddress(h, "XInputGetState");
        pXInputSetState = (XInputSetState_t)GetProcAddress(h, "XInputSetState");
        if (pXInputGetState && pXInputSetState) {
            Log("using %s", name);
            return true;
        }
    }
    Log("no XInput DLL found");
    return false;
}

static FARPROC Real(const char* name)
{
    if (!s_realDinput) s_realDinput = LoadSystemDll("dinput8.dll");
    return s_realDinput ? GetProcAddress(s_realDinput, name) : NULL;
}

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    DirectInput8Create_t fn = (DirectInput8Create_t)Real("DirectInput8Create");
    if (!fn) return DIERR_GENERIC;
    HRESULT hr = fn(inst, version, riid, out, outer);
    Log("DirectInput8Create -> %08lx", hr);
    if (FAILED(hr) || !out || !*out) return hr;

    static bool initialized;
    if (!initialized) {
        initialized = true;
        LoadXInput();
        ApplyGamePatches();
    }
    if (riid == IID_IDirectInput8A && pXInputGetState && g_cfg.virtualPads > 0)
        *out = WrapDirectInput8A((IDirectInput8A*)*out);
    return hr;
}

extern "C" HRESULT WINAPI Proxy_DllCanUnloadNow()
{
    DllCanUnloadNow_t fn = (DllCanUnloadNow_t)Real("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}

extern "C" HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out)
{
    DllGetClassObject_t fn = (DllGetClassObject_t)Real("DllGetClassObject");
    return fn ? fn(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI Proxy_DllRegisterServer()
{
    DllRegisterServer_t fn = (DllRegisterServer_t)Real("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_DllUnregisterServer()
{
    DllRegisterServer_t fn = (DllRegisterServer_t)Real("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}

extern "C" LPCDIDATAFORMAT WINAPI Proxy_GetdfDIJoystick()
{
    GetdfDIJoystick_t fn = (GetdfDIJoystick_t)Real("GetdfDIJoystick");
    return fn ? fn() : NULL;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        s_self = inst;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&s_logLock);
        LoadConfig();
        if (g_cfg.log) InstallD3DDiagnostics();
    }
    return TRUE;
}
