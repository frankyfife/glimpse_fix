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
void ModulePath(char* out, const char* file);  // file next to the DLL (game folder)

// XInput entry points, resolved from the system DLL
typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, XINPUT_VIBRATION*);
extern XInputGetState_t pXInputGetState;
extern XInputSetState_t pXInputSetState;

// dinput_wrap.cpp
IDirectInput8A* WrapDirectInput8A(IDirectInput8A* real);
bool PadSlotConnected(int slot);

// diag.cpp
void InstallD3DDiagnostics();

// audio_diag.cpp
void InstallAudioDiagnostics();
void AudioDiagTick();

// framerate.cpp
void PatchFrameRate();

// profile.cpp
void InstallProfiler();
void ProfilerTick();

// postfx.cpp
void PatchMotionBlur();

// game.cpp
bool BytesMatch(const void* at, const char* hex);     // hex string, no spaces
void WriteMemory(void* at, const void* src, size_t len);
void* Detour(BYTE* at, int len, void* hook);         // returns the trampoline
void ApplyGamePatches();
void PatchPadNames();
void OnInputFrame();       // called from the virtual device poll, on the game thread
void NotePadActivity();
