// Split/Second controller fix - shared declarations
#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <xinput.h>
#include <stdint.h>

enum GlyphStyle { GLYPHS_AUTO_XBOX, GLYPHS_AUTO_PS, GLYPHS_KEYBOARD };

struct Config {
    int   virtualPads;       // XInput slots exposed as DirectInput pads (0..4)
    bool  hideXInputDevices; // drop the real XInput devices from DirectInput enumeration
    bool  separateTriggers;  // LT/RT on their own axes instead of the shared Z axis
    float stickDeadzone;     // radial, 0..1
    float triggerDeadzone;   // 0..1
    float rumbleScale;       // multiplier for the game's motor values
    bool  menuRepeat;        // a held D-pad repeats in menus (outside a race), as repeated presses
    int   menuRepeatDelay;   // ms before the first repeat
    int   menuRepeatRate;    // ms between repeats
    GlyphStyle glyphs;
    bool  log;
    bool  dumpTextures;      // [Debug] DumpTextures: write the game's textures as DDS files at the next start, then quit
    unsigned long long dumpWatchPS[16];  // [Debug] DumpWatchPS: pixel shader hashes whose draws the F9 dump captures (diag.cpp)
    int   dumpWatchCount;
    bool  testFeed;          // read pad state from shared memory instead of XInput (development)
    int   frameRate;         // 30 (stock) or 60
    int   streamPoolMB;      // sector streaming pool, 0 = stock (57 MB)
    bool  fixIconSize;       // undo the double resolution scaling of the powerplay icons
    bool  opponentEngines;   // 256 KB CPUCar bank slots, 2048 EventMgr cap, multi-race slot cleanup and car matching
    bool  consoleReverb;     // the Xbox 360 release's reverb presets instead of the PC's stock ones
    bool  ducking;           // the mixer's side-chain ducking (volume meters attached to their groups)
    bool  powerPlayFlanger;  // restore the game's Powerplays mixer effect
    bool  soundPlacement;    // the game's own 3D placement of voices (VoiceDSP units attached)
    bool  soundBankFix;      // FMOD finds each sound of a bank at its real (32-byte padded) start
    int   opponentEngineVolume;  // percent on the levels the mixer sets for the CPUEngine group, 0..200 (100 = as the game)
    float motionBlurStrength; // multiplier on top of the 60 fps compensation
    int   anisotropy;        // forced anisotropic filtering, 0/1 = game default
    float lodDistanceScale;  // environment, particle and powerplay draw distances
    float shadowDistance;    // multiplier for the sun shadow range at High and Very High
    bool  vignette;          // keep the cameras' screen edge darkening
    bool  bloom;             // bloom added to the frame (tone mapping stays)
    bool  lensFlare;         // sun, light and screen space lens flares
    bool  particleFlare;     // flares of sparks, explosions and lights
    bool  hdr;               // HDR10 output through a DXGI swap chain (borderless window)
    int   hdrPeak;           // nits, the display's peak brightness
    int   hdrPaperWhite;     // nits, the brightness of the game's white
    int   skipIntro;         // 0 = boot movies as the game, 1 = skip the publisher logo, 2 = also the intro
    bool  smaa;              // SMAA 1x on the 3D picture (the HUD stays sharp)
    bool  cutsceneSkip;      // cutscenes end on a second press of A (or B, X, Y, Start, Enter, Space)
    bool  popInFix;          // ignore the sectors' precomputed visibility lists (distance and frustum only)
    int   carDetail;         // opponent car detail: 0 as the PC release, 1 as the Xbox 360, 2 high
    int   qualityCap;        // highest graphics quality level the game may use (3 = any, 0 = Low only)
    bool  shadows;           // false: nothing casts a shadow (the game's own debug switch)
    int   shadowResolution;  // 1 = the game's 1024 per sun shadow cascade, 2 or 4 = that many times as wide and high
    int   hudResolution;     // -1 = off, 0 = auto (2x up to 1080p, 4x above; 4x split screen), 1 = stock size with the
                             // upgraded font cache, 2 or 4 = that factor
    bool  dlcOverlays;       // experimental: the archives in glimpse_dlc are laid over data\resident.ark
    bool  dlcTracks;         // experimental: the converted DLC track Quarry gets a track index (dlcfs.cpp)
    bool  dlcTracksOnline;   // also with SteamLAN=1: every player must have the same tracks
    bool  dlcMusic;          // Quarry plays the DLC's race music DLC3_Music in Music/Music's place (dlcmusic.cpp)
    bool  dlcModes;          // experimental: the converted DLC game mode Deadline (TimeTrialPlus) opened in quick play (dlcmode.cpp)
    bool  dlcPickups;        // experimental: Deadline's green pickup boxes that stop the countdown (dlcpickups.cpp)
    bool  dlcPersonalBests;  // original DLC personal bests in native TrackRecords
    bool  pickupVignette;    // with dlcPickups: a cyan glow along the screen's edges while the clock is stopped
    float deadlineBonus;     // seconds added to Deadline's start times without the boxes (Modes=1, Pickups=0)
    bool  dlcCars;           // experimental: the overlay's DLC cars listed in the front-end car select without UnlockAll (dlccars.cpp)
    bool  dlcCarsCareer;     // experimental, with dlcCars: also in the career's front-end car select
    int   textureDetail;     // 0 = full; 1-3 skip that many of the textures' top mip levels
    bool  glassFix;          // car glass: no sun specular from the glass's opacity (G-buffer alpha as on the Xbox, glass.cpp)
    bool  textureLoadFix;    // skip D3DX CPU mipmap generation for 2D DDS textures authored without mip levels (diag.cpp)
    bool  raceEndTiming;     // show Continue when the finish card can accept it
    bool  unlockAll;         // all tracks, events, episodes, game modes and vehicles selectable
    int   laps;              // laps per race, 0 = as the game
    bool  lapSelect;         // the track select's difficulty button picks the laps
    int   sessionLaps;       // SteamLAN: the session's laps for Race (the host's, online.cpp), 0 = as the game
    bool  uiLog;             // research: the game's calls into its Scaleform menus ([Debug] UiLog, with Log=1)
    bool  bonusContent;      // the PC cheat code extras: Expressway track, branded cars, liveries
    float wreckTime;         // seconds from a wreck to the respot, 0 = the game's
    int   unlockDecals;      // achievement decals: 0 off, 1 all, 2 only the online ones and the Pure decal (saved)
    int   saveBackups;       // copies of the save folder kept, made at every start (0 = none, 1-5)
    bool  boost;             // X held: extra acceleration for player 1
    int   boostKey;          // virtual key that works like X for the boost, 0 = none
    WORD  boostButton;       // pad button (XINPUT_GAMEPAD_*) held for the boost, 0 = none
    bool  handbrake;         // a held button or key throws the car into a slide (player 1)
    WORD  handbrakeButton;   // pad button, 0 = none
    WORD  handbrakeButtonIni;  // as in the INI, before the clash with the boost button is resolved
    int   handbrakeKey;      // virtual key, 0 = none
    int   handbrakeMode;     // 1 = the game's forced drift, 2 = the brake pedal (above a minimum speed)
    float handbrakeStrength; // mode 1: deceleration while held, m/s^2
    float handbrakeDrift;    // mode 1: yaw rate at full steering while held, rad/s
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
    int   ultrawideFix;      // wider than 16:9: 0 stock, 1 race cameras keep the 16:9 picture height (camera.cpp),
                             // 2 experimental: the game's aspect, menus and HUD at 16:9 (ultrawide.cpp)
    bool  ultrawideBlackBackdrop;  // UltrawideFix=2: extend unlock/reward black bars to cover dimmed 3D strips
    bool  zoomKeys;          // zoom in/out while a key is held, and reset
    int   zoomInKey, zoomOutKey, zoomResetKey;  // virtual key codes
    bool  photoMode;         // free camera during a race, world held still and HUD hidden
    int   photoModeKey;      // virtual key code (the pad's Back button works as well)
    int   hudKey;            // virtual key code that switches the HUD off and on, 0 = none
    float cameraShake;       // race cameras' shake: 1 = as the game, 0 = their rotation smoothed most
    bool  explosionShakeOff; // no camera shake from explosions and power plays
    bool  gradient;          // keep the cameras' screen gradient (the blue band at the top)
    bool  steamLan;          // the game's LAN mode over Steam (lobby, relayed P2P), PROTOTYPE
    char  lobbyName[64];     // players with the same name share a lobby; empty = the public one
    bool  friendsOnly;       // the lobby is open to Steam friends only; a friend's lobby is joined, the name not used
    unsigned patchMask;      // PATCH_* bits that may be applied (development, to bisect side effects)
};

enum PatchBits {
    PATCH_TRIGGERS = 0x1, PATCH_RUMBLE = 0x2, PATCH_PROMPTBAR = 0x4, PATCH_TITLETEXT = 0x8,
    PATCH_FRAMERATE = 0x10, PATCH_STREAMPOOL = 0x20, PATCH_ICONSIZE = 0x40, PATCH_MOTIONBLUR = 0x80,
    /* 0x100: was RenderScale, removed */ PATCH_PADNAMES = 0x200, PATCH_LOD = 0x400, PATCH_D3D = 0x800,
    PATCH_AUDIODIAG = 0x1000, PATCH_VIGNETTE = 0x2000,
    PATCH_UNLOCK = 0x4000, PATCH_SAVEDIAG = 0x8000, PATCH_ROUTEMARKER = 0x10000, PATCH_BOOST = 0x20000, PATCH_AIBOOST = 0x40000, PATCH_FLARES = 0x80000, PATCH_INTRO = 0x100000, PATCH_CAMERA = 0x200000,
    PATCH_ONLINE = 0x400000, PATCH_RACEEND = 0x800000, PATCH_LAPS = 0x1000000,
    PATCH_POWERPLAY_FLANGER = 0x2000000, PATCH_HUD = 0x4000000, PATCH_TEXLOAD = 0x8000000,
    PATCH_DLC_RECORDS = 0x10000000,
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
#define GLIMPSE_VERSION "2.3.0-beta.4"  // keep in step with config/glimpse_config.rc
void LogEnvironment();                     // system, executable, files and modules, at start
void LogForeignModules(const char* when);  // modules loaded from outside the Windows folder
void ModulePath(char* out, const char* file);  // file next to the DLL (game folder)
void DumpMark(const char* what);  // frame dump (diag.cpp): a note in the dumped frame's log

// XInput entry points, resolved from the system DLL
typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, XINPUT_VIBRATION*);
extern XInputGetState_t pXInputGetState;
extern XInputSetState_t pXInputSetState;
bool PadsWrapped();  // main.cpp: VirtualPads > 0 and an XInput DLL, so the game's joysticks are the virtual pads

// dinput_wrap.cpp
IDirectInput8A* WrapDirectInput8A(IDirectInput8A* real);
bool PadSlotConnected(int slot);
void RefreshPadSlots();

// diag.cpp
void InstallD3DHooks();
void CheckD3DHooks();     // puts our Present/Reset in front of hooks other modules set later (Steam overlay)  // device log/capture (Log=1), anisotropic filtering
void InstallTextureLoadFix();  // TextureLoadFix=1: skip D3DX CPU mipmap synthesis for single-level 2D DDS textures
// hdr.cpp - HDR=1
struct IDirect3D9;
struct IDirect3DDevice9;
struct _D3DPRESENT_PARAMETERS_;
void HdrPresentParams(IDirect3D9* d3d, UINT adapter, HWND focus, _D3DPRESENT_PARAMETERS_* pp);  // before CreateDevice / Reset
void HdrDeviceCreated(IDirect3DDevice9* dev);
void HdrDeviceLost();                                // before Reset
void HdrDeviceReset(IDirect3DDevice9* dev);          // after a successful Reset
bool HdrPresent(IDirect3DDevice9* dev, long* result);  // instead of the device's Present; false = not active
extern volatile DWORD g_lastPresent;                 // diag.cpp: GetTickCount of the last Present
void RunTextureDump();                               // texdump.cpp: DumpTextures=1, never returns (the game ends)
void HdrToneMapped();                                // right after the game's tone mapping pass: the gain (stage 2)
void HdrInstallUiHook();                             // after UltrawideDeviceCreated: Scaleform hook for the HUD copy
struct IDirect3DSurface9;
IDirect3DSurface9* HdrGainTarget();                  // this frame's gain target after the tone mapping, or NULL
void SaveStage0Tracking();     // around a pass of our own that sets samplers (all stages are kept)
struct IDirect3DBaseTexture9;
void TextureDetailExclude(IDirect3DBaseTexture9* t);  // diag.cpp: a texture of ours keeps its filter and mip levels
void TextureDetailForget(IDirect3DBaseTexture9* t);   // the same texture before it is released
void RestoreStage0Tracking();
void BeginEdgeClamp();   // until EndEdgeClamp: texture addressing clamped at the edges (motion blur pass)
void EndEdgeClamp();
struct IDirect3DPixelShader9;
unsigned long long PixelShaderHash(IDirect3DPixelShader9* ps);  // diag.cpp: the dump's hash of the runtime's copy, 0 = unreadable

// glass.cpp - GlassFix=1: the car glass draws of the race write specular intensity 0 into the G-buffer, as on the Xbox
struct GlassSaved {
    DWORD srcBlendAlpha, colorWrite1;  // as the game had them, put back after the draw
};
extern bool g_glassBound;                                // the pixel shader set last is a glass shader
void GlassDeviceCreated(DWORD flags);                    // before the shader hooks go in
void GlassShaderCreated(void* ps, unsigned long long hash);  // CreatePixelShader, with the hash of its code
void GlassPixelShaderSet(IDirect3DPixelShader9* ps);    // SetPixelShader
void GlassDeviceReset();                                 // after Reset: no pixel shader set
void GlassSaveTracking();                                // with SaveStage0Tracking / RestoreStage0Tracking
void GlassRestoreTracking();
bool GlassBegin(IDirect3DDevice9* dev, GlassSaved* saved, const char* call, UINT prims);  // g_glassBound draws: true = changed
void GlassEnd(IDirect3DDevice9* dev, const GlassSaved* saved);                           // after a changed draw
LONG GlassTakeChangedDraws();                            // Log=1: draws changed since the last call

// audio_diag.cpp
void InstallAudioDiagnostics();
void PauseGameSound(bool pause);  // photo mode: request audio-thread pause of all sound categories used
void AudioDiagProjectsChanged();  // audio thread: FMOD project swap/unload (drop music/CPUEngine groups and probes)
void AudioDiagResetState();       // audio thread: clear all system-owned diagnostic, volume and category state

// framerate.cpp
void PatchFrameRate();
void FramePace();  // after every Present: 60 fps pacing of the main loop

// postfx.cpp
void PatchMotionBlur();
void PatchVignette();
bool GradePassHooked();  // the colour grading pass is hooked (after PatchVignette): Deadline's Frozen Time presets show
void PatchBloomAndFlares();
void PatchDepthOfField();  // PhotoMode=1: the depth of field pass, used by the photo mode only
void ApplyLensFlare();  // per frame
// the photo mode's look, set by camera.cpp: bloom and exposure multiply the game's values (1 = as is,
// bloom 0 = off), depth of field strength 0..1 (0 = the game's own) with the focus distance in metres
extern float g_photoBloom, g_photoExposure, g_photoDof, g_photoFocus;

// boost.cpp
void BoostTick();        // per input frame: boost on X or BoostKey, power bar fill on LS+RS or PowerBarFillKey
void RacePad(int slot, XINPUT_GAMEPAD* pad);  // the pad as the game sees it in a race: boost/handbrake buttons, Y on D-pad down
bool RaceRunning();      // camera.cpp: a race camera ran in the last half second (in a race, not paused)
float BoostBlurScale();  // motion blur multiplier while boosting (1 = none)

// ultrawide.cpp (UltrawideFix=2)
struct IDirect3DDevice9;
struct _D3DPRESENT_PARAMETERS_;
void UltrawideDeviceCreated(IDirect3DDevice9* dev, _D3DPRESENT_PARAMETERS_* pp, HWND focus);
void UltrawidePresent(IDirect3DDevice9* dev);   // diag.cpp's Present hook, before the device's Present
void UltrawideDeviceLost();                     // diag.cpp's Reset hook, before Reset
void UltrawideDeviceReset(IDirect3DDevice9* dev, _D3DPRESENT_PARAMETERS_* pp);  // after a successful Reset
bool UltrawideBinkHook(const void* fn);         // fn is one of its Bink import hooks
bool ReadMovieName(void* movie, char* out, size_t size);  // Steam GFxMovieDefImpl
bool SwapSlot(void** slot, void* hook, void** orig);
bool SwapSlotChecked(void** slot, void* expected, void* hook, void** orig);
const void* UltrawideSetViewportHook();
bool IntroSkipBinkHook(const void* fn);         // game.cpp: fn is one of the intro skip's Bink import hooks

// camera.cpp
void PatchCamera();
void CameraTick();       // per input frame: zoom keys, HUD key, photo mode
void ApplyExplosionShake();  // per input frame: ExplosionShakeOff=1
bool SinglePlayerRace(); // a race camera runs on a single screen (not paused)
bool CameraBlocksPad();  // photo mode drives the camera with the pad: the game sees it idle
bool RaceCameraHooked(); // the race camera updates are hooked, so RaceRunning() can see a race
bool RaceSceneRunning(); // a crash or takedown camera of the race runs (a car is wrecked): still the race
DWORD RaceCameraAge();   // ms since the last race camera update (0xFFFFFFFF: none yet)
bool RaceCameraPose(float* pos, float* fwd);  // the game's race camera, updated in the last half second
bool OnlineSession();      // SteamLAN: Quazal's LAN mode is in use (online.cpp)

// savediag.cpp
void WatchSaveFiles();  // Log=1: log every change of the profile files
void StartSaveBackup();  // SaveBackups: copies the save folder at the start (a thread of its own)

// builds.cpp - the fixed addresses in this source are those of the Steam build the fix was made on
// (German depot 297864, built 4BE13C66). G() returns where the same code or
// data is in the running exe: unchanged on that build and on unknown ones (their byte checks fail),
// from a table on the Russian depot's build (297867, built 4BE3EA74), 0 if that table lacks it.
enum GameBuild { BUILD_UNKNOWN, BUILD_STEAM, BUILD_STEAM_RU };
GameBuild CurrentBuild();
const char* BuildName();
DWORD G(DWORD addr);
bool BuildMapComplete();   // false once G() was asked for an address the table lacks
bool MapEmbedded(DWORD value, DWORD* mapped);  // value is a table address: its place in this build

// game.cpp
bool BytesMatch(const void* at, const char* hex);     // hex string, no spaces; embedded addresses are mapped
bool CodeMatches(const void* at, const void* expected, size_t len);  // the same with raw bytes
void WriteMemory(void* at, const void* src, size_t len);
// the vtable's RTTI complete object locator (vt[-1]) is col and names the class name (".?AV...@@");
// reads unguarded: call it inside __try
bool ClassIs(void** vt, DWORD col, const char* name);
bool WriteSlot(void** slot, void* value);  // checked write of one vtable / import slot
void** FindImport(const char* dll, const char* name);  // audio_diag.cpp: the exe's import slot of dll!name
int IniPercent(const char* section, const char* key, int def, int lo, int hi);  // main.cpp: a whole percentage from the INI

// Scaleform GFxValue as laid out by this game's GFx build; GFx movie view vtable: +0x2C SetVariable, +0x30 GetVariable, +0x48 Invoke
struct GFxValue {
    DWORD type;
    DWORD pad;
    union {
        const char* str;
        double num;
        BYTE boolean;
    };
};
enum { GFX_BOOL = 2, GFX_NUMBER = 3, GFX_STRING = 4, GFX_RESULT_STRING = 0x14 };

typedef bool(__thiscall* GFxInvoke_t)(void* movie, const char* path, GFxValue* result, const GFxValue* args, unsigned count);
typedef bool(__thiscall* GFxSetVariable_t)(void* movie, const char* path, const GFxValue* value, int setType);
typedef bool(__thiscall* GFxGetVariable_t)(void* movie, GFxValue* result, const char* path);
void ApplyGamePatches();

// online.cpp - SteamLAN=1 (PROTOTYPE): the game's LAN mode over Steam. A transport for Quazal's
// sockets (its SocketDriver interface) carries the packets through Steam to the players of a Steam
// lobby, each of them under a virtual 10.x.y.z address.
void OnlineStart();
void OnlineFrame();                              // game thread, every frame: lobby rules in a LAN session
struct IDirect3DDevice9;
void OnlinePresent(IDirect3DDevice9* dev);       // draws the lobby banner before Present
enum { OVERLAY_TOP_LEFT, OVERLAY_BOTTOM_RIGHT };
// overlay.cpp: up to 4 lines of UTF-8 text on a dark panel on the back buffer, colors as D3DCOLOR, alpha 0..1
void OverlayDraw(IDirect3DDevice9* dev, const char* const* lines, const DWORD* colors, int count, int corner, float alpha);
// Deadline's pickup feedback: centred lines (size 1 large, 2 medium) from yFrac of the height down, two bars round
// the first line (barColor, alpha 0 = none), a glow along the screen's edges (edgeColor, alpha 0 = none)
void OverlayDrawFeedback(IDirect3DDevice9* dev, const char* const* lines, const DWORD* colors, const int* sizes, int count,
                         float yFrac, DWORD barColor, DWORD edgeColor);
bool OverlayPreloadFeedback(IDirect3DDevice9* dev, const wchar_t* chars);  // its fonts and glyphs ahead of use
void OverlayDeviceLost();                        // before Reset
void OverlayDeviceReset();                       // after a successful Reset
void MoviePresent(IDirect3DDevice9* dev);        // game.cpp: the movie skip prompt
// smaa.cpp: SMAA 1x on the 3D picture before the HUD
void SmaaDeviceCreated(IDirect3DDevice9* dev);
void SmaaDeviceLost();                           // before Reset
void SmaaSceneDone();                            // postfx.cpp: the tone mapper ran
void SmaaBeforeHud();                            // hdr.cpp: a Scaleform movie is about to be drawn
bool HdrUiHooked();                              // hdr.cpp: the Scaleform hook is in place
void SmaaPresent(IDirect3DDevice9* dev);         // diag.cpp: start of Present
void DlcInstall();         // [DLC] Overlays: while the DLL loads (dlcfs.cpp)
void DlcLogStats();
void DlcUninstall();
void DlcRegisterTracks();  // [DLC] Tracks: right after ApplyGamePatches, after the exe's static initialisers (dlcfs.cpp)
bool DlcTracksDecided();   // DlcRegisterTracks has run (or [DLC] Tracks=0): the DLC track set is final
// the DLC content other players need for a LAN game together (online.cpp, its thread, after DlcTracksDecided): sig
// "none" without a merged track overlay and without DLC cars the game itself lists online (UnlockAll=1 and an overlay
// with DLC cars), else 16 hex digits over the fix version, the registered tracks and their files, and with those cars
// their IDs and every merged overlay; names "Quarry=15, nem_quarry=16; 11 DLC cars (UnlockAll)" (dlcfs.cpp; once)
void DlcOnlineSignature(char* sig, size_t sigSize, char* names, size_t namesSize);
// the converter's DLC cars the merged overlay brings (dlccars.cpp): their count, the IDs comma separated into ids
int DlcCarsInOverlay(char* ids, size_t idsSize);
bool DlcOverlayHas(const char* path);  // the merged resident.ark is in use and has this path (dlcfs.cpp)
bool ResidentEntryMatches(const char* path, size_t size, uint32_t crc);
void DlcInstallModes();    // [DLC] Modes: right after ApplyGamePatches (dlcmode.cpp)
struct IDirect3DSurface9;
// dlcpickups.cpp - [DLC] Pickups=1: Deadline's green pickup boxes (stop the countdown)
void DlcInstallRecords();
using QpItemUpdate_t = void(__thiscall*)(void* self, DWORD index, DWORD selected, DWORD widget);
bool DlcInstallTrackListHook();
void DlcRecordsUpdateTrack(QpItemUpdate_t original, void* self, DWORD index, DWORD selected, DWORD widget);
bool DlcTrackBestArgs(void* movie, const char* path, const GFxValue* args, unsigned count, GFxValue* out);
void DlcRecordsRaceStart();
void DlcRecordsRaceEnd(BYTE* card);
void DlcRecordsDeadlineHud(const BYTE* resources, float start);
bool InstallDlcRaceEnd();  // shared by Deadline's result presentation and DLC personal bests
bool DeadlineRecordRules(const BYTE* setup, int countdown);
bool DeadlineRowShowsTimeLeft(const BYTE* time);
void DlcInstallPickups();  // right after DlcInstallModes
bool PickupsWanted();      // Pickups=1, Steam build, the merged overlay brings the mode (after DlcInstall; decided once)
void PickupsDeviceCreated(IDirect3DDevice9* dev);
void PickupsDeviceLost();                        // before Reset
void PickupsDepthSet(IDirect3DSurface9* ds);     // diag.cpp's SetDepthStencilSurface hook, before the device's
void PickupsBeforePost();                        // postfx.cpp's bloom hook, before the game's pass (the scene is done)
void PickupsAfterToneMap();                      // postfx.cpp's bloom hook, right after the game's tone mapping
void PickupsPresent(IDirect3DDevice9* dev);      // diag.cpp's Present hook: the fallback "+0,5s" line
// postfx.cpp's colour cube pass (vignette, gradient, grading): Deadline's "Frozen Time" preset for the call while
// the clock is frozen; saved: 17 floats (the block's +0x14..+0x57), put back by PickupsGradeEnd after the call
bool PickupsGradeBegin(unsigned char* params, float* saved);
void PickupsGradeEnd(unsigned char* params, const float* saved);
void DlcInstallCars();     // [DLC] Cars: right after DlcInstallModes (dlccars.cpp)
// [DLC] Music (music) and Deadline's sounds (menu): per input frame, wraps two audio thread messages once (dlcmusic.cpp)
void DlcMusicTick(bool music, bool menu);
bool DeadlineMenuWanted();  // dlcpickups.cpp: Deadline's HUD is on and the overlay brings the converted DLCMenu bank
bool DeadlineRaceNow();     // the race setup's mode is TimeTrialPlus (any thread; read only)
bool DlcMusicIsDlcEvent(void* evt);  // the FMOD event is one of DLC3_Music's (audio_diag.cpp's log)
void InstallDiagnostics(); // Log=1: crash lines and the game's debug text in the log (online.cpp; once)
void ApplyQualityCap();    // QualityCap: before the device is made and every frame (game.cpp)
bool KnownBuild();         // the exe is the Steam build all fixed addresses come from
void FixOpponentEngineBanks();  // audio_fix.cpp, at DLL load: before the game's audio system starts
void CheckOpponentEngineBanks();
void OpponentEnginesTick();     // per input frame: wraps audio messages 43 (car match) and/or 51 (unload cleanup)
void FixReverbPresets();       // at DLL load: the console's reverb presets (Log=1: logged)
void FixGameDspEffects();      // at DLL load: restore the game's DSP attachments
void HookSoundBanks();         // with the import hooks: System::setFileSystem (lifecycle init + optional bank fix)
bool AudioSystemInitHooked();  // true once System::setFileSystem was wrapped so sound-system resets are tracked
void HookVoiceDspCreation();   // after HookSoundBanks: voice units made with a working read callback
bool MenuHookReady();
void InstallMenuHook(bool forLaps = false, bool forTrackBests = false);
bool OnlineMenuHookInstalled();
void* OnlineInvokeHook();       // online.cpp's Invoke wrapper while it holds the slot, else NULL
bool LapRaceHooksInstalled();   // lap_menu.cpp wraps cLaps::Reset and cRaceResults::Update (slot 3)
int HostLaps();                 // the laps this player offers as a SteamLAN host (0 = the game's)
void RemoveLapMenuHook();         // the game's Scaleform Invoke / SetVariable calls (lap choice; UiLog=1: logged)
bool MenuHookInstalled();       // the Scaleform slots are ours
bool UiSetText(void* movie, const char* path, const char* text);  // SetVariable past the hook
bool LapMenuInstalled();
void InstallLapMenu();        // LapSelect=1: Tab / pad Y pick the laps on the track select
void* PromptBarMovieNow();     // game.cpp: the prompt bar movie the game holds now
bool FrameHookInstalled();    // without wrapped pads: the per-frame extras run from the input update
void LapMenuInvoke(void* movie, const char* path, const GFxValue* args, unsigned count);
bool LapMenuInvokeArgs(void* movie, const char* path, const GFxValue* args, unsigned count, GFxValue* out);  // the finish card's line
const char* LapMenuLabel(void* movie, const char* path, const GFxValue* value);  // our label for the game's, or NULL
void LapMenuButtonTextSet(void* movie, const char* path, const char* text, bool substituted, bool ok);  // after a button text write
bool LapMenuButtonX(void* movie, const char* path, double* x);  // the button's place kept while our label changes
void LapMenuTick();           // every frame: a label held back while the prompt bar slides in
bool QuickPlayModeListReady();
bool HookQuickPlayModeList();  // dlcmode.cpp: the quick play mode list's item update (DLC modes, lap choice)
void LapMenuModeHighlighted(const BYTE* screen, const char* gameMode);  // from that hook: the highlighted mode's GameMode name
void HookReverbLog();          // Log=1, with the import hooks: every System::setReverbProperties call
void AudioSystemEnsureCurrent(void* system, const char* why);  // audio thread: reset cached state on FMOD::System* change
void AudioFixNoteGroupVolume(void* group, float stockVol, float appliedVol);  // audio thread: Log=1 ducking move check
void AudioFixNoteEventPeak();     // audio thread: Log=1 EventMgr sampled high-water mark
void PatchPadNames();
void OpponentEngineVolumeTick();  // audio_diag.cpp, every frame on the game thread
void OnInputFrame();       // called from the virtual device poll (pads not wrapped: the input update), on the game thread
void NotePadActivity();
