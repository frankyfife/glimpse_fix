// Split/Second controller fix - shared declarations
#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <xinput.h>

enum GlyphStyle { GLYPHS_AUTO_XBOX, GLYPHS_AUTO_PS, GLYPHS_KEYBOARD };

struct Config {
    int   virtualPads;       // XInput slots exposed as DirectInput pads (0..4)
    bool  hideXInputDevices; // drop the real XInput devices from DirectInput enumeration
    bool  separateTriggers;  // LT/RT on their own axes instead of the shared Z axis
    float stickDeadzone;     // radial, 0..1
    float triggerDeadzone;   // 0..1
    float rumbleScale;       // multiplier for the game's motor values
    GlyphStyle glyphs;
    bool  log;
    bool  testFeed;          // read pad state from shared memory instead of XInput (development)
    bool  profile;           // count calls of timing-relevant game functions (development)
    int   frameRate;         // 30 (stock) or 60
    int   streamPoolMB;      // sector streaming pool, 0 = stock (57 MB)
    bool  fixIconSize;       // undo the double resolution scaling of the powerplay icons
    float motionBlurStrength; // multiplier on top of the 60 fps compensation
    int   anisotropy;        // forced anisotropic filtering, 0/1 = game default
    float lodDistanceScale;  // environment, particle and powerplay draw distances
    bool  vignette;          // keep the cameras' screen edge darkening
    bool  bloom;             // bloom added to the frame (tone mapping stays)
    bool  lensFlare;         // sun, light and screen space lens flares
    bool  particleFlare;     // flares of sparks, explosions and lights
    int   skipIntro;         // 0 = boot movies as the game, 1 = skip the publisher logo, 2 = also the intro
    bool  popInFix;          // ignore the sectors' precomputed visibility lists (distance and frustum only)
    bool  unlockAll;         // all tracks, events, episodes, game modes and vehicles selectable
    bool  bonusContent;      // the PC cheat code extras: Expressway track, branded cars, liveries
    float wreckTime;         // seconds from a wreck to the respot, 0 = the game's
    int   unlockDecals;      // achievement decals: 0 off, 1 all, 2 only the online ones and the Pure decal (saved)
    bool  boost;             // X held: extra acceleration for player 1
    int   boostKey;          // virtual key that works like X for the boost, 0 = none
    float boostAcceleration; // m/s^2
    float boostMaxSpeed;     // m/s, boost only below this speed
    float boostMotionBlur;   // motion blur multiplier at full boost
    bool  powerFill;         // LS+RS: power bar full
    int   powerFillKey;      // virtual key that works like LS+RS, 0 = none
    bool  aiBoost;           // experimental: AI cars boost
    int   aiBoostLevel;      // 1 easy, 2 normal, 3 hard
    bool  aiBoostUnlimited;  // the tank never empties
    bool  aiBoostStraightOnly; // only on straights at speed
    bool  aiBoostChaos;      // hard, unlimited, anywhere, also when far ahead (overrides the three above)
    float fov;               // field of view factor of the race cameras
    bool  ultrawideFix;      // wider screens than 16:9 keep the vertical field of view of 16:9
    bool  zoomKeys;          // zoom in/out while a key is held, and reset
    int   zoomInKey, zoomOutKey, zoomResetKey;  // virtual key codes
    bool  photoMode;         // free camera during a race, world held still and HUD hidden
    int   photoModeKey;      // virtual key code (the pad's Back button works as well)
    int   hudKey;            // virtual key code that switches the HUD off and on, 0 = none
    float cameraShake;       // race cameras' shake: 1 = as the game, 0 = their rotation smoothed most
    bool  explosionShakeOff; // no camera shake from explosions and power plays
    bool  gradient;          // keep the cameras' screen gradient (the blue band at the top)
    unsigned patchMask;      // PATCH_* bits that may be applied (development, to bisect side effects)
};

enum PatchBits {
    PATCH_TRIGGERS = 0x1, PATCH_RUMBLE = 0x2, PATCH_PROMPTBAR = 0x4, PATCH_TITLETEXT = 0x8,
    PATCH_FRAMERATE = 0x10, PATCH_STREAMPOOL = 0x20, PATCH_ICONSIZE = 0x40, PATCH_MOTIONBLUR = 0x80,
    /* 0x100: was RenderScale, removed */ PATCH_PADNAMES = 0x200, PATCH_LOD = 0x400, PATCH_D3D = 0x800,
    PATCH_AUDIODIAG = 0x1000, PATCH_VIGNETTE = 0x2000,
    PATCH_UNLOCK = 0x4000, PATCH_SAVEDIAG = 0x8000, PATCH_ROUTEMARKER = 0x10000, PATCH_BOOST = 0x20000, PATCH_AIBOOST = 0x40000, PATCH_FLARES = 0x80000, PATCH_INTRO = 0x100000, PATCH_CAMERA = 0x200000,
    PATCH_ALL = 0xFFFFFFFF
};

// Shared memory layout for TestFeed=1, mapping name kTestFeedName
struct TestFeed {
    DWORD connected[XUSER_MAX_COUNT];
    XINPUT_STATE state[XUSER_MAX_COUNT];
    XINPUT_VIBRATION rumble[XUSER_MAX_COUNT];  // last values the game sent
    DWORD glyphsPad;                           // 1 while pad glyphs are shown
    DWORD shotRequest;                         // nonzero: save the next frame as SplitSecondPad_shot<n>.jpg
    float fps;                                 // presents per second
};
#define kTestFeedName "Local\\SplitSecondPadTestFeed"
TestFeed* GetTestFeed();

extern Config g_cfg;

void Log(const char* fmt, ...);

// envlog.cpp (Log=1)
#define GLIMPSE_VERSION "1.7.1"  // keep in step with config/glimpse_config.rc
void LogEnvironment();                     // system, executable, files and modules, at start
void LogForeignModules(const char* when);  // modules loaded from outside the Windows folder
void ModulePath(char* out, const char* file);  // file next to the DLL (game folder)
void DumpMark(const char* what);  // frame dump (diag.cpp): a note in the dumped frame's log

// XInput entry points, resolved from the system DLL
typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, XINPUT_VIBRATION*);
extern XInputGetState_t pXInputGetState;
extern XInputSetState_t pXInputSetState;

// dinput_wrap.cpp
IDirectInput8A* WrapDirectInput8A(IDirectInput8A* real);
bool PadSlotConnected(int slot);

// diag.cpp
void InstallD3DHooks();  // device log/capture (Log=1), anisotropic filtering
void BeginEdgeClamp();   // until EndEdgeClamp: texture addressing clamped at the edges (motion blur pass)
void EndEdgeClamp();

// audio_diag.cpp
void InstallAudioDiagnostics();
void AudioDiagTick();
void PauseGameSound(bool pause);  // photo mode: all sound categories the game has used

// framerate.cpp
void PatchFrameRate();
void FramePace();  // after every Present: 60 fps pacing of the main loop

// profile.cpp
void InstallProfiler();
void ProfilerTick();

// postfx.cpp
void PatchMotionBlur();
void PatchVignette();
void PatchBloomAndFlares();
void PatchDepthOfField();  // PhotoMode=1: the depth of field pass, used by the photo mode only
void ApplyLensFlare();  // per frame
// the photo mode's look, set by camera.cpp: bloom and exposure multiply the game's values (1 = as is,
// bloom 0 = off), depth of field strength 0..1 (0 = the game's own) with the focus distance in metres
extern float g_photoBloom, g_photoExposure, g_photoDof, g_photoFocus;

// boost.cpp
void BoostTick();        // per input frame: boost on X or BoostKey, power bar fill on LS+RS or PowerBarFillKey
float BoostBlurScale();  // motion blur multiplier while boosting (1 = none)

// camera.cpp
void PatchCamera();
void CameraTick();       // per input frame: zoom keys, HUD key, photo mode
void ApplyExplosionShake();  // per input frame: ExplosionShakeOff=1
bool SinglePlayerRace(); // a race camera runs on a single screen (not paused)
bool CameraBlocksPad();  // photo mode drives the camera with the pad: the game sees it idle

// savediag.cpp
void WatchSaveFiles();  // Log=1: log every change of the profile files

// game.cpp
bool BytesMatch(const void* at, const char* hex);     // hex string, no spaces
void WriteMemory(void* at, const void* src, size_t len);

// Scaleform GFxValue as laid out by this game's GFx build; GFx movie view vtable: +0x2C SetVariable, +0x48 Invoke
struct GFxValue {
    DWORD type;
    DWORD pad;
    union {
        const char* str;
        double num;
    };
};
enum { GFX_NUMBER = 3, GFX_STRING = 4, GFX_RESULT_STRING = 0x14 };

typedef bool(__thiscall* GFxInvoke_t)(void* movie, const char* path, GFxValue* result, const GFxValue* args, unsigned count);
typedef bool(__thiscall* GFxSetVariable_t)(void* movie, const char* path, const GFxValue* value, int setType);
void* Detour(BYTE* at, int len, void* hook);         // returns the trampoline
void ApplyGamePatches();
void PatchPadNames();
void OnInputFrame();       // called from the virtual device poll, on the game thread
void NotePadActivity();
