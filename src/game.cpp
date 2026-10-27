// Split/Second controller fix - patches into SplitSecond.exe (Steam build, image base 0x400000)
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Addresses in the decrypted image. Each one is checked against the expected bytes first.
static BYTE* const kPadPresets = (BYTE*)0x00CFABF8;          // per-device binding presets, preset 0 = Xbox 360 pad
static BYTE* const kPromptBarSetButtons = (BYTE*)0x004DADE0;  // cLayer_PromptBar: sets PC key icons + key names
static void** const kIatXInputSetState = (void**)0x00B9B370;

static const int kPresetEntries = 57;  // 5 dwords each: action, key, source index, source type, pov angle
enum { SRC_BUTTON = 0, SRC_AXIS = 1, SRC_POV = 2 };
enum { AXIS_ZPOS = 4, AXIS_ZNEG = 5, AXIS_RZPOS = 10 };
enum { ACTION_THROTTLE = 0x29, ACTION_BRAKE = 0x2A };

bool BytesMatch(const void* at, const char* hex)
{
    const BYTE* p = (const BYTE*)at;
    __try {
        for (; hex[0] && hex[1]; hex += 2, p++) {
            char b[3] = { hex[0], hex[1], 0 };
            if (*p != (BYTE)strtoul(b, NULL, 16)) return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

void WriteMemory(void* at, const void* src, size_t len)
{
    DWORD old;
    VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(at, src, len);
    VirtualProtect(at, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, len);
}

// ---------------------------------------------------------------- triggers

static void PatchTriggerPreset()
{
    DWORD* e = (DWORD*)kPadPresets;
    DWORD* throttle = NULL;
    DWORD* brake = NULL;
    for (int i = 0; i < kPresetEntries; i++, e += 5) {
        if (e[0] == ACTION_THROTTLE && e[3] == SRC_AXIS && e[2] == AXIS_ZNEG) throttle = e;
        if (e[0] == ACTION_BRAKE && e[3] == SRC_AXIS && e[2] == AXIS_ZPOS) brake = e;
    }
    if (!throttle || !brake) {
        Log("trigger preset not found, leaving bindings alone");
        return;
    }
    // LT already sits on Z+ (our Z axis carries only LT); move the throttle from Z- to RZ+ (RT)
    DWORD rz = AXIS_RZPOS;
    WriteMemory(&throttle[2], &rz, sizeof(rz));
    Log("360 preset: throttle -> RZ+, brake stays Z+");
}

// ---------------------------------------------------------------- binding names

// {code, name} tables behind the controls screen; a name without a translation is shown as is
struct InputName {
    DWORD code;
    const char* name;
};
static InputName* const kJoyAxisNames = (InputName*)0x00CFA5E0;    // JOY_AXIS_XPOS .. JOY_AXIS_SLIDER1NEG
static InputName* const kJoyButtonNames = (InputName*)0x00CFA6E0;  // JOY_BUTTON_1 .. JOY_BUTTON_16

static bool s_knownBuild;

void PatchPadNames()
{
    if (!s_knownBuild) return;
    static const char* const buttons[] = { "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS", "RS" };
    // DirectInput order: X+ X- Y+ Y- Z+ Z- RX+ RX- RY+ RY- RZ+ RZ-; Y grows downwards
    const char* axes[12] = { "LS Right", "LS Left", "LS Down", "LS Up", "LT", "-", "RS Right", "RS Left", "RS Down", "RS Up", "RT", "-" };
    if (!g_cfg.separateTriggers) {
        axes[5] = "RT";   // both triggers on Z
        axes[10] = "-";
    }
    if (!BytesMatch(kJoyButtonNames[0].name, "4A4F595F425554544F4E5F3100") ||  // "JOY_BUTTON_1"
        !BytesMatch(kJoyAxisNames[0].name, "4A4F595F415849535F58504F5300")) {  // "JOY_AXIS_XPOS"
        Log("binding name tables not found");
        return;
    }
    for (int i = 0; i < 10; i++) {
        if (kJoyButtonNames[i].code != (DWORD)i) return;
        WriteMemory(&kJoyButtonNames[i].name, &buttons[i], sizeof(char*));
    }
    for (int i = 0; i < 12; i++) {
        if (kJoyAxisNames[i].code != (DWORD)i) return;
        WriteMemory(&kJoyAxisNames[i].name, &axes[i], sizeof(char*));
    }
    Log("pad binding names patched");
}

// ---------------------------------------------------------------- streaming pool

// Sector streaming pool in MB (tweakable "Sectors/Streaming/d; Memory Pool MB"), read when a level
// loads. 57 MB is the console budget; once it is full the streamer evicts the high mips and the
// world falls back to blurry or flat textures. More pool needs the 4 GB address space (LAA).
static int* const kStreamPoolMB = (int*)0x00D389E0;

static void RaiseStreamPool()
{
    if (g_cfg.streamPoolMB <= 0) return;
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    if (ms.ullTotalVirtual <= 0x80000000ull) {
        Log("no large address space (exe without LAA flag), stream pool stays at %d MB", *kStreamPoolMB);
        return;
    }
    if (*kStreamPoolMB != 57) {
        Log("stream pool tweakable not recognised (%d), left alone", *kStreamPoolMB);
        return;
    }
    *kStreamPoolMB = g_cfg.streamPoolMB;
    Log("stream pool 57 -> %d MB", g_cfg.streamPoolMB);
}

// ---------------------------------------------------------------- powerplay icon size

// When the HUD loads its parameters (0x4C9AF0) the PC port multiplies the powerplay icon scales by
// screen width / 1280, but the icon renderer (0x55E740) already sizes icons by the screen width.
// Above 1280 pixels the icons grow twice: 1.5x at 1080p, 3x at 4K. Loading 1280.0 (0xBB847C)
// instead of the width makes that factor 1, as on the console.
static BYTE* const kIconScaleWidth = (BYTE*)0x004C9E18;  // fild dword ptr [edi+2B0h]

static void FixPowerplayIconSize()
{
    if (!g_cfg.fixIconSize) return;
    if (!BytesMatch(kIconScaleWidth, "DB87B0020000") || !BytesMatch(kIconScaleWidth + 0x24, "D905FCA8BA00") ||
        *(float*)0x00BB847C != 1280.0f) {
        Log("powerplay icon scale code not recognised");
        return;
    }
    static const BYTE fld1280[6] = { 0xD9, 0x05, 0x7C, 0x84, 0xBB, 0x00 };  // fld dword ptr [0BB847Ch]
    WriteMemory(kIconScaleWidth, fld1280, sizeof(fld1280));
    Log("powerplay icon size fixed");
}

// ---------------------------------------------------------------- rumble

static XInputSetState_t s_gameXInputSetState;

static DWORD WINAPI Hook_XInputSetState(DWORD slot, XINPUT_VIBRATION* vib)
{
    if (slot >= (DWORD)g_cfg.virtualPads || !PadSlotConnected((int)slot)) return ERROR_DEVICE_NOT_CONNECTED;
    XINPUT_VIBRATION v = *vib;
    if (g_cfg.rumbleScale != 1.0f) {
        float l = v.wLeftMotorSpeed * g_cfg.rumbleScale, r = v.wRightMotorSpeed * g_cfg.rumbleScale;
        v.wLeftMotorSpeed = (WORD)(l > 65535 ? 65535 : l);
        v.wRightMotorSpeed = (WORD)(r > 65535 ? 65535 : r);
    }
    if (TestFeed* feed = GetTestFeed()) {
        feed->rumble[slot] = v;
        return ERROR_SUCCESS;
    }
    return s_gameXInputSetState(slot, &v);
}

static void HookRumble()
{
    HMODULE x13 = GetModuleHandleA("xinput1_3.dll");
    void* expected = x13 ? (void*)GetProcAddress(x13, "XInputSetState") : NULL;
    if (!expected || *kIatXInputSetState != expected) {
        Log("XInputSetState import not where expected, rumble left as is");
        return;
    }
    s_gameXInputSetState = (XInputSetState_t)expected;
    void* hook = (void*)Hook_XInputSetState;
    WriteMemory(kIatXInputSetState, &hook, sizeof(hook));
    Log("rumble hooked");
}

// ---------------------------------------------------------------- prompt bar glyphs

// Scaleform GFxValue as laid out by this game's GFx build
struct GFxValue {
    DWORD type;
    DWORD pad;
    union {
        const char* str;
        double num;
    };
};
enum { GFX_STRING = 4, GFX_RESULT_STRING = 0x14 };

typedef void(__thiscall* PromptBarSetButtons_t)(void* self, const char* buttons);
typedef bool(__thiscall* GFxInvoke_t)(void* movie, const char* path, GFxValue* result, const GFxValue* args, unsigned count);

static PromptBarSetButtons_t s_origSetButtons;
static void* s_promptBar;
static char s_promptButtons[512];  // last button list, to rebuild the key captions when switching back
static bool s_padGlyphsShown;
static DWORD s_lastPadActivity, s_lastKbmActivity;
static BYTE s_lastKeys[256];

static const char* const kIconPaths[5] = {
    "main.bar_mc.buttonA_mc.icon_mc.gotoAndStop",
    "main.bar_mc.buttonB_mc.icon_mc.gotoAndStop",
    "main.bar_mc.buttonX_mc.icon_mc.gotoAndStop",
    "main.bar_mc.buttonY_mc.icon_mc.gotoAndStop",
    "main.bar_mc.buttonSTART_mc.icon_mc.gotoAndStop",
};
// frame labels inside Resident|UI/CommonUI/Prompt_Bar.gfx
static const char* const kXenonFrames[5] = { "Xenon_A", "Xenon_B", "Xenon_X", "Xenon_Y", "Xenon_Start" };
static const char* const kPs3Frames[5] = { "PS3_Cross", "PS3_Circle", "PS3_Square", "PS3_Triangle", "PS3_Start" };
static const char kPcFrame[] = "PC_GenKeyLong";

static void* PromptBarMovie(void* self)
{
    BYTE* layer = *(BYTE**)((BYTE*)self + 4 + 0x68);
    return layer ? *(void**)(layer + 4) : NULL;
}

static void SetPromptIcons(void* self, bool pad)
{
    void* movie = PromptBarMovie(self);
    if (!movie) return;
    GFxInvoke_t invoke = (GFxInvoke_t)(*(void***)movie)[0x48 / 4];
    const char* const* frames = g_cfg.glyphs == GLYPHS_AUTO_PS ? kPs3Frames : kXenonFrames;
    for (int i = 0; i < 5; i++) {
        GFxValue arg = {}, result = {};
        arg.type = GFX_STRING;
        arg.str = pad ? frames[i] : kPcFrame;
        result.type = GFX_RESULT_STRING;
        invoke(movie, kIconPaths[i], &result, &arg, 1);
    }
}

static bool WantPadGlyphs()
{
    return g_cfg.glyphs != GLYPHS_KEYBOARD && s_lastPadActivity && s_lastPadActivity >= s_lastKbmActivity;
}

static void __fastcall Hook_PromptBarSetButtons(void* self, void*, const char* buttons)
{
    s_origSetButtons(self, buttons);
    s_promptBar = self;
    if (buttons != s_promptButtons) strncpy_s(s_promptButtons, buttons, _TRUNCATE);
    s_padGlyphsShown = false;
    if (WantPadGlyphs()) {
        SetPromptIcons(self, true);
        s_padGlyphsShown = true;
    }
}

// Jumps from 'at' to 'hook'; returns a trampoline that runs the 'len' displaced bytes (whole
// instructions without relative operands) and continues behind them.
void* Detour(BYTE* at, int len, void* hook)
{
    BYTE* tramp = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return NULL;
    memcpy(tramp, at, len);
    tramp[len] = 0xE9;
    *(DWORD*)(tramp + len + 1) = (DWORD)(at + len) - (DWORD)(tramp + len + 5);

    BYTE jmp[16];
    memset(jmp, 0x90, sizeof(jmp));
    jmp[0] = 0xE9;
    *(DWORD*)(jmp + 1) = (DWORD)hook - (DWORD)(at + 5);
    WriteMemory(at, jmp, len);
    return tramp;
}

static void HookPromptBar()
{
    if (!BytesMatch(kPromptBarSetButtons, "81ECA4000000535556")) {
        Log("prompt bar code not recognised, glyphs disabled");
        return;
    }
    s_origSetButtons = (PromptBarSetButtons_t)Detour(kPromptBarSetButtons, 6, (void*)Hook_PromptBarSetButtons);
    Log("prompt bar hooked");
}

// ---------------------------------------------------------------- title screen text

// Dictionary lookup, key -> localised UTF-8 text (or a copy of the key)
static BYTE* const kTextLookup = (BYTE*)0x00A15BF0;
typedef const char*(__thiscall* TextLookup_t)(void* dict, const char* key);
static TextLookup_t s_origLookup;

static void* s_dict;            // dictionary object, seen on the first lookup
static char* s_pressStart;      // TEXT_PRESS_START inside the dictionary
static char s_pressStartOrig[256], s_pressStartPad[256];
static bool s_pressStartIsPad;

// The flash title screen reads the dictionary text directly, so the entry itself is rewritten:
// "ZUM STARTEN ENTER DRUECKEN" -> "ZUM STARTEN START DRUECKEN", with the localised name of the
// Enter key (KEY_RETURN) as the word to replace.
static void PreparePressStart()
{
    char* text = (char*)s_origLookup(s_dict, "TEXT_PRESS_START");
    if (!text || !strcmp(text, "TEXT_PRESS_START")) return;  // missing entries come back as the key
    lstrcpynA(s_pressStartOrig, text, sizeof(s_pressStartOrig));
    char enter[64];
    lstrcpynA(enter, s_origLookup(s_dict, "KEY_RETURN"), sizeof(enter));
    size_t n = strlen(enter);
    for (const char* p = s_pressStartOrig; n && *p; p++) {
        if (_strnicmp(p, enter, n) == 0 && n >= 5) {  // rewritten in place, so it must not grow
            _snprintf_s(s_pressStartPad, _TRUNCATE, "%.*s%s%s", (int)(p - s_pressStartOrig), s_pressStartOrig, "START", p + n);
            s_pressStart = text;
            Log("title text: '%s' -> '%s'", s_pressStartOrig, s_pressStartPad);
            return;
        }
    }
    Log("title text: '%s' has no '%s', unchanged", s_pressStartOrig, enter);
}

static void UpdatePressStart(bool pad)
{
    if (!s_pressStart || pad == s_pressStartIsPad) return;
    strcpy_s(s_pressStart, strlen(s_pressStartOrig) + 1, pad ? s_pressStartPad : s_pressStartOrig);
    s_pressStartIsPad = pad;
}

static const char* __fastcall Hook_TextLookup(void* dict, void*, const char* key)
{
    if (!s_dict) s_dict = dict;
    return s_origLookup(dict, key);
}

static void HookTitleText()
{
    if (!BytesMatch(kTextLookup, "568BF1833E00752E")) {
        Log("text lookup not recognised, title text unchanged");
        return;
    }
    s_origLookup = (TextLookup_t)Detour(kTextLookup, 6, (void*)Hook_TextLookup);
    Log("title text hooked");
}

void NotePadActivity()
{
    s_lastPadActivity = GetTickCount() | 1;
}

void OnInputFrame()
{
    if (g_cfg.profile) ProfilerTick();
    if (g_cfg.log) AudioDiagTick();
    if (g_cfg.glyphs == GLYPHS_KEYBOARD) return;

    // a controller that is already connected counts as the active device until a key is pressed
    static bool first = true;
    if (first) {
        first = false;
        for (int i = 0; i < XUSER_MAX_COUNT && !s_lastPadActivity; i++)
            if (PadSlotConnected(i)) s_lastPadActivity = 1;
    }

    // keyboard / mouse button presses switch the glyphs back to keys
    BYTE keys[256];
    if (GetKeyboardState(keys)) {
        for (int vk = 1; vk < 256; vk++) {
            if ((keys[vk] & 0x80) && !(s_lastKeys[vk] & 0x80)) {
                s_lastKbmActivity = GetTickCount() | 1;
                break;
            }
        }
        memcpy(s_lastKeys, keys, sizeof(keys));
    }

    bool want = WantPadGlyphs();
    static bool pressStartPrepared;
    if (s_dict && !pressStartPrepared) {
        pressStartPrepared = true;
        PreparePressStart();
    }
    UpdatePressStart(want);
    if (s_promptBar && want != s_padGlyphsShown) {
        if (want) {
            SetPromptIcons(s_promptBar, true);
            s_padGlyphsShown = true;
        } else {
            // the key frames lose their captions once left, so let the game rebuild the bar
            Hook_PromptBarSetButtons(s_promptBar, NULL, s_promptButtons);
        }
        Log("glyphs -> %s", want ? "pad" : "keyboard");
    }
    if (TestFeed* feed = GetTestFeed()) feed->glyphsPad = s_padGlyphsShown;
}

void ApplyGamePatches()
{
    if (!BytesMatch((void*)0x0040F0F0, "5355568BB16CCF0000")) {
        Log("unknown SplitSecond.exe build, no patches applied");
        return;
    }
    s_knownBuild = true;
    if (g_cfg.separateTriggers) PatchTriggerPreset();
    HookRumble();
    HookPromptBar();
    if (g_cfg.glyphs != GLYPHS_KEYBOARD) HookTitleText();
    PatchFrameRate();
    RaiseStreamPool();
    FixPowerplayIconSize();
    PatchMotionBlur();
    if (g_cfg.profile) InstallProfiler();
    if (g_cfg.log) InstallAudioDiagnostics();
}
