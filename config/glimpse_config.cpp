// glimpse_fix - settings for SplitSecondPad.ini
//
// A plain Win32 program (static CRT, only Windows' own DLLs) with a dark look of its own. It edits the
// INI next to it, or one picked with the file dialog. Values are changed in place, so comments, order and
// unknown keys stay as they are; a key that is missing is added to its section. Every change is saved at
// once: the bloom and exposure values are read by the game every 2 s, everything else at the next start.
// Before the first change the INI as it was opened is kept as SplitSecondPad.ini.bak.
#define WIN32_LEAN_AND_MEAN
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------------------------------------------------------- options

enum Kind { BOOL_OPT, CHOICE, RANGE };

struct Option {
    const char* section;
    const char* key;
    Kind kind;
    const char* text;
    const char* help;
    const char* def;
    // CHOICE: "value|Text;value|Text;..."   RANGE: min, max, step, decimals
    const char* choices;
    double lo, hi, step;
    int decimals;
    HWND label, ctrl, value;  // label (CHOICE, RANGE), control, value label (RANGE)
};

// keys for the camera and gameplay options, as virtual key codes (P is the game's own pause key)
#define KEY_CHOICES "0x31|1;0x32|2;0x33|3;0x34|4;0x35|5;0x36|6;0x37|7;0x38|8;0x39|9;0x30|0;0x41|A;0x42|B;0x43|C;0x44|D;0x45|E;0x46|F;0x47|G;0x48|H;0x49|I;0x4A|J;0x4B|K;0x4C|L;0x4D|M;0x4E|N;0x4F|O;0x51|Q;0x52|R;0x53|S;0x54|T;0x55|U;0x56|V;0x57|W;0x58|X;0x59|Y;0x5A|Z;0x70|F1;0x71|F2;0x72|F3;0x73|F4;0x74|F5;0x75|F6;0x76|F7;0x77|F8;0x78|F9;0x79|F10;0x7A|F11;0x7B|F12;0xA0|Left Shift;0xA2|Left Ctrl;0x20|Space;0x2D|Insert;0x2E|Delete;0x24|Home;0x21|Page Up;0x22|Page Down;0x60|Num 0;0x6E|Num ,;0x6B|Num +;0x6D|Num -"

static Option kOptions[] = {
    // Controller
    { "Controller", "VirtualPads", RANGE, "Virtual pads", "XInput slots presented to the game as Xbox 360 pads. The game takes at most 4 joysticks; lower this to make room for a wheel.", "4", 0, 0, 4, 1, 0 },
    { "Controller", "HideXInputFromDirectInput", BOOL_OPT, "Hide the real pads from DirectInput", "Keeps XInput controllers from showing up twice.", "1" },
    { "Controller", "SeparateTriggers", BOOL_OPT, "Separate triggers", "LT and RT on their own axes, so you can brake and accelerate at the same time. Off = the stock shared axis.", "1" },
    { "Controller", "StickDeadzone", RANGE, "Stick deadzone", "Radial deadzone of the sticks.", "0.15", 0, 0, 0.5, 0.01, 2 },
    { "Controller", "TriggerDeadzone", RANGE, "Trigger deadzone", "Deadzone of LT and RT.", "0.05", 0, 0, 0.5, 0.01, 2 },
    { "Controller", "RumbleStrength", RANGE, "Rumble strength", "Multiplier on the game's rumble. 0 = off.", "1.0", 0, 0, 2, 0.05, 2 },
    { "Controller", "ButtonGlyphs", CHOICE, "Button prompts", "Icons in the prompt bar while the pad is in use. Keyboard = never switch.", "xbox", "xbox|Xbox;playstation|PlayStation;keyboard|Keyboard" },
    // Graphics
    { "Graphics", "FrameRate", CHOICE, "Frame rate", "60 fps with the simulation stepped exactly as at 30 fps.", "60", "60|60 fps;30|30 fps (stock)" },
    { "Graphics", "RenderScale", CHOICE, "Internal resolution", "Render resolution as a factor of the screen resolution. Auto = as the game: 2.0 at Very High, 1.0 below. Anything but Auto changes game code; leave it at Auto if the game hangs after a few minutes.", "auto", "auto|Auto;0.75|0.75x;1.0|1.0x;1.25|1.25x;1.5|1.5x;2.0|2.0x;3.0|3.0x;4.0|4.0x" },
    { "Graphics", "Anisotropy", CHOICE, "Anisotropic filtering", "Sharper textures at an angle. Off = the game's plain linear filtering.", "16", "0|Off;2|2x;4|4x;8|8x;16|16x" },
    { "Graphics", "MotionBlurStrength", RANGE, "Motion blur", "Multiplier on the cameras' motion blur (graphics quality High or Very High). 0 = off.", "1.0", 0, 0, 3, 0.1, 1 },
    { "Graphics", "Vignette", BOOL_OPT, "Vignette", "Darkened screen edges of the race cameras.", "0" },
    { "Graphics", "Gradient", BOOL_OPT, "Screen gradient", "The translucent blue band the cameras lay over the top of the screen.", "1" },
    { "Graphics", "Bloom", BOOL_OPT, "Bloom", "Glow around bright areas. Off keeps the tone mapping.", "1" },
    { "Graphics", "BloomStrength", RANGE, "  Bloom strength", "Glow on bright areas, on top of each camera's value. Applies while the game runs.", "1.0", 0, 0, 5, 0.05, 2 },
    { "Graphics", "BloomTone", RANGE, "  Bloom tone", "Second bright pass value, a softer effect. Applies while the game runs.", "1.0", 0, 0, 5, 0.05, 2 },
    { "Graphics", "BloomCurve", RANGE, "  Bloom curve", "How the glow is added; below 1.0 has no effect. Applies while the game runs.", "1.0", 0, 0, 5, 0.05, 2 },
    { "Graphics", "Exposure", RANGE, "Exposure", "Brightness of the whole picture. Applies while the game runs.", "1.0", 0, 0, 3, 0.05, 2 },
    { "Graphics", "LensFlare", BOOL_OPT, "Lens flares", "Flares of the sun and lights, including the screen space flare.", "1" },
    { "Graphics", "ParticleFlare", BOOL_OPT, "Particle flares", "Flares of sparks, explosions and lights.", "1" },
    { "Graphics", "FixPowerplayIconSize", BOOL_OPT, "Power play icons at console size", "The PC port draws the power play icons and route markers too big above 1280 pixels width (3x at 4K).", "1" },
    // World
    { "World", "StreamPoolMB", CHOICE, "Streaming pool", "Memory for the track's textures and props. Bigger keeps them sharp instead of blurry or grey. Needs SplitSecond.exe with the LAA flag (setlaa.exe).", "512", "0|Stock (57 MB);256|256 MB;512|512 MB;768|768 MB" },
    { "World", "LodDistanceScale", RANGE, "Draw distance", "Multiplier for LOD, streaming, particle, debris and power play distances.", "2.0", 0, 0.5, 8, 0.1, 1 },
    { "World", "PopInFix", BOOL_OPT, "Pop-in fix", "Buildings beside the track no longer pop in at section borders (a little more to draw).", "1" },
    // Gameplay
    // Camera
    { "Camera", "FOV", RANGE, "Field of view", "Race cameras (chase and bumper) as a factor of the game's own; the game still widens the view with speed.", "1.0", 0, 0.5, 2, 0.05, 2 },
    { "Camera", "UltrawideFix", BOOL_OPT, "Ultrawide fix", "On screens wider than 16:9 the picture keeps the height of 16:9 instead of losing the top and bottom.", "1" },
    { "Camera", "ZoomKeys", BOOL_OPT, "Zoom keys", "Zoom the race camera while a key is held.", "0" },
    { "Camera", "ZoomInKey", CHOICE, "  Zoom in", "Key held to zoom closer.", "0x31", KEY_CHOICES },
    { "Camera", "ZoomOutKey", CHOICE, "  Zoom out", "Key held to zoom wider.", "0x32", KEY_CHOICES },
    { "Camera", "ZoomResetKey", CHOICE, "  Zoom reset", "Back to the field of view above.", "0x33", KEY_CHOICES },
    { "Camera", "PhotoMode", BOOL_OPT, "Photo mode", "During a race the key or the pad's Back button holds the world still, hides the HUD and frees the camera. Sticks or W A S D and I J K L to move and look, triggers or Q E down and up, Y + LB RB or U O to tilt.", "0" },
    { "Camera", "PhotoModeKey", CHOICE, "  Photo mode key", "Keys the game uses itself (Esc, P, Enter, arrows) do not work here.", "0x46", KEY_CHOICES },
    { "Camera", "HudKey", CHOICE, "HUD on/off key", "Switch the HUD off and on during a race.", "0", "0|None;" KEY_CHOICES },
    // Gameplay
    { "Gameplay", "UnlockAll", BOOL_OPT, "Everything unlocked", "All tracks, events, episodes, game modes and cars selectable. Your profile is not changed.", "0" },
    { "Gameplay", "BonusContent", BOOL_OPT, "Bonus content", "The PC version's cheat code extras: Expressway track, Cobretti SlipStream, branded cars, Euro livery.", "0" },
    { "Gameplay", "UnlockDecals", CHOICE, "Achievements and decals", "Unlocked in the next single player race and saved with the profile (backed up to glimpse_backup first). Unobtainable = the six ranked online ones and the Pure decal.", "0", "0|As the game;2|Unobtainable only;1|All" },
    { "Gameplay", "SkipIntro", CHOICE, "Boot movies", "Skip the movies at every start. The files stay untouched.", "0", "0|Play both;1|Skip the logo;2|Skip logo and intro" },
    { "Gameplay", "Boost", BOOL_OPT, "Boost (hold X)", "Your car surges forward while X or the boost key is held, with more motion blur.", "0" },
    { "Gameplay", "BoostKey", CHOICE, "  Boost key", "Keyboard key held for the boost, like X on the pad. Left Shift is not used by the game.", "0xA0", "0|None;" KEY_CHOICES },
    { "Gameplay", "BoostAcceleration", RANGE, "  Boost acceleration", "m/s per second; 15 is about 1.5 g. Also used by the AI boost.", "15", 0, 1, 60, 1, 0 },
    { "Gameplay", "BoostMaxSpeed", RANGE, "  Boost top speed", "No boost above this speed, in m/s (90 m/s = 324 km/h). Also used by the AI boost.", "90", 0, 10, 200, 1, 0 },
    { "Gameplay", "BoostMotionBlur", RANGE, "  Boost motion blur", "Motion blur multiplier at full boost.", "3.0", 0, 1, 5, 0.1, 1 },
    { "Gameplay", "PowerBarFill", BOOL_OPT, "Full power play bar (LS+RS)", "Press both sticks or the power bar key to fill your power play bar.", "0" },
    { "Gameplay", "PowerBarFillKey", CHOICE, "  Power bar key", "Keyboard key that fills the power play bar, like LS+RS on the pad. Left Ctrl is not used by the game.", "0xA2", "0|None;" KEY_CHOICES },
    { "Gameplay", "AIBoost", BOOL_OPT, "AI boost (experimental)", "The AI cars boost too, while they are behind you or not far ahead.", "0" },
    { "Gameplay", "AIBoostLevel", CHOICE, "  AI boost level", "Easy: now and then, up to 30 m ahead. Normal: about once a second, 60 m. Hard: almost constantly, 150 m.", "2", "1|Easy;2|Normal;3|Hard" },
    { "Gameplay", "AIBoostUnlimited", BOOL_OPT, "  Unlimited AI boost", "The AI's boost tank never empties.", "0" },
    { "Gameplay", "AIBoostStraightOnly", BOOL_OPT, "  AI boosts on straights only", "Off = anywhere while driving forward, also in corners.", "1" },
    { "Gameplay", "AIBoostChaos", BOOL_OPT, "  Chaos mode", "Every AI car boosts all the time, everywhere, with an endless tank, a full power play bar and the highest AI skill. Overrides the three settings above.", "0" },
    // Debug
    { "Debug", "Log", BOOL_OPT, "Write SplitSecondPad.log", "A log next to the game, for bug reports.", "0" },
};
static const int kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);
static const char* const kPages[] = { "Controller", "Graphics", "World", "Camera", "Gameplay", "AI", "Debug" };
static const int kPageCount = 7;

// the page an option is shown on: its INI section, except the AI boost options (in [Gameplay])
static const char* PageOf(const Option& o) { return strncmp(o.key, "AIBoost", 7) ? o.section : "AI"; }

// ---------------------------------------------------------------- INI file, edited in place

static char s_path[MAX_PATH];
static std::vector<std::string> s_lines;
static bool s_crlf = true;

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static bool LoadIni()
{
    s_lines.clear();
    FILE* f = fopen(s_path, "rb");
    if (!f) return false;
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    fclose(f);
    s_crlf = all.find("\r\n") != std::string::npos || all.find('\n') == std::string::npos;
    size_t start = 0;
    while (start <= all.size()) {
        size_t end = all.find('\n', start);
        std::string line = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (end == std::string::npos) {
            if (!line.empty()) s_lines.push_back(line);
            break;
        }
        s_lines.push_back(line);
        start = end + 1;
    }
    return true;
}

// SplitSecondPad.ini.bak: the INI as it was when the program opened it, written before the first change
static std::vector<std::string> s_opened;
static bool s_backedUp;

static std::string BackupPath() { return std::string(s_path) + ".bak"; }

static bool SaveLines(const char* path, const std::vector<std::string>& lines)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    for (const std::string& l : lines) {
        fputs(l.c_str(), f);
        fputs(s_crlf ? "\r\n" : "\n", f);
    }
    fclose(f);
    return true;
}

static bool SaveIni()
{
    if (!s_backedUp && !s_opened.empty()) s_backedUp = SaveLines(BackupPath().c_str(), s_opened);
    return SaveLines(s_path, s_lines);
}

// line index of key in section, -1 if missing; *sectionEnd gets the line after the section's last key
static int FindKey(const char* section, const char* key, int* sectionEnd)
{
    std::string current;
    int found = -1, end = -1;
    for (int i = 0; i < (int)s_lines.size(); i++) {
        std::string t = Trim(s_lines[i]);
        if (t.size() > 2 && t[0] == '[' && t.back() == ']') {
            current = t.substr(1, t.size() - 2);
            continue;
        }
        if (_stricmp(current.c_str(), section)) continue;
        if (t.empty() || t[0] == ';') continue;
        end = i + 1;
        size_t eq = t.find('=');
        if (eq != std::string::npos && !_stricmp(Trim(t.substr(0, eq)).c_str(), key)) found = i;
    }
    if (sectionEnd) *sectionEnd = end;
    return found;
}

static std::string ValueAt(int line)
{
    const std::string& l = s_lines[line];
    return Trim(l.substr(l.find('=') + 1));
}

// as the DLL reads it: the key's own section first, then where older INIs kept it
static std::string GetValue(const Option& o)
{
    int i = FindKey(o.section, o.key, NULL);
    if (i < 0) i = FindKey(!strcmp(o.section, "Debug") ? "Controller" : "Game", o.key, NULL);
    return i < 0 ? o.def : ValueAt(i);
}

static void SetValue(const Option& o, const std::string& v)
{
    int end;
    int i = FindKey(o.section, o.key, &end);
    std::string line = std::string(o.key) + "=" + v;
    if (i >= 0) {
        s_lines[i] = line;
        return;
    }
    if (end < 0) {  // no such section yet
        if (!s_lines.empty() && !Trim(s_lines.back()).empty()) s_lines.push_back("");
        s_lines.push_back(std::string("[") + o.section + "]");
        s_lines.push_back(line);
        return;
    }
    s_lines.insert(s_lines.begin() + end, line);
}

// ---------------------------------------------------------------- look

// dark theme: the controls are drawn here; the lists open as popup menus in Windows' dark menu style
static const COLORREF kBack = RGB(24, 25, 28), kSide = RGB(32, 33, 38), kSideSel = RGB(46, 48, 55),
                      kField = RGB(48, 50, 57), kBorder = RGB(66, 69, 78), kText = RGB(232, 233, 236),
                      kDim = RGB(150, 154, 163), kOff = RGB(88, 91, 99), kAccent = RGB(255, 120, 20),
                      kAccent2 = RGB(255, 70, 35);
static HBRUSH s_backBrush, s_sideBrush, s_fieldBrush;
static HFONT s_font, s_fontBold, s_fontTitle, s_fontBrand, s_fontSmall;

enum Role { R_NONE, R_LABEL, R_VALUE, R_DIM, R_TITLE, R_BRAND, R_SIDEDIM, R_NAV, R_PRESET, R_ACTION, R_TOGGLE, R_SELECT };

static HWND s_wnd, s_status, s_info, s_title, s_subtitle, s_nav[5];
static int s_dpi = 96;
static int s_page;
static bool s_loading;
static bool s_toggle[64];  // BOOL_OPT state by option index
static int s_choice[64];   // CHOICE: selected item by option index, -1 = a value set by hand
static std::string s_custom[64];  // that value
static const char kSliderClass[] = "glimpse_slider";  // the own slider control, see SliderProc

static int S(int px) { return MulDiv(px, s_dpi, 96); }

static HFONT MakeFont(int pt, int weight)
{
    return CreateFontW(-MulDiv(pt, s_dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static void MakeFonts()
{
    HFONT old[] = { s_font, s_fontBold, s_fontSmall, s_fontTitle, s_fontBrand };
    s_font = MakeFont(10, FW_NORMAL);
    s_fontBold = MakeFont(10, FW_SEMIBOLD);
    s_fontSmall = MakeFont(9, FW_NORMAL);
    s_fontTitle = MakeFont(18, FW_SEMIBOLD);
    s_fontBrand = MakeFont(16, FW_BOLD);
    for (HFONT f : old)
        if (f) DeleteObject(f);
}

// Per-monitor DPI (v2): the layout and fonts follow the monitor the window is on. Older Windows versions
// fall back to the system DPI.
static void DpiAware()
{
    HMODULE user = GetModuleHandleA("user32.dll");
    typedef BOOL(WINAPI * SetContext_t)(HANDLE);
    SetContext_t set = (SetContext_t)GetProcAddress(user, "SetProcessDpiAwarenessContext");
    if (!set || !set((HANDLE)-4)) SetProcessDPIAware();  // -4 = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

static int WindowDpi(HWND wnd)
{
    typedef UINT(WINAPI * GetDpi_t)(HWND);
    GetDpi_t get = (GetDpi_t)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow");
    if (get && wnd) return (int)get(wnd);
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    return dpi;
}

// the outer window size for the client area at the current DPI
static SIZE WindowSize(DWORD style)
{
    RECT r = { 0, 0, MulDiv(880, s_dpi, 96), MulDiv(660, s_dpi, 96) };
    typedef BOOL(WINAPI * Adjust_t)(RECT*, DWORD, BOOL, DWORD, UINT);
    Adjust_t adjust = (Adjust_t)GetProcAddress(GetModuleHandleA("user32.dll"), "AdjustWindowRectExForDpi");
    if (adjust) adjust(&r, style, FALSE, 0, (UINT)s_dpi);
    else AdjustWindowRect(&r, style, FALSE);
    SIZE size = { r.right - r.left, r.bottom - r.top };
    return size;
}

static BOOL CALLBACK DestroyChild(HWND child, LPARAM)
{
    DestroyWindow(child);
    return TRUE;
}

static void RoundRect(HDC dc, RECT r, int radius, COLORREF fill, COLORREF line)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    ::RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

// Windows' dark style for the popup menus (uxtheme ordinals, Windows 10 1903 and later)
static void EnableDarkMenus()
{
    HMODULE ux = LoadLibraryA("uxtheme.dll");
    if (!ux) return;
    typedef int(WINAPI * SetPreferredAppMode_t)(int);
    typedef void(WINAPI * FlushMenuThemes_t)();
    SetPreferredAppMode_t setMode = (SetPreferredAppMode_t)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    FlushMenuThemes_t flush = (FlushMenuThemes_t)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (setMode) setMode(2);  // force dark
    if (flush) flush();
}

static void DarkTitleBar(HWND wnd)
{
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT(WINAPI * DwmSet_t)(HWND, DWORD, LPCVOID, DWORD);
    DwmSet_t set = (DwmSet_t)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!set) return;
    BOOL on = TRUE;
    if (FAILED(set(wnd, 20, &on, sizeof(on)))) set(wnd, 19, &on, sizeof(on));  // immersive dark mode
    COLORREF caption = kSide;
    set(wnd, 35, &caption, sizeof(caption));  // caption colour, Windows 11
}

// ---------------------------------------------------------------- options on screen

static Option* Find(const char* key)
{
    for (Option& o : kOptions)
        if (!strcmp(o.key, key)) return &o;
    return NULL;
}

static int IndexOf(const Option& o) { return (int)(&o - kOptions); }

static std::vector<std::pair<std::string, std::string>> Choices(const Option& o)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::string s = o.choices;
    size_t p = 0;
    while (p < s.size()) {
        size_t semi = s.find(';', p);
        std::string item = s.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
        size_t bar = item.find('|');
        out.push_back({ item.substr(0, bar), item.substr(bar + 1) });
        if (semi == std::string::npos) break;
        p = semi + 1;
    }
    return out;
}

static std::string FormatRange(const Option& o, double v)
{
    char buf[32];
    sprintf(buf, "%.*f", o.decimals, v);
    return buf;
}

static int RangeSteps(const Option& o) { return (int)((o.hi - o.lo) / o.step + 0.5); }

static void ShowValue(Option& o, double v)
{
    std::string text = FormatRange(o, v);
    if (!strcmp(o.key, "BoostMaxSpeed")) text += " m/s  " + std::to_string((int)(v * 3.6 + 0.5)) + " km/h";
    else if (!strcmp(o.key, "BoostAcceleration")) text += " m/s per s";
    else if (strstr(o.key, "Strength") || strstr(o.key, "Blur") || strstr(o.key, "Bloom") || strstr(o.key, "Scale") ||
             !strcmp(o.key, "Exposure") || !strcmp(o.key, "FOV"))
        text += "x";
    SetWindowTextA(o.value, text.c_str());
}

static bool IsOn(const char* key)
{
    Option* o = Find(key);
    return o && s_toggle[IndexOf(*o)];
}

static void UpdateEnables()
{
    bool bloom = IsOn("Bloom"), boost = IsOn("Boost"), ai = IsOn("AIBoost"), chaos = IsOn("AIBoostChaos");
    bool zoom = IsOn("ZoomKeys"), photo = IsOn("PhotoMode");
    struct { const char* key; bool on; } rules[] = {
        { "BloomStrength", bloom }, { "BloomTone", bloom }, { "BloomCurve", bloom },
        { "BoostMotionBlur", boost }, { "BoostKey", boost }, { "PowerBarFillKey", IsOn("PowerBarFill") },
        { "ZoomInKey", zoom }, { "ZoomOutKey", zoom }, { "ZoomResetKey", zoom }, { "PhotoModeKey", photo },
        { "AIBoostLevel", ai && !chaos }, { "AIBoostUnlimited", ai && !chaos },
        { "AIBoostStraightOnly", ai && !chaos }, { "AIBoostChaos", ai },
    };
    for (auto& r : rules) {
        Option* o = Find(r.key);
        EnableWindow(o->ctrl, r.on);
        EnableWindow(o->label, r.on);
        if (o->value) EnableWindow(o->value, r.on);
        InvalidateRect(o->label, NULL, TRUE);
        if (o->value) InvalidateRect(o->value, NULL, TRUE);
    }
}

static void Status(const char* text) { SetWindowTextA(s_status, text); }

static void LoadControls()
{
    s_loading = true;
    for (Option& o : kOptions) {
        std::string v = GetValue(o);
        if (o.kind == BOOL_OPT) {
            s_toggle[IndexOf(o)] = atoi(v.c_str()) != 0;
            InvalidateRect(o.ctrl, NULL, TRUE);
        } else if (o.kind == CHOICE) {
            auto items = Choices(o);
            int sel = -1;
            for (int i = 0; i < (int)items.size(); i++)
                if (!_stricmp(items[i].first.c_str(), v.c_str()) ||
                    (isdigit((unsigned char)v[0]) && atof(v.c_str()) == atof(items[i].first.c_str())))
                    sel = i;
            s_choice[IndexOf(o)] = sel;
            s_custom[IndexOf(o)] = v + " (custom)";
            InvalidateRect(o.ctrl, NULL, TRUE);
        } else {
            double d = atof(v.c_str());
            if (d < o.lo) d = o.lo;
            if (d > o.hi) d = o.hi;
            SendMessageA(o.ctrl, TBM_SETPOS, TRUE, (LPARAM)(int)((d - o.lo) / o.step + 0.5));
            InvalidateRect(o.ctrl, NULL, TRUE);
            ShowValue(o, d);
        }
    }
    s_loading = false;
    UpdateEnables();
}

static void Changed(Option& o)
{
    if (s_loading) return;
    std::string v;
    if (o.kind == BOOL_OPT) {
        v = s_toggle[IndexOf(o)] ? "1" : "0";
    } else if (o.kind == CHOICE) {
        int sel = s_choice[IndexOf(o)];
        auto items = Choices(o);
        if (sel < 0 || sel >= (int)items.size()) return;  // a value set by hand stays as it is
        v = items[sel].first;
    } else {
        double d = o.lo + SendMessageA(o.ctrl, TBM_GETPOS, 0, 0) * o.step;
        InvalidateRect(o.ctrl, NULL, TRUE);
        ShowValue(o, d);
        v = FormatRange(o, d);
    }
    SetValue(o, v);
    UpdateEnables();
    char msg[200];
    if (SaveIni()) sprintf(msg, "Saved  %s = %s", o.key, v.c_str());
    else sprintf(msg, "Could not write %s", s_path);
    Status(msg);
}

static const char* const kPageText[] = {
    "Pads, triggers and button prompts.",
    "Frame rate, image quality and effects. Bloom and exposure apply while the game runs.",
    "Streaming, draw distance and pop-in.",
    "Field of view, zoom and photo mode.",
    "Unlocks, boost and the power play bar.",
    "Boost for the AI cars (experimental).",
    "For bug reports.",
};

static void ShowPage(int page)
{
    s_page = page;
    SetWindowTextA(s_title, kPages[page]);
    SetWindowTextA(s_subtitle, kPageText[page]);
    for (Option& o : kOptions) {
        int cmd = !strcmp(PageOf(o), kPages[page]) ? SW_SHOW : SW_HIDE;
        ShowWindow(o.label, cmd);
        ShowWindow(o.ctrl, cmd);
        if (o.value) ShowWindow(o.value, cmd);
    }
    SetWindowTextA(s_info, "Point at an option to see what it does. Every change is saved at once.");
    for (int i = 0; i < kPageCount; i++) InvalidateRect(s_nav[i], NULL, TRUE);
}

struct Preset {
    const char* name;
    const char* values;  // "Key=value;..."
};
static const Preset kPresets[] = {
    { "Recommended", "" },  // every option at its default
    { "Original game", "FrameRate=30;RenderScale=auto;Anisotropy=0;MotionBlurStrength=1.0;Vignette=1;Bloom=1;BloomStrength=1.0;BloomTone=1.0;BloomCurve=1.0;Exposure=1.0;LensFlare=1;ParticleFlare=1;FixPowerplayIconSize=0;StreamPoolMB=0;LodDistanceScale=1.0;PopInFix=0;SeparateTriggers=0;UltrawideFix=0" },
    { "Chaos", "UnlockAll=1;Boost=1;PowerBarFill=1;AIBoost=1;AIBoostChaos=1" },
};

static void ApplyPreset(const Preset& p)
{
    for (Option& o : kOptions) {
        if (!strcmp(o.section, "Debug")) continue;
        std::string v = o.def;
        std::string list = std::string(";") + p.values + ";", pat = std::string(";") + o.key + "=";
        size_t at = list.find(pat);
        if (at != std::string::npos) {
            size_t s = at + pat.size();
            v = list.substr(s, list.find(';', s) - s);
        }
        SetValue(o, v);
    }
    LoadControls();
    char msg[160];
    sprintf(msg, SaveIni() ? "Preset \"%s\" saved" : "Could not write the INI", p.name);
    Status(msg);
}

static HWND Child(const char* cls, const char* text, DWORD style, int x, int y, int w, int h, int id, Role role,
                  HFONT font)
{
    HWND h_ = CreateWindowExA(0, cls, text, WS_CHILD | style, x, y, w, h, s_wnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageA(h_, WM_SETFONT, (WPARAM)font, TRUE);
    SetWindowLongPtrA(h_, GWLP_USERDATA, role);
    return h_;
}

enum { ID_NAV0 = 10, ID_PRESET0 = 20, ID_OPEN = 30, ID_CLOSE = 31, ID_RESTORE = 32, ID_OPTION0 = 100 };

// layout, in 96 dpi pixels
static const int kWidth = 880, kHeight = 700, kSideW = 210, kContentX = 240, kLabelW = 300, kRowH = 33, kRowTop = 100;

static void CreateControls()
{
    const int ctrlX = kContentX + kLabelW;
    Child("STATIC", "glimpse_fix", WS_VISIBLE | SS_LEFT, S(22), S(22), S(180), S(30), 0, R_BRAND, s_fontBrand);
    Child("STATIC", "Split/Second PC fix", WS_VISIBLE | SS_LEFT, S(23), S(54), S(180), S(20), 0, R_SIDEDIM, s_fontSmall);
    for (int i = 0; i < kPageCount; i++)
        s_nav[i] = Child("BUTTON", kPages[i], WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, S(96) + i * S(42), S(kSideW),
                         S(40), ID_NAV0 + i, R_NAV, s_font);
    Child("STATIC", "PRESETS", WS_VISIBLE | SS_LEFT, S(22), S(424), S(160), S(18), 0, R_SIDEDIM, s_fontSmall);
    for (int i = 0; i < 3; i++)
        Child("BUTTON", kPresets[i].name, WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, S(18), S(448) + i * S(40), S(174), S(32),
              ID_PRESET0 + i, R_PRESET, s_font);

    s_title = Child("STATIC", "", WS_VISIBLE | SS_LEFT, S(kContentX), S(26), S(600), S(36), 0, R_TITLE, s_fontTitle);
    s_subtitle = Child("STATIC", "", WS_VISIBLE | SS_LEFT, S(kContentX), S(64), S(610), S(22), 0, R_DIM, s_font);

    int row[kPageCount] = {};
    for (int i = 0; i < kOptionCount; i++) {
        Option& o = kOptions[i];
        int page = 0;
        while (strcmp(kPages[page], PageOf(o))) page++;
        int y = S(kRowTop) + row[page]++ * S(kRowH);
        int id = ID_OPTION0 + i;
        const char* text = o.text;
        int indent = 0;
        while (*text == ' ') text++, indent += S(9);
        o.label = Child("STATIC", text, SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, S(kContentX) + indent, y, S(kLabelW) - indent,
                        S(26), 0, R_LABEL, s_font);
        if (o.kind == BOOL_OPT) {
            o.ctrl = Child("BUTTON", "", WS_TABSTOP | BS_OWNERDRAW, S(ctrlX), y + S(2), S(46), S(22), id, R_TOGGLE, s_font);
        } else if (o.kind == CHOICE) {
            o.ctrl = Child("BUTTON", "", WS_TABSTOP | BS_OWNERDRAW, S(ctrlX), y, S(250), S(28), id, R_SELECT, s_font);
        } else {
            o.ctrl = Child(kSliderClass, "", WS_TABSTOP, S(ctrlX) - S(6), y, S(200), S(26), id, R_NONE, s_font);
            SendMessageA(o.ctrl, TBM_SETRANGE, TRUE, MAKELPARAM(0, RangeSteps(o)));
            o.value = Child("STATIC", "", SS_LEFT | SS_CENTERIMAGE, S(ctrlX) + S(200), y, S(140), S(26), 0, R_VALUE, s_font);
        }
    }
    s_info = Child("STATIC", "", WS_VISIBLE | SS_LEFT, S(kContentX), S(kHeight - 104), S(kWidth - kContentX - 24), S(42), 0,
                   R_DIM, s_font);
    s_status = Child("STATIC", "", WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS | SS_CENTERIMAGE, S(kContentX), S(kHeight - 46),
                     S(kWidth - 400 - kContentX), S(30), 0, R_DIM, s_fontSmall);
    Child("BUTTON", "Restore backup", WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, S(kWidth - 386), S(kHeight - 48), S(140),
          S(32), ID_RESTORE, R_ACTION, s_font);
    Child("BUTTON", "Open INI...", WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, S(kWidth - 236), S(kHeight - 48), S(104), S(32),
          ID_OPEN, R_ACTION, s_font);
    Child("BUTTON", "Close", WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, S(kWidth - 122), S(kHeight - 48), S(104), S(32),
          ID_CLOSE, R_ACTION, s_font);
}

static bool PickIni(HWND owner)
{
    char file[MAX_PATH];
    strcpy(file, s_path);
    OPENFILENAMEA ofn = { sizeof(ofn) };
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "SplitSecondPad.ini\0SplitSecondPad.ini\0INI files\0*.ini\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "SplitSecondPad.ini in the Split/Second folder";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn)) return false;
    strcpy(s_path, file);
    return true;
}

static void Open()
{
    s_backedUp = false;
    s_opened.clear();
    if (LoadIni()) s_opened = s_lines;
    if (s_opened.empty()) {
        s_lines.clear();
        Status("No SplitSecondPad.ini here yet: it is created with the first change.");
    } else {
        std::string msg = std::string("Editing ") + s_path;
        Status(msg.c_str());
    }
    LoadControls();
}

// puts the backup back and reloads it
static void RestoreBackup(HWND wnd)
{
    std::string bak = BackupPath();
    if (GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxA(wnd, "There is no backup yet. It is made before the first change.", "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string q = "Replace the settings with the backup?\n\n" + bak;
    if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (!CopyFileA(bak.c_str(), s_path, FALSE)) {
        Status("Could not restore the backup");
        return;
    }
    Open();
    s_backedUp = true;  // keep the backup as it is
    Status("Backup restored");
}

// ---------------------------------------------------------------- drawing

static void DrawButton(const DRAWITEMSTRUCT* di)
{
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    Role role = (Role)GetWindowLongPtrA(di->hwndItem, GWLP_USERDATA);
    bool pressed = (di->itemState & ODS_SELECTED) != 0, disabled = (di->itemState & ODS_DISABLED) != 0;
    char text[64];
    GetWindowTextA(di->hwndItem, text, sizeof(text));
    SetBkMode(dc, TRANSPARENT);

    if (role == R_TOGGLE) {
        int index = GetDlgCtrlID(di->hwndItem) - ID_OPTION0;
        bool on = s_toggle[index];
        FillRect(dc, &r, s_backBrush);
        RECT track = { r.left, r.top + S(1), r.left + S(42), r.bottom - S(1) };
        COLORREF fill = on ? (disabled ? RGB(110, 70, 45) : kAccent) : kField;
        RoundRect(dc, track, S(20), fill, on ? fill : kBorder);
        int d = track.bottom - track.top - S(6);
        int x = on ? track.right - S(3) - d : track.left + S(3);
        RECT knob = { x, track.top + S(3), x + d, track.top + S(3) + d };
        COLORREF k = disabled ? kOff : (on ? RGB(255, 255, 255) : kDim);
        RoundRect(dc, knob, d, k, k);
        return;
    }
    if (role == R_NAV) {
        int page = GetDlgCtrlID(di->hwndItem) - ID_NAV0;
        bool sel = page == s_page;
        HBRUSH b = CreateSolidBrush(sel ? kSideSel : kSide);
        FillRect(dc, &r, b);
        DeleteObject(b);
        if (sel) {
            RECT bar = { r.left, r.top + S(8), r.left + S(4), r.bottom - S(8) };
            HBRUSH a = CreateSolidBrush(kAccent);
            FillRect(dc, &bar, a);
            DeleteObject(a);
        }
        SelectObject(dc, sel ? s_fontBold : s_font);
        SetTextColor(dc, sel ? kText : kDim);
        RECT t = r;
        t.left += S(24);
        DrawTextA(dc, text, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        return;
    }
    if (role == R_SELECT) {  // a list: the current item and a chevron
        int index = GetDlgCtrlID(di->hwndItem) - ID_OPTION0;
        Option& o = kOptions[index];
        auto items = Choices(o);
        int sel = s_choice[index];
        std::string value = sel >= 0 && sel < (int)items.size() ? items[sel].second : s_custom[index];
        FillRect(dc, &r, s_backBrush);
        RoundRect(dc, r, S(8), pressed ? kSideSel : kField, disabled ? kField : kBorder);
        RECT t = r;
        t.left += S(12);
        t.right -= S(30);
        SelectObject(dc, s_font);
        SetTextColor(dc, disabled ? kOff : kText);
        DrawTextA(dc, value.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        HPEN pen = CreatePen(PS_SOLID, S(2), disabled ? kOff : kAccent);
        HGDIOBJ op = SelectObject(dc, pen);
        int cx = r.right - S(17), cy = (r.top + r.bottom) / 2;
        MoveToEx(dc, cx - S(5), cy - S(2), NULL);
        LineTo(dc, cx, cy + S(3));
        LineTo(dc, cx + S(5), cy - S(2));
        SelectObject(dc, op);
        DeleteObject(pen);
        return;
    }
    // presets and actions: outlined pills, filled while pressed
    HBRUSH b = CreateSolidBrush(role == R_PRESET ? kSide : kBack);
    FillRect(dc, &r, b);
    DeleteObject(b);
    COLORREF line = role == R_PRESET && !strcmp(text, "Chaos") ? kAccent2 : kBorder;
    RoundRect(dc, r, S(10), pressed ? kSideSel : (role == R_PRESET ? kSide : kField), line);
    SelectObject(dc, s_font);
    SetTextColor(dc, kText);
    DrawTextA(dc, text, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

// The slider: an own control instead of the common trackbar, which only repaints the area of its own
// (narrow) thumb and left parts of the round one behind. It understands the trackbar messages used here
// (TBM_SETRANGE, TBM_SETPOS, TBM_GETPOS), reports changes with WM_HSCROLL like a trackbar and draws the
// whole control, double buffered, on every change. Mouse: click or drag; keys: arrows, Page Up/Down,
// Home, End.
static int SliderPos(HWND h) { return (int)GetWindowLongPtrA(h, 0); }
static int SliderMax(HWND h) { return (int)GetWindowLongPtrA(h, sizeof(LONG_PTR)); }

static void SliderTrack(HWND h, int* x0, int* x1)
{
    RECT r;
    GetClientRect(h, &r);
    int radius = S(8) + 1;
    *x0 = r.left + radius;
    *x1 = r.right - radius;
}

static void SliderSet(HWND h, int pos, bool notify)
{
    int max = SliderMax(h);
    if (pos < 0) pos = 0;
    if (pos > max) pos = max;
    if (pos == SliderPos(h)) return;
    SetWindowLongPtrA(h, 0, pos);
    InvalidateRect(h, NULL, FALSE);
    if (notify) SendMessageA(GetParent(h), WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, pos), (LPARAM)h);
}

static void SliderFromMouse(HWND h, int x)
{
    int x0, x1, max = SliderMax(h);
    SliderTrack(h, &x0, &x1);
    if (x1 <= x0 || max <= 0) return;
    SliderSet(h, (int)(((double)(x - x0) / (x1 - x0)) * max + 0.5), true);
}

static void SliderPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT r;
    GetClientRect(h, &r);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, r.right, r.bottom);
    HGDIOBJ old = SelectObject(dc, bmp);
    FillRect(dc, &r, s_backBrush);

    bool enabled = IsWindowEnabled(h) != 0, focus = GetFocus() == h;
    int x0, x1, max = SliderMax(h), pos = SliderPos(h);
    SliderTrack(h, &x0, &x1);
    int cy = (r.top + r.bottom) / 2;
    int x = max > 0 ? x0 + (int)((double)pos / max * (x1 - x0) + 0.5) : x0;
    RECT all = { x0, cy - S(2), x1, cy + S(2) };
    RoundRect(dc, all, S(4), kField, kField);
    RECT done = { x0, cy - S(2), x, cy + S(2) };
    COLORREF accent = enabled ? kAccent : kOff;
    if (x > x0) RoundRect(dc, done, S(4), accent, accent);
    int radius = S(8);
    RECT knob = { x - radius, cy - radius, x + radius, cy + radius };
    RoundRect(dc, knob, radius * 2, enabled ? RGB(255, 255, 255) : kOff, focus && enabled ? kAccent : (enabled ? RGB(255, 255, 255) : kOff));

    BitBlt(wdc, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK SliderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case TBM_SETRANGE:
        SetWindowLongPtrA(h, sizeof(LONG_PTR), HIWORD(lp));
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case TBM_SETPOS:
        SliderSet(h, (int)lp, false);
        return 0;
    case TBM_GETPOS:
        return SliderPos(h);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        SliderPaint(h);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        SetCapture(h);
        SliderFromMouse(h, (short)LOWORD(lp));
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == h) SliderFromMouse(h, (short)LOWORD(lp));
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) ReleaseCapture();
        return 0;
    case WM_KEYDOWN: {
        int pos = SliderPos(h), max = SliderMax(h), page = max / 10 > 1 ? max / 10 : 1;
        switch (wp) {
        case VK_LEFT: case VK_DOWN: pos -= 1; break;
        case VK_RIGHT: case VK_UP: pos += 1; break;
        case VK_PRIOR: pos += page; break;
        case VK_NEXT: pos -= page; break;
        case VK_HOME: pos = 0; break;
        case VK_END: pos = max; break;
        default: return DefWindowProcA(h, msg, wp, lp);
        }
        SliderSet(h, pos, true);
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void RegisterSlider(HINSTANCE inst)
{
    WNDCLASSA wc = {};
    wc.lpfnWndProc = SliderProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_HAND);
    wc.cbWndExtra = 2 * sizeof(LONG_PTR);  // position, maximum
    wc.lpszClassName = kSliderClass;
    RegisterClassA(&wc);
}

static void ShowHelpFor(HWND hwnd)
{
    static HWND last;
    if (hwnd == last) return;
    last = hwnd;
    for (Option& o : kOptions)
        if (hwnd == o.ctrl || hwnd == o.label || hwnd == o.value) {
            SetWindowTextA(s_info, o.help);
            return;
        }
}

static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(wnd, &r);
        FillRect((HDC)wp, &r, s_backBrush);
        RECT side = { 0, 0, S(kSideW), r.bottom };
        FillRect((HDC)wp, &side, s_sideBrush);
        RECT sep = { S(kContentX), S(kHeight - 116), r.right - S(24), S(kHeight - 115) };
        HBRUSH b = CreateSolidBrush(kBorder);
        FillRect((HDC)wp, &sep, b);
        DeleteObject(b);
        return 1;
    }
    case WM_DRAWITEM:
        DrawButton((DRAWITEMSTRUCT*)lp);
        return TRUE;
    case WM_SETCURSOR:
        ShowHelpFor((HWND)wp);
        break;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= ID_OPTION0 && id < ID_OPTION0 + kOptionCount) {
            Option& o = kOptions[id - ID_OPTION0];
            if (o.kind == BOOL_OPT && code == BN_CLICKED) {
                s_toggle[id - ID_OPTION0] = !s_toggle[id - ID_OPTION0];
                InvalidateRect(o.ctrl, NULL, TRUE);
                Changed(o);
            } else if (o.kind == CHOICE && code == BN_CLICKED) {
                // the items as a popup menu below the field
                auto items = Choices(o);
                HMENU menu = CreatePopupMenu();
                for (int i = 0; i < (int)items.size(); i++)
                    AppendMenuA(menu, MF_STRING | (i == s_choice[id - ID_OPTION0] ? MF_CHECKED : 0) |
                                          (i && i % 12 == 0 ? MF_MENUBARBREAK : 0), i + 1,
                                items[i].second.c_str());
                RECT r;
                GetWindowRect(o.ctrl, &r);
                int pick = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom + S(2), 0,
                                          wnd, NULL);
                DestroyMenu(menu);
                if (pick > 0) {
                    s_choice[id - ID_OPTION0] = pick - 1;
                    InvalidateRect(o.ctrl, NULL, TRUE);
                    Changed(o);
                }
            }
            if (code == BN_SETFOCUS) ShowHelpFor(o.ctrl);
        } else if (id >= ID_NAV0 && id < ID_NAV0 + kPageCount) {
            ShowPage(id - ID_NAV0);
        } else if (id >= ID_PRESET0 && id < ID_PRESET0 + 3) {
            char q[200];
            sprintf(q, "Set every option to \"%s\"?", kPresets[id - ID_PRESET0].name);
            if (MessageBoxA(wnd, q, "glimpse_fix", MB_YESNO | MB_ICONQUESTION) == IDYES) ApplyPreset(kPresets[id - ID_PRESET0]);
        } else if (id == ID_RESTORE) {
            RestoreBackup(wnd);
        } else if (id == ID_OPEN) {
            if (PickIni(wnd)) Open();
        } else if (id == ID_CLOSE) {
            DestroyWindow(wnd);
        }
        return 0;
    }
    case WM_HSCROLL:
        for (Option& o : kOptions)
            if (o.ctrl == (HWND)lp) {
                Changed(o);
                ShowHelpFor(o.ctrl);
            }
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        HWND c = (HWND)lp;
        Role role = (Role)GetWindowLongPtrA(c, GWLP_USERDATA);
        bool side = role == R_BRAND || role == R_SIDEDIM;
        SetBkMode(dc, TRANSPARENT);
        COLORREF col = kText;
        if (role == R_BRAND) col = kAccent;
        else if (role == R_DIM || role == R_SIDEDIM) col = kDim;
        else if (role == R_VALUE) col = IsWindowEnabled(c) ? kAccent : kOff;
        else if (role == R_LABEL) col = IsWindowEnabled(c) ? kText : kOff;
        SetTextColor(dc, col);
        return (LRESULT)(side ? s_sideBrush : s_backBrush);
    }
    case WM_DPICHANGED: {
        s_dpi = HIWORD(wp);
        MakeFonts();
        EnumChildWindows(wnd, DestroyChild, 0);
        for (Option& o : kOptions) o.label = o.ctrl = o.value = NULL;
        CreateControls();
        LoadControls();
        ShowPage(s_page);
        const RECT* r = (const RECT*)lp;
        SetWindowPos(wnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(wnd, NULL, TRUE);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(wnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show)
{
    DpiAware();
    s_dpi = WindowDpi(NULL);
    EnableDarkMenus();
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    RegisterSlider(inst);
    s_backBrush = CreateSolidBrush(kBack);
    s_sideBrush = CreateSolidBrush(kSide);
    s_fieldBrush = CreateSolidBrush(kField);

    // the INI next to this program, else ask
    GetModuleFileNameA(NULL, s_path, MAX_PATH);
    char* slash = strrchr(s_path, '\\');
    strcpy(slash ? slash + 1 : s_path, "SplitSecondPad.ini");
    if (GetFileAttributesA(s_path) == INVALID_FILE_ATTRIBUTES) {
        char exe[MAX_PATH];
        strcpy(exe, s_path);
        strcpy(strrchr(exe, '\\') + 1, "SplitSecond.exe");
        if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES && !PickIni(NULL)) return 0;
    }

    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.hIcon = (HICON)LoadImageA(inst, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageA(inst, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = "glimpse_config";
    RegisterClassExA(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    SIZE size = WindowSize(style);
    s_wnd = CreateWindowExA(0, wc.lpszClassName, "glimpse_fix settings", style, CW_USEDEFAULT, CW_USEDEFAULT, size.cx,
                            size.cy, NULL, NULL, inst, NULL);
    int dpi = WindowDpi(s_wnd);  // the monitor the window opened on
    if (dpi != s_dpi) {
        s_dpi = dpi;
        size = WindowSize(style);
        SetWindowPos(s_wnd, NULL, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    MakeFonts();
    DarkTitleBar(s_wnd);
    CreateControls();
    Open();
    ShowPage(0);
    ShowWindow(s_wnd, show);

    MSG m;
    while (GetMessageA(&m, NULL, 0, 0)) {
        if (IsDialogMessageA(s_wnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
