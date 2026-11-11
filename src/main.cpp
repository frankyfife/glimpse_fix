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

// Keys are looked up in their section; INIs from before the sections were split kept the game
// options in [Game] and the debug options in [Controller], so those are read as a fallback.
static char s_ini[MAX_PATH];

static void IniString(const char* section, const char* key, const char* def, char* out, DWORD size)
{
    static const char kMissing[] = "\x01";
    GetPrivateProfileStringA(section, key, kMissing, out, size, s_ini);
    if (strcmp(out, kMissing)) return;
    GetPrivateProfileStringA(!strcmp(section, "Debug") ? "Controller" : "Game", key, def, out, size, s_ini);
}

static int IniInt(const char* section, const char* key, int def)
{
    char buf[32];
    IniString(section, key, "", buf, sizeof(buf));
    return buf[0] ? (int)strtol(buf, NULL, 0) : def;
}

static bool IniBool(const char* section, const char* key, bool def)
{
    return IniInt(section, key, def ? 1 : 0) != 0;
}

static float IniFloat(const char* section, const char* key, float def, float lo, float hi)
{
    char buf[32];
    IniString(section, key, "", buf, sizeof(buf));
    float v = buf[0] ? (float)atof(buf) : def;
    return v < lo ? lo : v > hi ? hi : v;
}

static void LoadConfig()
{
    ModulePath(s_ini, "SplitSecondPad.ini");

    // [Debug] first, so the log is open for everything below
    g_cfg.log = IniBool("Debug", "Log", false);
    g_cfg.testFeed = IniBool("Debug", "TestFeed", false);
    g_cfg.profile = IniBool("Debug", "Profile", false);
    if (g_cfg.log) {
        char path[MAX_PATH];
        ModulePath(path, "SplitSecondPad.log");
        s_log = _fsopen(path, "w", _SH_DENYWR);
    }
    {
        char buf[32];
        IniString("Debug", "PatchMask", "0xFFFFFFFF", buf, sizeof(buf));
        g_cfg.patchMask = strtoul(buf, NULL, 0);
        if (g_cfg.patchMask != PATCH_ALL) Log("patch mask 0x%X", g_cfg.patchMask);
    }

    // [Controller]
    g_cfg.virtualPads = IniInt("Controller", "VirtualPads", 4);
    if (g_cfg.virtualPads < 0) g_cfg.virtualPads = 0;
    if (g_cfg.virtualPads > XUSER_MAX_COUNT) g_cfg.virtualPads = XUSER_MAX_COUNT;
    g_cfg.hideXInputDevices = IniBool("Controller", "HideXInputFromDirectInput", true);
    g_cfg.separateTriggers = IniBool("Controller", "SeparateTriggers", true);
    g_cfg.stickDeadzone = IniFloat("Controller", "StickDeadzone", 0.15f, 0.0f, 1.0f);
    g_cfg.triggerDeadzone = IniFloat("Controller", "TriggerDeadzone", 0.05f, 0.0f, 1.0f);
    g_cfg.rumbleScale = IniFloat("Controller", "RumbleStrength", 1.0f, 0.0f, 10.0f);
    char glyphs[32];
    IniString("Controller", "ButtonGlyphs", "xbox", glyphs, sizeof(glyphs));
    if (!_stricmp(glyphs, "playstation") || !_stricmp(glyphs, "ps"))
        g_cfg.glyphs = GLYPHS_AUTO_PS;
    else if (!_stricmp(glyphs, "keyboard") || !_stricmp(glyphs, "off"))
        g_cfg.glyphs = GLYPHS_KEYBOARD;
    else
        g_cfg.glyphs = GLYPHS_AUTO_XBOX;

    // [Graphics]
    g_cfg.frameRate = IniInt("Graphics", "FrameRate", 60) == 30 ? 30 : 60;
    g_cfg.anisotropy = IniInt("Graphics", "Anisotropy", 16);
    if (g_cfg.anisotropy > 16) g_cfg.anisotropy = 16;
    g_cfg.motionBlurStrength = IniFloat("Graphics", "MotionBlurStrength", 1.0f, 0.0f, 10.0f);
    g_cfg.vignette = IniBool("Graphics", "Vignette", false);
    g_cfg.gradient = IniBool("Graphics", "Gradient", true);
    g_cfg.bloom = IniBool("Graphics", "Bloom", true);
    g_cfg.lensFlare = IniBool("Graphics", "LensFlare", true);
    g_cfg.particleFlare = IniBool("Graphics", "ParticleFlare", true);
    g_cfg.fixIconSize = IniBool("Graphics", "FixPowerplayIconSize", true);

    // [World]
    g_cfg.streamPoolMB = IniInt("World", "StreamPoolMB", 512);
    {
        float lod = IniFloat("World", "LodDistanceScale", 2.0f, 0.0f, 8.0f);
        g_cfg.lodDistanceScale = lod < 0.5f ? 1.0f : lod;
    }
    g_cfg.popInFix = IniBool("World", "PopInFix", true);

    // [Camera]
    g_cfg.fov = IniFloat("Camera", "FOV", 1.0f, 0.3f, 2.5f);
    g_cfg.ultrawideFix = IniBool("Camera", "UltrawideFix", true);
    g_cfg.zoomKeys = IniBool("Camera", "ZoomKeys", false);
    g_cfg.zoomInKey = IniInt("Camera", "ZoomInKey", '1');
    g_cfg.zoomOutKey = IniInt("Camera", "ZoomOutKey", '2');
    g_cfg.zoomResetKey = IniInt("Camera", "ZoomResetKey", '3');
    g_cfg.photoMode = IniBool("Camera", "PhotoMode", false);
    g_cfg.photoModeKey = IniInt("Camera", "PhotoModeKey", 'F');
    g_cfg.hudKey = IniInt("Camera", "HudKey", 0);
    g_cfg.cameraShake = IniFloat("Camera", "CameraShake", 1.0f, 0.0f, 1.0f);
    g_cfg.explosionShakeOff = IniBool("Camera", "ExplosionShakeOff", false);

    // [Gameplay]
    g_cfg.unlockAll = IniBool("Gameplay", "UnlockAll", false);
    g_cfg.bonusContent = IniBool("Gameplay", "BonusContent", false);
    g_cfg.unlockDecals = IniInt("Gameplay", "UnlockDecals", 0);
    g_cfg.wreckTime = IniFloat("Gameplay", "WreckTime", 0.0f, 0.0f, 120.0f);
    if (g_cfg.unlockDecals < 0 || g_cfg.unlockDecals > 2) g_cfg.unlockDecals = 0;
    g_cfg.skipIntro = IniInt("Gameplay", "SkipIntro", 0);
    g_cfg.boost = IniBool("Gameplay", "Boost", false);
    g_cfg.boostKey = IniInt("Gameplay", "BoostKey", VK_LSHIFT);
    g_cfg.boostAcceleration = IniFloat("Gameplay", "BoostAcceleration", 15.0f, 1.0f, 100.0f);
    g_cfg.boostMaxSpeed = IniFloat("Gameplay", "BoostMaxSpeed", 90.0f, 10.0f, 200.0f);
    g_cfg.boostMotionBlur = IniFloat("Gameplay", "BoostMotionBlur", 3.0f, 1.0f, 5.0f);
    g_cfg.powerFill = IniBool("Gameplay", "PowerBarFill", false);
    g_cfg.powerFillKey = IniInt("Gameplay", "PowerBarFillKey", VK_LCONTROL);
    g_cfg.aiBoost = IniBool("Gameplay", "AIBoost", false);
    g_cfg.aiBoostLevel = IniInt("Gameplay", "AIBoostLevel", 2);
    if (g_cfg.aiBoostLevel < 1) g_cfg.aiBoostLevel = 1;
    if (g_cfg.aiBoostLevel > 3) g_cfg.aiBoostLevel = 3;
    g_cfg.aiBoostUnlimited = IniBool("Gameplay", "AIBoostUnlimited", false);
    g_cfg.aiBoostStraightOnly = IniBool("Gameplay", "AIBoostStraightOnly", true);
    g_cfg.aiBoostChaos = IniBool("Gameplay", "AIBoostChaos", false);
    if (g_cfg.aiBoostChaos) {
        g_cfg.aiBoostLevel = 3;
        g_cfg.aiBoostUnlimited = true;
        g_cfg.aiBoostStraightOnly = false;
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
        LogEnvironment();  // here, not in DllMain: it reads files, the registry and the module list
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
        if ((g_cfg.log || g_cfg.anisotropy > 1 || g_cfg.frameRate == 60) && (g_cfg.patchMask & PATCH_D3D)) InstallD3DHooks();
    }
    return TRUE;
}
