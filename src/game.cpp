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
    if (!s_knownBuild || !(g_cfg.patchMask & PATCH_PADNAMES)) return;
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

// Sector streaming pool in MB (tweakable "Sectors/Streaming/d; Memory Pool MB", 57 = the console
// budget). Once it is full the streamer evicts the high mips and props and the world falls back
// to blurry or flat textures. Level data rewrites the tweakable before the pool is created
// (0x7C6140), so the read at 0x7C654F goes through a stub that takes the larger of the level's
// value and ours. More pool needs the 4 GB address space (LAA).
static int* const kStreamPoolMB = (int*)0x00D389E0;
static int s_streamPoolMB;     // configured size
static int s_levelStreamPoolMB;  // what the level asked for, for the log

__declspec(naked) static void Stub_ReadStreamPool()
{
    __asm {
        mov eax, dword ptr ds:[0x00D389E0]
        mov s_levelStreamPoolMB, eax
        cmp eax, s_streamPoolMB
        jge done
        mov eax, s_streamPoolMB
    done:
        ret
    }
}

static void RaiseStreamPool()
{
    if (g_cfg.streamPoolMB <= 0) return;
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    if (ms.ullTotalVirtual <= 0x80000000ull) {
        Log("no large address space (exe without LAA flag), stream pool stays at %d MB", *kStreamPoolMB);
        return;
    }
    if (!BytesMatch((void*)0x007C654F, "A1E089D300")) {
        Log("stream pool read not recognised, left alone");
        return;
    }
    s_streamPoolMB = g_cfg.streamPoolMB;
    BYTE call[5] = { 0xE8 };
    *(DWORD*)(call + 1) = (DWORD)Stub_ReadStreamPool - (0x007C654F + 5);
    WriteMemory((void*)0x007C654F, call, sizeof(call));
    Log("stream pool at least %d MB", s_streamPoolMB);
}

// ---------------------------------------------------------------- powerplay icon size

// When the HUD loads its parameters (0x4C9AF0) the PC port multiplies the powerplay icon scales by
// render width / 1280, but the icon renderer (0x55E740) already sizes icons by the screen width.
// Above 1280 pixels the icons grow twice: 1.5x at 1080p, 3x at 4K. The scales are divided back
// in the loaded data, once per load. Patching the multiplication in the code instead set off
// SecuROM's integrity checks (they stop freeing their VM contexts and the game hangs minutes later).
static void* const* const kHud = (void* const*)0x00D66B24;  // HUD, its parameter block at +0x20

static void FixPowerplayIconSize()
{
    static BYTE* s_params;
    static float s_written[2];
    BYTE* hud = (BYTE*)*kHud;
    if (!hud) return;
    BYTE* p = hud + 0x20;
    float* scales = (float*)(p + 0x214);  // +0x214, +0x218
    int width = *(int*)(p + 0x2B0);
    if (width <= 0) return;
    if (p == s_params && scales[0] == s_written[0] && scales[1] == s_written[1]) return;  // done
    float factor = width / 1280.0f;
    s_params = p;
    s_written[0] = scales[0] / factor;
    s_written[1] = scales[1] / factor;
    scales[0] = s_written[0];
    scales[1] = s_written[1];
    Log("powerplay icon scales %.3f %.3f (width %d)", s_written[0], s_written[1], width);
}

// ---------------------------------------------------------------- LOD distances

// Tweakables with their stock values: environment LOD switch and fade distances (normal and
// split screen) and the particle LOD distances, in metres. The low LOD distance is left alone:
// beyond it each level hands over to its backdrop models, and pushing it out makes both draw at
// once (far buildings flicker). The game streams the more detailed sectors further out, which the
// bigger streaming pool absorbs.
static const struct { float* var; float stock; } kLodDistances[] = {
    { (float*)0x00D38A90, 150.0f },  // EnvironmentSS/LODs/a; High LOD Draw Dist
    { (float*)0x00D38A98, 300.0f },  // b; Mid LOD Draw Dist
    { (float*)0x00D38AA8, 30.0f },   // e; High LOD Fade Range
    { (float*)0x00D38AB8, 60.0f },   // Split Screen High LOD Draw Dist
    { (float*)0x00D38AC0, 120.0f },  // Split Screen Mid LOD Draw Dist
    { (float*)0x00D38AD0, 20.0f },   // Split Screen High LOD Fade Range
    { (float*)0x00D373DC, 150.0f },  // Particles/Min LOD Distance
    { (float*)0x00D373FC, 200.0f },  // Particles/Further LOD Distance
    { (float*)0x00D3742C, 100.0f },  // Particles/Split Screen/f; Min LOD Distance
    { (float*)0x00D193F0, 500.0f },  // Debris/FadeInDist
    { (float*)0x00D389F0, 400.0f },  // Sectors/Streaming/e; Max Sector Stream Dist
    { (float*)0x00D9AFD4, 0.0f },    // Sectors/Entities/a; Default Entity Stream Dist (set at run time)
    { (float*)0x00D5FDDC, 400.0f },  // Sectors/Entities/b; Max Entity Stream Dist
    { (float*)0x00D40A84, 5.0f },    // powerplays/DrawDists/Auto Size Scale (assets without a set distance:
    { (float*)0x00D40A8C, 200.0f },  // powerplays/DrawDists/Auto Offset      size * scale + offset, 0x813797)
};

static float s_lodWritten[sizeof(kLodDistances) / sizeof(kLodDistances[0])];

// Levels load their own LOD distances, so every value that no longer holds what we wrote is
// taken as the new base and scaled again. Called once per input frame.
static void ScaleLodDistances()
{
    if (g_cfg.lodDistanceScale == 1.0f) return;
    for (size_t i = 0; i < sizeof(kLodDistances) / sizeof(kLodDistances[0]); i++) {
        float v = *kLodDistances[i].var;
        if (v == s_lodWritten[i] || v <= 0 || v > 100000.0f) continue;
        s_lodWritten[i] = v * g_cfg.lodDistanceScale;
        // the mid LOD must still switch before the low LOD (0xD38AA0 / split screen 0xD38AC8)
        if (kLodDistances[i].var == (float*)0x00D38A98 && s_lodWritten[i] > *(float*)0x00D38AA0 * 0.9f)
            s_lodWritten[i] = *(float*)0x00D38AA0 * 0.9f;
        if (kLodDistances[i].var == (float*)0x00D38AC0 && s_lodWritten[i] > *(float*)0x00D38AC8 * 0.9f)
            s_lodWritten[i] = *(float*)0x00D38AC8 * 0.9f;
        *kLodDistances[i].var = s_lodWritten[i];
        if (i == 0) Log("LOD distances rescaled (high %.0f -> %.0f)", v, s_lodWritten[i]);
        if (kLodDistances[i].var == (float*)0x00D389F0 || kLodDistances[i].var == (float*)0x00D5FDDC ||
            kLodDistances[i].var == (float*)0x00D9AFD4)
            Log("stream distance %08X: %.0f -> %.0f", (DWORD)kLodDistances[i].var, v, s_lodWritten[i]);
    }
}

// Powerplay assets (terminal facade, radar tower, cranes, ...) get their draw distance per level
// from Params/PP_DrawDists_<level>_<track>.params. The records are a list: [0xD8B384] -> node,
// node+0 = next, the list ends at 0xD8B388; node+0x10 = name ("powerplays/DrawDists/<level>/<asset>"),
// node+0x90 = draw distance in metres (0 = derived from the asset size). The asset test (0x813730)
// reads the record every time, so scaling it in place is enough.
static void ScalePowerplayDrawDists()
{
    static struct { DWORD node; float written; } s_done[1024];
    static int s_doneCount;
    if (g_cfg.lodDistanceScale == 1.0f) return;
    int scaled = 0;
    __try {
        DWORD node = *(DWORD*)0x00D8B384;
        for (int n = 0; node && node != 0x00D8B388 && n < 4096; n++, node = *(DWORD*)node) {
            float* dist = (float*)(node + 0x90);
            if (*dist <= 0 || *dist > 100000.0f) continue;
            int i = 0;
            while (i < s_doneCount && s_done[i].node != node) i++;
            if (i < s_doneCount && s_done[i].written == *dist) continue;  // already scaled
            if (i == s_doneCount) {
                if (s_doneCount == 1024) continue;
                s_done[s_doneCount++].node = node;
            }
            *dist *= g_cfg.lodDistanceScale;
            s_done[i].written = *dist;
            scaled++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (scaled) Log("powerplay draw distances: %d scaled", scaled);
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

typedef bool(__thiscall* GFxInvoke_t)(void* movie, const char* path, GFxValue* result, const GFxValue* args, unsigned count);
typedef bool(__thiscall* GFxSetVariable_t)(void* movie, const char* path, const GFxValue* value, int setType);

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
// key captions SetButtons writes into the PC frames, fixed per button
static const char* const kCaptionPaths[5] = {
    "main.bar_mc.buttonA_mc.icon_mc.keyText_txt.text",
    "main.bar_mc.buttonB_mc.icon_mc.keyText_txt.text",
    "main.bar_mc.buttonX_mc.icon_mc.keyText_txt.text",
    "main.bar_mc.buttonY_mc.icon_mc.keyText_txt.text",
    "main.bar_mc.buttonSTART_mc.icon_mc.keyText_txt.text",
};
static const char* const kCaptionKeys[5] = { "KEY_RETURN", "KEY_ESCAPE", "KEY_SPACE", "KEY_TAB", "KEY_END" };

// The game's own copies of the icon paths (only SetButtons uses them). While pad glyphs are on,
// their last letter is changed so SetButtons' gotoAndStop calls miss and leave our frames alone:
// some screens (main menu) call SetButtons every frame, after our per-frame update.
static char* const kGameIconPaths[5] = { (char*)0x00BBAC7C, (char*)0x00BBAC50, (char*)0x00BBAC24,
                                         (char*)0x00BBABF8, (char*)0x00BBABC8 };

static bool GameIconPathsKnown()
{
    for (int i = 0; i < 5; i++)
        if (strcmp(kGameIconPaths[i], kIconPaths[i])) return false;
    return true;
}

static void MuteGameIconPaths(bool mute)
{
    for (char* path : kGameIconPaths) {
        char* last = path + strlen(path) - 1;
        char c = mute ? 'q' : 'p';  // gotoAndStop / gotoAndStoq
        if (*last != c) WriteMemory(last, &c, 1);
    }
}

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
    if (pad) return;
    // the key frames come back without captions
    GFxSetVariable_t setVariable = (GFxSetVariable_t)(*(void***)movie)[0x2C / 4];
    for (int i = 0; i < 5; i++) {
        GFxValue value = {};
        value.type = GFX_STRING;
        value.str = kCaptionKeys[i];
        setVariable(movie, kCaptionPaths[i], &value, 1);
    }
}

static bool WantPadGlyphs()
{
    return g_cfg.glyphs != GLYPHS_KEYBOARD && s_lastPadActivity && s_lastPadActivity >= s_lastKbmActivity;
}

// The prompt bar is a single layer, reached the way the game's own screens do (0x469CB0):
// [[[0xD64E0C]+8]] - 4. Code hooks on SetButtons (0x4DADE0) set off SecuROM's integrity checks, so
// instead of reacting to SetButtons the pad frames are applied every frame while the pad is in use.
static void* PromptBar()
{
    __try {
        BYTE* ui = *(BYTE**)0x00D64E0C;
        if (!ui) return NULL;
        BYTE** slot = *(BYTE***)(ui + 8);
        BYTE* layer = slot ? *slot : NULL;
        return layer ? layer - 4 : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

static void UpdatePromptBar(bool pad)
{
    if (pad != s_padGlyphsShown) MuteGameIconPaths(pad);
    void* bar = PromptBar();
    if (!bar) return;
    if (pad) {
        SetPromptIcons(bar, true);  // a freshly loaded bar starts on the key frames
    } else if (s_padGlyphsShown) {
        SetPromptIcons(bar, false);
    }
    if (pad != s_padGlyphsShown) {
        s_padGlyphsShown = pad;
        Log("glyphs -> %s", pad ? "pad" : "keyboard");
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

static bool s_promptBarKnown;

static void CheckPromptBar()
{
    s_promptBarKnown = BytesMatch(kPromptBarSetButtons, "81ECA4000000535556") &&
                       BytesMatch((void*)0x00469CB0, "A10C4ED600578BF98B4808") && GameIconPathsKnown();
    Log(s_promptBarKnown ? "prompt bar found" : "prompt bar code not recognised, glyphs disabled");
}

// ---------------------------------------------------------------- title screen text

// Dictionary lookup, key -> localised UTF-8 text (or a copy of the key). Called directly, not
// hooked: the dictionary object is the global the game's own callers pass (0x518416).
static BYTE* const kTextLookup = (BYTE*)0x00A15BF0;
typedef const char*(__thiscall* TextLookup_t)(void* dict, const char* key);
static TextLookup_t s_origLookup;

static void* s_dict;            // dictionary object, [0xD64E28]
static char* s_pressStart;      // TEXT_PRESS_START inside the dictionary
static char s_pressStartOrig[256], s_pressStartPad[256];
static bool s_pressStartIsPad;

// The flash title screen reads the dictionary text directly, so the entry itself is rewritten:
// "ZUM STARTEN ENTER DRUECKEN" -> "ZUM STARTEN START DRUECKEN", with the localised name of the
// Enter key (KEY_RETURN) as the word to replace.
// false while the dictionary is not loaded yet
static bool PreparePressStart()
{
    char* text = (char*)s_origLookup(s_dict, "TEXT_PRESS_START");
    if (!text || !strcmp(text, "TEXT_PRESS_START")) return false;  // missing entries come back as the key
    lstrcpynA(s_pressStartOrig, text, sizeof(s_pressStartOrig));
    char enter[64];
    lstrcpynA(enter, s_origLookup(s_dict, "KEY_RETURN"), sizeof(enter));
    size_t n = strlen(enter);
    for (const char* p = s_pressStartOrig; n && *p; p++) {
        if (_strnicmp(p, enter, n) == 0 && n >= 5) {  // rewritten in place, so it must not grow
            _snprintf_s(s_pressStartPad, _TRUNCATE, "%.*s%s%s", (int)(p - s_pressStartOrig), s_pressStartOrig, "START", p + n);
            s_pressStart = text;
            Log("title text: '%s' -> '%s'", s_pressStartOrig, s_pressStartPad);
            return true;
        }
    }
    Log("title text: '%s' has no '%s', unchanged", s_pressStartOrig, enter);
    return true;
}

static void UpdatePressStart(bool pad)
{
    if (!s_pressStart || pad == s_pressStartIsPad) return;
    strcpy_s(s_pressStart, strlen(s_pressStartOrig) + 1, pad ? s_pressStartPad : s_pressStartOrig);
    s_pressStartIsPad = pad;
}

static void CheckTitleText()
{
    if (!BytesMatch(kTextLookup, "568BF1833E00752E") || !BytesMatch((void*)0x00518416, "8B0D284ED600")) {
        Log("text lookup not recognised, title text unchanged");
        return;
    }
    s_origLookup = (TextLookup_t)kTextLookup;
    Log("title text lookup found");
}

void NotePadActivity()
{
    s_lastPadActivity = GetTickCount() | 1;
}

// SecuROM VM: ten interpreter contexts, pointers at 0x1E0266A, +0x24 == 0x66666666 while in use.
// A hang after the finish line was the game thread waiting for a free context with all ten taken.
// Log=1 logs every change of the in-use count to see when and from which entry they pile up.
static int CountVmContexts(DWORD* kinds)
{
    int used = 0;
    __try {
        for (int i = 0; i < 10; i++) {
            BYTE* c = *(BYTE**)(0x01E0266A + i * 4);
            kinds[i] = 0;
            if (c && *(DWORD*)(c + 0x24) == 0x66666666) {
                kinds[i] = *(DWORD*)(c + 0x18);
                used++;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return used;
}

static void WatchVmContexts()
{
    static int lastUsed;
    static unsigned frame;
    frame++;
    DWORD kinds[10];
    int used = CountVmContexts(kinds);
    if (used == lastUsed) return;
    lastUsed = used;
    char list[10 * 9 + 1] = "";
    for (int i = 0; i < 10; i++)
        if (kinds[i]) wsprintfA(list + lstrlenA(list), " %X", kinds[i] & 0xFFFF);
    Log("securom vm: %d of 10 contexts in use at frame %u (%lu ms):%s", used, frame, GetTickCount(), list);
}

void OnInputFrame()
{
    if (g_cfg.profile) ProfilerTick();
    if (s_knownBuild && (g_cfg.patchMask & PATCH_LOD)) {
        static unsigned frame;
        ScaleLodDistances();
        if (frame++ % 30 == 0) ScalePowerplayDrawDists();
        // Precomputed visibility: each track sector lists what can be seen from it, and the rest is
        // skipped, so buildings pop in at sector borders whatever their distance. The game's own
        // debug switches turn the lists off; then streaming distance, LOD distances and the frustum
        // decide. Sectors/Visibility/Disable Powerplay Visibility (0x8137FB), Disable Sector
        // Visibility (environment sectors, 0x7C1CC2) and Disable Visibility Map (entities, 0xAE6720).
        if (g_cfg.popInFix) {
            *(BYTE*)0x00D8B368 = 1;
            *(BYTE*)0x00D82788 = 1;
            *(BYTE*)0x00D9AFCC = 1;
        }
    }
    if (s_knownBuild && g_cfg.fixIconSize && (g_cfg.patchMask & PATCH_ICONSIZE)) FixPowerplayIconSize();
    if (g_cfg.log && s_knownBuild) WatchVmContexts();
    if (g_cfg.log) {
        // what the tweakables and the renderer look like while playing (they may be reloaded per level)
        static DWORD last;
        DWORD now = GetTickCount();
        if (now - last > 15000) {
            last = now;
            BYTE* r = *(BYTE**)0x00D64708;
            BYTE* rr = r ? *(BYTE**)(r + 4) : NULL;
            Log("state: quality=%d internal=%dx%d screen=%dx%d LOD high=%.0f mid=%.0f low=%.0f pool level=%d used=%d",
                *(int*)0x00D0F7E8,
                rr ? *(int*)(rr + 0x4C) : -1, rr ? *(int*)(rr + 0x50) : -1, *(int*)0x00D56648, *(int*)0x00D5664C,
                *(float*)0x00D38A90, *(float*)0x00D38A98, *(float*)0x00D38AA0, s_levelStreamPoolMB,
                s_levelStreamPoolMB > s_streamPoolMB ? s_levelStreamPoolMB : s_streamPoolMB);
        }
    }
    if (g_cfg.log) AudioDiagTick();
    if (g_cfg.glyphs == GLYPHS_KEYBOARD) return;

    // a controller that is already connected counts as the active device until a key is pressed
    static bool first = true;
    if (first) {
        first = false;
        for (int i = 0; i < XUSER_MAX_COUNT && !s_lastPadActivity; i++)
            if (PadSlotConnected(i)) s_lastPadActivity = 1;
    }

    // keyboard / mouse button presses switch the glyphs back to keys; modifiers, Windows keys,
    // F keys and Print are left out, as overlay hotkeys (screenshots, recording) use them
    BYTE keys[256];
    if (GetKeyboardState(keys)) {
        for (int vk = 1; vk < 256; vk++) {
            bool hotkey = (vk >= VK_SHIFT && vk <= VK_MENU) || (vk >= VK_LSHIFT && vk <= VK_RMENU) ||
                          vk == VK_LWIN || vk == VK_RWIN || vk == VK_SNAPSHOT || (vk >= VK_F1 && vk <= VK_F24) ||
                          (vk >= 0xC3 && vk <= 0xDA);  // VK_GAMEPAD_*: Windows' own key events for the pad
            if (!hotkey && (keys[vk] & 0x80) && !(s_lastKeys[vk] & 0x80)) {
                s_lastKbmActivity = GetTickCount() | 1;
                break;
            }
        }
        memcpy(s_lastKeys, keys, sizeof(keys));
    }

    bool want = WantPadGlyphs();
    static bool pressStartPrepared;
    if (s_origLookup && !pressStartPrepared) {
        s_dict = *(void**)0x00D64E28;
        if (s_dict) pressStartPrepared = PreparePressStart();
    }
    UpdatePressStart(want);
    if (s_promptBarKnown) UpdatePromptBar(want);
    if (TestFeed* feed = GetTestFeed()) feed->glyphsPad = s_padGlyphsShown;
}

void ApplyGamePatches()
{
    if (!BytesMatch((void*)0x0040F0F0, "5355568BB16CCF0000")) {
        Log("unknown SplitSecond.exe build, no patches applied");
        return;
    }
    s_knownBuild = true;
    const unsigned mask = g_cfg.patchMask;
    if (g_cfg.separateTriggers && (mask & PATCH_TRIGGERS)) PatchTriggerPreset();
    if (mask & PATCH_RUMBLE) HookRumble();
    if (mask & PATCH_PROMPTBAR) CheckPromptBar();
    if (g_cfg.glyphs != GLYPHS_KEYBOARD && (mask & PATCH_TITLETEXT)) CheckTitleText();
    if (mask & PATCH_FRAMERATE) PatchFrameRate();
    if (mask & PATCH_STREAMPOOL) RaiseStreamPool();
    if (mask & PATCH_MOTIONBLUR) PatchMotionBlur();
    if (mask & PATCH_VIGNETTE) PatchVignette();
    if (mask & PATCH_RENDERSCALE) PatchRenderScale();
    if (g_cfg.profile) InstallProfiler();
    if (g_cfg.log && (mask & PATCH_AUDIODIAG)) InstallAudioDiagnostics();
}
