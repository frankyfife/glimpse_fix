// Split/Second controller fix - dinput8.dll proxy entry points, config and logging
#include "common.h"
#include "ini_percent.h"
#include "hud_resolution.h"
#include <stdio.h>
#include <ctype.h>
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

// a whole number in percent, base 10 ("080" is 80), optionally followed by " %"; anything else (empty, a word,
// a fraction) is the default; clamped to lo..hi. OpponentEngineVolume is read this way at the start, while the
// game runs (audio_diag.cpp), and in glimpse_config.exe (ini_percent.h), so all reads agree.
int IniPercent(const char* section, const char* key, int def, int lo, int hi)
{
    char buf[256];
    IniString(section, key, "", buf, sizeof(buf));
    return ParsePercentValue(buf, def, lo, hi);
}

static int IniHudResolution()
{
    char value[256];
    IniString("Graphics", "HudResolution", "", value, sizeof(value));
    bool recognized = true;
    int size = ParseHudResolution(value, &recognized);
    if (!recognized) Log("HudResolution=%s not recognised, using auto", value);
    return size;
}

static bool IniBool(const char* section, const char* key, bool def)
{
    return IniInt(section, key, def ? 1 : 0) != 0;
}

// a pad button by name: X, Y, LS, RS (the ones the game leaves free while driving), None
static WORD IniButton(const char* section, const char* key, WORD def)
{
    char buf[16];
    IniString(section, key, "", buf, sizeof(buf));
    if (!buf[0]) return def;
    if (!_stricmp(buf, "X")) return XINPUT_GAMEPAD_X;
    if (!_stricmp(buf, "Y")) return XINPUT_GAMEPAD_Y;
    if (!_stricmp(buf, "LS")) return XINPUT_GAMEPAD_LEFT_THUMB;
    if (!_stricmp(buf, "RS")) return XINPUT_GAMEPAD_RIGHT_THUMB;
    return 0;
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
    g_cfg.uiLog = g_cfg.log && IniBool("Debug", "UiLog", false);
    g_cfg.dumpTextures = IniBool("Debug", "DumpTextures", false);
    g_cfg.testFeed = IniBool("Debug", "TestFeed", false);
    if (g_cfg.log) {
        char path[MAX_PATH], prev[MAX_PATH];
        ModulePath(path, "SplitSecondPad.log");
        ModulePath(prev, "SplitSecondPad.prev.log");
        MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);  // the last run's log stays for one more start
        s_log = _fsopen(path, "w", _SH_DENYWR);
    }
    {
        char buf[32];
        IniString("Debug", "PatchMask", "0xFFFFFFFF", buf, sizeof(buf));
        g_cfg.patchMask = strtoul(buf, NULL, 0);
        if (g_cfg.patchMask != PATCH_ALL) Log("patch mask 0x%X", g_cfg.patchMask);
    }
    {
        // the frame dump's watched pixel shaders: hashes as the dump log shows them (hex, 0x optional),
        // separated by commas or spaces
        char buf[512];
        IniString("Debug", "DumpWatchPS", "", buf, sizeof(buf));
        g_cfg.dumpWatchCount = 0;
        for (char* p = buf; *p && g_cfg.dumpWatchCount < 16;) {
            if (*p == ',' || *p == ' ' || *p == '\t') {
                p++;
                continue;
            }
            char* end;
            unsigned long long v = _strtoui64(p, &end, 16);
            if (end == p || (*end && *end != ',' && *end != ' ' && *end != '\t')) {  // not a number: skipped
                Log("DumpWatchPS: \"%s\" is not a hash, skipped", p);
                while (*p && *p != ',') p++;
                continue;
            }
            if (v) g_cfg.dumpWatchPS[g_cfg.dumpWatchCount++] = v;
            p = end;
        }
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
    g_cfg.menuRepeat = IniBool("Controller", "MenuRepeat", true);
    g_cfg.menuRepeatDelay = IniInt("Controller", "MenuRepeatDelay", 450);
    if (g_cfg.menuRepeatDelay < 100) g_cfg.menuRepeatDelay = 100;
    if (g_cfg.menuRepeatDelay > 2000) g_cfg.menuRepeatDelay = 2000;
    g_cfg.menuRepeatRate = IniInt("Controller", "MenuRepeatRate", 160);
    if (g_cfg.menuRepeatRate < 80) g_cfg.menuRepeatRate = 80;  // the D-pad is held this long between two releases
    if (g_cfg.menuRepeatRate > 1000) g_cfg.menuRepeatRate = 1000;
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
    g_cfg.hdr = IniBool("Graphics", "HDR", false);
    g_cfg.hdrPeak = IniInt("Graphics", "HDRPeakBrightness", 1000);
    g_cfg.hdrPeak = g_cfg.hdrPeak < 400 ? 400 : g_cfg.hdrPeak > 10000 ? 10000 : g_cfg.hdrPeak;
    g_cfg.hdrPaperWhite = IniInt("Graphics", "HDRPaperWhite", 200);
    g_cfg.hdrPaperWhite = g_cfg.hdrPaperWhite < 80 ? 80 : g_cfg.hdrPaperWhite > 500 ? 500 : g_cfg.hdrPaperWhite;
    g_cfg.fixIconSize = IniBool("Graphics", "FixPowerplayIconSize", true);
    g_cfg.hudResolution = IniHudResolution();
    g_cfg.qualityCap = IniInt("Graphics", "QualityCap", 3);
    if (g_cfg.qualityCap < 0 || g_cfg.qualityCap > 3) g_cfg.qualityCap = 3;
    g_cfg.textureDetail = IniInt("Graphics", "TextureDetail", 0);
    if (g_cfg.textureDetail < 0 || g_cfg.textureDetail > 3) g_cfg.textureDetail = 0;
    g_cfg.glassFix = IniBool("Graphics", "GlassFix", true);
    g_cfg.textureLoadFix = IniBool("Graphics", "TextureLoadFix", true);
    g_cfg.opponentEngines = IniBool("Audio", "OpponentEngines", true);
    g_cfg.consoleReverb = IniBool("Audio", "ConsoleReverb", true);
    g_cfg.ducking = IniBool("Audio", "Ducking", true);
    g_cfg.powerPlayFlanger = IniBool("Audio", "PowerPlayFlanger", true);
    g_cfg.soundPlacement = IniBool("Audio", "SoundPlacement", true);
    g_cfg.soundBankFix = IniBool("Audio", "SoundBankFix", true);
    g_cfg.opponentEngineVolume = IniPercent("Audio", "OpponentEngineVolume", 100, 0, 200);

    // [World]
    g_cfg.streamPoolMB = IniInt("World", "StreamPoolMB", 512);
    {
        float lod = IniFloat("World", "LodDistanceScale", 2.0f, 0.0f, 8.0f);
        g_cfg.lodDistanceScale = lod < 0.5f ? 1.0f : lod;
    }
    g_cfg.popInFix = IniBool("World", "PopInFix", true);
    g_cfg.shadowDistance = IniFloat("World", "ShadowDistance", 2.0f, 1.0f, 3.0f);
    g_cfg.carDetail = IniInt("World", "CarDetail", 1);
    if (g_cfg.carDetail < 0 || g_cfg.carDetail > 2) g_cfg.carDetail = 1;
    g_cfg.shadows = IniBool("World", "Shadows", true);
    g_cfg.shadowResolution = IniInt("World", "ShadowResolution", 2);
    if (g_cfg.shadowResolution != 1 && g_cfg.shadowResolution != 4) g_cfg.shadowResolution = 2;

    // [Camera]
    g_cfg.fov = IniFloat("Camera", "FOV", 1.0f, 0.3f, 2.5f);
    g_cfg.ultrawideFix = IniInt("Camera", "UltrawideFix", 1);
    if (g_cfg.ultrawideFix < 0 || g_cfg.ultrawideFix > 2) g_cfg.ultrawideFix = 1;
    if (g_cfg.ultrawideFix == 2 && !(g_cfg.patchMask & PATCH_D3D)) g_cfg.ultrawideFix = 1;  // needs the device hooks
    g_cfg.ultrawideBlackBackdrop = IniBool("Camera", "UltrawideBlackBackdrop", false);
    g_cfg.zoomKeys = IniBool("Camera", "ZoomKeys", false);
    g_cfg.zoomInKey = IniInt("Camera", "ZoomInKey", '1');
    g_cfg.zoomOutKey = IniInt("Camera", "ZoomOutKey", '2');
    g_cfg.zoomResetKey = IniInt("Camera", "ZoomResetKey", '3');
    g_cfg.photoMode = IniBool("Camera", "PhotoMode", false);
    g_cfg.photoModeKey = IniInt("Camera", "PhotoModeKey", 'F');
    g_cfg.hudKey = IniInt("Camera", "HudKey", 0);
    g_cfg.cameraShake = IniFloat("Camera", "CameraShake", 1.0f, 0.0f, 1.0f);
    g_cfg.explosionShakeOff = IniBool("Camera", "ExplosionShakeOff", false);
    g_cfg.dlcOverlays = IniBool("DLC", "Overlays", false);
    g_cfg.dlcTracks = IniBool("DLC", "Tracks", false);
    g_cfg.dlcTracksOnline = IniBool("DLC", "TracksOnline", false);
    g_cfg.dlcMusic = IniBool("DLC", "Music", true);
    g_cfg.dlcModes = IniBool("DLC", "Modes", false);
    g_cfg.dlcPersonalBests = IniBool("DLC", "PersonalBests", false);
    g_cfg.dlcPickups = IniBool("DLC", "Pickups", false);
    g_cfg.pickupVignette = IniBool("DLC", "PickupVignette", true);
    g_cfg.deadlineBonus = IniFloat("DLC", "DeadlineBonus", 50.0f, 0.0f, 300.0f);
    g_cfg.dlcCars = IniBool("DLC", "Cars", true);
    g_cfg.dlcCarsCareer = IniBool("DLC", "CarsCareer", false);
    g_cfg.steamLan = IniBool("Online", "SteamLAN", false);
    IniString("Online", "LobbyName", "", g_cfg.lobbyName, sizeof(g_cfg.lobbyName));
    {  // only letters, digits, - and _ (as glimpse_config.exe lets you type), at most 32
        char* w = g_cfg.lobbyName;
        for (const char* r = g_cfg.lobbyName; *r && w - g_cfg.lobbyName < 32; r++)
            if (isalnum((unsigned char)*r) || *r == '-' || *r == '_') *w++ = *r;
        *w = 0;
    }
    g_cfg.friendsOnly = IniBool("Online", "FriendsOnly", false);

    // [Gameplay]
    g_cfg.raceEndTiming = IniBool("Gameplay", "RaceEndTiming", false);
    g_cfg.unlockAll = IniBool("Gameplay", "UnlockAll", false);
    g_cfg.laps = IniInt("Gameplay", "Laps", 0);
    if (g_cfg.laps < 0 || g_cfg.laps > 16) g_cfg.laps = 0;
    g_cfg.lapSelect = IniBool("Gameplay", "LapSelect", false);
    g_cfg.bonusContent = IniBool("Gameplay", "BonusContent", false);
    g_cfg.unlockDecals = IniInt("Gameplay", "UnlockDecals", 0);
    g_cfg.wreckTime = IniFloat("Gameplay", "WreckTime", 0.0f, 0.0f, 120.0f);
    if (g_cfg.unlockDecals < 0 || g_cfg.unlockDecals > 2) g_cfg.unlockDecals = 0;
    g_cfg.saveBackups = IniInt("Gameplay", "SaveBackups", 0);
    if (g_cfg.saveBackups < 0) g_cfg.saveBackups = 0;
    if (g_cfg.saveBackups > 5) g_cfg.saveBackups = 5;
    g_cfg.skipIntro = IniInt("Gameplay", "SkipIntro", 0);
    g_cfg.cutsceneSkip = IniBool("Gameplay", "CutsceneSkip", true);
    g_cfg.smaa = IniBool("Graphics", "SMAA", true);
    g_cfg.boost = IniBool("Gameplay", "Boost", false);
    g_cfg.boostKey = IniInt("Gameplay", "BoostKey", VK_LSHIFT);
    g_cfg.boostButton = IniButton("Gameplay", "BoostButton", XINPUT_GAMEPAD_X);
    g_cfg.handbrake = IniBool("Gameplay", "Handbrake", false);
    g_cfg.handbrakeButton = IniButton("Gameplay", "HandbrakeButton", XINPUT_GAMEPAD_Y);
    g_cfg.handbrakeButtonIni = g_cfg.handbrakeButton;  // before the clash check: SteamLAN rules decide again
    g_cfg.handbrakeKey = IniInt("Gameplay", "HandbrakeKey", VK_SPACE);  // in neither of the game's keyboard layouts
    g_cfg.handbrakeMode = IniInt("Gameplay", "HandbrakeMode", 1) == 2 ? 2 : 1;
    g_cfg.handbrakeStrength = IniFloat("Gameplay", "HandbrakeStrength", 15.0f, 0.0f, 40.0f);
    g_cfg.handbrakeDrift = IniFloat("Gameplay", "HandbrakeDrift", 2.5f, 0.0f, 5.0f);
    if (g_cfg.handbrakeButton == g_cfg.boostButton && g_cfg.boost && g_cfg.handbrake) {
        Log("BoostButton and HandbrakeButton are the same pad button, the handbrake gets none");
        g_cfg.handbrakeButton = 0;
    }
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

// The game's DirectInput interface is wrapped and presents the XInput pads (Proxy_DirectInput8Create). Otherwise the
// game reads every controller itself, and the per-frame extras leave the pad to it (keys only)
bool PadsWrapped()
{
    return g_cfg.virtualPads > 0 && pXInputGetState && pXInputSetState;
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
        // [DLC] Tracks: here and not inside ApplyGamePatches, which leaves early on an exe it does not recognise by
        // its code: the track overlay was merged at DLL load by the build's time stamp alone (DlcInstall), and
        // unregistered its tracks would be listed without an index. Checks build, overlay and tables itself; the
        // online thread (OnlineStart, above) waits for it (DlcTracksDecided).
        if (g_cfg.dlcTracks) DlcRegisterTracks();
        DlcInstallModes();  // [DLC] Modes: same moment as DlcRegisterTracks, checks build and overlay itself
        DlcInstallPickups();  // [DLC] Pickups / DeadlineBonus: the same, checks build and overlay itself
        DlcInstallRecords();
        DlcInstallCars();   // [DLC] Cars: the same, checks build, UnlockAll and overlay itself
    }
    if (riid == IID_IDirectInput8A && PadsWrapped())
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
        StartSaveBackup();  // a thread: runs once the loader is done, long before the title screen loads a profile
        DlcInstall();  // before the game's first read of resident.ark
        FixOpponentEngineBanks();  // plain data in the exe file, read when the audio system starts
        FixReverbPresets();        // fmod_event.dll is mapped already (an import of the exe)
        FixGameDspEffects();       // .rdata, read when the game builds its audio groups and voices
        if ((g_cfg.log || g_cfg.anisotropy > 1 || g_cfg.frameRate == 60 || g_cfg.hdr || g_cfg.ultrawideFix == 2 ||
             g_cfg.dumpTextures || g_cfg.cutsceneSkip || g_cfg.smaa || g_cfg.textureDetail > 0 || g_cfg.qualityCap < 3 ||
             g_cfg.shadowResolution > 1 || (g_cfg.hudResolution >= 0 && (g_cfg.patchMask & PATCH_HUD)) ||
             g_cfg.glassFix || PickupsWanted()) &&
            (g_cfg.patchMask & PATCH_D3D))
            InstallD3DHooks();
    } else if (reason == DLL_PROCESS_DETACH) {
        DlcUninstall();
    }
    return TRUE;
}
