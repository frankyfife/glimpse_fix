// glimpse_fix - settings for SplitSecondPad.ini
//
// A plain Win32 program (static CRT, only Windows' own DLLs) that edits the INI next to it, or one picked
// with the file dialog. Values are changed in place, so comments, order and unknown keys stay as they
// are; a key that is missing is added to its section. Every change is saved at once: the bloom and
// exposure values are read by the game every 2 s, everything else at the next start.
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
    { "Gameplay", "UnlockAll", BOOL_OPT, "Everything unlocked", "All tracks, events, episodes, game modes and cars selectable. Your profile is not changed.", "0" },
    { "Gameplay", "BonusContent", BOOL_OPT, "Bonus content", "The PC version's cheat code extras: Expressway track, Cobretti SlipStream, branded cars, Euro livery.", "0" },
    { "Gameplay", "SkipIntro", CHOICE, "Boot movies", "Skip the movies at every start. The files stay untouched.", "0", "0|Play both;1|Skip the logo;2|Skip logo and intro" },
    { "Gameplay", "Boost", BOOL_OPT, "Boost (hold X)", "Your car surges forward while X is held, with more motion blur.", "0" },
    { "Gameplay", "BoostAcceleration", RANGE, "  Boost acceleration", "m/s per second; 15 is about 1.5 g. Also used by the AI boost.", "15", 0, 1, 60, 1, 0 },
    { "Gameplay", "BoostMaxSpeed", RANGE, "  Boost top speed", "No boost above this speed, in m/s (90 m/s = 324 km/h). Also used by the AI boost.", "90", 0, 10, 200, 1, 0 },
    { "Gameplay", "BoostMotionBlur", RANGE, "  Boost motion blur", "Motion blur multiplier at full boost.", "3.0", 0, 1, 5, 0.1, 1 },
    { "Gameplay", "PowerBarFill", BOOL_OPT, "Full power play bar (LS+RS)", "Press both sticks to fill your power play bar.", "0" },
    { "Gameplay", "AIBoost", BOOL_OPT, "AI boost (experimental)", "The AI cars boost too, while they are behind you or not far ahead.", "0" },
    { "Gameplay", "AIBoostLevel", CHOICE, "  AI boost level", "Easy: now and then, up to 30 m ahead. Normal: about once a second, 60 m. Hard: almost constantly, 150 m.", "2", "1|Easy;2|Normal;3|Hard" },
    { "Gameplay", "AIBoostUnlimited", BOOL_OPT, "  Unlimited AI boost", "The AI's boost tank never empties.", "0" },
    { "Gameplay", "AIBoostStraightOnly", BOOL_OPT, "  AI boosts on straights only", "Off = anywhere while driving forward, also in corners.", "1" },
    { "Gameplay", "AIBoostChaos", BOOL_OPT, "  Chaos mode", "Every AI car boosts all the time, everywhere, with an endless tank and a full power play bar. Overrides the three settings above.", "0" },
    // Debug
    { "Debug", "Log", BOOL_OPT, "Write SplitSecondPad.log", "A log next to the game, for bug reports.", "0" },
};
static const int kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);
static const char* const kPages[] = { "Controller", "Graphics", "World", "Gameplay", "Debug" };
static const int kPageCount = 5;

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

static bool SaveIni()
{
    FILE* f = fopen(s_path, "wb");
    if (!f) return false;
    for (const std::string& l : s_lines) {
        fputs(l.c_str(), f);
        fputs(s_crlf ? "\r\n" : "\n", f);
    }
    fclose(f);
    return true;
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

// ---------------------------------------------------------------- window

static HWND s_wnd, s_tab, s_status, s_tip;
static HFONT s_font;
static int s_dpi = 96;
static int s_page;
static bool s_loading;

static int S(int px) { return MulDiv(px, s_dpi, 96); }

static Option* Find(const char* key)
{
    for (Option& o : kOptions)
        if (!strcmp(o.key, key)) return &o;
    return NULL;
}

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
    if (!strcmp(o.key, "BoostMaxSpeed")) text += " m/s  (" + std::to_string((int)(v * 3.6 + 0.5)) + " km/h)";
    if (!strcmp(o.key, "BoostAcceleration")) text += " m/s per second";
    SetWindowTextA(o.value, text.c_str());
}

static bool IsOn(const char* key)
{
    Option* o = Find(key);
    return o && SendMessageA(o->ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void UpdateEnables()
{
    bool bloom = IsOn("Bloom"), boost = IsOn("Boost"), ai = IsOn("AIBoost"), chaos = IsOn("AIBoostChaos");
    struct { const char* key; bool on; } rules[] = {
        { "BloomStrength", bloom }, { "BloomTone", bloom }, { "BloomCurve", bloom },
        { "BoostMotionBlur", boost },
        { "AIBoostLevel", ai && !chaos }, { "AIBoostUnlimited", ai && !chaos },
        { "AIBoostStraightOnly", ai && !chaos }, { "AIBoostChaos", ai },
    };
    for (auto& r : rules) {
        Option* o = Find(r.key);
        EnableWindow(o->ctrl, r.on);
        if (o->value) EnableWindow(o->value, r.on);
    }
}

static void Status(const char* text) { SetWindowTextA(s_status, text); }

static void LoadControls()
{
    s_loading = true;
    for (Option& o : kOptions) {
        std::string v = GetValue(o);
        if (o.kind == BOOL_OPT) {
            SendMessageA(o.ctrl, BM_SETCHECK, atoi(v.c_str()) ? BST_CHECKED : BST_UNCHECKED, 0);
        } else if (o.kind == CHOICE) {
            auto items = Choices(o);
            int sel = -1;
            for (int i = 0; i < (int)items.size(); i++)
                if (!_stricmp(items[i].first.c_str(), v.c_str()) || (atof(v.c_str()) == atof(items[i].first.c_str()) &&
                                                                       isdigit((unsigned char)v[0]))) sel = i;
            if (sel < 0) {  // a value set by hand that is not in the list
                SendMessageA(o.ctrl, CB_ADDSTRING, 0, (LPARAM)(v + " (custom)").c_str());
                sel = (int)items.size();
            }
            SendMessageA(o.ctrl, CB_SETCURSEL, sel, 0);
        } else {
            double d = atof(v.c_str());
            if (d < o.lo) d = o.lo;
            if (d > o.hi) d = o.hi;
            SendMessageA(o.ctrl, TBM_SETPOS, TRUE, (LPARAM)(int)((d - o.lo) / o.step + 0.5));
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
        v = SendMessageA(o.ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0";
    } else if (o.kind == CHOICE) {
        int sel = (int)SendMessageA(o.ctrl, CB_GETCURSEL, 0, 0);
        auto items = Choices(o);
        if (sel < 0 || sel >= (int)items.size()) return;  // the custom entry stays as it is
        v = items[sel].first;
    } else {
        double d = o.lo + SendMessageA(o.ctrl, TBM_GETPOS, 0, 0) * o.step;
        ShowValue(o, d);
        v = FormatRange(o, d);
    }
    SetValue(o, v);
    UpdateEnables();
    char msg[160];
    if (SaveIni()) sprintf(msg, "Saved %s=%s", o.key, v.c_str());
    else sprintf(msg, "Could not write %s", s_path);
    Status(msg);
}

static void ShowPage(int page)
{
    s_page = page;
    for (Option& o : kOptions) {
        bool show = !strcmp(o.section, kPages[page]);
        int cmd = show ? SW_SHOW : SW_HIDE;
        if (o.label) ShowWindow(o.label, cmd);
        ShowWindow(o.ctrl, cmd);
        if (o.value) ShowWindow(o.value, cmd);
    }
}

struct Preset {
    const char* name;
    const char* values;  // "Key=value;..."
};
static const Preset kPresets[] = {
    { "Recommended", "" },  // every option at its default
    { "Original game", "FrameRate=30;RenderScale=auto;Anisotropy=0;MotionBlurStrength=1.0;Vignette=1;Bloom=1;BloomStrength=1.0;BloomTone=1.0;BloomCurve=1.0;Exposure=1.0;LensFlare=1;ParticleFlare=1;FixPowerplayIconSize=0;StreamPoolMB=0;LodDistanceScale=1.0;PopInFix=0;SeparateTriggers=0" },
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

static void AddTip(HWND ctrl, const char* text)
{
    TOOLINFOA ti = { sizeof(ti) };
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = s_wnd;
    ti.uId = (UINT_PTR)ctrl;
    ti.lpszText = (LPSTR)text;
    SendMessageA(s_tip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
}

static HWND Child(const char* cls, const char* text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND h_ = CreateWindowExA(0, cls, text, WS_CHILD | style, x, y, w, h, s_wnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageA(h_, WM_SETFONT, (WPARAM)s_font, TRUE);
    return h_;
}

enum { ID_TAB = 1, ID_PRESET0 = 10, ID_OPEN = 20, ID_CLOSE = 21, ID_OPTION0 = 100 };

static void CreateControls()
{
    const int left = S(24), labelW = S(250), ctrlX = left + labelW, ctrlW = S(260), rowH = S(30), top = S(64);
    s_tab = Child(WC_TABCONTROLA, "", WS_VISIBLE | WS_CLIPSIBLINGS, S(10), S(10), S(640), S(560), ID_TAB);
    for (int i = 0; i < kPageCount; i++) {
        TCITEMA item = { TCIF_TEXT };
        item.pszText = (LPSTR)kPages[i];
        SendMessageA(s_tab, TCM_INSERTITEMA, i, (LPARAM)&item);
    }
    int row[kPageCount] = {};
    for (int i = 0; i < kOptionCount; i++) {
        Option& o = kOptions[i];
        int page = 0;
        while (strcmp(kPages[page], o.section)) page++;
        int y = top + row[page]++ * rowH;
        int id = ID_OPTION0 + i;
        if (o.kind == BOOL_OPT) {
            o.ctrl = Child("BUTTON", o.text, WS_TABSTOP | BS_AUTOCHECKBOX, left, y, labelW + ctrlW, S(22), id);
        } else {
            o.label = Child("STATIC", o.text, SS_LEFT | SS_CENTERIMAGE, left, y, labelW, S(22), 0);
            if (o.kind == CHOICE) {
                o.ctrl = Child("COMBOBOX", "", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ctrlX, y, ctrlW, S(300), id);
                for (auto& c : Choices(o)) SendMessageA(o.ctrl, CB_ADDSTRING, 0, (LPARAM)c.second.c_str());
            } else {
                o.ctrl = Child(TRACKBAR_CLASSA, "", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, ctrlX, y, S(170), S(26), id);
                SendMessageA(o.ctrl, TBM_SETRANGE, TRUE, MAKELPARAM(0, RangeSteps(o)));
                SendMessageA(o.ctrl, TBM_SETPAGESIZE, 0, max(1, RangeSteps(o) / 10));
                o.value = Child("STATIC", "", SS_LEFT | SS_CENTERIMAGE, ctrlX + S(176), y, S(150), S(22), 0);
            }
        }
        AddTip(o.ctrl, o.help);
    }
    Child("STATIC", "Hover an option for details. Changes are saved at once.", WS_VISIBLE | SS_LEFT, S(24),
          S(40), S(600), S(20), 0);
    int by = S(582);
    Child("STATIC", "Preset:", WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, S(14), by, S(50), S(26), 0);
    for (int i = 0; i < 3; i++)
        Child("BUTTON", kPresets[i].name, WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, S(66) + i * S(118), by, S(112),
              S(26), ID_PRESET0 + i);
    Child("BUTTON", "Open INI...", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, S(440), by, S(100), S(26), ID_OPEN);
    Child("BUTTON", "Close", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, S(550), by, S(100), S(26), ID_CLOSE);
    s_status = Child("STATIC", "", WS_VISIBLE | SS_LEFT | SS_PATHELLIPSIS, S(14), S(616), S(636), S(20), 0);
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
    if (!LoadIni()) {
        s_lines.clear();
        Status("No SplitSecondPad.ini here yet: it is created with the first change.");
    } else {
        std::string msg = std::string("Editing ") + s_path;
        Status(msg.c_str());
    }
    LoadControls();
}

static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NOTIFY:
        if (((NMHDR*)lp)->idFrom == ID_TAB && ((NMHDR*)lp)->code == TCN_SELCHANGE)
            ShowPage((int)SendMessageA(s_tab, TCM_GETCURSEL, 0, 0));
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= ID_OPTION0 && id < ID_OPTION0 + kOptionCount) {
            Option& o = kOptions[id - ID_OPTION0];
            if ((o.kind == BOOL_OPT && code == BN_CLICKED) || (o.kind == CHOICE && code == CBN_SELCHANGE)) Changed(o);
        } else if (id >= ID_PRESET0 && id < ID_PRESET0 + 3) {
            char q[200];
            sprintf(q, "Set every option to \"%s\"?", kPresets[id - ID_PRESET0].name);
            if (MessageBoxA(wnd, q, "glimpse_fix", MB_YESNO | MB_ICONQUESTION) == IDYES) ApplyPreset(kPresets[id - ID_PRESET0]);
        } else if (id == ID_OPEN) {
            if (PickIni(wnd)) Open();
        } else if (id == ID_CLOSE) {
            DestroyWindow(wnd);
        }
        return 0;
    }
    case WM_HSCROLL:
        for (Option& o : kOptions)
            if (o.ctrl == (HWND)lp) Changed(o);
        return 0;
    case WM_CTLCOLORSTATIC:  // labels on the tab page background
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(wnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show)
{
    SetProcessDPIAware();
    HDC dc = GetDC(NULL);
    s_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    NONCLIENTMETRICSA ncm = { sizeof(ncm) };
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    s_font = CreateFontIndirectA(&ncm.lfMessageFont);

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

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = "glimpse_config";
    RegisterClassA(&wc);
    RECT r = { 0, 0, S(660), S(644) };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRect(&r, style, FALSE);
    s_wnd = CreateWindowExA(0, wc.lpszClassName, "glimpse_fix settings", style, CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    s_tip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0, 0, s_wnd, NULL,
                            inst, NULL);
    SendMessageA(s_tip, TTM_SETMAXTIPWIDTH, 0, S(420));
    SendMessageA(s_tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
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
