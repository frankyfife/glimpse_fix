// glimpse_fix - settings for SplitSecondPad.ini
//
// A plain Win32 program (static CRT, only Windows' own DLLs) with a dark look of its own. It edits the
// INI next to it, or one picked with the file dialog. Values are changed in place, so comments, order and
// unknown keys stay as they are; a key that is missing is added to its section. Every change is saved at
// once: the bloom, exposure and opponents' engine volume values are read by the game every 2 s, everything else
// at the next start.
// Before the first change the INI as it was opened is kept as SplitSecondPad.ini.bak.
#define WIN32_LEAN_AND_MEAN
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shldisp.h>
#include <knownfolders.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include "../src/ini_percent.h"
#include "../src/hud_resolution.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")     // the folder picker (IFileOpenDialog), the shell's zip folders
#pragma comment(lib, "oleaut32.lib")  // ... their VARIANTs
#pragma comment(lib, "uuid.lib")      // the known folders (Desktop, Downloads), the shell's ids
#pragma comment(lib, "advapi32.lib")  // Steam's folder in the registry
#pragma comment(lib, "version.lib")   // this program's version (the DLC tools of the same release)
#pragma comment(lib, "winhttp.lib")   // the DLC tools download
#pragma comment(lib, "bcrypt.lib")    // ... its SHA-256
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------------------------------------------------------- options

enum Kind { BOOL_OPT, CHOICE, RANGE, TEXT, PATH };  // TEXT: a line of text (TextCharOk, TextLimit); PATH: a folder

struct Option {
    const char* section;
    const char* key;
    Kind kind;
    const char* text;
    const char* help;
    const char* def;
    // CHOICE: "value|Text;value|Text;..."   RANGE: min, max, step, decimals
    // TEXT: choices = the characters allowed besides letters and digits (NULL: - and _), hi = most characters (0: 32)
    const char* choices;
    double lo, hi, step;
    int decimals;
    HWND label, ctrl, value;  // label (CHOICE, RANGE), control, value label (RANGE)
};

// keys for the camera and gameplay options, as virtual key codes (P is the game's own pause key)
// pad buttons the game leaves free while driving
#define PAD_CHOICES "X|X;Y|Y;LS|Left stick click;RS|Right stick click;None|None"
#define KEY_CHOICES "0x31|1;0x32|2;0x33|3;0x34|4;0x35|5;0x36|6;0x37|7;0x38|8;0x39|9;0x30|0;0x41|A;0x42|B;0x43|C;0x44|D;0x45|E;0x46|F;0x47|G;0x48|H;0x49|I;0x4A|J;0x4B|K;0x4C|L;0x4D|M;0x4E|N;0x4F|O;0x51|Q;0x52|R;0x53|S;0x54|T;0x55|U;0x56|V;0x57|W;0x58|X;0x59|Y;0x5A|Z;0x70|F1;0x71|F2;0x72|F3;0x73|F4;0x74|F5;0x75|F6;0x76|F7;0x77|F8;0x78|F9;0x79|F10;0x7A|F11;0x7B|F12;0xA0|Left Shift;0xA2|Left Ctrl;0x20|Space;0x2D|Insert;0x2E|Delete;0x24|Home;0x21|Page Up;0x22|Page Down;0x60|Num 0;0x6E|Num ,;0x6B|Num +;0x6D|Num -"
// the photo mode key: without the keys the photo mode itself uses (W A S D, I J K L, Q E, U O, Shift, Ctrl,
// the default zoom keys 1 2 3)
#define PHOTO_KEY_CHOICES "0x34|4;0x35|5;0x36|6;0x37|7;0x38|8;0x39|9;0x30|0;0x42|B;0x43|C;0x46|F;0x47|G;0x48|H;0x4D|M;0x4E|N;0x52|R;0x54|T;0x56|V;0x58|X;0x59|Y;0x5A|Z;0x70|F1;0x71|F2;0x72|F3;0x73|F4;0x74|F5;0x75|F6;0x76|F7;0x77|F8;0x78|F9;0x79|F10;0x7A|F11;0x7B|F12;0x20|Space;0x2D|Insert;0x2E|Delete;0x24|Home;0x21|Page Up;0x22|Page Down;0x60|Num 0;0x6E|Num ,;0x6B|Num +;0x6D|Num -"

static Option kOptions[] = {
    // Controller
    { "Controller", "VirtualPads", RANGE, "Virtual pads", "XInput slots presented as Xbox 360 pads. Lower this to make room for a wheel within the four-joystick limit. At 0 the extras still run, but the game reads the pad itself and acts on every button, so the boost, handbrake, power bar fill and photo mode then work with their keys only.", "4", 0, 0, 4, 1, 0 },
    { "Controller", "HideXInputFromDirectInput", BOOL_OPT, "Hide the real pads from DirectInput", "Keeps XInput controllers from showing up twice.", "1" },
    { "Controller", "SeparateTriggers", BOOL_OPT, "Separate triggers", "LT and RT on their own axes, so you can brake and accelerate at the same time. Off = the stock shared axis.", "1" },
    { "Controller", "StickDeadzone", RANGE, "Stick deadzone", "Radial deadzone of the sticks.", "0.15", 0, 0, 0.5, 0.01, 2 },
    { "Controller", "TriggerDeadzone", RANGE, "Trigger deadzone", "Deadzone of LT and RT.", "0.05", 0, 0, 0.5, 0.01, 2 },
    { "Controller", "RumbleStrength", RANGE, "Rumble strength", "Multiplier on the game's rumble. 0 = off.", "1.0", 0, 0, 2, 0.05, 2 },
    { "Controller", "ButtonGlyphs", CHOICE, "Button prompts", "Icons in the prompt bar while the pad is in use. Keyboard = never switch.", "xbox", "xbox|Xbox;playstation|PlayStation;keyboard|Keyboard" },
    { "Controller", "MenuRepeat", BOOL_OPT, "D-pad repeat in menus", "A held D-pad keeps scrolling in the menus and the car select, one step at a time. For the virtual pads, not for other DirectInput devices. Not during a race; a direction pressed within half a second of opening the pause menu repeats only after pressing it again. Off = one step per press.", "1" },
    { "Controller", "MenuRepeatDelay", RANGE, "  D-pad repeat delay (ms)", "How long the D-pad is held before it repeats.", "450", 0, 100, 2000, 10, 0 },
    { "Controller", "MenuRepeatRate", RANGE, "  D-pad repeat rate (ms)", "Time between two repeated steps: a step every this many ms plus a 40 ms release. Higher = slower, more precise.", "160", 0, 80, 1000, 10, 0 },
    // Graphics
    { "Graphics", "FrameRate", CHOICE, "Frame rate", "60 fps with the simulation stepped exactly as at 30 fps.", "60", "60|60 fps;30|30 fps (stock)" },
    { "Graphics", "SMAA", BOOL_OPT, "Anti-aliasing (SMAA)", "Smooths the jagged edges of the 3D picture with SMAA 1x before the game draws its HUD, so text and HUD stay sharp. Costs about 0.1 ms per frame at 4K on an RTX 5090 (at Very High, which renders at twice the resolution, about four times that).", "1" },
    { "Graphics", "QualityCap", CHOICE, "Graphics quality limit", "The highest of the game's graphics quality levels it may use, whatever its menu is set to. The level chosen in the game's menu is kept and applies again once the limit allows it. Below High the sun shadows are simpler and shorter and there is no motion blur; Low also has no depth of field.", "3", "3|None (the game's setting);2|At most High;1|At most Medium;0|Low" },
    { "Graphics", "TextureDetail", CHOICE, "Texture detail", "Full = the textures as the game streams them. Lower draws every texture that has smaller versions of itself at half, a quarter or an eighth of its size. Text stays sharp; menu and HUD pictures that have smaller versions get blurrier too. It changes only what is drawn, not what is loaded.", "0", "0|Full;1|Half;2|Quarter;3|Eighth" },
    { "Graphics", "GlassFix", BOOL_OPT, "Car glass as on the Xbox 360", "The PC port lets every car window take the sun's shine of whatever lies behind it; the window frames of the converted DLC cars turn flat white in the sun. On = the glass as on the Xbox 360: no sun highlight on it, its reflections stay. The glass of the base cars changes slightly too.", "1" },
    { "Graphics", "TextureLoadFix", BOOL_OPT, "Menu loading stutter", "Removes the short freezes when menus, the HUD and the race results screen load. Single-level textures (menus, HUD, skies, lens flares and colour grading) are loaded as authored, without generating mip copies on the CPU; pre-mipped world textures stay unchanged. Off = as the game.", "1" },
    { "Graphics", "Anisotropy", CHOICE, "Anisotropic filtering", "Sharper textures at an angle. Off = the game's plain linear filtering.", "16", "0|Off;2|2x;4|4x;8|8x;16|16x" },
    { "Graphics", "MotionBlurStrength", RANGE, "Motion blur", "Multiplier on the cameras' motion blur (graphics quality High or Very High). 0 = off.", "1.0", 0, 0, 3, 0.1, 1 },
    { "Graphics", "Vignette", BOOL_OPT, "Vignette", "Darkened screen edges of the race cameras.", "0" },
    { "Graphics", "Gradient", BOOL_OPT, "Screen gradient", "The translucent blue band the cameras lay over the top of the screen.", "1" },
    { "Graphics", "Bloom", BOOL_OPT, "Bloom", "Glow around bright areas. Off keeps the tone mapping.", "1" },
    { "Graphics", "BloomStrength", RANGE, "  Bloom strength", "Glow on bright areas, on top of each camera's value. Applies while the game runs.", "1.0", 0, 0, 10, 0.05, 2 },
    { "Graphics", "BloomTone", RANGE, "  Bloom tone", "Second bright pass value, a softer effect. Applies while the game runs.", "1.0", 0, 0, 10, 0.05, 2 },
    { "Graphics", "BloomCurve", RANGE, "  Bloom curve", "How the glow is added; below 1.0 has no effect. Applies while the game runs.", "1.0", 0, 0, 10, 0.05, 2 },
    { "Graphics", "Exposure", RANGE, "Exposure", "Brightness of the whole picture. Applies while the game runs.", "1.0", 0, 0.25, 3, 0.05, 2 },
    { "Graphics", "LensFlare", BOOL_OPT, "Lens flares", "Flares of the sun and lights, including the screen space flare.", "1" },
    { "Graphics", "ParticleFlare", BOOL_OPT, "Particle flares", "Flares of sparks, explosions and lights.", "1" },
    { "Graphics", "HDR", BOOL_OPT, "HDR output", "HDR10 picture for an HDR display (HDR switched on in the Windows display settings). The game then runs in a borderless window over the whole screen.", "0" },
    { "Graphics", "HDRPeakBrightness", RANGE, "  Peak brightness", "The display's peak brightness in nits (see the display's data or the Windows HDR calibration). The brightest lights go up to this.", "1000", 0, 400, 4000, 50, 0 },
    { "Graphics", "HDRPaperWhite", RANGE, "  Paper white", "Brightness of the game's white (menus, HUD, a white wall in daylight) in nits. Higher = the whole picture brighter.", "200", 0, 80, 500, 10, 0 },
    { "Graphics", "FixPowerplayIconSize", BOOL_OPT, "Power play icons at console size", "The PC port draws the power play icons and route markers too big above 1280 pixels width (3x at 4K).", "1" },
    { "Graphics", "HudResolution", CHOICE, "3D HUD resolution", "Sharper HUD behind the car (lap, position, power bar). Auto: double size up to 1080p, quadruple above and in split screen. Original size keeps the game's texture but fixes the split-screen text colors. Off leaves the HUD as the game draws it. Steam build 385319 only (not the Russian depot).", "0",
      "0|Auto (recommended);1|Original size;2|Double;4|Quadruple;-1|Off (as the game)" },
    // World
    { "World", "StreamPoolMB", CHOICE, "Streaming pool", "Memory for the track's textures and props. Bigger keeps them sharp instead of blurry or grey. Needs SplitSecond.exe with the LAA flag (setlaa.exe).", "512", "0|Stock (57 MB);256|256 MB;512|512 MB;768|768 MB" },
    { "World", "LodDistanceScale", RANGE, "Draw distance", "Multiplier for LOD, streaming, particle, debris and power play distances.", "2.0", 0, 0.5, 8, 0.1, 1 },
    { "World", "ShadowDistance", RANGE, "Shadow distance", "Multiplier for how far the sun shadows reach (the game: 300 m) at the High and Very High quality levels in single player. Shadows close to the car get a little softer the higher it is.", "2.0", 0, 1, 3, 0.1, 1 },
    { "World", "ShadowResolution", CHOICE, "Shadow resolution", "Size of the sun shadow map: the game draws three cascades of 1024x1024 pixels. Higher = sharper shadow edges, also with a longer shadow distance. Double takes about 70 MB more graphics memory, quadruple about 360 MB (experimental).", "2", "1|As the game (1024);2|Double (2048);4|Quadruple (4096, experimental)" },
    { "World", "Shadows", BOOL_OPT, "Shadows", "Off: nothing casts a shadow. The game's own switch for everything that draws into its shadow maps (cars, track, debris).", "1" },
    { "World", "CarDetail", CHOICE, "Opponent car detail", "How detailed the opponents' cars are drawn. The PC release uses the PS3's settings: only 3 opponents in medium detail and every opponent coarse from 100 m on, so cars visibly pop between detail levels. As the Xbox 360: medium detail for all, coarse from 200 m on. High: all opponents in full detail up to 40 m.", "1", "0|As the PC release;1|As the Xbox 360;2|High" },
    { "World", "PopInFix", BOOL_OPT, "Pop-in fix", "Buildings beside the track no longer pop in at section borders (a little more to draw).", "1" },
    // Gameplay
    // Camera
    { "Camera", "FOV", RANGE, "Field of view", "Race cameras (chase and bumper) as a factor of the game's own; the game still widens the view with speed.", "1.0", 0, 0.5, 2, 0.05, 2 },
    { "Camera", "UltrawideFix", CHOICE, "Ultrawide fix", "Screens wider than 16:9. Race cameras: they keep the picture height of 16:9 instead of losing the top and bottom. Full (experimental): every camera does, and menus, HUD and result screens are shown at 16:9 in the centre.", "1", "0|Off;1|Race cameras;2|Full (experimental)" },
    { "Camera", "UltrawideBlackBackdrop", CHOICE, "  Unlock screen backdrop", "Car unlock, decal unlock and season reward screens in full ultrawide mode. As the game: keeps the stock black bars and the dimmed 3D track strips above and below the centre banner. Solid black: extends the top and bottom black bars to meet the banner, hiding the 3D strips while keeping text, icons and reveal art at their original proportions.", "0", "0|As the game (dimmed 3D strips);1|Solid black (full height)" },
    { "Camera", "CameraShake", RANGE, "Camera shake", "How much the race cameras (chase and bumper) shake on bumps and hits. Lower values smooth their rotation and help against motion sickness; turns follow a little later.", "1.0", 0, 0, 1, 0.05, 2 },
    { "Camera", "ExplosionShakeOff", BOOL_OPT, "No explosion shake", "On: the camera no longer shakes when explosions, collapsing buildings and power plays go off nearby.", "0" },
    { "Camera", "ZoomKeys", BOOL_OPT, "Zoom keys", "Zoom the race camera while a key is held.", "0" },
    { "Camera", "ZoomInKey", CHOICE, "  Zoom in", "Key held to zoom closer.", "0x31", KEY_CHOICES },
    { "Camera", "ZoomOutKey", CHOICE, "  Zoom out", "Key held to zoom wider.", "0x32", KEY_CHOICES },
    { "Camera", "ZoomResetKey", CHOICE, "  Zoom reset", "Back to the field of view above.", "0x33", KEY_CHOICES },
    { "Camera", "PhotoMode", BOOL_OPT, "Photo mode", "!The key or Back holds the race still, hides the HUD and frees the camera: sticks or W A S D / I J K L move and look, triggers or Q E up and down, Y + LB RB or U O tilt; d-pad or arrows set focus and depth of field, with X or Ctrl bloom and exposure. Once your car is wrecked the crash sequence decides the view: tied to the car on the road, not free after leaving the track.", "0" },
    { "Camera", "PhotoModeKey", CHOICE, "  Photo mode key", "Keys the game uses itself (Esc, P, Enter, arrows) and the photo mode's own controls are not offered.", "0x46", PHOTO_KEY_CHOICES },
    { "Camera", "HudKey", CHOICE, "HUD on/off key", "Switch the HUD off and on during a race.", "0", "0|None;" KEY_CHOICES },
    // Gameplay
    { "Gameplay", "LapSelect", BOOL_OPT, "Pick laps on the track select", "On quick play Race's track select the difficulty button (Tab, pad Y) picks the laps (the track's own count shown), also in split screen, and saves them as Laps. With Online over Steam the host's pick applies to its LAN game (8 at most). A race with changed laps keeps no personal best. Steam build only.", "0" },
    { "Gameplay", "Laps", RANGE, "  Laps per race", "The lap count with 'Pick laps on the track select' on; the track select picks 1-9. 10 to 16 work, but the game's HUD shows only one digit: from 10 on the lap and the total are cut off (10 reads as 1). 0 = as the game.", "0", 0, 0, 16, 1, 0 },
    { "Gameplay", "UnlockAll", BOOL_OPT, "Everything unlocked", "All tracks, events, episodes, game modes and cars selectable. Your profile is not changed.", "0" },
    { "Gameplay", "RaceEndTiming", BOOL_OPT, "Post-race Continue timing", "Show Continue when the single-player finish card accepts it. Shortens the offline Quick Play entrance and keeps the Season animation; split screen and online stay stock. Steam build only (not the Russian depot), requires the stock finish card.", "0" },
    { "Gameplay", "BonusContent", BOOL_OPT, "Bonus content", "The PC version's cheat code extras: Expressway track, Cobretti SlipStream, branded cars, Euro livery.", "0" },
    { "Gameplay", "UnlockDecals", CHOICE, "Achievements and decals", "Unlocked in the next single player race and saved with the profile (backed up to glimpse_backup first). Unobtainable = the six ranked online ones and the Pure decal.", "0", "0|As the game;2|Unobtainable only;1|All" },
    { "Gameplay", "SaveBackups", CHOICE, "Save backups at every start", "At every game start the save folder (Documents\\Disney Interactive Studios\\Split Second) is copied to %LOCALAPPDATA%\\glimpse_fix\\save_backups; this many copies are kept, the oldest goes first. Unchanged saves make no new copy. The saves are not checked for damage. To go back: quit the game, copy a backup's files into the save folder (Steam Cloud off).", "0", "0|Off;1|1 copy;2|2 copies;3|3 copies;4|4 copies;5|5 copies" },
    { "Gameplay", "CutsceneSkip", BOOL_OPT, "Skippable cutscenes", "The season's cutscenes (Coming up, Next time, the end sequence), the demo movies and the boot movies: the first press of A, B, X, Y, Start, Enter or Space shows \"Press again to skip\", a second press within 3 seconds skips the movie. The boot movies end on the first press.", "1" },
    { "Gameplay", "SkipIntro", CHOICE, "Boot movies", "Skip the movies at every start. The files stay untouched.", "0", "0|Play both;1|Skip the logo;2|Skip logo and intro" },
    { "Gameplay", "WreckTime", CHOICE, "Time until the respot (experimental)", "!Highly experimental: can softlock the game, especially together with the photo mode. How long after a wreck of yours (crashes, takedowns, power plays) the game puts your car back on the track.", "0", "0|As the game;5|5 s;8|8 s;10|10 s;15|15 s;20|20 s;30|30 s;60|60 s" },
    { "Gameplay", "Boost", BOOL_OPT, "Boost", "Your car surges forward while the boost button or key is held, with more motion blur.", "0" },
    { "Gameplay", "BoostButton", CHOICE, "  Boost button", "Pad button held for the boost. X and Y are free while driving; when Y is used, the game's camera change moves to D-pad down.", "X", PAD_CHOICES },
    { "Gameplay", "BoostKey", CHOICE, "  Boost key", "Keyboard key held for the boost. Left Shift is not used by the game.", "0xA0", "0|None;" KEY_CHOICES },
    { "Gameplay", "BoostAcceleration", RANGE, "  Boost acceleration", "m/s per second; 15 is about 1.5 g. Also used by the AI boost.", "15", 0, 1, 60, 1, 0 },
    { "Gameplay", "BoostMaxSpeed", RANGE, "  Boost top speed", "No boost above this speed, in m/s (90 m/s = 324 km/h). Also used by the AI boost.", "90", 0, 10, 200, 1, 0 },
    { "Gameplay", "BoostMotionBlur", RANGE, "  Boost motion blur", "Motion blur multiplier at full boost.", "3.0", 0, 1, 5, 0.1, 1 },
    { "Gameplay", "Handbrake", BOOL_OPT, "Handbrake", "The game has none: while the handbrake button or key is held, your car is thrown into a slide. In a network race the host's handbrake setting applies to everybody.", "0" },
    { "Gameplay", "HandbrakeButton", CHOICE, "  Handbrake button", "Pad button held for the handbrake. When Y is used, the game's camera change moves to D-pad down.", "Y", PAD_CHOICES },
    { "Gameplay", "HandbrakeKey", CHOICE, "  Handbrake key", "Keyboard key held for the handbrake. Space is in neither of the game's keyboard layouts (X looks back in keyboard 0).", "0x20", "0|None;" KEY_CHOICES },
    { "Gameplay", "HandbrakeMode", CHOICE, "  Handbrake type", "Drift: the game's own slide physics, as when a power play blast throws a car sideways. Brake: the brake pedal fully pressed, the game brakes the rear the more you steer (above 30 km/h); a player without a pad (keyboard in split screen) gets the drift.", "1", "1|Drift;2|Brake" },
    { "Gameplay", "HandbrakeStrength", RANGE, "  Handbrake pull", "Drift type: how hard the handbrake slows the car while held, in m/s² (10 = about 1 g). 0 = the slide only.", "15", 0, 0, 40, 1, 0 },
    { "Gameplay", "HandbrakeDrift", RANGE, "  Handbrake oversteer", "Drift type: how strongly the car oversteers with the handbrake, as its turning rate at full steering in rad/s (2.5 = about 140 degrees a second; higher = the rear swings out further). 0 = no extra oversteer.", "2.5", 0, 0, 5, 0.1, 1 },
    { "Gameplay", "PowerBarFill", BOOL_OPT, "Full power play bar (LS+RS)", "Press both sticks or the power bar key to fill your power play bar.", "0" },
    { "Gameplay", "PowerBarFillKey", CHOICE, "  Power bar key", "Keyboard key that fills the power play bar, like LS+RS on the pad. Left Ctrl is not used by the game.", "0xA2", "0|None;" KEY_CHOICES },
    { "Gameplay", "AIBoost", BOOL_OPT, "AI boost (experimental)", "The AI cars boost too, while they are behind you or not far ahead.", "0" },
    { "Gameplay", "AIBoostLevel", CHOICE, "  AI boost level", "Easy: now and then, up to 30 m ahead. Normal: about once a second, 60 m. Hard: almost constantly, 150 m.", "2", "1|Easy;2|Normal;3|Hard" },
    { "Gameplay", "AIBoostUnlimited", BOOL_OPT, "  Unlimited AI boost", "The AI's boost tank never empties.", "0" },
    { "Gameplay", "AIBoostStraightOnly", BOOL_OPT, "  AI boosts on straights only", "Off = anywhere while driving forward, also in corners.", "1" },
    { "Gameplay", "AIBoostChaos", BOOL_OPT, "  Chaos mode", "Every AI car boosts all the time, everywhere, with an endless tank, a full power play bar and the highest AI skill. Overrides the three settings above.", "0" },
    // Audio
    { "Audio", "OpponentEngines", BOOL_OPT, "Opponents' engines", "Matches engine sounds to their cars and reloads them between races. "
      "Cars without a matching sound use an available stock engine. High Octane cars have individual fallbacks. "
      "Car matching, reload and event capacity fixes are Steam build only (not the Russian depot).", "1" },
    { "Audio", "OpponentEngineVolume", RANGE, "  Opponents' engine volume (%)", "Volume of the opponents' engines in percent; your own car stays as it is. 70 is about 3 dB quieter, 140 about 3 dB louder. 100 = as the game. Read while the game runs; applies when the next sound starts or the mixer updates the group.", "100", 0, 0, 200, 5, 0 },
    { "Audio", "Ducking", BOOL_OPT, "Ducking", "Mixer side-chain ducking as designed (DynamicMixer.xml): collisions duck music and engines; power plays duck opponents' engines, surfaces, player engine and collisions; HUD ducks music; speech mutes music/swooshes/particles and ducks engines, collisions, power plays, surfaces, HUD and ambience. Steam build only.", "1" },
    { "Audio", "PowerPlayFlanger", BOOL_OPT, "Power Play flanger",
      "Restore the Xbox 360's sweeping effect on Power Play sounds when the game's mix calls for it. "
      "Steam build only (not the Russian depot).", "1" },
    { "Audio", "SoundPlacement", BOOL_OPT, "Sound placement", "The opponents' engines and other positional sounds get louder and quieter with distance and come from their direction, as the game was made. The PC release plays them at one level on every speaker. Steam build only.", "1" },
    { "Audio", "SoundBankFix", BOOL_OPT, "Fix sound start offsets", "FMOD on PC starts many sounds a few frames early, inside the sound before: the result screen music opens with a scrap of another track. Each sound starts where its data is.", "1" },
    { "Audio", "ConsoleReverb", BOOL_OPT, "Console reverb", "The reverb presets of the Xbox 360 release (halls, tunnels, under bridges, the menu). The PC release has one stock preset for almost all of them.", "1" },
    // Online
    { "Online", "SteamLAN", BOOL_OPT, "Online over Steam (experimental)", "EXPERIMENTAL: the game's LAN mode with the players of a Steam lobby instead of the local network. Needs steam_api.dll next to the game. Steam friends join through \"Join game\". In a LAN game the host's boost, handbrake and AI settings apply to all; the photo mode is a free camera there, the race goes on. Presets leave this page alone.", "0" },
    { "Online", "FriendsOnly", BOOL_OPT, "  Friends only", "Your lobby is open to your Steam friends only and not listed for anybody else. When a friend is already in one, you join theirs. The lobby name is not used then.", "0" },
    { "Online", "LobbyName", TEXT, "  Lobby name", "Players with the same name meet in one lobby, like a password: pick an unusual one to stay among yourselves. Empty = the public lobby, where everybody with the fix meets. Letters, digits, - and _.", "" },
    // Debug
    { "Debug", "DumpTextures", BOOL_OPT, "Dump textures on the next start", "At the next start the game's textures are written as DDS files to the folder glimpse_textures in the game folder (one folder per archive, each with an index.txt; about 570 MB, roughly 10 seconds). A window shows that the dump is running, then the game closes and this switch goes off again.", "0" },
    { "Debug", "Log", BOOL_OPT, "Write SplitSecondPad.log", "A log next to the game, for bug reports (with up to 2000 reverb changes). F9 in the game then saves the next frame pass by pass to glimpse_dump, for image errors.", "0" },
    { "Debug", "DumpWatchPS", TEXT, "  Frame dump: watched pixel shaders", "Development: pixel shader hashes as the F9 frame dump's log shows them, separated by commas (tools\\dlc\\shaders\\pshash.py lists those of the game's shaders). For the first 4 draws with one of them, the dump saves every render target right before and after the draw, with the draw's textures and shader constants in the log. Empty = none.", "", ",", 0, 271, 0, 0 },
    // DLC: your own Xbox 360 DLC, converted on this PC (the buttons below the options, see DlcStart)
    { "DLC", "DumpFolder", PATH, "Xbox DLC folder", "Your own Split/Second DLC from an Xbox 360 or Xenia: the folder 425607E7, its subfolder 00000002, or a folder with the package files. It is only read. Set up DLC looks for it by itself (Xenia, Desktop, Downloads, Xbox USB drives); \"...\" lists what it finds. No DLC is downloaded: without your own DLC there is nothing to convert.", "" },
    { "DLC", "Overlays", BOOL_OPT, "Converted DLC (experimental)", "EXPERIMENTAL: the converted archives in glimpse_dlc in the game folder are laid over the game's data: the DLC cars (11) and the Deadline mode, and the tracks below. Off = the game as it is, whatever glimpse_dlc holds.", "0" },
    { "DLC", "Cars", BOOL_OPT, "DLC cars in the car select without Unlock all", "The converted DLC cars in the car select of quick play and free play, also without \"Everything unlocked\". Not in the career unless the option below is on, not online.", "1" },
    { "DLC", "CarsCareer", BOOL_OPT, "  DLC cars in the career (experimental)", "EXPERIMENTAL: the DLC cars can also be picked for career events started from the episode screen that have no fixed car. The in-game car select between events still offers only the season's cars.", "0" },
    { "DLC", "Tracks", BOOL_OPT, "DLC tracks Quarry and Minepit Park (experimental)", "EXPERIMENTAL: Quarry and Minepit Park (Survival At The Rock's Survival track) get track numbers, so quick play loads them; Minepit Park brings the split screen mode Survival Race. Needs Overlays=1 and the converted tracks in glimpse_dlc. Off with Online over Steam unless the option below is on; not for the game's own LAN mode with players who lack the tracks.", "0" },
    { "DLC", "Music", BOOL_OPT, "  DLC race music on Quarry", "In races on Quarry the DLC package's own race music plays, as on the Xbox 360; Minepit Park keeps the game's music. Off = the game's own race music on Quarry too. Needs the DLC overlays and the DLC tracks (both above) built with the track's music (\"Set up DLC\" does that).", "1" },
    { "DLC", "TracksOnline", BOOL_OPT, "DLC track also online", "The DLC tracks also with Online over Steam. The game sends only the track's number, so you then see and join only the LAN games of players with the same DLC tracks (same track pack and fix version), and they yours; the in-game banner says when a game is not shown. Players without DLC tracks play with each other as before.", "0" },
    { "DLC", "PersonalBests", BOOL_OPT, "Original DLC personal bests", "Save local bests for the original Xbox 360 DLC events in your normal profile and show solo bests on the Quick Play track card. Quarry Race uses three laps. Deadline uses the original countdown and pickup layout. Steam build only (not the Russian depot).", "0" },
    // page "Deadline mode" (PageOf): the DLC game mode
    { "DLC", "Modes", BOOL_OPT, "Deadline game mode (experimental)", "EXPERIMENTAL: opens the entry of the DLC game mode Deadline (a countdown time trial) in quick play; nothing else is unlocked. Single player. Also shows Survival Race's own quick play movie (the mode itself comes with the DLC tracks). Needs the converted DLC (page DLC), built with the mode (\"Set up DLC\" does that).", "0" },
    { "DLC", "Pickups", BOOL_OPT, "  Deadline pickups (experimental)", "EXPERIMENTAL: Deadline's time pickups as on the Xbox 360: its hexagonal tokens (from your own DLC; plain cubes without it) and its HUD: the timer turns cyan while the clock stands, \"+0.5s\" / \"+2.0s\" at the top and the pickup sounds. The texts are glimpse_fix's own wording; the official ones are in the Xbox title update, not in the DLC.", "0" },
    { "DLC", "PickupVignette", BOOL_OPT, "  Frozen Time look while the clock is stopped", "While a pickup holds the Deadline clock, the screen gets the Xbox's Frozen Time look (cyan edges, the colour grade off). Off = no such look; the clock stops all the same.", "1" },
    { "DLC", "DeadlineBonus", RANGE, "  Deadline extra start time (without pickups)", "The converted Deadline mode starts with the Xbox times, which count on the pickups for more time. Without pickups these seconds are added to the start time so the races can be finished. With the pickups on, the Xbox rules apply and nothing is added.", "50", 0, 0, 300, 5, 0 },
};
static const int kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);
// the pages in the sidebar; an option's page is its INI section, split up where a section is long (PageOf)
static const char* const kPages[] = { "Presets", "Controller", "Display", "Effects", "World", "Camera",
                                      "Gameplay", "Boost and handbrake", "AI", "Audio", "Online", "DLC", "Deadline mode",
                                      "Debug" };
static const int kPageCount = sizeof(kPages) / sizeof(kPages[0]);
static const int kDlcPage = 11;  // its options, then the build and install buttons (CreateControls)
// [DLC] keys of the DLC game modes, on a page of their own (PageOf); a new mode's keys go here too
static const char* const kDlcModeKeys[] = { "Modes", "Pickups", "PickupVignette", "DeadlineBonus" };
// the line with the packages found below the folder, the buttons and the three-line state below them, in rows of
// kRowH (the presets page stays the tallest, so the window keeps its size)
static const int kDlcExtraRows = 5;

// options the presets do not touch
// Debug, Online and DLC are the user's own; so is Laps, which the game's lap choice keeps up to date
// itself (a preset that names Laps, "Original game", still sets it: see InPreset); and SaveBackups, which is no
// setting of the game (a preset must not switch the backups off)
static bool OutsidePresets(const Option& o)
{
    return !strcmp(o.section, "Debug") || !strcmp(o.section, "Online") || !strcmp(o.section, "DLC") ||
           !strcmp(o.key, "Laps") || !strcmp(o.key, "SaveBackups");
}

// the page an option is shown on: its INI section; [Graphics] is split into the display and the effects,
// [Gameplay] into the game, the boost and handbrake, and the AI boost
static const char* PageOf(const Option& o)
{
    if (!strcmp(o.section, "Graphics")) {
        static const char* const effects[] = { "MotionBlurStrength", "Vignette", "Gradient", "Bloom", "Exposure",
                                               "LensFlare", "ParticleFlare" };
        for (const char* e : effects)
            if (!strncmp(o.key, e, strlen(e))) return "Effects";
        return "Display";
    }
    if (!strcmp(o.section, "Gameplay")) {
        if (!strncmp(o.key, "AIBoost", 7)) return "AI";
        if (!strncmp(o.key, "Boost", 5) || !strncmp(o.key, "Handbrake", 9)) return "Boost and handbrake";
    }
    if (!strcmp(o.section, "DLC"))
        for (const char* k : kDlcModeKeys)
            if (!strcmp(o.key, k)) return "Deadline mode";
    return o.section;
}

// ---------------------------------------------------------------- INI file, edited in place

static char s_path[MAX_PATH];
static std::vector<std::string> s_lines;
static bool s_crlf = true;

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static bool LoadIni(const char* path = s_path)
{
    s_lines.clear();
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) return false;
    if (all.compare(0, 3, "\xEF\xBB\xBF") == 0) all.erase(0, 3);  // UTF-8 byte order mark
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
    // written to a temporary file first and then put in place, so the game, which reads some values
    // while it runs, never sees a half written INI
    std::string tmp = std::string(path) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = true;
    for (const std::string& l : lines) {
        ok = fputs(l.c_str(), f) >= 0 && ok;
        ok = fputs(s_crlf ? "\r\n" : "\n", f) >= 0 && ok;
    }
    ok = fclose(f) == 0 && ok;
    if (ok && MoveFileExA(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileA(tmp.c_str());
    return false;
}

// The game and this tool both write the INI (the game only [Gameplay] Laps). A named mutex, held by the tool from
// reading the file to replacing it and by the game around its one-key write, keeps the two from overlapping:
// without it nothing is written.
struct IniLock {
    HANDLE h;
    DWORD wait;
    IniLock() : h(CreateMutexA(NULL, FALSE, "Local\\GlimpseFixIni")),
                wait(h ? WaitForSingleObject(h, 1000) : WAIT_FAILED) {}
    ~IniLock()
    {
        if (ok()) ReleaseMutex(h);
        if (h) CloseHandle(h);
    }
    bool ok() const { return wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED; }
    const char* error() const
    {
        return wait == WAIT_TIMEOUT ? "The INI is busy. Try again." : "Could not acquire the INI lock. Try again.";
    }
};

// Every change here starts from the file as it is now, so a lap count the game wrote meanwhile survives
// saving anything else here. False (nothing changed) when the file exists but cannot be read whole; a
// missing file is fine, the change creates it.
static bool Refresh(bool keepMissing = true)
{
    std::vector<std::string> before = s_lines;
    if (LoadIni()) return true;
    bool missing = GetFileAttributesA(s_path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
    if (!missing || keepMissing) s_lines.swap(before);
    return missing;
}

// The write itself; only under IniLock, after Refresh (Changed, ApplyPreset, PutBackUndo, SaveValues)
static bool WriteIni(const std::vector<std::string>& before)
{
    if (!s_backedUp && !s_opened.empty()) s_backedUp = SaveLines(BackupPath().c_str(), s_opened);
    if (SaveLines(s_path, s_lines)) return true;
    s_lines = before;
    return false;
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
            if (end < 0 && !_stricmp(current.c_str(), section)) end = i + 1;
            continue;
        }
        if (_stricmp(current.c_str(), section)) continue;
        if (t.empty() || t[0] == ';') continue;
        end = i + 1;
        size_t eq = t.find('=');
        // the first one, as Windows reads it
        if (found < 0 && eq != std::string::npos && !_stricmp(Trim(t.substr(0, eq)).c_str(), key)) found = i;
    }
    if (sectionEnd) *sectionEnd = end;
    return found;
}

static std::string ValueAt(int line)
{
    const std::string& l = s_lines[line];
    std::string v = Trim(l.substr(l.find('=') + 1));
    // surrounding quotes are dropped, as GetPrivateProfileString does
    if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
    return v;
}

// as the DLL reads it: the key's own section first, then where older INIs kept it
static std::string GetValue(const Option& o)
{
    int i = FindKey(o.section, o.key, NULL);
    if (i < 0) i = FindKey(!strcmp(o.section, "Debug") ? "Controller" : "Game", o.key, NULL);
    if (i < 0) return o.def;
    std::string v = ValueAt(i);
    // an empty number means the default to the DLL as well
    return v.empty() && o.kind == RANGE ? o.def : v;
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
                      kWarn = RGB(255, 85, 75);
static HBRUSH s_backBrush, s_sideBrush, s_fieldBrush;
static HFONT s_font, s_fontBold, s_fontTitle, s_fontBrand, s_fontSmall;

enum Role { R_NONE, R_LABEL, R_VALUE, R_DIM, R_TITLE, R_BRAND, R_SIDEDIM, R_NAV, R_PRESET, R_ACTION, R_TOGGLE, R_SELECT,
            R_LAUNCH, R_PRIMARY };

static bool s_infoWarn;  // the help text shown is a warning
static const int kPresetCount = 5;
static HWND s_wnd, s_status, s_info, s_title, s_subtitle, s_nav[kPageCount], s_preset[kPresetCount],
    s_presetText[kPresetCount], s_undo, s_undoText, s_launch;
// the DLC page: the folder's "..." button and the packages found in it, Set up DLC, the single steps Build / Install /
// Uninstall, Open log, and the state lines below them
static HWND s_dlcBrowse, s_dlcFound, s_dlcSetup, s_dlcBuild, s_dlcInstall, s_dlcUninstall, s_dlcLog, s_dlcState;
static std::string s_dlcText;  // what the state line shows
static bool s_dlcWarn;         // ... in red: the last build or install failed
static int s_dpi = 96;
static int s_page;
static bool s_loading;
static bool s_toggle[kOptionCount];  // BOOL_OPT state by option index
static int s_choice[kOptionCount];   // CHOICE: selected item by option index, -1 = a value set by hand
static std::string s_custom[kOptionCount];  // that value
static bool s_dim[kOptionCount];  // the option is greyed out (its label and value are drawn in kOff)
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

// layout, in 96 dpi pixels
static const int kWidth = 880, kSideW = 210, kContentX = 240, kLabelW = 300, kRowH = 33, kRowTop = 100, kPathLabelW = 150;
// the sidebar: the pages from 90 on, kNavStep apart; the presets page: a row for each preset (its button
// and what it sets), then the way back to the settings from before a preset
static const int kNavTop = 90, kNavStep = 36, kNavH = 34, kPresetRowH = 96, kPresetRowAvg = 76;

// the client height: the longest page above the help area, and the sidebar's pages
static int kHeight = 700;
static void FitHeight()
{
    int rows[kPageCount] = {}, most = 0;
    for (const Option& o : kOptions) {
        int page = 0;
        while (page < kPageCount - 1 && strcmp(kPages[page], PageOf(o))) page++;
        if (++rows[page] > most) most = rows[page];
    }
    if (rows[kDlcPage] + kDlcExtraRows > most) most = rows[kDlcPage] + kDlcExtraRows;
    int content = kRowTop + most * kRowH + 8 + 130;  // the rows, a gap, help text and buttons
    int presets = kRowTop + kPresetCount * kPresetRowAvg + 48 + 130;  // rows as tall as their text on average, plus the way back
    if (presets > content) content = presets;
    int side = kNavTop + kPageCount * kNavStep + 16;  // the pages end there
    kHeight = 600;
    if (content > kHeight) kHeight = content;
    if (side > kHeight) kHeight = side;
}

// the outer window size for the client area at the current DPI
static SIZE WindowSize(DWORD style)
{
    RECT r = { 0, 0, S(kWidth), S(kHeight) };
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
    if (!strcmp(o.key, "CameraShake")) text = std::to_string((int)(v * 100 + 0.5)) + " %";
    else if (!strcmp(o.key, "BoostMaxSpeed")) text += " m/s  " + std::to_string((int)(v * 3.6 + 0.5)) + " km/h";
    else if (!strcmp(o.key, "BoostAcceleration") || !strcmp(o.key, "HandbrakeStrength")) text += " m/s per s";
    else if (!strcmp(o.key, "HandbrakeDrift")) text += " rad/s  " + std::to_string((int)(v * 57.29578 + 0.5)) + " deg/s";
    else if (!strcmp(o.key, "HDRPeakBrightness") || !strcmp(o.key, "HDRPaperWhite")) text += " nits";
    else if (!strcmp(o.key, "DeadlineBonus")) text += " s";
    else if (strstr(o.key, "Strength") || strstr(o.key, "Blur") || strstr(o.key, "Bloom") || strstr(o.key, "Scale") ||
             !strcmp(o.key, "Exposure") || !strcmp(o.key, "FOV"))
        text += "x";
    SetWindowTextA(o.value, text.c_str());
}

// TEXT: what may be typed (the INI and Steam's lobby filter take it as it is), and how much
// PATH: what a folder name may hold (no control characters, none of " < > | * ?), up to MAX_PATH
static bool TextCharOk(const Option& o, char c)
{
    if (o.kind == PATH) return (unsigned char)c >= 0x20 && !strchr("\"<>|*?", c);
    return isalnum((unsigned char)c) || (c && strchr(o.choices ? o.choices : "-_", c));
}
static int TextLimit(const Option& o) { return o.kind == PATH ? MAX_PATH - 1 : o.hi > 0 ? (int)o.hi : 32; }
static bool IsText(const Option& o) { return o.kind == TEXT || o.kind == PATH; }

static bool IsOn(const char* key)
{
    Option* o = Find(key);
    return o && s_toggle[IndexOf(*o)];
}

static bool ChoiceIs(const char* key, const char* val)
{
    Option* o = Find(key);
    if (!o) return false;
    int sel = s_choice[IndexOf(*o)];
    auto items = Choices(*o);
    return sel >= 0 && sel < (int)items.size() && items[sel].first == val;
}

static void UpdateEnables()
{
    bool bloom = IsOn("Bloom"), boost = IsOn("Boost"), ai = IsOn("AIBoost"), chaos = IsOn("AIBoostChaos");
    bool zoom = IsOn("ZoomKeys"), photo = IsOn("PhotoMode");
    struct { const char* key; bool on; } rules[] = {
        { "UltrawideBlackBackdrop", ChoiceIs("UltrawideFix", "2") },
        { "BloomStrength", bloom }, { "BloomTone", bloom }, { "BloomCurve", bloom },
        { "BoostMotionBlur", boost }, { "BoostKey", boost }, { "BoostButton", boost },
        { "HandbrakeButton", IsOn("Handbrake") }, { "HandbrakeKey", IsOn("Handbrake") }, { "HandbrakeMode", IsOn("Handbrake") }, { "HandbrakeStrength", IsOn("Handbrake") }, { "HandbrakeDrift", IsOn("Handbrake") }, { "PowerBarFillKey", IsOn("PowerBarFill") },
        { "ZoomInKey", zoom }, { "ZoomOutKey", zoom }, { "ZoomResetKey", zoom }, { "PhotoModeKey", photo },
        { "AIBoostLevel", ai && !chaos }, { "AIBoostUnlimited", ai && !chaos },
        { "AIBoostStraightOnly", ai && !chaos }, { "AIBoostChaos", ai },
        { "FriendsOnly", IsOn("SteamLAN") }, { "LobbyName", IsOn("SteamLAN") && !IsOn("FriendsOnly") },
        { "DumpWatchPS", IsOn("Log") },
        { "Cars", IsOn("Overlays") }, { "CarsCareer", IsOn("Overlays") && IsOn("Cars") }, { "Modes", IsOn("Overlays") }, { "Pickups", IsOn("Overlays") && IsOn("Modes") },
        { "PersonalBests", IsOn("Overlays") },
        { "PickupVignette", IsOn("Overlays") && IsOn("Modes") && IsOn("Pickups") },
        { "DeadlineBonus", IsOn("Overlays") && IsOn("Modes") && !IsOn("Pickups") },
        { "Tracks", IsOn("Overlays") }, { "TracksOnline", IsOn("Overlays") && IsOn("Tracks") },
        { "Music", IsOn("Overlays") && IsOn("Tracks") },
        { "OpponentEngineVolume", IsOn("OpponentEngines") },
        { "Laps", IsOn("LapSelect") },
    };
    for (auto& r : rules) {
        Option* o = Find(r.key);
        // only the control is disabled: Windows draws disabled static text embossed and ignores the
        // text colour, which looks smeared on the dark background; labels and values are dimmed instead
        EnableWindow(o->ctrl, r.on);
        s_dim[IndexOf(*o)] = !r.on;
        InvalidateRect(o->label, NULL, TRUE);
        if (o->value) InvalidateRect(o->value, NULL, TRUE);
    }
}

static void Status(const char* text) { SetWindowTextA(s_status, text); }

// both strings are whole numbers (not just a leading part of them, as atof reads "0B0041BB") and equal
static bool SameNumber(const std::string& a, const std::string& b)
{
    char *ea, *eb;
    double x = strtod(a.c_str(), &ea), y = strtod(b.c_str(), &eb);
    return ea != a.c_str() && eb != b.c_str() && !*ea && !*eb && fabs(x - y) < 1e-6;
}

// one control from the INI lines; LoadControls does them all
static void LoadControl(Option& o)
{
    bool was = s_loading;
    s_loading = true;
    {
        std::string v = GetValue(o);
        if (o.kind == BOOL_OPT) {
            s_toggle[IndexOf(o)] = atoi(v.c_str()) != 0;
            InvalidateRect(o.ctrl, NULL, TRUE);
        } else if (IsText(o)) {
            std::string t;
            for (char c : v)
                if (TextCharOk(o, c) && (int)t.size() < TextLimit(o)) t += c;
            SetWindowTextA(o.ctrl, t.c_str());
        } else if (o.kind == CHOICE) {
            auto items = Choices(o);
            if (!strcmp(o.key, "HudResolution")) v = std::to_string(ParseHudResolution(v.c_str()));
            int sel = -1;
            for (int i = 0; i < (int)items.size() && sel < 0; i++)
                if (!_stricmp(items[i].first.c_str(), v.c_str()) || SameNumber(v, items[i].first)) sel = i;
            s_choice[IndexOf(o)] = sel;
            s_custom[IndexOf(o)] = v + " (custom)";
            InvalidateRect(o.ctrl, NULL, TRUE);
        } else {
            double real = !strcmp(o.key, "OpponentEngineVolume")
                              ? (double)ParsePercentValue(v.c_str(), atoi(o.def), (int)o.lo, (int)o.hi)
                              : atof(v.c_str());
            double d = real;
            if (d < o.lo) d = o.lo;
            if (d > o.hi) d = o.hi;
            SendMessageA(o.ctrl, TBM_SETPOS, TRUE, (LPARAM)(int)((d - o.lo) / o.step + 0.5));
            InvalidateRect(o.ctrl, NULL, TRUE);
            ShowValue(o, real);  // a value set by hand beyond the slider's range shows as it is
        }
    }
    s_loading = was;
}

static void LoadControls()
{
    s_loading = true;
    for (Option& o : kOptions) LoadControl(o);
    s_loading = false;
    UpdateEnables();
    for (HWND b : s_preset) InvalidateRect(b, NULL, TRUE);  // the preset that matches is outlined
}

static void UpdateUndo();

static void Changed(Option& o)
{
    if (s_loading) return;
    std::string v;
    if (o.kind == BOOL_OPT) {
        v = s_toggle[IndexOf(o)] ? "1" : "0";
    } else if (IsText(o)) {
        char buf[512];  // DumpWatchPS: up to 16 hashes, 271 characters; a PATH up to MAX_PATH
        GetWindowTextA(o.ctrl, buf, sizeof(buf));
        v = buf;
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
    char msg[200 + MAX_PATH];
    IniLock lock;
    if (!lock.ok()) {
        Status(lock.error());
        LoadControl(o);
        return;
    }
    if (!Refresh()) {
        _snprintf_s(msg, _TRUNCATE, "Could not read %s; nothing was saved", s_path);
        Status(msg);
        LoadControl(o);  // back to what the file holds
        return;
    }
    const auto before = s_lines;
    SetValue(o, v);
    bool saved = WriteIni(before);
    if (!saved) LoadControl(o);
    // the game may have written Laps meanwhile: its slider follows (only that one, so a text field being
    // typed in keeps its caret)
    if (strcmp(o.key, "Laps"))
        if (Option* laps = Find("Laps")) LoadControl(*laps);
    UpdateEnables();
    for (HWND b : s_preset) InvalidateRect(b, NULL, TRUE);
    if (saved) _snprintf_s(msg, _TRUNCATE, "Saved  %s = %s", o.key, v.c_str());
    else _snprintf_s(msg, _TRUNCATE, "Could not write %s", s_path);
    Status(msg);
}

// Settings changed by the program itself (DLC on/off, the tools folder): as a change on a page, under the INI
// lock and from the file as it is now, so a lap count the game saved meanwhile stays. NULL when saved, else why not.
static const char* SaveValues(const std::vector<std::pair<const Option*, std::string>>& values)
{
    IniLock lock;
    if (!lock.ok()) return lock.error();
    if (!Refresh()) return "Could not read the INI; nothing was saved";
    const auto before = s_lines;
    for (const auto& v : values) SetValue(*v.first, v.second);
    bool saved = WriteIni(before);
    LoadControls();
    return saved ? NULL : "Could not write the INI";
}

// ---------------------------------------------------------------- DLC: build and install
//
// "Set up DLC" does it all in one go (DlcSetup): it finds the game folder (this INI's, else the one Steam has) and the
// Xbox DLC (FindDlcPlaces), gets the DLC tools if they are missing (after asking: Python with numpy, vgmstream-cli and
// ffmpeg, one download of the same release, DownloadTools), offers the DirectX runtime the DLC tracks need, builds,
// shows the install plan in plain words, installs after one Yes and turns on the [DLC] keys the build needs.
// The single steps: "Build" runs tools\dlc\build_all.py on the folder of [DLC] DumpFolder, "Install" and "Uninstall"
// run tools\dlc\install_dlc.py (first without --apply, to show what it would do, then with it, for the same game
// folder). They run with Python (the DLC tools' own, else through the py launcher) in a
// console without a window (CREATE_NO_WINDOW), inside a job object that ends them together with this program. Their
// output goes to build.log / install.log in %LOCALAPPDATA%\glimpse_fix\dlc_work, their state to a status file next to
// it (lines "state", "step", "text", "detail", see build_all.py), read twice a second. The converted files stay on
// this PC: build_all.py writes them to %LOCALAPPDATA%\glimpse_fix\dlc_staging, install_dlc.py copies them into
// glimpse_dlc in the game folder after a copy of the saves.

// PLAN / UPLAN: the dry runs of the install and the uninstall; DOWNLOAD: the DLC tools (a thread of this program);
// DEPS: setup_deps.bat for the DirectX runtime, in a console of its own
enum DlcTask { DLC_NONE, DLC_BUILD, DLC_PLAN, DLC_INSTALL, DLC_UPLAN, DLC_UNINSTALL, DLC_DOWNLOAD, DLC_DEPS };
static DlcTask s_task;
static HANDLE s_taskProc, s_taskJob;  // the process (DOWNLOAD: the thread)
static DWORD s_taskStart;
static bool s_taskStopped, s_taskRan;
static std::string s_taskStatus, s_dlcLogPath;  // the running task's status file, the last task's log
static std::string s_taskGame;  // the game folder of the dry run, which the --apply run after it uses as well
static const UINT_PTR kDlcTimer = 1, kFoundTimer = 2;
// where the tools are when they are not next to this program (asked once, kept in the INI; not shown as an option)
static Option s_toolsOpt = { "DLC", "ToolsFolder", PATH, "", "", "" };
static const char kDlcManifest[] = "glimpse_dlc_manifest.tsv", kDlcInstalled[] = "glimpse_dlc_installed.tsv";
static const char kNoPython[] =
    "Python 3 was not found. The DLC is converted on this PC by the converters of glimpse_fix, which are Python "
    "scripts.\n\n"
    "Set up DLC offers to download the DLC tools of glimpse_fix for this (Python with numpy, vgmstream-cli and ffmpeg). "
    "Or install Python 3.8 or newer from python.org yourself (keep \"py launcher\" ticked), then, in a command prompt: "
    "py -3 -m pip install numpy\n\n"
    "The DLC tracks also need vgmstream-cli and ffmpeg: setup_deps.bat -Install in the folder glimpse_tools\\dlc gets "
    "them (see the README, \"Your own Xbox DLC\").";

// "Set up DLC": the running task is one of its steps (DlcFinished goes on with the next one)
static bool s_flow;
static std::string s_flowDump, s_flowTools;  // the DLC folder (the packages' own) and the converters it uses
static std::vector<std::string> s_flowNotes; // what the build left out and why, for the question and the summary
static bool s_skipDirectX;  // "No" to the DirectX runtime: not asked again until the program is restarted
static bool s_viaSteam;     // the INI is the one in the game folder Steam has (this program is not in the game folder)

static bool DlcBusy() { return s_task != DLC_NONE; }

static bool FileExists(const std::string& p)
{
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirExists(const std::string& p)
{
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string EnvVar(const char* name)
{
    char buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableA(name, buf, MAX_PATH);
    return n && n < MAX_PATH ? buf : "";
}

static std::string LocalAppData()
{
    std::string d = EnvVar("LOCALAPPDATA");
    return d.empty() ? EnvVar("USERPROFILE") + "\\AppData\\Local" : d;
}

static std::string DlcWork() { return LocalAppData() + "\\glimpse_fix\\dlc_work"; }
static std::string DlcStaging() { return LocalAppData() + "\\glimpse_fix\\dlc_staging"; }

// the game folder: where the INI is
static std::string GameDir()
{
    std::string d = s_path;
    size_t slash = d.find_last_of("\\/");
    return slash == std::string::npos ? "." : d.substr(0, slash);
}

// ---- the large address aware flag of SplitSecond.exe (what setlaa.exe sets; a fresh install or Steam's "Verify
// integrity of game files" takes it away again)

// the exe's file header: false if it cannot be read or is not a 32-bit Windows executable
static bool ReadExeHeader(HANDLE f, LONG& at, IMAGE_FILE_HEADER& fh)
{
    IMAGE_DOS_HEADER dos;
    DWORD sig, got;
    if (!ReadFile(f, &dos, sizeof(dos), &got, NULL) || got != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !ReadFile(f, &sig, 4, &got, NULL) || got != 4 || sig != IMAGE_NT_SIGNATURE ||
        !ReadFile(f, &fh, sizeof(fh), &got, NULL) || got != sizeof(fh) || fh.Machine != IMAGE_FILE_MACHINE_I386)
        return false;
    at = dos.e_lfanew + 4;
    return true;
}

// 1 = the flag is set, 0 = it is not, -1 = no readable 32-bit SplitSecond.exe there
static int ExeLargeAddressAware(const std::string& exe)
{
    HANDLE f = CreateFileA(exe.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    LONG at;
    IMAGE_FILE_HEADER fh;
    int r = ReadExeHeader(f, at, fh) ? (fh.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0 : -1;
    CloseHandle(f);
    return r;
}

// as setlaa.exe: the untouched file is kept as SplitSecond.exe.orig (an existing .orig stays), then the flag is set in
// the file header. Only that one header field changes; no code. "" when done, else why not.
static std::string SetLargeAddressAware(const std::string& exe)
{
    std::string orig = exe + ".orig";
    if (!CopyFileA(exe.c_str(), orig.c_str(), TRUE) && GetLastError() != ERROR_FILE_EXISTS)
        return "The copy " + orig + " could not be written (error " + std::to_string(GetLastError()) + ").";
    // no sharing: fails while the game runs
    HANDLE f = CreateFileA(exe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return e == ERROR_SHARING_VIOLATION ? "SplitSecond.exe is in use: quit the game first."
             : e == ERROR_ACCESS_DENIED     ? "Windows does not allow changing SplitSecond.exe here (access denied). Drag "
                                              "SplitSecond.exe onto setlaa.exe instead, or run this program as administrator."
                                            : "SplitSecond.exe could not be opened (error " + std::to_string(e) + ").";
    }
    LONG at;
    IMAGE_FILE_HEADER fh;
    std::string err;
    DWORD put = 0;
    if (!ReadExeHeader(f, at, fh)) {
        err = "SplitSecond.exe is not a 32-bit Windows executable.";
    } else if (!(fh.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE)) {
        fh.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
        if (SetFilePointer(f, at, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
            !WriteFile(f, &fh, sizeof(fh), &put, NULL) || put != sizeof(fh))
            err = "SplitSecond.exe could not be written (error " + std::to_string(GetLastError()) + ").";
    }
    CloseHandle(f);
    return err;
}

// asks once per game folder and session (again: also when it asked before) when its SplitSecond.exe lacks the flag,
// and sets it after a yes
static void CheckLargeAddressAware(HWND wnd, bool again = false)
{
    static std::string asked;
    std::string game = GameDir(), exe = game + "\\SplitSecond.exe";
    if (ExeLargeAddressAware(exe) != 0 || (asked == game && !again)) return;
    asked = game;
    std::string q = "SplitSecond.exe in\n" + game + "\nis not large address aware: the game has only 2 GB of memory to work "
                    "with. Then the bigger streaming pool stays off, and at high resolutions, with HDR or with the "
                    "converted DLC the game can run out of memory and crash in a race.\n\n"
                    "A fresh install and Steam's \"Verify integrity of game files\" take the flag away; setlaa.exe sets it.\n\n"
                    "Set it now? Only that flag in the file header changes, no game code; the original stays as "
                    "SplitSecond.exe.orig.";
    if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONWARNING) != IDYES) {
        Status("SplitSecond.exe is not large address aware (setlaa.exe sets the flag)");
        return;
    }
    std::string err = SetLargeAddressAware(exe);
    if (err.empty() && ExeLargeAddressAware(exe) == 1) {
        Status("SplitSecond.exe is large address aware now (the original is SplitSecond.exe.orig)");
        return;
    }
    if (err.empty()) err = "The flag did not stick.";
    MessageBoxA(wnd, ("The flag could not be set. " + err).c_str(), "glimpse_fix", MB_OK | MB_ICONWARNING);
    asked.clear();  // ask again next time
}

static std::string NoTrailingSlash(std::string p)
{
    while (p.size() > 3 && (p.back() == '\\' || p.back() == '/')) p.pop_back();
    return p;
}

// one command line argument, quoted as the C runtime reads it back (backslashes before a quote doubled)
static std::string Quote(const std::string& a)
{
    std::string out = "\"";
    size_t bs = 0;
    for (char c : a) {
        if (c == '\\') {
            bs++;
            continue;
        }
        out.append(c == '"' ? bs * 2 + 1 : bs, '\\');
        bs = 0;
        out += c;
    }
    out.append(bs * 2, '\\');
    return out + "\"";
}

static std::string Utf8ToAnsi(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) return s;
    std::vector<wchar_t> w(n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    int m = WideCharToMultiByte(CP_ACP, 0, w.data(), n, NULL, 0, NULL, NULL);
    std::string out(m > 0 ? m : 0, '\0');
    if (m > 0) WideCharToMultiByte(CP_ACP, 0, w.data(), n, &out[0], m, NULL, NULL);
    return out;
}

// the lines of a small text file, also while another process writes it
static std::vector<std::string> ReadLines(const std::string& fn)
{
    std::vector<std::string> out;
    HANDLE h = CreateFileA(fn.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return out;
    std::string all;
    char buf[4096];
    DWORD n;
    while (all.size() < (1 << 20) && ReadFile(h, buf, sizeof(buf), &n, NULL) && n) all.append(buf, n);
    CloseHandle(h);
    all = Utf8ToAnsi(all);
    size_t p = 0;
    while (p < all.size()) {
        size_t e = all.find('\n', p);
        std::string l = all.substr(p, e == std::string::npos ? std::string::npos : e - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
        if (e == std::string::npos) break;
        p = e + 1;
    }
    return out;
}

// the value of "key<TAB>value" in a status file or manifest ("" if none)
static std::string TsvValue(const std::vector<std::string>& lines, const char* key)
{
    std::string k = std::string(key) + "\t";
    for (const std::string& l : lines)
        if (!l.compare(0, k.size(), k)) return l.substr(k.size());
    return "";
}

struct DlcStatus {
    std::string state, step, text;
    std::vector<std::string> details, others;  // others: archives in glimpse_dlc that are not from this build
};

static DlcStatus ReadDlcStatus(const std::string& fn)
{
    DlcStatus st;
    std::vector<std::string> lines = ReadLines(fn);
    st.state = TsvValue(lines, "state");
    st.step = TsvValue(lines, "step");
    st.text = TsvValue(lines, "text");
    for (const std::string& l : lines) {
        if (!l.compare(0, 7, "detail\t")) st.details.push_back(l.substr(7));
        if (!l.compare(0, 6, "other\t")) st.others.push_back(l.substr(6));
    }
    return st;
}

static void DlcSetText(const std::string& text, bool warn)
{
    s_dlcText = text;
    s_dlcWarn = warn;
    SetWindowTextA(s_dlcState, text.c_str());
    InvalidateRect(s_dlcState, NULL, TRUE);
}

// "Set up DLC" may be stopped while it downloads, builds or checks the install (not while it installs)
static bool FlowStoppable() { return s_flow && (s_task == DLC_DOWNLOAD || s_task == DLC_BUILD || s_task == DLC_PLAN); }

static void DlcButtons()
{
    SetWindowTextA(s_dlcSetup, FlowStoppable() ? "Stop" : "Set up DLC");
    EnableWindow(s_dlcSetup, !DlcBusy() || FlowStoppable());
    bool build = !s_flow && s_task == DLC_BUILD;
    SetWindowTextA(s_dlcBuild, build ? "Stop" : "Build");
    EnableWindow(s_dlcBuild, !DlcBusy() || build);
    EnableWindow(s_dlcInstall, !DlcBusy());
    EnableWindow(s_dlcUninstall, !DlcBusy());
    for (HWND h : { s_dlcSetup, s_dlcBuild, s_dlcInstall, s_dlcUninstall }) InvalidateRect(h, NULL, TRUE);
}

// what is built and installed, while nothing runs and nothing ran yet, and the game folder it goes to
static void DlcRefresh()
{
    if (DlcBusy() || s_taskRan) return;
    std::vector<std::string> staged = ReadLines(DlcStaging() + "\\" + kDlcManifest);
    std::vector<std::string> inst = ReadLines(GameDir() + "\\glimpse_dlc\\" + kDlcInstalled);
    std::string text;
    if (!staged.empty()) text = "Converted DLC ready to install (built " + TsvValue(staged, "built") + ").";
    if (!inst.empty()) {
        std::string when = TsvValue(inst, "installed");
        text += (text.empty() ? "" : " ") + (when == "in progress" ? std::string("The last install was interrupted: run Set up DLC again.")
                                                                    : "Installed " + when + ".");
    }
    if (text.empty()) text = "Set up DLC converts your own Xbox DLC on this PC and adds it to the game in one go. Build, Install "
                             "and Uninstall are its single steps.";
    std::string game = GameDir();
    if (FileExists(game + "\\data\\resident.ark"))
        text += "\nGame folder: " + game + (s_viaSteam ? " (found through Steam)" : "");
    else
        text += "\nThe settings file is not in the game folder; Set up DLC asks Steam where the game is (or Open INI...).";
    DlcSetText(text, false);
}

// <name> in PATH (not the current folder)
static std::string InPath(const char* name)
{
    std::vector<char> path(32768);
    DWORD n = GetEnvironmentVariableA("PATH", path.data(), (DWORD)path.size());
    char hit[MAX_PATH];
    if (n && n < path.size() && SearchPathA(path.data(), name, NULL, MAX_PATH, hit, NULL)) return hit;
    return "";
}

// py.exe in PATH, else where the python.org installer puts it
static std::string FindPy()
{
    std::string hit = InPath("py.exe");
    if (!hit.empty()) return hit;
    std::string more[] = { LocalAppData() + "\\Programs\\Python\\Launcher\\py.exe", EnvVar("SystemRoot") + "\\py.exe" };
    for (const std::string& p : more)
        if (FileExists(p)) return p;
    return "";
}

static std::wstring Widen(const std::string& s)
{
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, NULL, 0);
    std::vector<wchar_t> w(n > 0 ? n : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), n);
    return w.data();
}

// a wide path in the ANSI code page this program works in; false if it has characters that code page lacks
static bool Narrow(const wchar_t* w, std::string& out)
{
    BOOL lossy = FALSE;
    int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, NULL, 0, NULL, &lossy);
    if (n <= 0 || lossy) return false;
    std::vector<char> a(n);
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, a.data(), n, NULL, NULL);
    out = a.data();
    return true;
}

static std::string KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR w = NULL;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &w)) && w) Narrow(w, out);
    CoTaskMemFree(w);
    return out;
}

static std::string Lower(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static bool SamePath(const std::string& a, const std::string& b) { return !_stricmp(NoTrailingSlash(a).c_str(), NoTrailingSlash(b).c_str()); }

static std::string Megabytes(long long bytes)
{
    char t[32];
    if (bytes >= (1LL << 30)) sprintf(t, "%.1f GB", bytes / 1073741824.0);
    else sprintf(t, "%.1f MB", bytes / 1048576.0);
    return t;
}

// "a", "a and b", "a, b and c"
static std::string JoinAnd(const std::vector<std::string>& v)
{
    std::string out;
    for (size_t i = 0; i < v.size(); i++) out += (i == 0 ? "" : i + 1 == v.size() ? " and " : ", ") + v[i];
    return out;
}

// "a, b" from a manifest line; "-" or "" = none
static std::vector<std::string> SplitList(const std::string& s)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p < s.size()) {
        size_t c = s.find(',', p);
        std::string item = Trim(s.substr(p, c == std::string::npos ? std::string::npos : c - p));
        if (!item.empty() && item != "-") out.push_back(item);
        if (c == std::string::npos) break;
        p = c + 1;
    }
    return out;
}

// this program's version (VERSIONINFO, glimpse_config.rc), e.g. "2.3.0-beta.1"; the DLC tools of the same release fit it
static std::string ProgramVersion()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    DWORD dummy = 0, size = GetFileVersionInfoSizeA(exe, &dummy);
    if (!size) return "";
    std::vector<char> buf(size);
    char* v = NULL;
    UINT len = 0;
    if (!GetFileVersionInfoA(exe, 0, size, buf.data()) ||
        !VerQueryValueA(buf.data(), "\\StringFileInfo\\040904b0\\ProductVersion", (void**)&v, &len) || !v || !len)
        return "";
    return Trim(std::string(v, strnlen(v, len)));
}

static bool IsWow64()
{
    typedef BOOL(WINAPI * IsWow64_t)(HANDLE, PBOOL);
    IsWow64_t is = (IsWow64_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process");
    BOOL w = FALSE;
    return is && is(GetCurrentProcess(), &w) && w;
}

// a program of Windows itself (tar.exe): the 64 bit one where there is one (this program is 32 bit)
static std::string SystemExe(const char* name)
{
    std::string root = EnvVar("SystemRoot");
    if (IsWow64() && FileExists(root + "\\Sysnative\\" + name)) return root + "\\Sysnative\\" + name;
    return FileExists(root + "\\System32\\" + name) ? root + "\\System32\\" + name : "";
}

// ---------------------------------------------------------------- DLC: the DLC tools (one download per release)
//
// The contract with the release (the same as tools\dlc\setup_deps.ps1 and build_all.py keep): everything here.
// glimpse_fix-dlc-tools-<version>.zip, an asset of the GitHub release v<version>, holds python\ (an embeddable Python
// with numpy), vgmstream\, ffmpeg\, the version file glimpse_dlc_tools.txt (UTF-8 "key<TAB>value": bundle = the
// glimpse_fix version, format = 1, ...), glimpse_dlc_tools_manifest.tsv and THIRD_PARTY_dlc_tools.txt, without a top
// folder; it is unpacked into %LOCALAPPDATA%\glimpse_fix\tools, where build_all.py finds it (and runs in its Python).
// Next to the zip: <zip>.sha256, "<64 hex digits>  <file name>". The bundle counts when glimpse_dlc_tools.txt has
// format 1 and python\python.exe is there. setup_deps.ps1 does the same download from PowerShell; this program does it
// itself so that it can show the size before asking, the progress while it runs, and get the bundle without the
// DirectX runtime (setup_deps -Install installs that as well, which the user is asked about separately here).
static const char kToolsUrlFmt[] = "https://github.com/frankyfife/glimpse_fix/releases/download/v%s/glimpse_fix-dlc-tools-%s.zip";
static const char kToolsHashExt[] = ".sha256";
static const char kToolsPython[] = "python\\python.exe";          // below ToolsDir()
static const char kToolsVersionFile[] = "glimpse_dlc_tools.txt";  // below ToolsDir()
static const char* const kToolsItems[] = { "python", "vgmstream", "ffmpeg", "glimpse_dlc_tools.txt", "glimpse_dlc_tools_manifest.tsv",
                                           "THIRD_PARTY_dlc_tools.txt" };  // all the zip may hold (setup_deps.ps1 BundleItems)
static const int kToolsFormat = 1;
static const long long kToolsMaxBytes = 1LL << 31;

static std::string ToolsBase() { return LocalAppData() + "\\glimpse_fix"; }
static std::string ToolsDir() { return ToolsBase() + "\\tools"; }

static std::string ToolsUrl()
{
    std::string v = ProgramVersion();
    if (v.empty() || v.size() > 40) return "";
    char url[256];
    sprintf(url, kToolsUrlFmt, v.c_str(), v.c_str());
    return url;
}

// the DLC tools in dir are a bundle this program knows: glimpse_dlc_tools.txt of format 1 and python\python.exe
static bool BundleIn(const std::string& dir, std::string* version = NULL)
{
    std::vector<std::string> l = ReadLines(dir + "\\" + kToolsVersionFile);
    if (version) *version = Trim(TsvValue(l, "bundle"));
    return atoi(TsvValue(l, "format").c_str()) == kToolsFormat && FileExists(dir + "\\" + kToolsPython);
}

struct Python {
    std::string exe;
    bool bundled;  // the DLC tools' own python.exe (no py launcher: no -3)
};

// the DLC tools' Python first (it has numpy; build_all.py would switch to it anyway), else the py launcher
static Python FindPython()
{
    Python p = { ToolsDir() + "\\" + kToolsPython, true };
    if (BundleIn(ToolsDir())) return p;
    p.exe = FindPy();
    p.bundled = false;
    return p;
}

// D3DCompiler_41.dll of the DirectX End-User Runtime (June 2010), which the DLC tracks' shaders need. build_all.py
// (d3dcompiler_ok) loads it from System32 with the bitness of its Python: the DLC tools' Python is 64 bit, so on 64 bit
// Windows that is the 64 bit DLL (Sysnative from this 32 bit program, whose System32 is SysWOW64); with the py launcher,
// whose Python may be either, both. The runtime installs both.
static bool D3DCompilerThere()
{
    std::string root = EnvVar("SystemRoot");
    const bool own = FileExists(root + "\\System32\\D3DCompiler_41.dll");  // this program's bitness
    if (!IsWow64()) return own;
    const bool dll64 = FileExists(root + "\\Sysnative\\D3DCompiler_41.dll");
    return dll64 && (own || FindPython().bundled);
}

// vgmstream-cli / ffmpeg where build_all.py's find_exe looks: PATH, then the DLC tools folder
static bool HaveTool(const char* name)
{
    std::string exe = std::string(name) + ".exe";
    if (!InPath(exe.c_str()).empty()) return true;
    for (const char* sub : { "", "vgmstream\\", "ffmpeg\\", "bin\\" })
        if (FileExists(ToolsDir() + "\\" + sub + exe)) return true;
    return false;
}

// ---- HTTPS (WinHTTP), only to the release's own address

struct HttpResult {
    DWORD status;
    long long total;  // the size of the file (also from a probe)
    std::string error;
};

static std::string WinHttpError(DWORD e)
{
    switch (e) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "the server's name could not be resolved (no internet connection?)";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "could not connect to the server";
    case ERROR_WINHTTP_TIMEOUT: return "the server did not answer in time";
    case ERROR_WINHTTP_SECURE_FAILURE: return "the secure connection failed (certificate or TLS)";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "the connection was broken off";
    }
    char t[48];
    sprintf(t, "network error %lu", e);
    return t;
}

// the hosts the DLC tools may come from: github.com (the release) and the GitHub hosts its downloads are redirected to
static bool GitHubHost(const wchar_t* h)
{
    static const wchar_t sfx[] = L".githubusercontent.com";
    size_t n = wcslen(h), m = wcslen(sfx);
    return !_wcsicmp(h, L"github.com") || (n > m && !_wcsicmp(h + n - m, sfx));
}

// the address a request ended up at (after redirects) is https on a GitHub host
static bool FinalUrlOk(HINTERNET q)
{
    DWORD bytes = 0;
    WinHttpQueryOption(q, WINHTTP_OPTION_URL, NULL, &bytes);
    if (!bytes || bytes > 65536) return false;
    std::vector<wchar_t> u(bytes / sizeof(wchar_t) + 1, L'\0');
    if (!WinHttpQueryOption(q, WINHTTP_OPTION_URL, u.data(), &bytes)) return false;
    wchar_t host[256];
    URL_COMPONENTS uc = { sizeof(uc) };
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    return WinHttpCrackUrl(u.data(), 0, 0, &uc) && uc.nScheme == INTERNET_SCHEME_HTTPS && GitHubHost(host);
}

// GET url (https on github.com only, redirects followed to GitHub's download hosts only); the body goes to sink
// (false = stop), at most maxBytes of it. probe: only the size (a request for its first byte), nothing is kept.
static bool HttpFetch(const std::string& url, bool probe, long long maxBytes,
                      const std::function<bool(const char*, DWORD)>& sink, HttpResult& r)
{
    r.status = 0;
    r.total = -1;
    r.error.clear();
    std::wstring w = Widen(url);
    wchar_t host[256], path[2048];
    URL_COMPONENTS uc = { sizeof(uc) };
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS || _wcsicmp(host, L"github.com")) {
        r.error = "not an https address on github.com: " + url;
        return false;
    }
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
    HINTERNET s = WinHttpOpen(L"glimpse_fix-config", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"glimpse_fix-config", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        r.error = WinHttpError(GetLastError());
        return false;
    }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;  // an older Windows without TLS 1.3
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    // the probe runs on the window's thread: short timeouts, so a bad connection does not hang the window for long
    if (probe) WinHttpSetTimeouts(s, 5000, 8000, 8000, 10000);
    else WinHttpSetTimeouts(s, 10000, 15000, 30000, 60000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET q = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    bool ok = false;
    if (q && WinHttpSendRequest(q, probe ? L"Range: bytes=0-0" : WINHTTP_NO_ADDITIONAL_HEADERS, probe ? (DWORD)-1L : 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(q, NULL)) {
        DWORD len = sizeof(r.status);
        WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &r.status, &len,
                            WINHTTP_NO_HEADER_INDEX);
        wchar_t hv[128];
        len = sizeof(hv);
        if (r.status == 206 && WinHttpQueryHeaders(q, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX, hv, &len, WINHTTP_NO_HEADER_INDEX)) {
            const wchar_t* slash = wcschr(hv, L'/');  // "bytes 0-0/12345"
            if (slash) r.total = _wtoi64(slash + 1);
        }
        len = sizeof(hv);
        if (r.total < 0 && r.status == 200 && WinHttpQueryHeaders(q, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, hv, &len, WINHTTP_NO_HEADER_INDEX))
            r.total = _wtoi64(hv);
        if (!FinalUrlOk(q)) {
            r.error = "the download was redirected away from GitHub";
        } else if (r.status == 404) {
            r.error = "it is not on GitHub (HTTP 404)";
        } else if (r.status != 200 && !(probe && r.status == 206)) {
            char t[48];
            sprintf(t, "the server answered HTTP %lu", r.status);
            r.error = t;
        } else if (probe) {
            ok = r.total > 0;
            if (!ok) r.error = "the server did not say the size";
        } else if (r.total > maxBytes) {
            r.error = "the file is far bigger than expected";
        } else {
            std::vector<char> buf(1 << 16);
            long long got = 0;
            ok = true;
            for (;;) {
                DWORD n = 0;
                if (!WinHttpReadData(q, buf.data(), (DWORD)buf.size(), &n)) {
                    r.error = WinHttpError(GetLastError());
                    ok = false;
                    break;
                }
                if (!n) break;
                got += n;
                if (got > maxBytes) {
                    r.error = "the file is far bigger than expected";
                    ok = false;
                    break;
                }
                if (!sink(buf.data(), n)) {
                    if (r.error.empty()) r.error = "stopped";
                    ok = false;
                    break;
                }
            }
            if (ok && r.total >= 0 && got != r.total) {
                r.error = "the download was cut short";
                ok = false;
            }
        }
    } else {
        r.error = WinHttpError(GetLastError());
    }
    if (q) WinHttpCloseHandle(q);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

// the first 64 hex digits standing alone in a .sha256 file ("" if none), lower case
static std::string ParseSha256(const std::string& t)
{
    for (size_t i = 0; i + 64 <= t.size(); i++) {
        if (i && isxdigit((unsigned char)t[i - 1])) continue;
        size_t n = 0;
        while (i + n < t.size() && isxdigit((unsigned char)t[i + n])) n++;
        if (n == 64) return Lower(t.substr(i, 64));
        i += n;
    }
    return "";
}

struct Sha256 {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    Sha256()
    {
        if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)))
            BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0);
    }
    ~Sha256()
    {
        if (h) BCryptDestroyHash(h);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    void Add(const char* d, DWORD n) { if (h) BCryptHashData(h, (PUCHAR)d, n, 0); }
    std::string Hex()
    {
        unsigned char out[32];
        if (!h || !BCRYPT_SUCCESS(BCryptFinishHash(h, out, sizeof(out), 0))) return "";
        char hex[65];
        for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", out[i]);
        return hex;
    }
};

// the download, on a thread of its own (DownloadThread); the window reads its state twice a second (DlcPoll)
static struct {
    CRITICAL_SECTION cs;
    std::string url, step, error;  // url: set before the thread starts
    long long done, total;
    bool ok;
    volatile LONG cancel;
    FILE* log;  // download.log in the work folder, the thread's own
} s_dl;

static void DlLog(const std::string& line)
{
    if (s_dl.log) {
        fprintf(s_dl.log, "%s\n", line.c_str());
        fflush(s_dl.log);
    }
}

static void DlStep(const char* step)
{
    EnterCriticalSection(&s_dl.cs);
    s_dl.step = step;
    LeaveCriticalSection(&s_dl.cs);
    DlLog(std::string("== ") + step);
}

// removes a folder and everything in it, but only the two this program unpacks into (never anything else)
static void RemoveTreeIn(const std::string& dir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            std::string full = dir + "\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) RemoveDirectoryA(full.c_str());  // the link, not its target
                else RemoveTreeIn(full);
            } else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesA(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileA(full.c_str());
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

static std::string ToolsNew() { return ToolsBase() + "\\tools_new"; }
static std::string ToolsOld() { return ToolsBase() + "\\tools_old"; }

static bool RemoveOwnTree(const std::string& dir)
{
    if (!SamePath(dir, ToolsNew()) && !SamePath(dir, ToolsOld())) return false;
    if (DirExists(dir)) RemoveTreeIn(dir);
    return !DirExists(dir);
}

static long long TreeSize(const std::string& dir, int* files)
{
    long long total = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) total += TreeSize(dir + "\\" + fd.cFileName, files);
        } else {
            total += ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            (*files)++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return total;
}

// runs a program without a window and waits for it; its exit code, or -1 if it did not start
static long RunHidden(const std::string& cmd)
{
    std::vector<char> line(cmd.begin(), cmd.end());
    line.push_back(0);
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(NULL, line.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (long)code;
}

// the zip through the shell's own zip folders (Windows before tar.exe came with it); CopyHere may return before the
// copy is done, so it waits until the files stop growing
static bool ShellUnzip(const std::string& zip, const std::string& dest)
{
    IShellDispatch* sh = NULL;
    if (FAILED(CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void**)&sh)) || !sh) return false;
    bool ok = false;
    VARIANT vz, vd;
    VariantInit(&vz);
    VariantInit(&vd);
    vz.vt = vd.vt = VT_BSTR;
    vz.bstrVal = SysAllocString(Widen(zip).c_str());
    vd.bstrVal = SysAllocString(Widen(dest).c_str());
    Folder *from = NULL, *to = NULL;
    if (SUCCEEDED(sh->NameSpace(vz, &from)) && from && SUCCEEDED(sh->NameSpace(vd, &to)) && to) {
        FolderItems* items = NULL;
        if (SUCCEEDED(from->Items(&items)) && items) {
            VARIANT vi, vo;
            VariantInit(&vi);
            VariantInit(&vo);
            vi.vt = VT_DISPATCH;
            vi.pdispVal = items;
            vo.vt = VT_I4;
            vo.lVal = 4 | 16 | 512 | 1024;  // no progress window, yes to all, no questions, no error windows
            ok = SUCCEEDED(to->CopyHere(vi, vo));
            items->Release();
        }
    }
    if (from) from->Release();
    if (to) to->Release();
    VariantClear(&vz);
    VariantClear(&vd);
    sh->Release();
    if (!ok) return false;
    long long last = -1;
    int still = 0;
    for (int i = 0; i < 1200 && still < 6; i++) {  // at most 10 minutes; done after 3 s without change
        Sleep(500);
        int files = 0;
        long long now = TreeSize(dest, &files);
        still = now == last && files ? still + 1 : 0;
        last = now;
    }
    return true;
}

static bool Unzip(const std::string& zip, const std::string& dest, std::string& err)
{
    std::string tar = SystemExe("tar.exe");  // Windows 10 1803 and later
    if (!tar.empty()) {
        long code = RunHidden(Quote(tar) + " -xf " + Quote(zip) + " -C " + Quote(dest));
        DlLog("tar.exe: exit code " + std::to_string(code));
        if (code == 0) return true;
        if (!RemoveOwnTree(dest) || !CreateDirectoryA(dest.c_str(), NULL)) {  // not on top of what tar left
            err = "The download could not be unpacked.";
            return false;
        }
    }
    DlLog("unpacking with the shell's zip folders");
    if (ShellUnzip(zip, dest)) return true;
    err = "The download could not be unpacked.";
    return false;
}

// every file the unpacked bundle's manifest lists, with its size and SHA-256 (as setup_deps.ps1 Test-Bundle and
// build_all.py check_bundle check it): the unpacking is complete and nothing in it was changed (the shell's zip folders
// give no sign when they are done)
static bool VerifyBundle(const std::string& root, std::string& err)
{
    const std::string man = root + "\\glimpse_dlc_tools_manifest.tsv";
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(man.c_str(), GetFileExInfoStandard, &fa) || fa.nFileSizeHigh || fa.nFileSizeLow >= (1u << 20)) {
        err = "The download has no usable glimpse_dlc_tools_manifest.tsv; not installed.";
        return false;
    }
    int n = 0;
    std::vector<char> buf(1 << 16);
    for (const std::string& l : ReadLines(man)) {
        if (l.compare(0, 5, "file\t")) continue;
        size_t a = l.find('\t', 5), b = a == std::string::npos ? a : l.find('\t', a + 1);
        if (b == std::string::npos) {
            err = "The DLC tools' manifest has a broken line; not installed.";
            return false;
        }
        size_t c = l.find('\t', b + 1);
        std::string rel = l.substr(5, a - 5), want = Lower(Trim(l.substr(b + 1, c == std::string::npos ? c : c - b - 1)));
        long long size = _atoi64(l.substr(a + 1, b - a - 1).c_str());
        if (rel.empty() || rel[0] == '/' || rel.find_first_of(":\\") != std::string::npos ||
            ("/" + rel + "/").find("/../") != std::string::npos) {
            err = "The DLC tools' manifest names a file outside the bundle (" + rel + "); not installed.";
            return false;
        }
        for (char& ch : rel)
            if (ch == '/') ch = '\\';
        HANDLE h = CreateFileA((root + "\\" + rel).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        bool same = false;
        if (h != INVALID_HANDLE_VALUE) {
            Sha256 sha;
            long long got = 0;
            DWORD r = 0;
            while (ReadFile(h, buf.data(), (DWORD)buf.size(), &r, NULL) && r) {
                sha.Add(buf.data(), r);
                got += r;
            }
            CloseHandle(h);
            same = got == size && sha.Hex() == want;
        }
        if (!same) {
            err = "The unpacked DLC tools are incomplete or damaged (" + rel + "); not installed.";
            return false;
        }
        n++;
    }
    if (!n) {
        err = "The DLC tools' manifest lists no files; not installed.";
        return false;
    }
    DlLog("checked " + std::to_string(n) + " files against the manifest");
    return true;
}

// The unpacked bundle in place of the old one: only the bundle's own items (anything else in the zip refuses it), each
// replacing its namesake in ToolsDir() (moved to tools_old first, then removed; nothing else there is touched). The
// version file goes first out and last in, so a half done swap is never taken for a bundle.
static bool PlaceTools(const std::string& root, std::string& err)
{
    std::string ver;
    if (!BundleIn(root, &ver)) {
        err = "The download is not a DLC tools bundle this program knows (no glimpse_dlc_tools.txt of format 1 with "
              "python\\python.exe).";
        return false;
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((root + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        bool bad = false;
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            bool known = false;
            for (const char* item : kToolsItems) known = known || !_stricmp(fd.cFileName, item);
            if (!known) {
                err = std::string("The download holds ") + fd.cFileName + ", which is not part of the DLC tools; not installed.";
                bad = true;
            }
        } while (!bad && FindNextFileA(h, &fd));
        FindClose(h);
        if (bad) return false;
    }
    if (!VerifyBundle(root, err)) return false;
    DlLog("bundle " + ver);
    std::string tools = ToolsDir(), old = ToolsOld();
    CreateDirectoryA(ToolsBase().c_str(), NULL);
    CreateDirectoryA(tools.c_str(), NULL);
    RemoveOwnTree(old);
    CreateDirectoryA(old.c_str(), NULL);
    std::string verFile = tools + "\\" + kToolsVersionFile;
    if (FileExists(verFile) && !MoveFileExA(verFile.c_str(), (old + "\\" + kToolsVersionFile).c_str(), 0)) {
        err = "The old " + verFile + " could not be replaced.";
        return false;
    }
    for (int last = 0; last < 2; last++)
        for (const char* item : kToolsItems) {
            if ((_stricmp(item, kToolsVersionFile) == 0) != (last == 1)) continue;
            std::string src = root + "\\" + item, dst = tools + "\\" + item;
            if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            if (GetFileAttributesA(dst.c_str()) != INVALID_FILE_ATTRIBUTES && !MoveFileExA(dst.c_str(), (old + "\\" + item).c_str(), 0)) {
                err = "The old " + dst + " could not be replaced. Is a program from it still running?";
                return false;
            }
            if (!MoveFileExA(src.c_str(), dst.c_str(), 0)) {
                err = "Could not move the DLC tools into " + dst + ".";
                return false;
            }
            DlLog("placed " + dst);
        }
    RemoveOwnTree(old);
    return true;
}

static bool DownloadTools(std::string& err)
{
    const std::string url = s_dl.url, hashUrl = url + kToolsHashExt;
    std::string name = url.substr(url.find_last_of('/') + 1);
    DlLog("glimpse_fix: the DLC tools");
    DlLog("from " + url);
    DlStep("Getting the checksum");
    std::string hashText;
    HttpResult r;
    if (!HttpFetch(hashUrl, false, 4096, [&](const char* d, DWORD n) { hashText.append(d, n); return true; }, r)) {
        err = "The checksum " + hashUrl + " could not be downloaded: " + r.error + ".";
        return false;
    }
    std::string want = ParseSha256(hashText);
    if (want.empty()) {
        err = "The checksum file " + hashUrl + " holds no SHA-256 checksum.";
        return false;
    }
    DlLog("published SHA-256 " + want);

    DlStep("Downloading");
    std::string dir = ToolsBase() + "\\download", part = dir + "\\" + name + ".part", zip = dir + "\\" + name;
    CreateDirectoryA(ToolsBase().c_str(), NULL);
    CreateDirectoryA(dir.c_str(), NULL);
    FILE* f = fopen(part.c_str(), "wb");
    if (!f) {
        err = "Could not write " + part + ".";
        return false;
    }
    Sha256 sha;
    bool wrote = true;
    bool got = HttpFetch(url, false, kToolsMaxBytes, [&](const char* d, DWORD n) {
        if (s_dl.cancel) return false;
        if (fwrite(d, 1, n, f) != n) {
            wrote = false;
            return false;
        }
        sha.Add(d, n);
        EnterCriticalSection(&s_dl.cs);
        s_dl.done += n;
        s_dl.total = r.total;
        LeaveCriticalSection(&s_dl.cs);
        return true;
    }, r);
    wrote = fclose(f) == 0 && wrote;
    if (!got || !wrote) {
        DeleteFileA(part.c_str());
        err = s_dl.cancel ? "Stopped." : !wrote ? "Could not write " + part + " (is the drive full?)." : "The download failed: " + r.error + ".";
        return false;
    }
    std::string have = sha.Hex();
    DlLog("downloaded " + std::to_string(s_dl.done) + " bytes, SHA-256 " + have);
    if (have != want) {
        DeleteFileA(part.c_str());
        err = "The download does not match its published checksum, so it was not used (a damaged download, or not the "
              "published file). Nothing was installed.";
        return false;
    }
    if (!MoveFileExA(part.c_str(), zip.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = "Could not write " + zip + ".";
        return false;
    }

    if (s_dl.cancel) {  // stopped after the last byte: nothing is unpacked (once unpacking runs, it is let finish)
        DeleteFileA(zip.c_str());
        err = "Stopped.";
        return false;
    }
    DlStep("Unpacking");
    std::string fresh = ToolsNew();
    RemoveOwnTree(fresh);
    CreateDirectoryA(fresh.c_str(), NULL);
    bool ok = Unzip(zip, fresh, err);
    if (ok && s_dl.cancel) {  // stopped while unpacking: nothing is put in place
        ok = false;
        err = "Stopped.";
    }
    if (ok) {
        DlStep("Installing");
        ok = PlaceTools(fresh, err);  // the zip has no top folder
    }
    RemoveOwnTree(fresh);
    DeleteFileA(zip.c_str());
    return ok;
}

static DWORD WINAPI DownloadThread(LPVOID)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);  // the shell's zip folders
    std::string err;
    bool ok = DownloadTools(err);
    DlLog(ok ? "RESULT: OK" : "FAILED: " + err);
    EnterCriticalSection(&s_dl.cs);
    s_dl.ok = ok;
    s_dl.error = err;
    LeaveCriticalSection(&s_dl.cs);
    if (s_dl.log) fclose(s_dl.log);
    s_dl.log = NULL;
    CoUninitialize();
    return 0;
}

// ---------------------------------------------------------------- DLC: finding the Xbox DLC on this PC

struct DlcPackage {
    std::string file, name;  // name: the display name in the package's header
};

static DWORD Be32(const unsigned char* p) { return (DWORD)p[0] << 24 | (DWORD)p[1] << 16 | (DWORD)p[2] << 8 | p[3]; }

// a Split/Second DLC package as build_all.py's find_packages takes it: an STFS file (LIVE, PIRS or CON) of at least
// 0xB000 bytes whose header has the game's title id 425607E7 and content type 2 (DLC); only its header is read
static bool ReadPackage(const std::string& fn, DlcPackage& p)
{
    HANDLE h = CreateFileA(fn.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    unsigned char hd[0x500];
    DWORD n = 0;
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0xB000 && ReadFile(h, hd, sizeof(hd), &n, NULL) && n == sizeof(hd);
    CloseHandle(h);
    if (!ok || (memcmp(hd, "LIVE", 4) && memcmp(hd, "PIRS", 4) && memcmp(hd, "CON ", 4))) return false;
    if (Be32(hd + 0x360) != 0x425607E7 || Be32(hd + 0x344) != 2) return false;  // title id, content type (stfs.py)
    wchar_t w[0x41] = {};
    for (int i = 0; i < 0x40; i++) w[i] = (wchar_t)(hd[0x411 + 2 * i] << 8 | hd[0x412 + 2 * i]);  // UTF-16 big endian
    char a[0x100];
    p.file = fn;
    p.name = WideCharToMultiByte(CP_ACP, 0, w, -1, a, sizeof(a), NULL, NULL) > 0 ? Trim(a) : "";
    return true;
}

// a folder on a network share (\\server\share, or a mapped network drive); GetDriveType does not touch the share
static bool OnNetwork(const std::string& p)
{
    if (p.size() >= 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/')) return true;
    if (p.size() < 2 || p[1] != ':') return false;
    char root[4] = { p[0], ':', '\\', 0 };
    return GetDriveTypeA(root) == DRIVE_REMOTE;
}

// the packages directly in dir; anyName = false: only files without an extension (as the console names them)
static std::vector<DlcPackage> PackagesIn(const std::string& dir, bool anyName)
{
    std::vector<DlcPackage> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileExA((dir + "\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 2 /* large fetch */);
    if (h == INVALID_HANDLE_VALUE) return out;
    int tried = 0;
    do {
        // not a file only in the cloud (OneDrive): reading its header would download it
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_OFFLINE | 0x00040000 | 0x00400000)) continue;
        if ((((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow) < 0xB000) continue;
        if (!anyName && strchr(fd.cFileName, '.')) continue;
        if (++tried > 300) break;  // a folder full of big files is not a DLC folder
        DlcPackage p;
        if (ReadPackage(dir + "\\" + fd.cFileName, p)) out.push_back(p);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

// the subfolders of dir (no links, none of Windows' own)
static std::vector<std::string> SubDirs(const std::string& dir)
{
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileExA((dir + "\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, NULL, 2);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        const char* n = fd.cFileName;
        if (!strcmp(n, ".") || !strcmp(n, "..") || n[0] == '$' || !_stricmp(n, "System Volume Information") ||
            !_stricmp(n, "Windows") || !_stricmp(n, "AppData") || !_stricmp(n, "WinSxS") || !_stricmp(n, "node_modules") ||
            !_stricmp(n, ".git"))
            continue;
        out.push_back(dir + "\\" + n);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

// The folder that holds the packages, for a folder as the user may give it: the packages' own folder, the 00000002
// folder, the title folder 425607E7, or a content folder of Xenia or an Xbox drive (<profile>\425607E7\00000002).
// "" if there are none.
static std::string PackageFolder(const std::string& given, std::vector<DlcPackage>& pk, bool anyName)
{
    std::string dir = NoTrailingSlash(given);
    pk.clear();
    if (dir.empty() || !DirExists(dir)) return "";
    std::string tries[] = { dir, dir + "\\00000002", dir + "\\425607E7\\00000002" };
    for (int i = 0; i < 3; i++) {
        if (i && !DirExists(tries[i])) continue;
        pk = PackagesIn(tries[i], anyName || i > 0);
        if (!pk.empty()) return tries[i];
    }
    for (const std::string& sub : SubDirs(dir)) {
        std::string t = sub + "\\425607E7\\00000002";
        if (!DirExists(t)) continue;
        pk = PackagesIn(t, true);
        if (!pk.empty()) return t;
    }
    return "";
}

// "7 packages: Deadline, High Octane, The Elite (2), ..."
static std::string PackageSummary(const std::vector<DlcPackage>& pk)
{
    static const struct { const char *key, *name; } known[] = {
        { "Deadline", "Deadline" }, { "High Octane", "High Octane" }, { "Elite", "The Elite" }, { "Onslaught", "Onslaught" },
        { "Survival", "Survival At The Rock" }, { "Unlock", "Master Unlock" },
    };
    struct Name {
        std::string text;
        int order, count;  // order: the known packs in the order above, others after them
    };
    std::vector<Name> names;
    for (const DlcPackage& p : pk) {
        Name n = { p.name.empty() ? "unnamed" : p.name, 99, 1 };
        for (int i = 0; i < (int)(sizeof(known) / sizeof(known[0])); i++)
            if (strstr(p.name.c_str(), known[i].key)) {
                n.text = known[i].name;
                n.order = i;
                break;
            }
        bool seen = false;
        for (Name& e : names)
            if (e.text == n.text) {
                e.count++;
                seen = true;
            }
        if (!seen) names.push_back(n);
    }
    std::stable_sort(names.begin(), names.end(), [](const Name& a, const Name& b) { return a.order < b.order; });
    std::string out = std::to_string(pk.size()) + (pk.size() == 1 ? " package: " : " packages: ");
    for (size_t i = 0; i < names.size(); i++)
        out += (i ? ", " : "") + names[i].text + (names[i].count > 1 ? " (" + std::to_string(names[i].count) + ")" : "");
    return out;
}

struct DlcPlace {
    std::string dir;
    std::vector<DlcPackage> packs;
};

struct DlcScan {
    DWORD start;
    std::vector<DlcPlace> found;
    bool Late() const { return GetTickCount() - start > 1500; }  // the scan stays below about 1.5 s, whatever the drives
    void Add(const std::string& dir, bool anyName)
    {
        DlcPlace p;
        p.dir = PackageFolder(dir, p.packs, anyName);
        if (p.dir.empty()) return;
        for (const DlcPlace& f : found)
            if (SamePath(f.dir, p.dir)) return;
        found.push_back(p);
    }
    // a portable Xenia (xenia*.exe) at most two folders below root: its folder "content"
    void Xenia(const std::string& root, int depth)
    {
        if (Late()) return;
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileExA((root + "\\xenia*.exe").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
        if (h != INVALID_HANDLE_VALUE) {
            FindClose(h);
            Add(root + "\\content", true);
        }
        if (depth < 2)
            for (const std::string& sub : SubDirs(root)) Xenia(sub, depth + 1);
    }
};

// Where Split/Second DLC packages are on this PC, cheapest and likeliest places first, in about 1.5 s at most: the
// folder used before, Xenia's content folders (Documents\Xenia, portable installs on the drives and in the user's
// folders), dumps on the Desktop, in Downloads and Documents (two folders deep), and the Xbox layout
// Content\<profile>\425607E7\00000002 on every fixed and removable drive. Whole drives are never searched.
static std::vector<DlcPlace> FindDlcPlaces(const std::string& before)
{
    DlcScan s = { GetTickCount() };
    // never a network folder (a redirected Documents, a mapped drive): a disconnected one stalls every look at it
    auto local = [](const std::string& p) { return OnNetwork(p) ? std::string() : p; };
    if (!before.empty() && !OnNetwork(before)) s.Add(before, true);
    std::string docs = local(KnownFolder(FOLDERID_Documents)), desk = local(KnownFolder(FOLDERID_Desktop)),
                down = local(KnownFolder(FOLDERID_Downloads)), home = local(EnvVar("USERPROFILE"));
    if (!docs.empty()) s.Add(docs + "\\Xenia\\content", true);
    std::vector<std::string> drives;
    DWORD mask = GetLogicalDrives();
    for (int i = 2; i < 26; i++) {  // not A: and B:
        if (!(mask & (1u << i))) continue;
        std::string d = std::string(1, (char)('A' + i)) + ":";
        UINT type = GetDriveTypeA((d + "\\").c_str());
        if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) drives.push_back(d);
    }
    for (const std::string& d : drives) {  // Xbox drive layout (and a copy of it)
        if (s.Late()) break;
        s.Add(d + "\\Content", true);
        s.Add(d + "\\425607E7", true);
    }
    for (const std::string& u : { desk, down, docs }) {
        if (u.empty() || s.Late()) continue;
        s.Add(u, false);
        for (const std::string& sub : SubDirs(u)) {
            if (s.Late()) break;
            s.Add(sub, false);  // also sub\425607E7 and sub\<x>\425607E7\00000002
        }
    }
    for (const std::string& u : { desk, down, docs, home })
        if (!u.empty()) s.Xenia(u, 0);
    for (const std::string& d : drives) s.Xenia(d, 0);
    return s.found;
}

// ---------------------------------------------------------------- DLC: the game folder through Steam

// Valve's text KeyValues (libraryfolders.vdf, appmanifest_*.acf): every value of key (any depth); quoted strings
// with \\ and \" escapes, // comments
static std::vector<std::string> VdfValues(const std::string& t, const char* key, bool numberedPaths = false)
{
    std::vector<std::pair<bool, std::string>> tok;  // (a string, its text); false: a brace
    size_t i = 0;
    while (i < t.size()) {
        char c = t[i];
        if (isspace((unsigned char)c)) {
            i++;
        } else if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') {
            while (i < t.size() && t[i] != '\n') i++;
        } else if (c == '{' || c == '}') {
            tok.push_back({ false, std::string(1, c) });
            i++;
        } else {
            std::string s;
            if (c == '"') {
                for (i++; i < t.size() && t[i] != '"'; i++) {
                    if (t[i] == '\\' && i + 1 < t.size()) {
                        char e = t[++i];
                        s += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                    } else {
                        s += t[i];
                    }
                }
                i++;
            } else {
                while (i < t.size() && !isspace((unsigned char)t[i]) && t[i] != '{' && t[i] != '}' && t[i] != '"') s += t[i++];
            }
            tok.push_back({ true, s });
        }
    }
    std::vector<std::string> out;
    for (size_t k = 0; k + 1 < tok.size(); k++) {
        if (!tok[k].first || !tok[k + 1].first) continue;
        const std::string& name = tok[k].second;
        bool numbered = numberedPaths && !name.empty() && name.find_first_not_of("0123456789") == std::string::npos &&
                        tok[k + 1].second.find(':') != std::string::npos;  // the old libraryfolders.vdf: "1" "D:\\..."
        if (!_stricmp(name.c_str(), key) || numbered) out.push_back(tok[k + 1].second);
        k++;  // the value
    }
    return out;
}

static std::string ReadText(const std::string& fn)
{
    std::string all;
    for (const std::string& l : ReadLines(fn)) all += l + "\n";
    return all;
}

static std::string RegString(HKEY root, const char* key, const char* value)
{
    char buf[MAX_PATH];
    DWORD n = sizeof(buf);
    if (RegGetValueA(root, key, value, RRF_RT_REG_SZ, NULL, buf, &n) != ERROR_SUCCESS) return "";
    std::string s = buf;
    for (char& c : s)
        if (c == '/') c = '\\';
    return NoTrailingSlash(s);
}

// Split/Second's folder as Steam has it (app 297860), read only: Steam's folder from the registry, its libraries
// from steamapps\libraryfolders.vdf, the game's installdir from appmanifest_297860.acf; "" if none has SplitSecond.exe
static std::string SteamGameDir()
{
    std::vector<std::string> steam = { RegString(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath"),
                                        RegString(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath"),
                                        RegString(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath") };
    std::vector<std::string> libs;
    auto addLib = [&](std::string p) {
        for (char& c : p)
            if (c == '/') c = '\\';
        p = NoTrailingSlash(p);
        if (p.empty()) return;
        for (const std::string& l : libs)
            if (SamePath(l, p)) return;
        libs.push_back(p);
    };
    for (const std::string& s : steam) {
        if (s.empty()) continue;
        addLib(s);
        for (const char* vdf : { "\\steamapps\\libraryfolders.vdf", "\\config\\libraryfolders.vdf" })
            for (const std::string& p : VdfValues(ReadText(s + vdf), "path", true)) addLib(p);
    }
    for (const std::string& lib : libs) {
        std::vector<std::string> dir = VdfValues(ReadText(lib + "\\steamapps\\appmanifest_297860.acf"), "installdir");
        if (dir.empty() || dir[0].empty() || dir[0].find("..") != std::string::npos) continue;
        std::string game = lib + "\\steamapps\\common\\" + dir[0];
        if (FileExists(game + "\\SplitSecond.exe")) return game;
    }
    for (const std::string& lib : libs)  // a library without the manifest (copied over by hand)
        if (FileExists(lib + "\\steamapps\\common\\SplitSecond\\SplitSecond.exe")) return lib + "\\steamapps\\common\\SplitSecond";
    return "";
}

// a folder through Windows' own folder picker; the path in the ANSI code page this program works in
static bool PickFolder(HWND owner, const wchar_t* title, std::string& path)
{
    IFileOpenDialog* d = NULL;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
    DWORD opt = 0;
    d->GetOptions(&opt);
    d->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    d->SetTitle(title);
    if (!path.empty()) {
        wchar_t w[MAX_PATH];
        IShellItem* start = NULL;
        if (MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, w, MAX_PATH) &&
            SUCCEEDED(SHCreateItemFromParsingName(w, NULL, IID_PPV_ARGS(&start)))) {
            d->SetFolder(start);
            start->Release();
        }
    }
    bool ok = false;
    IShellItem* item = NULL;
    if (SUCCEEDED(d->Show(owner)) && SUCCEEDED(d->GetResult(&item))) {
        PWSTR w = NULL;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &w))) {
            char a[MAX_PATH];
            BOOL lossy = FALSE;
            if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, a, MAX_PATH, NULL, &lossy) && !lossy) {
                path = a;
                ok = true;
            } else {
                MessageBoxA(owner, "This folder's name has characters this program cannot pass on. Copy or rename the folder "
                                   "(for example to C:\\xbox_dlc) and pick it again.", "glimpse_fix", MB_OK | MB_ICONWARNING);
            }
            CoTaskMemFree(w);
        }
        item->Release();
    }
    d->Release();
    return ok;
}

// the folder with build_all.py and install_dlc.py: [DLC] ToolsFolder, else tools\dlc next to this program or a few
// folders up (the repository's build folder), else asked for once
static std::string FindTools(HWND owner)
{
    std::vector<std::string> cand;
    std::string set = NoTrailingSlash(GetValue(s_toolsOpt));
    if (!set.empty()) cand.insert(cand.end(), { set, set + "\\dlc", set + "\\tools\\dlc" });
    cand.push_back(GameDir() + "\\glimpse_tools\\dlc");  // this program elsewhere, the game folder found through Steam
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    std::string d = exe;
    for (int up = 0; up < 4; up++) {
        size_t s = d.find_last_of('\\');
        if (s == std::string::npos) break;
        d = d.substr(0, s);
        cand.insert(cand.end(), { d + "\\tools\\dlc", d + "\\glimpse_tools\\dlc" });
    }
    for (const std::string& c : cand)
        if (FileExists(c + "\\build_all.py") && FileExists(c + "\\install_dlc.py")) return c;
    MessageBoxA(owner, "The DLC converter of glimpse_fix (tools\\dlc\\build_all.py) is not next to this program.\n\n"
                       "In the next window, pick the folder \"tools\" of glimpse_fix (the one that holds the folder dlc).",
                "glimpse_fix", MB_OK | MB_ICONINFORMATION);
    std::string pick = set;
    if (!PickFolder(owner, L"The folder tools of glimpse_fix (with dlc\\build_all.py)", pick)) return "";
    for (const std::string& c : { pick, pick + "\\dlc", pick + "\\tools\\dlc" })
        if (FileExists(c + "\\build_all.py") && FileExists(c + "\\install_dlc.py")) {
            if (const char* err = SaveValues({ { &s_toolsOpt, NoTrailingSlash(pick) } })) Status(err);
            return c;
        }
    MessageBoxA(owner, "There is no dlc\\build_all.py in that folder.", "glimpse_fix", MB_OK | MB_ICONWARNING);
    return "";
}

static void OpenDlcLog(HWND wnd)
{
    std::string log = s_dlcLogPath;
    if (log.empty() || !FileExists(log)) {
        // the newer of the two logs of an earlier session
        std::string b = DlcWork() + "\\build.log", i = DlcWork() + "\\install.log";
        WIN32_FILE_ATTRIBUTE_DATA fb = {}, fi = {};
        bool hb = GetFileAttributesExA(b.c_str(), GetFileExInfoStandard, &fb) != 0;
        bool hi = GetFileAttributesExA(i.c_str(), GetFileExInfoStandard, &fi) != 0;
        log = hb && (!hi || CompareFileTime(&fb.ftLastWriteTime, &fi.ftLastWriteTime) >= 0) ? b : hi ? i : "";
    }
    if (log.empty()) {
        Status("No DLC build or install has run yet");
        return;
    }
    ShellExecuteA(wnd, "open", "notepad.exe", Quote(log).c_str(), NULL, SW_SHOWNORMAL);
}

static bool DlcStart(HWND wnd, DlcTask task, const std::string& script, const std::string& args)
{
    Python python = FindPython();
    const std::string& py = python.exe;
    if (py.empty()) {
        MessageBoxA(wnd, kNoPython, "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    std::string work = DlcWork();
    CreateDirectoryA((LocalAppData() + "\\glimpse_fix").c_str(), NULL);
    CreateDirectoryA(work.c_str(), NULL);
    s_dlcLogPath = work + (task == DLC_BUILD ? "\\build.log" : "\\install.log");
    s_taskStatus = work + (task == DLC_BUILD ? "\\build_status.txt" : "\\install_status.txt");
    DeleteFileA(s_taskStatus.c_str());
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };  // the handles below are inherited by Python
    HANDLE log = CreateFileA(s_dlcLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE) {
        std::string m = "Could not write " + s_dlcLogPath;
        MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_OK | MB_ICONWARNING);
        return false;
    }
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    std::string cmd = Quote(py) + (python.bundled ? " -I -X utf8 " : " -3 -I -X utf8 ") + Quote(script) + " " + args + " --status " +
                      Quote(s_taskStatus);
    std::vector<char> line(cmd.begin(), cmd.end());
    line.push_back(0);
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = si.hStdError = log;
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(NULL, line.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, work.c_str(), &si, &pi);
    DWORD err = GetLastError();
    CloseHandle(log);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        char m[MAX_PATH + 100];
        sprintf(m, "Could not start Python (%.*s, error %lu).", MAX_PATH, py.c_str(), err);
        MessageBoxA(wnd, m, "glimpse_fix", MB_OK | MB_ICONWARNING);
        return false;
    }
    // the job ends Python and everything it starts when this program goes
    s_taskJob = CreateJobObjectA(NULL, NULL);
    if (s_taskJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(s_taskJob, JobObjectExtendedLimitInformation, &li, sizeof(li));
        if (!AssignProcessToJobObject(s_taskJob, pi.hProcess)) {
            CloseHandle(s_taskJob);
            s_taskJob = NULL;
        }
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    s_taskProc = pi.hProcess;
    s_task = task;
    s_taskStart = GetTickCount();
    s_taskStopped = false;
    s_taskRan = true;
    SetTimer(wnd, kDlcTimer, 500, NULL);
    DlcButtons();
    DlcSetText(task == DLC_BUILD ? "Building: starting Python" : "Installing: starting Python", false);
    return true;
}

static void DlcStop()
{
    if (!DlcBusy() || s_task == DLC_DEPS) return;  // the DirectX installer is let finish
    s_taskStopped = true;
    if (s_task == DLC_DOWNLOAD) InterlockedExchange(&s_dl.cancel, 1);  // the thread stops at its next read
    else if (s_taskJob) TerminateJobObject(s_taskJob, 1);
    else TerminateProcess(s_taskProc, 1);
}

static void DlcInstall(HWND wnd, bool apply, bool moveOthers = false);
static void DlcUninstall(HWND wnd, bool apply);

// the [DLC] keys the staged build needs ("ini" lines of its manifest), turned on after asking
static void DlcOfferIni(HWND wnd)
{
    std::vector<Option*> off;
    std::string names;
    for (const std::string& l : ReadLines(DlcStaging() + "\\" + kDlcManifest)) {
        if (l.compare(0, 4, "ini\t")) continue;
        std::string kv = l.substr(4);
        size_t tab = kv.find('\t');
        if (tab == std::string::npos) continue;
        Option* o = Find(kv.substr(0, tab).c_str());
        if (!o || strcmp(o->section, "DLC") || o->kind != BOOL_OPT || kv.substr(tab + 1) != "1" || IsOn(o->key)) continue;
        off.push_back(o);
        names += std::string(names.empty() ? "" : ", ") + o->text;
    }
    if (off.empty()) return;
    std::string q = "Turn on " + names + " now?\n\nThe converted DLC is only used with these on (this page).";
    if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    std::vector<std::pair<const Option*, std::string>> values;
    for (Option* o : off) values.push_back({ o, "1" });
    const char* err = SaveValues(values);
    Status(err ? err : "Saved: the converted DLC is on");
}

static void DlcFailed(HWND wnd, const std::string& text, const std::vector<std::string>& details)
{
    std::string m = text;
    for (size_t i = 0; i < details.size() && i < 10; i++) m += (i ? "\n" : "\n\n") + details[i];
    m += "\n\nOpen the log?";
    if (MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_YESNO | MB_ICONWARNING) == IDYES) OpenDlcLog(wnd);
}

static void FlowAskInstall(HWND wnd, const DlcStatus& st);
static void FlowDone(HWND wnd, const DlcStatus& st);

static void DlcFinished(HWND wnd, DlcTask task, DWORD code, const DlcStatus& st)
{
    bool flow = s_flow;
    if (s_taskStopped) {
        s_flow = false;
        DlcSetText(task == DLC_BUILD ? "Stopped. Set up DLC (or Build) goes on from there; the finished steps are kept."
                                     : "Stopped.", false);
        return;
    }
    if (st.state.empty() || st.state == "RUNNING") {
        s_flow = false;
        char m[160];
        sprintf(m, "Python did not finish the %s (exit code %lu). See the log.",
                task == DLC_BUILD ? "build" : task == DLC_UPLAN || task == DLC_UNINSTALL ? "uninstall" : "install", code);
        DlcSetText(m, true);
        std::string q = std::string(m) + "\n\nIs Python 3 installed? " + kNoPython;
        DlcFailed(wnd, q, std::vector<std::string>());
        return;
    }
    bool ok = st.state == "OK" || st.state == "WARN";
    if (!ok) {
        s_flow = false;
        DlcSetText(st.text, true);
        Status(flow ? "Set up DLC stopped" : "The DLC task failed");
        DlcFailed(wnd, flow ? "Set up DLC stopped: " + st.text : st.text, st.details);
        return;
    }
    if (flow) {  // "Set up DLC": the next step
        if (task == DLC_BUILD) {
            s_flowNotes = st.state == "WARN" ? st.details : std::vector<std::string>();
            Status("DLC built");
            DlcInstall(wnd, false);  // the dry run: what the install would do
            if (!DlcBusy()) s_flow = false;
        } else if (task == DLC_PLAN) {
            FlowAskInstall(wnd, st);
        } else {
            s_flow = false;
            FlowDone(wnd, st);
        }
        return;
    }
    if (task == DLC_BUILD) {
        DlcSetText(st.text, false);
        Status("DLC built");
        if (st.state == "WARN") {
            std::string m = st.text;
            for (const std::string& d : st.details) m += "\n\n" + d;
            MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        }
    } else if (task == DLC_PLAN) {
        std::string q = "Install the converted DLC into\n" + s_taskGame + "\\glimpse_dlc?\n\n" + st.text + "\n";
        for (size_t i = 0; i < st.details.size() && i < 18; i++) q += "\n" + st.details[i];
        if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            DlcSetText("Not installed. " + st.text, false);
            return;
        }
        // archives in glimpse_dlc that are not from this build are laid over the game too: offer to move them away
        bool moveOthers = false;
        if (!st.others.empty()) {
            std::string list;
            for (const std::string& n : st.others) list += "\n    " + n;
            std::string m = "glimpse_dlc also holds archives that are not from this build:" + list +
                            "\n\nThe game lays them over its data as well, and an older copy of the same cars or track can "
                            "hide parts of the new one. Move them to glimpse_dlc_replaced_<date> in the game folder? "
                            "(Uninstall puts them back.)";
            int r = MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_YESNOCANCEL | MB_ICONWARNING);
            if (r == IDCANCEL) {
                DlcSetText("Not installed. " + st.text, false);
                return;
            }
            moveOthers = r == IDYES;
        }
        DlcInstall(wnd, true, moveOthers);
    } else if (task == DLC_INSTALL) {
        DlcSetText(st.text, false);
        Status("DLC installed");
        if (st.state == "WARN" || !st.details.empty()) {
            std::string m = st.text;
            for (const std::string& d : st.details) m += "\n\n" + d;
            MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_OK | (st.state == "WARN" ? MB_ICONWARNING : MB_ICONINFORMATION));
        }
        DlcOfferIni(wnd);
    } else if (task == DLC_UPLAN) {
        std::string q = "Remove the converted DLC from\n" + s_taskGame + "\\glimpse_dlc?\n\n" + st.text + "\n";
        for (size_t i = 0; i < st.details.size() && i < 18; i++) q += "\n" + st.details[i];
        q += "\n\nYour saves are copied first.";
        if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) == IDYES) DlcUninstall(wnd, true);
        else DlcSetText("Nothing removed.", false);
    } else {
        DlcSetText(st.text, st.state == "WARN");
        Status("DLC removed");
        std::string m = st.text;
        for (const std::string& d : st.details) m += "\n\n" + d;
        if (IsOn("Overlays")) {
            m += "\n\nTurn off \"Converted DLC\" now?";
            if (MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                const char* err = SaveValues({ { Find("Overlays"), "0" } });
                Status(err ? err : "Saved: the converted DLC is off");
            }
        } else {
            MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        }
    }
}

static const char* DlcTaskVerb(DlcTask t)
{
    switch (t) {
    case DLC_BUILD: return "Building";
    case DLC_PLAN: return "Checking the install";
    case DLC_INSTALL: return "Installing";
    case DLC_UPLAN: return "Checking the uninstall";
    case DLC_DOWNLOAD: return "Getting the DLC tools";
    case DLC_DEPS: return "Installing the DirectX runtime";
    default: return "Removing";
    }
}

static void DlcDownloadFinished(HWND wnd);
static void DlcDepsFinished(HWND wnd);

// WM_TIMER while a task runs: its state on the page, and what follows once it has ended
static void DlcPoll(HWND wnd)
{
    if (!DlcBusy()) return;
    bool ended = WaitForSingleObject(s_taskProc, 0) == WAIT_OBJECT_0;
    if (!ended) {
        DWORD secs = (GetTickCount() - s_taskStart) / 1000;
        char head[96];
        std::string text;
        if (s_task == DLC_DOWNLOAD) {
            EnterCriticalSection(&s_dl.cs);
            std::string step = s_dl.step;
            long long done = s_dl.done, total = s_dl.total;
            LeaveCriticalSection(&s_dl.cs);
            if (step == "Downloading")
                step = Megabytes(done) + (total > 0 ? " of " + Megabytes(total) : std::string()) + " downloaded";
            sprintf(head, "%s (%lu:%02lu): ", DlcTaskVerb(s_task), secs / 60, secs % 60);
            text = head + step;
        } else if (s_task == DLC_DEPS) {
            // setup_deps.ps1's last line of output
            std::string last;
            for (const std::string& l : ReadLines(s_dlcLogPath))
                if (!Trim(l).empty()) last = Trim(l);
            sprintf(head, "%s (%lu:%02lu): ", DlcTaskVerb(s_task), secs / 60, secs % 60);
            text = head + (last.empty() ? std::string("starting") : last);
        } else {
            DlcStatus st = ReadDlcStatus(s_taskStatus);
            sprintf(head, "%s (%lu:%02lu%s%s): ", DlcTaskVerb(s_task), secs / 60, secs % 60, st.step.empty() ? "" : ", step ",
                    st.step.c_str());
            text = head + (st.text.empty() ? std::string("starting Python") : st.text);
        }
        DlcSetText(text, false);
        return;
    }
    DWORD code = 1;
    if (s_task == DLC_DOWNLOAD) GetExitCodeThread(s_taskProc, &code);
    else GetExitCodeProcess(s_taskProc, &code);
    CloseHandle(s_taskProc);
    s_taskProc = NULL;
    if (s_taskJob) CloseHandle(s_taskJob);
    s_taskJob = NULL;
    KillTimer(wnd, kDlcTimer);
    DlcTask task = s_task;
    s_task = DLC_NONE;
    DlcButtons();
    if (task == DLC_DOWNLOAD) DlcDownloadFinished(wnd);
    else if (task == DLC_DEPS) DlcDepsFinished(wnd);
    else DlcFinished(wnd, task, code, ReadDlcStatus(s_taskStatus));
}

static bool StartBuild(HWND wnd, const std::string& dump, const std::string& tools)
{
    if (!DlcStart(wnd, DLC_BUILD, tools + "\\build_all.py", "--dump " + Quote(dump) + " --game " + Quote(GameDir()))) return false;
    Status("DLC build started; this window can stay open or be used meanwhile");
    return true;
}

static void DlcBuild(HWND wnd)
{
    if (DlcBusy()) {
        if (s_task == DLC_BUILD &&
            MessageBoxA(wnd, "Stop the DLC build? The steps finished so far are kept.", "glimpse_fix", MB_YESNO | MB_ICONQUESTION) == IDYES)
            DlcStop();
        return;
    }
    Option* o = Find("DumpFolder");
    std::string dump = NoTrailingSlash(GetValue(*o));
    if (dump.empty() || !DirExists(dump)) {
        MessageBoxA(wnd, dump.empty() ? "Pick your Xbox DLC folder first: the folder 425607E7 of your Xbox 360 dump (or its "
                                        "subfolder 00000002)."
                                      : "The Xbox DLC folder does not exist. Pick it again.",
                    "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string game = GameDir();
    if (!FileExists(game + "\\data\\resident.ark")) {
        std::string m = "This INI is not in the Split/Second folder (there is no data\\resident.ark next to it):\n" + std::string(s_path) +
                        "\n\nOpen the SplitSecondPad.ini in the game folder (Open INI...).";
        MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string tools = FindTools(wnd);
    if (tools.empty()) return;
    StartBuild(wnd, dump, tools);
}

// the dry run first (for the game folder of this INI), then, after the question, --apply for that same folder
static void DlcInstall(HWND wnd, bool apply, bool moveOthers)
{
    if (DlcBusy()) return;
    if (!FileExists(DlcStaging() + "\\" + kDlcManifest)) {
        MessageBoxA(wnd, "There is nothing to install yet: Build DLC first.", "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string tools = FindTools(wnd);
    if (tools.empty()) return;
    if (!apply) s_taskGame = GameDir();
    DlcStart(wnd, apply ? DLC_INSTALL : DLC_PLAN, tools + "\\install_dlc.py",
             "--game " + Quote(s_taskGame) + (apply ? " --apply" : "") + (moveOthers ? " --move-others" : ""));
}

static void DlcUninstall(HWND wnd, bool apply)
{
    if (DlcBusy()) return;
    if (!apply) s_taskGame = GameDir();
    if (!FileExists(s_taskGame + "\\glimpse_dlc\\" + kDlcInstalled)) {
        MessageBoxA(wnd, "There is nothing to remove: the converted DLC was not installed into this game folder by this "
                         "program.", "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string tools = FindTools(wnd);
    if (tools.empty()) return;
    DlcStart(wnd, apply ? DLC_UNINSTALL : DLC_UPLAN, tools + "\\install_dlc.py",
             "--game " + Quote(s_taskGame) + " --uninstall" + (apply ? " --apply" : ""));
}

// ---------------------------------------------------------------- DLC: "Set up DLC", all of it in one go

static bool Open(const char* path = NULL, bool viaSteam = false);

// the packages in the folder of [DLC] DumpFolder, on the line below it
static void DlcShowFound()
{
    if (!s_dlcFound) return;
    std::string cur = NoTrailingSlash(GetValue(*Find("DumpFolder"))), t;
    std::vector<DlcPackage> pk;
    if (cur.empty()) t = "No folder yet: Set up DLC looks for it on this PC (Xenia, Desktop, Downloads, Xbox USB drives).";
    else if (OnNetwork(cur)) t = "A network folder: Set up DLC looks into it when it runs.";  // not on every page change
    else if (!PackageFolder(cur, pk, true).empty()) t = "Found " + PackageSummary(pk);
    else t = DirExists(cur) ? "No Split/Second DLC packages in this folder." : "This folder does not exist.";
    SetWindowTextA(s_dlcFound, t.c_str());
}

static void SetDumpFolder(const std::string& dir)
{
    SetWindowTextA(Find("DumpFolder")->ctrl, dir.c_str());  // EN_CHANGE saves it
    DlcShowFound();
}

// a folder picked by hand; dir = the folder that holds its packages
static bool PickDlcFolder(HWND wnd, std::string& dir)
{
    std::string pick = NoTrailingSlash(GetValue(*Find("DumpFolder")));
    if (!PickFolder(wnd, L"Your Split/Second DLC: the folder 425607E7, 00000002, or a folder with the package files", pick)) return false;
    std::vector<DlcPackage> pk;
    std::string at = PackageFolder(pick, pk, true);
    if (at.empty()) {
        std::string m = "There are no Split/Second DLC packages in\n" + pick +
                        "\n\nPick the folder 425607E7 of your Xbox 360 or Xenia content, its subfolder 00000002, or a folder "
                        "with the package files (the long hex names, as the console keeps them).";
        MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_OK | MB_ICONWARNING);
        return false;
    }
    dir = at;
    return true;
}

// the place found that has every package the others have (the same dump twice, or a fuller one), else -1
static int BestPlace(const std::vector<DlcPlace>& places)
{
    std::vector<std::vector<std::string>> sets;
    for (const DlcPlace& p : places) {
        std::vector<std::string> n;
        for (const DlcPackage& k : p.packs) n.push_back(Lower(k.file.substr(k.file.find_last_of('\\') + 1)));
        std::sort(n.begin(), n.end());
        sets.push_back(n);
    }
    for (size_t i = 0; i < sets.size(); i++) {
        bool all = true;
        for (size_t j = 0; j < sets.size() && all; j++)
            all = i == j || std::includes(sets[i].begin(), sets[i].end(), sets[j].begin(), sets[j].end());
        if (all) return (int)i;
    }
    return -1;
}

// the places found as a menu below anchor, and "Other folder..."
static bool ChooseDlcFolder(HWND wnd, HWND anchor, const std::vector<DlcPlace>& places, std::string& dir)
{
    std::vector<DlcPackage> pk;
    std::string cur = PackageFolder(GetValue(*Find("DumpFolder")), pk, true);  // ticked: the folder set now
    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING | MF_GRAYED, 0, "Split/Second DLC found on this PC:");
    for (size_t i = 0; i < places.size() && i < 20; i++) {
        std::string t;
        for (char c : places[i].dir) t += c == '&' ? std::string("&&") : std::string(1, c);
        t += "\t" + std::to_string(places[i].packs.size()) + " packages";
        AppendMenuA(menu, MF_STRING | (SamePath(places[i].dir, cur) ? MF_CHECKED : 0), i + 1, t.c_str());
    }
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, 1000, "Other folder...");
    RECT r;
    GetWindowRect(anchor, &r);
    bool right = anchor == s_dlcBrowse;
    int pick = TrackPopupMenu(menu, TPM_RETURNCMD | (right ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_TOPALIGN, right ? r.right : r.left,
                              r.bottom + S(2), 0, wnd, NULL);
    DestroyMenu(menu);
    if (pick == 1000) return PickDlcFolder(wnd, dir);
    if (pick < 1 || pick > (int)places.size()) return false;
    dir = places[pick - 1].dir;
    return true;
}

// "..." next to the folder: what the search finds, or the folder picker straight away
static void BrowseDlcFolder(HWND wnd)
{
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    std::vector<DlcPlace> places = FindDlcPlaces(NoTrailingSlash(GetValue(*Find("DumpFolder"))));
    SetCursor(old);
    std::string dir;
    if (places.empty() ? PickDlcFolder(wnd, dir) : ChooseDlcFolder(wnd, s_dlcBrowse, places, dir)) SetDumpFolder(dir);
}

// the game folder: this INI's, else (after asking) the one Steam has
static bool EnsureGameDir(HWND wnd)
{
    std::string game = GameDir();
    if (FileExists(game + "\\data\\resident.ark")) return true;
    std::string steam = SteamGameDir();
    if (!steam.empty() && !SamePath(steam, game) && FileExists(steam + "\\data\\resident.ark") && steam.size() + 24 < MAX_PATH) {
        std::string q = "These settings are not in the Split/Second folder:\n" + std::string(s_path) + "\n\nSteam has the game in\n" +
                        steam + "\n\nUse that game folder? Its SplitSecondPad.ini is edited from then on (Open INI... picks another one).";
        if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) != IDYES) return false;
        if (!Open((steam + "\\SplitSecondPad.ini").c_str(), true)) return false;
        DlcShowFound();
        return true;
    }
    std::string m = "Split/Second was not found: these settings are not in the game folder (there is no data\\resident.ark next "
                    "to them), and Steam does not have the game either:\n" + std::string(s_path) +
                    "\n\nOpen the SplitSecondPad.ini in the game folder (Open INI...), then Set up DLC again.";
    MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_OK | MB_ICONINFORMATION);
    return false;
}

// the DLC folder: the one set if it has packages, else what the search finds (asked only when the places found differ)
static bool EnsureDumpFolder(HWND wnd, std::string& dump)
{
    std::string cur = NoTrailingSlash(GetValue(*Find("DumpFolder")));
    std::vector<DlcPackage> pk;
    dump = PackageFolder(cur, pk, true);
    if (!dump.empty()) return true;
    DlcSetText("Looking for your Xbox DLC on this PC...", false);
    UpdateWindow(s_dlcState);
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    std::vector<DlcPlace> places = FindDlcPlaces(cur);
    SetCursor(old);
    int best = BestPlace(places);
    if (best >= 0) {
        dump = places[best].dir;
    } else if (places.empty()) {
        DlcRefresh();
        MessageBoxA(wnd, "Your Split/Second DLC was not found in the usual places (Xenia, Desktop, Downloads, Documents, Xbox USB "
                         "drives).\n\nIn the next window, pick the folder 425607E7 of your Xbox 360 or Xenia content, its subfolder "
                         "00000002, or a folder with the package files.", "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        if (!PickDlcFolder(wnd, dump)) return false;
    } else if (!ChooseDlcFolder(wnd, s_dlcSetup, places, dump)) {
        DlcRefresh();
        return false;
    }
    SetDumpFolder(dump);
    return true;
}

// what the staged build holds, in words: "11 cars, Quarry, Minepit Park and the Deadline and Survival Race modes"
static std::string ContentSummary(const std::vector<std::string>& manifest)
{
    std::vector<std::string> parts;
    std::vector<std::string> cars = SplitList(TsvValue(manifest, "cars")), modes = SplitList(TsvValue(manifest, "mode"));
    if (!cars.empty()) parts.push_back(std::to_string(cars.size()) + (cars.size() == 1 ? " car" : " cars"));
    for (const std::string& t : SplitList(TsvValue(manifest, "track"))) parts.push_back(!_stricmp(t.c_str(), "nem_quarry") ? "Minepit Park" : t);
    if (!modes.empty()) parts.push_back("the " + JoinAnd(modes) + (modes.size() == 1 ? " mode" : " modes"));
    return JoinAnd(parts);
}

// a [DLC] key as the summary names it
static std::string IniShortName(const Option& o)
{
    static const struct { const char *key, *name; } names[] = {
        { "Overlays", "Converted DLC" }, { "Tracks", "DLC tracks" }, { "Modes", "Deadline game mode" }, { "Music", "DLC race music" },
        { "Cars", "DLC cars in the car select" }, { "Pickups", "Deadline pickups" },
    };
    for (auto& n : names)
        if (!strcmp(o.key, n.key)) return n.name;
    return Trim(o.text);
}

static bool StartBuild(HWND wnd, const std::string& dump, const std::string& tools);

static void FlowBuild(HWND wnd)
{
    if (!StartBuild(wnd, s_flowDump, s_flowTools)) s_flow = false;
    DlcButtons();
}

// setup_deps.ps1 -DirectXOnly for the DirectX runtime: only that, whatever else is there or missing (no bundle, Python,
// numpy, vgmstream-cli or ffmpeg is downloaded, installed or replaced: the user was asked about the DirectX runtime
// alone). The 64 bit PowerShell (it checks System32), without a window, its output in deps.log, whose last line the
// state shows; not in the job, so closing this program does not stop an installer.
static bool StartDeps(HWND wnd)
{
    std::string ps = SystemExe("WindowsPowerShell\\v1.0\\powershell.exe");
    if (ps.empty()) ps = "powershell.exe";
    std::string line = Quote(ps) + " -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + Quote(s_flowTools + "\\setup_deps.ps1") +
                       " -DirectXOnly";
    std::vector<char> l(line.begin(), line.end());
    l.push_back(0);
    CreateDirectoryA(ToolsBase().c_str(), NULL);
    CreateDirectoryA(DlcWork().c_str(), NULL);
    s_dlcLogPath = DlcWork() + "\\deps.log";
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE log = CreateFileA(s_dlcLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = si.hStdError = log;
    PROCESS_INFORMATION pi = {};
    BOOL ok = log != INVALID_HANDLE_VALUE &&
              CreateProcessA(NULL, l.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, s_flowTools.c_str(), &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        MessageBoxA(wnd, "setup_deps.ps1 could not be started. Set up DLC goes on without the DLC tracks.", "glimpse_fix",
                    MB_OK | MB_ICONWARNING);
        return false;
    }
    CloseHandle(pi.hThread);
    s_taskProc = pi.hProcess;
    s_taskJob = NULL;
    s_task = DLC_DEPS;
    s_taskStart = GetTickCount();
    s_taskStopped = false;
    s_taskRan = true;
    SetTimer(wnd, kDlcTimer, 500, NULL);
    DlcButtons();
    DlcSetText("Installing the DirectX runtime (Windows asks for admin rights). Set up DLC goes on once it is done.", false);
    return true;
}

static void DlcDepsFinished(HWND wnd)
{
    if (!s_flow) return;
    if (!D3DCompilerThere()) s_skipDirectX = true;  // the build says what it left out
    FlowBuild(wnd);
}

// the DirectX runtime for the tracks (asked once), then the build
static void FlowAfterTools(HWND wnd)
{
    if (FindPython().exe.empty()) {
        s_flow = false;
        MessageBoxA(wnd, kNoPython, "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!D3DCompilerThere() && !s_skipDirectX) {
        bool canRun = FileExists(s_flowTools + "\\setup_deps.ps1");
        std::string q = "The DLC tracks Quarry and Minepit Park need Microsoft's DirectX End-User Runtime (June 2010), which is "
                        "not on this PC (D3DCompiler_41.dll is missing).\n\n";
        q += canRun ? "Install it now? It comes from Microsoft (setup_deps.ps1 gets it), and Windows asks for admin rights.\n\n"
                      "No: go on without the tracks; the DLC cars and the Deadline mode work all the same."
                    : "Go on without the tracks? The DLC cars and the Deadline mode work all the same.";
        int a = MessageBoxA(wnd, q.c_str(), "glimpse_fix", (canRun ? MB_YESNOCANCEL : MB_OKCANCEL) | MB_ICONQUESTION);
        if (a == IDCANCEL) {
            s_flow = false;
            DlcSetText("Not set up.", false);
            return;
        }
        if (a == IDYES && StartDeps(wnd)) return;
        s_skipDirectX = true;
    }
    FlowBuild(wnd);
}

static bool StartDownload(HWND wnd, const std::string& url)
{
    CreateDirectoryA(ToolsBase().c_str(), NULL);
    CreateDirectoryA(DlcWork().c_str(), NULL);
    s_dl.url = url;
    s_dl.step = "Starting";
    s_dl.error.clear();
    s_dl.done = s_dl.total = 0;
    s_dl.ok = false;
    s_dl.cancel = 0;
    s_dlcLogPath = DlcWork() + "\\download.log";
    s_dl.log = fopen(s_dlcLogPath.c_str(), "w");
    HANDLE t = CreateThread(NULL, 0, DownloadThread, NULL, 0, NULL);
    if (!t) {
        if (s_dl.log) fclose(s_dl.log);
        s_dl.log = NULL;
        MessageBoxA(wnd, "The download could not be started.", "glimpse_fix", MB_OK | MB_ICONWARNING);
        return false;
    }
    s_taskProc = t;
    s_taskJob = NULL;
    s_task = DLC_DOWNLOAD;
    s_taskStart = GetTickCount();
    s_taskStopped = false;
    s_taskRan = true;
    SetTimer(wnd, kDlcTimer, 500, NULL);
    DlcButtons();
    DlcSetText("Getting the DLC tools: starting", false);
    return true;
}

static void DlcDownloadFinished(HWND wnd)
{
    bool flow = s_flow;
    if (s_taskStopped) {
        s_flow = false;
        // a Stop that came while it was unpacking or putting the tools in place: they were installed all the same
        DlcSetText(s_dl.ok ? "Stopped. The DLC tools were installed already." : "Download stopped; nothing was installed.", false);
        return;
    }
    if (!s_dl.ok) {
        s_flow = false;
        DlcSetText("The DLC tools could not be downloaded.", true);
        DlcFailed(wnd, "The DLC tools could not be downloaded or put in place:\n" + s_dl.error, std::vector<std::string>());
        return;
    }
    Status("DLC tools installed");
    DlcSetText("The DLC tools are ready.", false);
    if (flow) FlowAfterTools(wnd);
}

// Python (the DLC tools' own or the py launcher) and what the tracks need; else the DLC tools are offered, with their
// size and where they come from, and downloaded only after Yes
static void FlowGetTools(HWND wnd)
{
    Python py = FindPython();
    bool trackTools = py.bundled || (HaveTool("vgmstream-cli") && HaveTool("ffmpeg"));
    if (!py.exe.empty() && trackTools) {
        FlowAfterTools(wnd);
        return;
    }
    bool tooOld = FileExists(ToolsDir() + "\\" + kToolsPython);  // there, but not a bundle this program knows (FindPython)
    std::string url = ToolsUrl();
    HttpResult r = {};
    bool reach = false;
    if (!url.empty()) {
        DlcSetText("Looking up the DLC tools on GitHub...", false);
        UpdateWindow(s_dlcState);
        HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
        reach = HttpFetch(url, true, 0, [](const char*, DWORD) { return true; }, r);
        SetCursor(old);
    } else {
        r.error = "this program's version is not known";
    }
    if (!reach) {
        if (!py.exe.empty()) {
            s_flowNotes.push_back("The DLC tools could not be reached (" + r.error + "), so the Python on this PC is used; the DLC "
                                  "tracks may be left out.");
            FlowAfterTools(wnd);
            return;
        }
        s_flow = false;
        DlcSetText("Not set up: converting the DLC needs Python 3 or the DLC tools.", true);
        std::string m = "The DLC tools could not be downloaded from\n" + url + "\n(" + r.error + ").\n\n" + kNoPython;
        MessageBoxA(wnd, m.c_str(), "glimpse_fix", MB_OK | MB_ICONWARNING);
        return;
    }
    std::string q;
    if (py.exe.empty())
        q = std::string(tooOld ? "The DLC tools on this PC are incomplete or of a kind this version of glimpse_fix does not know."
                               : "Converting the DLC needs the DLC tools of glimpse_fix (Python with numpy, vgmstream-cli and "
                                 "ffmpeg). They are not on this PC yet.") + "\n\nDownload them now?";
    else
        q = "Python was found, but not vgmstream-cli and ffmpeg, which the DLC tracks need. The DLC tools of glimpse_fix bring "
            "all of it (Python with numpy, vgmstream-cli and ffmpeg).\n\nDownload them now? (No: go on with the Python on this "
            "PC; the DLC tracks may be left out.)";
    q += "\n\nSize: " + Megabytes(r.total) + "\nFrom: " + url + "\nTo: " + ToolsDir() +
         "\n\nThe download is checked against the SHA-256 checksum published next to it. Your DLC is not uploaded anywhere.";
    if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        if (!StartDownload(wnd, url)) s_flow = false;
        return;
    }
    if (!py.exe.empty()) {
        FlowAfterTools(wnd);
        return;
    }
    s_flow = false;
    DlcSetText("Not set up: converting the DLC needs the DLC tools (or Python 3).", false);
}

// the install plan in plain words, then the install after one Yes (older archives in glimpse_dlc are moved aside)
static void FlowAskInstall(HWND wnd, const DlcStatus& st)
{
    std::string what = ContentSummary(ReadLines(DlcStaging() + "\\" + kDlcManifest)), files = st.text;
    const char pre[] = "Ready to install: ";
    if (!files.compare(0, sizeof(pre) - 1, pre)) files = files.substr(sizeof(pre) - 1);
    size_t dot = files.find(". ");
    if (dot != std::string::npos) files = files.substr(0, dot);
    if (!files.empty() && files.back() == '.') files.pop_back();
    std::string q = "Add the converted DLC to the game?\n\n";
    if (!what.empty()) q += "Adds: " + what + ".\n";
    q += "Into: " + s_taskGame + "\\glimpse_dlc\nFiles: " + files + ".\n\nYour saves are backed up first. Only files this program "
         "installed before are replaced; nothing else in the game folder is changed. Uninstall takes it out again.";
    if (!st.others.empty())
        q += "\n\nglimpse_dlc also holds archives that are not from this build (" + JoinAnd(st.others) + "). The game would lay "
             "them over the new DLC, so they are moved to glimpse_dlc_replaced_<date> in the game folder; Uninstall puts them back.";
    for (size_t i = 0; i < s_flowNotes.size() && i < 3; i++) q += "\n\nNote: " + s_flowNotes[i];
    if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        s_flow = false;
        DlcSetText("Not installed. The converted DLC is kept: Set up DLC (or Install) adds it later.", false);
        return;
    }
    DlcInstall(wnd, true, !st.others.empty());
    if (!DlcBusy()) s_flow = false;
}

// installed: the [DLC] keys the build needs on (no question: Set up DLC was asked for), and the summary
static void FlowDone(HWND wnd, const DlcStatus& st)
{
    std::vector<std::string> m = ReadLines(DlcStaging() + "\\" + kDlcManifest), on;
    std::vector<std::pair<const Option*, std::string>> values;
    for (const std::string& l : m) {
        if (l.compare(0, 4, "ini\t")) continue;
        std::string kv = l.substr(4);
        size_t tab = kv.find('\t');
        if (tab == std::string::npos) continue;
        Option* o = Find(kv.substr(0, tab).c_str());
        if (!o || strcmp(o->section, "DLC") || o->kind != BOOL_OPT || kv.substr(tab + 1) != "1" || IsOn(o->key)) continue;
        values.push_back({ o, "1" });
        on.push_back(IniShortName(*o));
    }
    bool saved = values.empty() || !SaveValues(values);
    std::string what = ContentSummary(m);
    std::string text = (what.empty() ? std::string("The converted DLC is") : what + " are") + " now in the game.";
    std::string keys = on.empty() ? " Its settings on this page were on already." : " Turned on for it: " + JoinAnd(on) + ".";
    std::string msg = text + keys;
    if (!saved) msg += "\n\nThe settings could not be saved to " + std::string(s_path) + ".";
    if (!FileExists(s_taskGame + "\\dinput8.dll"))
        msg += "\n\nglimpse_fix itself (dinput8.dll) is not in the game folder yet; the DLC shows up only with it.";
    for (size_t i = 0; i < s_flowNotes.size() && i < 3; i++) msg += "\n\nNote: " + s_flowNotes[i];
    if (st.state == "WARN")
        for (size_t i = 0; i < st.details.size() && i < 4; i++) msg += "\n\n" + st.details[i];
    DlcSetText(text + keys, st.state == "WARN");
    Status(saved ? "DLC set up" : "Could not write the INI");
    MessageBoxA(wnd, msg.c_str(), "glimpse_fix", MB_OK | (st.state == "WARN" ? MB_ICONWARNING : MB_ICONINFORMATION));
}

static void DlcSetup(HWND wnd)
{
    static bool asking;  // a click that comes while its own questions are open (they let messages through)
    if (asking) return;
    if (DlcBusy()) {
        if (FlowStoppable()) {
            const char* q = s_task == DLC_DOWNLOAD ? "Stop the download of the DLC tools?"
                                                   : "Stop Set up DLC? The conversion steps finished so far are kept.";
            HANDLE task = s_taskProc;  // the flow goes on while the question is open: stop only the step it was about
            asking = true;
            bool yes = MessageBoxA(wnd, q, "glimpse_fix", MB_YESNO | MB_ICONQUESTION) == IDYES;
            asking = false;
            if (yes && s_taskProc == task && FlowStoppable()) DlcStop();
        }
        return;
    }
    asking = true;
    bool ready = EnsureGameDir(wnd);
    if (ready) CheckLargeAddressAware(wnd, true);  // the DLC needs the memory most
    std::string dump, tools;
    if (ready) ready = EnsureDumpFolder(wnd, dump);
    if (ready) tools = FindTools(wnd);
    asking = false;
    if (!ready || tools.empty() || DlcBusy()) return;
    s_flow = true;
    s_flowNotes.clear();
    s_flowDump = dump;
    s_flowTools = tools;
    s_taskRan = true;
    asking = true;
    FlowGetTools(wnd);  // asks before the download and the DirectX runtime, then starts the first step
    asking = false;
}

static const char* const kPageText[kPageCount] = {
    "Settings for all pages but Online, DLC, Deadline mode and Debug. The matching one is outlined.",
    "Pads, triggers, button prompts and the D-pad repeat in menus.",
    "Frame rate, quality limit, texture detail and filtering, SMAA, HDR output and the icon size.",
    "Motion blur, bloom, flares and the screen overlays. * applies while the game runs.",
    "Streaming, draw distance, shadows, pop-in and the opponents' car detail.",
    "Field of view, ultrawide screens, camera shake, zoom, photo mode and the HUD key.",
    "Unlocks, cutscenes and boot movies, respot time and the power play bar.",
    "Boost and handbrake for your car, with their buttons and keys.",
    "Boost for the AI cars (experimental).",
    "Opponent engines, ducking, sound placement, Power Play flanger, bank offsets and console reverb.",
    "Online play over Steam (experimental). Presets leave this page alone.",
    "Your own Xbox 360 DLC, converted on this PC (experimental). Presets leave this page alone.",
    "The DLC game mode Deadline, from your converted DLC (experimental). Presets leave this page alone.",
    "For bug reports, and the texture dump.",
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
    int presets = page == 0 ? SW_SHOW : SW_HIDE;
    for (int i = 0; i < kPresetCount; i++) {
        ShowWindow(s_preset[i], presets);
        ShowWindow(s_presetText[i], presets);
    }
    ShowWindow(s_undo, presets);
    ShowWindow(s_undoText, presets);
    if (page == 0) UpdateUndo();
    int dlc = page == kDlcPage ? SW_SHOW : SW_HIDE;
    for (HWND h : { s_dlcBrowse, s_dlcFound, s_dlcSetup, s_dlcBuild, s_dlcInstall, s_dlcUninstall, s_dlcLog, s_dlcState })
        ShowWindow(h, dlc);
    if (page == kDlcPage) {
        DlcRefresh();
        DlcShowFound();
    }
    s_infoWarn = false;
    SetWindowTextA(s_info, page == 0 ? "Before a preset is applied, your own settings are kept, and \"Back to your settings\" "
                                       "puts them back. Presets change only this INI, never the game's own options or your profile."
                                     : "Point at an option to see what it does. Every change is saved at once.");
    InvalidateRect(s_info, NULL, TRUE);
    for (int i = 0; i < kPageCount; i++) InvalidateRect(s_nav[i], NULL, TRUE);
    InvalidateRect(s_wnd, NULL, TRUE);  // the text fields are painted by the window
}

struct Preset {
    const char* name;
    const char* help;    // shown next to the preset's button
    const char* values;  // "Key=value;..."
};
static const Preset kPresets[kPresetCount] = {
    { "Recommended", "Every option at its default: 60 fps, SMAA and the fixes on, the extras off (Pick laps too; the lap count you picked is kept for when you switch it on again).", "" },
    { "Original game", "The game as released: 30 fps, no SMAA, linear filtering, the stock streaming pool, distances, pop-in, car detail, icon size and audio, all effects on, the PC's car glass, stock menu loading, shared triggers, no ultrawide fix or cutscene skip. The controller support stays.", "FrameRate=30;Anisotropy=0;SMAA=0;GlassFix=0;TextureLoadFix=0;ShadowResolution=1;HudResolution=-1;MotionBlurStrength=1.0;Vignette=1;Bloom=1;BloomStrength=1.0;BloomTone=1.0;BloomCurve=1.0;Exposure=1.0;LensFlare=1;ParticleFlare=1;FixPowerplayIconSize=0;HDR=0;OpponentEngines=0;OpponentEngineVolume=100;Ducking=0;SoundPlacement=0;SoundBankFix=0;PowerPlayFlanger=0;LapSelect=0;Laps=0;ConsoleReverb=0;StreamPoolMB=0;LodDistanceScale=1.0;ShadowDistance=1.0;PopInFix=0;CarDetail=0;CutsceneSkip=0;SeparateTriggers=0;UltrawideFix=0" },
    { "Potato", "For very weak systems, at 60 fps: graphics quality Low (the level in the game's menu is kept), no shadows, no effects, no SMAA, plain filtering, the shortest draw distances and the stock streaming pool.",
      "FrameRate=60;QualityCap=0;SMAA=0;Anisotropy=0;MotionBlurStrength=0.0;Vignette=0;Gradient=0;Bloom=0;LensFlare=0;ParticleFlare=0;StreamPoolMB=0;LodDistanceScale=0.5;ShadowDistance=1.0;ShadowResolution=1;HudResolution=-1;Shadows=0;CarDetail=0;PopInFix=0" },
    { "Peeled potato", "Potato, and the textures at an eighth of their size as well.",
      "FrameRate=60;QualityCap=0;TextureDetail=3;SMAA=0;Anisotropy=0;MotionBlurStrength=0.0;Vignette=0;Gradient=0;Bloom=0;LensFlare=0;ParticleFlare=0;StreamPoolMB=0;LodDistanceScale=0.5;ShadowDistance=1.0;ShadowResolution=1;HudResolution=-1;Shadows=0;CarDetail=0;PopInFix=0" },
    { "Chaos", "Recommended plus everything unlocked, the boost, the power bar fill and the AI boost in chaos mode.", "UnlockAll=1;Boost=1;PowerBarFill=1;AIBoost=1;AIBoostChaos=1" },
};

// the value a preset gives an option: its own, else the option's default
static std::string PresetValue(const Preset& p, const Option& o)
{
    std::string list = std::string(";") + p.values + ";", pat = std::string(";") + o.key + "=";
    size_t at = list.find(pat);
    if (at == std::string::npos) return o.def;
    size_t s = at + pat.size();
    return list.substr(s, list.find(';', s) - s);
}

// the options a preset sets: all but those outside presets, plus any of those the preset names itself
static bool InPreset(const Preset& p, const Option& o)
{
    if (!OutsidePresets(o)) return true;
    std::string list = std::string(";") + p.values + ";", pat = std::string(";") + o.key + "=";
    return strcmp(o.section, "Debug") && strcmp(o.section, "Online") && strcmp(o.section, "DLC") && list.find(pat) != std::string::npos;
}

// whether every option the preset sets currently has the value the preset gives it
static bool PresetMatches(const Preset& p)
{
    for (const Option& o : kOptions) {
        if (!InPreset(p, o)) continue;
        std::string want = PresetValue(p, o), have = GetValue(o);
        if (!strcmp(o.key, "HudResolution")) {
            if (ParseHudResolution(want.c_str()) == ParseHudResolution(have.c_str())) continue;
            return false;
        }
        if (!_stricmp(want.c_str(), have.c_str())) continue;
        if (!strcmp(o.key, "OpponentEngineVolume")) {
            int defVal = atoi(o.def);
            if (ParsePercentValue(want.c_str(), defVal, (int)o.lo, (int)o.hi) ==
                ParsePercentValue(have.c_str(), defVal, (int)o.lo, (int)o.hi))
                continue;
            return false;
        }
        char* end1;
        char* end2;
        double a = strtod(want.c_str(), &end1), b = strtod(have.c_str(), &end2);
        if (!*end1 && !*end2 && end1 != want.c_str() && end2 != have.c_str() && fabs(a - b) < 1e-6) continue;
        return false;
    }
    return true;
}

// Your settings from before a preset, to go back to: the values of every option a preset sets, in a file
// next to the INI. Taken when a preset is applied to settings of your own (none of the presets); while the
// settings are one preset's, applying another keeps the file, so it holds what you had before the first.
static std::string UndoPath() { return std::string(s_path) + ".before_preset"; }

static bool AnyPresetMatches()
{
    for (const Preset& p : kPresets)
        if (PresetMatches(p)) return true;
    return false;
}

static bool UndoExists() { return GetFileAttributesA(UndoPath().c_str()) != INVALID_FILE_ATTRIBUTES; }

static bool SaveUndo(const Preset& p)
{
    std::vector<std::string> lines;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char head[160];
    sprintf(head, ";%s|%04u-%02u-%02u %02u:%02u", p.name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    lines.push_back("; glimpse_fix: your settings from before a preset (\"Back to your settings\" on the Presets page)");
    lines.push_back(head);
    for (const Option& o : kOptions)
        if (InPreset(p, o)) lines.push_back(std::string(o.key) + "=" + GetValue(o));
    return SaveLines(UndoPath().c_str(), lines);
}

// A kept file from an earlier preset lacks a key the new preset sets but the earlier one did not (only Laps:
// "Original game"): it gets the current value, so going back still restores it. Other presets never touch
// Laps, so after them a lap count picked in the game since stays.
static bool ExtendUndo(const Preset& p)
{
    Option* laps = Find("Laps");
    if (!laps || !InPreset(p, *laps)) return true;
    FILE* f = fopen(UndoPath().c_str(), "rb");
    if (!f) return false;
    std::vector<std::string> lines;
    char line[512];
    bool has = false;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        std::string l = Trim(line);
        if (!_strnicmp(l.c_str(), "Laps=", 5)) has = true;
        lines.push_back(line);
    }
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) return false;
    if (has) return true;
    lines.push_back(std::string("Laps=") + GetValue(*laps));
    return SaveLines(UndoPath().c_str(), lines);
}

// what the file says it was taken before: "Potato, 2026-11-19 20:15"
static std::string UndoInfo()
{
    FILE* f = fopen(UndoPath().c_str(), "rb");
    if (!f) return "";
    char line[256];
    std::string info;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;  // fgets keeps the line end
        std::string l = Trim(line);
        if (l.size() > 1 && l[0] == ';' && l.find('|') != std::string::npos && l[1] != ' ') {
            info = l.substr(1);
            size_t bar = info.find('|');
            info = "\"" + info.substr(0, bar) + "\", " + info.substr(bar + 1);
            break;
        }
    }
    fclose(f);
    return info;
}

static void UpdateUndo()
{
    if (!s_undo) return;
    bool have = UndoExists();
    EnableWindow(s_undo, have);
    std::string info = UndoInfo();
    std::string text = have ? (info.empty() ? std::string("Your settings from before a preset.")
                                            : "Your settings from before the preset " + info + ".")
                            : "Applying a preset keeps your own settings first, to come back to here.";
    SetWindowTextA(s_undoText, text.c_str());
    InvalidateRect(s_undo, NULL, TRUE);
}

static void ApplyPreset(const Preset& p)
{
    IniLock lock;
    if (!lock.ok()) {
        Status(lock.error());
        return;
    }
    if (!Refresh()) {
        Status("Could not read the INI; the preset was not applied");
        return;
    }
    bool fresh = !UndoExists() || !AnyPresetMatches();
    if (fresh ? !SaveUndo(p) : !ExtendUndo(p)) {
        Status("Could not keep your settings, the preset was not applied");
        return;
    }
    const auto before = s_lines;
    for (Option& o : kOptions) {
        if (!InPreset(p, o)) continue;
        SetValue(o, PresetValue(p, o));
    }
    bool saved = WriteIni(before);
    LoadControls();
    char msg[160];
    sprintf(msg, saved ? "Preset \"%s\" saved" : "Could not write the INI", p.name);
    Status(msg);
    UpdateUndo();
}

// the settings from before the preset back, then the file goes
static void PutBackUndo()
{
    FILE* f = fopen(UndoPath().c_str(), "rb");
    if (!f) {
        UpdateUndo();
        return;
    }
    std::vector<std::pair<Option*, std::string>> values;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;  // fgets keeps the line end
        std::string l = Trim(line);
        size_t eq = l.find('=');
        if (l.empty() || l[0] == ';' || eq == std::string::npos) continue;
        Option* o = Find(l.substr(0, eq).c_str());
        if (o && (!OutsidePresets(*o) || !strcmp(o->key, "Laps"))) values.push_back({ o, l.substr(eq + 1) });
    }
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad || values.empty()) {
        Status("The file with your earlier settings could not be read; nothing was changed");
        return;
    }
    IniLock lock;
    if (!lock.ok()) {
        Status(lock.error());
        return;
    }
    if (!Refresh()) {
        Status("Could not read the INI; nothing was changed");
        return;
    }
    const auto before = s_lines;
    int laps = FindKey("Gameplay", "Laps", NULL);
    // Only Original game sets Laps. Undo its reset only while its saved zero is still present.
    bool restoreLaps = laps >= 0 && ValueAt(laps) == "0";
    // an option the file does not name came after it was written: it had its default then
    for (Option& o : kOptions)
        if (!OutsidePresets(o)) SetValue(o, o.def);
    for (const auto& v : values)
        if (restoreLaps || strcmp(v.first->key, "Laps")) SetValue(*v.first, v.second);
    bool saved = WriteIni(before);
    LoadControls();
    if (saved) {
        DeleteFileA(UndoPath().c_str());
        Status("Your settings are back");
    } else {
        Status("Could not write the INI");
    }
    UpdateUndo();
}

static void RestoreUndo(HWND wnd)
{
    std::string q = "Put back your settings from before the preset " + UndoInfo() + "?";
    if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) == IDYES) PutBackUndo();
}

static HWND Child(const char* cls, const char* text, DWORD style, int x, int y, int w, int h, int id, Role role,
                  HFONT font)
{
    HWND h_ = CreateWindowExA(0, cls, text, WS_CHILD | style, x, y, w, h, s_wnd, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageA(h_, WM_SETFONT, (WPARAM)font, TRUE);
    SetWindowLongPtrA(h_, GWLP_USERDATA, role);
    return h_;
}

enum { ID_NAV0 = 10, ID_PRESET0 = 40, ID_OPEN = 30, ID_CLOSE = 31, ID_RESTORE = 32, ID_UNDO = 33, ID_LAUNCH = 34,
       ID_DLCBROWSE = 35, ID_DLCBUILD = 36, ID_DLCINSTALL = 37, ID_DLCLOG = 38, ID_DLCUNINSTALL = 39, ID_DLCSETUP = 50,
       ID_OPTION0 = 100 };
static_assert(ID_NAV0 + kPageCount <= ID_OPEN && ID_DLCUNINSTALL < ID_PRESET0 && ID_PRESET0 + kPresetCount <= ID_DLCSETUP &&
              ID_DLCSETUP < ID_OPTION0, "control ids overlap");


static void CreateControls()
{
    const int ctrlX = kContentX + kLabelW;
    Child("STATIC", "glimpse_fix", WS_VISIBLE | SS_LEFT, S(22), S(22), S(180), S(30), 0, R_BRAND, s_fontBrand);
    Child("STATIC", "Split/Second PC fix", WS_VISIBLE | SS_LEFT, S(23), S(54), S(180), S(20), 0, R_SIDEDIM, s_fontSmall);
    for (int i = 0; i < kPageCount; i++)
        s_nav[i] = Child("BUTTON", kPages[i], WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, S(kNavTop) + i * S(kNavStep),
                         S(kSideW), S(kNavH), ID_NAV0 + i, R_NAV, s_font);
    // bottom left in the sidebar, after the pages in the tab order: start the game through Steam
    s_launch = Child("BUTTON", "Launch game", WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(18), S(kHeight - 48), S(174),
                     S(32), ID_LAUNCH, R_LAUNCH, s_font);
    // the presets page: each preset's button with what it sets beside it, then the way back
    // each row as tall as its text (at most kPresetRowH, which FitHeight counts with)
    const int textX = kContentX + 190, textW = kWidth - textX - 24;
    HDC dc = GetDC(s_wnd);
    HGDIOBJ oldFont = SelectObject(dc, s_font);
    int presetY = S(kRowTop);
    for (int i = 0; i < kPresetCount; i++) {
        RECT t = { 0, 0, S(textW), 0 };
        DrawTextA(dc, kPresets[i].help, -1, &t, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        int textH = t.bottom + S(4), rowH = (textH > S(32) ? textH : S(32)) + S(18);
        if (rowH > S(kPresetRowH)) rowH = S(kPresetRowH);
        s_preset[i] = Child("BUTTON", kPresets[i].name, WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(kContentX), presetY, S(170),
                            S(32), ID_PRESET0 + i, R_PRESET, s_font);
        s_presetText[i] = Child("STATIC", kPresets[i].help, SS_LEFT | SS_NOPREFIX, S(textX), presetY + S(6), S(textW), rowH - S(12), 0,
                                R_DIM, s_font);
        presetY += rowH;
    }
    SelectObject(dc, oldFont);
    ReleaseDC(s_wnd, dc);
    int undoY = presetY + S(8);
    s_undo = Child("BUTTON", "Back to your settings", WS_TABSTOP | BS_OWNERDRAW, S(kContentX), undoY, S(170), S(32), ID_UNDO,
                   R_ACTION, s_font);
    s_undoText = Child("STATIC", "", SS_LEFT | SS_NOPREFIX, S(textX), undoY + S(6), S(textW), S(44), 0, R_DIM, s_font);

    s_title = Child("STATIC", "", WS_VISIBLE | SS_LEFT, S(kContentX), S(26), S(600), S(36), 0, R_TITLE, s_fontTitle);
    s_subtitle = Child("STATIC", "", WS_VISIBLE | SS_LEFT, S(kContentX), S(64), S(610), S(22), 0, R_DIM, s_font);

    int row[kPageCount] = {};
    for (int i = 0; i < kOptionCount; i++) {
        Option& o = kOptions[i];
        int page = 0;
        while (page < kPageCount - 1 && strcmp(kPages[page], PageOf(o))) page++;
        int y = S(kRowTop) + row[page]++ * S(kRowH);
        int id = ID_OPTION0 + i;
        const char* text = o.text;
        int indent = 0;
        while (*text == ' ') text++, indent += S(9);
        // a folder: a shorter label, a wide field and its "..." button at the right
        int labelW = o.kind == PATH ? kPathLabelW : kLabelW;
        o.label = Child("STATIC", text, SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, S(kContentX) + indent, y, S(labelW) - indent,
                        S(26), 0, R_LABEL, s_font);
        if (o.kind == PATH) {
            int fieldX = kContentX + kPathLabelW, browseX = kWidth - 24 - 40;
            o.ctrl = Child("EDIT", "", WS_TABSTOP | ES_AUTOHSCROLL, S(fieldX) + S(12), y + S(5), S(browseX - 8 - fieldX - 24), S(20), id,
                           R_NONE, s_font);
            SendMessageA(o.ctrl, EM_LIMITTEXT, TextLimit(o), 0);
            s_dlcBrowse = Child("BUTTON", "...", WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(browseX), y, S(40), S(28), ID_DLCBROWSE,
                                R_ACTION, s_font);
            // the line below it: the packages found in that folder (a row of its own, kDlcExtraRows)
            s_dlcFound = Child("STATIC", "", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, S(kContentX), y + S(kRowH) + S(2),
                               S(kWidth - kContentX - 24), S(22), 0, R_DIM, s_fontSmall);
            row[page]++;
        } else if (o.kind == BOOL_OPT) {
            o.ctrl = Child("BUTTON", "", WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(ctrlX), y + S(2), S(46), S(22), id, R_TOGGLE, s_font);
        } else if (o.kind == TEXT) {
            // a borderless edit inside a field the window paints (WM_ERASEBKGND), like the lists' fields
            o.ctrl = Child("EDIT", "", WS_TABSTOP | ES_AUTOHSCROLL, S(ctrlX) + S(12), y + S(5), S(226), S(20), id, R_NONE, s_font);
            SendMessageA(o.ctrl, EM_LIMITTEXT, TextLimit(o), 0);
        } else if (o.kind == CHOICE) {
            o.ctrl = Child("BUTTON", "", WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(ctrlX), y, S(250), S(28), id, R_SELECT, s_font);
        } else {
            o.ctrl = Child(kSliderClass, "", WS_TABSTOP, S(ctrlX) - S(6), y, S(200), S(26), id, R_NONE, s_font);
            SendMessageA(o.ctrl, TBM_SETRANGE, TRUE, MAKELPARAM(0, RangeSteps(o)));
            o.value = Child("STATIC", "", SS_LEFT | SS_CENTERIMAGE, S(ctrlX) + S(200), y, S(140), S(26), 0, R_VALUE, s_font);
        }
    }
    // the DLC page: after its options Set up DLC, the single steps as smaller buttons beside it, then the state of the
    // last task (kDlcExtraRows)
    int dlcY = S(kRowTop) + row[kDlcPage] * S(kRowH) + S(10);
    s_dlcSetup = Child("BUTTON", "Set up DLC", WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(kContentX), dlcY, S(150), S(34), ID_DLCSETUP,
                       R_PRIMARY, s_fontBold);
    const int smallX = kContentX + 150 + 24, smallW = 92, smallStep = 100;
    s_dlcBuild = Child("BUTTON", "Build", WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(smallX), dlcY + S(4), S(smallW), S(26),
                       ID_DLCBUILD, R_ACTION, s_fontSmall);
    s_dlcInstall = Child("BUTTON", "Install", WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(smallX + smallStep), dlcY + S(4), S(smallW),
                         S(26), ID_DLCINSTALL, R_ACTION, s_fontSmall);
    s_dlcUninstall = Child("BUTTON", "Uninstall", WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(smallX + 2 * smallStep), dlcY + S(4),
                           S(smallW), S(26), ID_DLCUNINSTALL, R_ACTION, s_fontSmall);
    s_dlcLog = Child("BUTTON", "Open log", WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, S(smallX + 3 * smallStep), dlcY + S(4), S(smallW),
                     S(26), ID_DLCLOG, R_ACTION, s_fontSmall);
    s_dlcState = Child("STATIC", s_dlcText.c_str(), SS_LEFT | SS_NOPREFIX, S(kContentX), dlcY + S(46), S(kWidth - kContentX - 24),
                       S(66), 0, R_LABEL, s_font);
    DlcButtons();
    s_info = Child("STATIC", "", WS_VISIBLE | SS_LEFT, S(kContentX), S(kHeight - 122), S(kWidth - kContentX - 24), S(72), 0,
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

static bool PickIni(HWND owner, char* picked = s_path)
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
    strcpy(picked, file);
    return true;
}

static bool Open(const char* path, bool viaSteam)
{
    IniLock lock;
    if (!lock.ok()) {
        Status(lock.error());
        return false;
    }
    if (path) {
        strcpy(s_path, path);
        s_viaSteam = viaSteam;
    }
    s_backedUp = false;
    s_opened.clear();
    if (LoadIni()) s_opened = s_lines;
    const char* steam = s_viaSteam ? " (found through Steam)" : "";
    if (s_opened.empty()) {
        s_lines.clear();
        std::string msg = std::string("No SplitSecondPad.ini here yet: it is created with the first change.") + steam;
        Status(msg.c_str());
    } else {
        std::string msg = std::string("Editing ") + s_path + steam;
        Status(msg.c_str());
    }
    LoadControls();
    UpdateUndo();
    return true;
}

// Restore the backed-up settings while keeping the current lap choice (including an omitted Laps key).
static bool RestoreSettings(const char* backup)
{
    if (!Refresh(false)) return false;
    const Option& laps = *Find("Laps");
    int current = FindKey(laps.section, laps.key, NULL);
    if (current < 0) current = FindKey("Game", laps.key, NULL);  // older INIs
    std::string value = current >= 0 ? ValueAt(current) : "";
    std::vector<std::string> live;
    live.swap(s_lines);
    bool crlf = s_crlf;
    if (LoadIni(backup)) {
        for (const char* section : { laps.section, "Game" }) {
            int line;
            while ((line = FindKey(section, laps.key, NULL)) >= 0) s_lines.erase(s_lines.begin() + line);
        }
        if (current >= 0) SetValue(laps, value);
        if (SaveLines(s_path, s_lines)) return true;
    }
    s_lines.swap(live);
    s_crlf = crlf;
    return false;
}

static void RestoreBackup(HWND wnd)
{
    std::string bak = BackupPath();
    if (GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxA(wnd, "There is no backup yet. It is made before the first change.", "glimpse_fix", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string q = "Restore the backed-up settings and keep your current lap choice?\n\n" + bak;
    if (MessageBoxA(wnd, q.c_str(), "glimpse_fix", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    IniLock lock;
    if (!lock.ok()) {
        Status(lock.error());
        return;
    }
    if (!RestoreSettings(bak.c_str())) {
        Status("Could not restore the backup");
        return;
    }
    Open();
    s_backedUp = true;  // keep the backup as it is
    Status("Backup restored");
}

// ---------------------------------------------------------------- drawing

// the keyboard focus: an outline around the control
static void FocusOutline(HDC dc, RECT r, int radius)
{
    HPEN p = CreatePen(PS_SOLID, 1, kText);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH)), op = SelectObject(dc, p);
    ::RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
}

static void DrawButton(const DRAWITEMSTRUCT* di)
{
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    Role role = (Role)GetWindowLongPtrA(di->hwndItem, GWLP_USERDATA);
    bool pressed = (di->itemState & ODS_SELECTED) != 0, disabled = (di->itemState & ODS_DISABLED) != 0;
    bool focus = (di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT);
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
        if (focus) FocusOutline(dc, r, S(22));
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
        if (focus) {
            RECT f = { r.left + S(8), r.top + S(4), r.right - S(8), r.bottom - S(4) };
            FocusOutline(dc, f, S(8));
        }
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
        if (focus) FocusOutline(dc, r, S(8));
        return;
    }
    if (role == R_PRIMARY) {  // Set up DLC: a filled pill in the accent colour, dark text
        FillRect(dc, &r, s_backBrush);
        COLORREF fill = disabled ? kField : pressed ? RGB(215, 98, 12) : kAccent;
        RoundRect(dc, r, S(10), fill, fill);
        SelectObject(dc, s_fontBold);
        SetTextColor(dc, disabled ? kOff : kBack);
        DrawTextA(dc, text, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        if (focus) {
            RECT f = { r.left + S(3), r.top + S(3), r.right - S(3), r.bottom - S(3) };
            FocusOutline(dc, f, S(8));
        }
        return;
    }
    // presets and actions: outlined pills, filled while pressed (the launch button on the sidebar)
    FillRect(dc, &r, role == R_LAUNCH ? s_sideBrush : s_backBrush);
    bool current = role == R_PRESET && PresetMatches(kPresets[GetDlgCtrlID(di->hwndItem) - ID_PRESET0]);
    COLORREF line = current || role == R_LAUNCH ? kAccent : disabled ? kField : kBorder;
    RoundRect(dc, r, S(10), pressed ? kSideSel : role == R_LAUNCH ? kSide : kField, line);
    SelectObject(dc, current ? s_fontBold : (HFONT)SendMessageA(di->hwndItem, WM_GETFONT, 0, 0));
    SetTextColor(dc, disabled ? kOff : kText);
    DrawTextA(dc, text, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    if (focus) {
        RECT f = { r.left + S(3), r.top + S(3), r.right - S(3), r.bottom - S(3) };
        FocusOutline(dc, f, S(8));
    }
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

static HWND s_sliderDragHwnd;
static int s_sliderDragStart;
static int s_sliderDragOffset;
static bool s_sliderDragChanged;

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
    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lp), x0, x1, max = SliderMax(h), pos = SliderPos(h);
        SliderTrack(h, &x0, &x1);
        int thumbX = max > 0 ? x0 + (int)((double)pos / max * (x1 - x0) + 0.5) : x0;
        int radius = S(8);
        s_sliderDragHwnd = h;
        s_sliderDragStart = pos;
        s_sliderDragChanged = false;
        SetFocus(h);
        SetCapture(h);
        if (mx >= thumbX - radius && mx <= thumbX + radius) {
            s_sliderDragOffset = mx - thumbX;
        } else {
            s_sliderDragOffset = 0;
            SliderFromMouse(h, mx);
            if (SliderPos(h) != s_sliderDragStart) s_sliderDragChanged = true;
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == h) {
            SliderFromMouse(h, (short)LOWORD(lp) - s_sliderDragOffset);
            if (SliderPos(h) != s_sliderDragStart) s_sliderDragChanged = true;
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) ReleaseCapture();  // ends the drag through WM_CAPTURECHANGED
        return 0;
    case WM_CAPTURECHANGED:  // the drag ended, also when the capture was taken away
        if (s_sliderDragHwnd == h) {
            bool changed = s_sliderDragChanged;
            s_sliderDragHwnd = NULL;
            s_sliderDragOffset = 0;
            s_sliderDragChanged = false;
            if (changed) SendMessageA(GetParent(h), WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, SliderPos(h)), (LPARAM)h);
        }
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
        int was = SliderPos(h);
        SliderSet(h, pos, true);
        if (SliderPos(h) != was) SendMessageA(GetParent(h), WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, SliderPos(h)), (LPARAM)h);
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
    if (hwnd == s_launch && s_launch) {
        s_infoWarn = false;
        SetWindowTextA(s_info, "Starts Split/Second through Steam. Every change here is saved already.");
        InvalidateRect(s_info, NULL, TRUE);
        return;
    }
    struct { HWND h; const char* text; } dlc[] = {
        { s_dlcBrowse, "The Split/Second DLC found on this PC to pick from, or another folder (425607E7, its subfolder "
                       "00000002, or a folder with the package files)." },
        { s_dlcSetup, "All in one go: finds your Xbox DLC and the game, gets the DLC tools if they are missing (asks first), "
                      "converts the DLC on this PC (a few minutes, about 10 the first time; this window can be used meanwhile), "
                      "shows what it will add and, after your Yes, installs it into glimpse_dlc in the game folder (your saves "
                      "are backed up first) and turns on the settings it needs." },
        { s_dlcBuild, "Single step: converts your DLC on this PC with Python (tools\\dlc\\build_all.py). The result goes to "
                      "%LOCALAPPDATA%\\glimpse_fix\\dlc_staging; the game folder is not changed. Nothing is sent anywhere." },
        { s_dlcInstall, "Single step: shows what it will do, then copies the converted files into glimpse_dlc in the game "
                        "folder, after a copy of your saves (Split Second backup_<date>_dlc). It only ever replaces or removes "
                        "files it installed itself." },
        { s_dlcUninstall, "Shows what it will do, then removes the files Install copied into glimpse_dlc (after a copy of your "
                          "saves) and puts back what Install moved aside. Files that were there before stay." },
        { s_dlcLog, "The log of the last build, install, uninstall or download." },
    };
    for (auto& d : dlc)
        if (hwnd == d.h && d.h) {
            s_infoWarn = false;
            SetWindowTextA(s_info, d.text);
            InvalidateRect(s_info, NULL, TRUE);
            return;
        }
    for (Option& o : kOptions)
        if (hwnd == o.ctrl || hwnd == o.label || hwnd == o.value) {
            // a help text starting with '!' is a warning, shown in red
            s_infoWarn = o.help[0] == '!';
            SetWindowTextA(s_info, o.help + s_infoWarn);
            InvalidateRect(s_info, NULL, TRUE);
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
        RECT sep = { S(kContentX), S(kHeight - 130), r.right - S(24), S(kHeight - 129) };
        HBRUSH b = CreateSolidBrush(kBorder);
        FillRect((HDC)wp, &sep, b);
        DeleteObject(b);
        for (int i = 0; i < kOptionCount; i++) {  // the fields of the text options on this page
            const Option& o = kOptions[i];
            if (!IsText(o) || !o.ctrl || !IsWindowVisible(o.ctrl)) continue;
            RECT f;
            GetWindowRect(o.ctrl, &f);
            MapWindowPoints(NULL, wnd, (POINT*)&f, 2);
            InflateRect(&f, S(12), S(5));
            RoundRect((HDC)wp, f, S(6), kField, kBorder);
        }
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
            if (IsText(o) && code == EN_CHANGE && !s_loading) {
                // only the characters the option may have; the rest is dropped as it is typed
                char buf[512], clean[512];
                GetWindowTextA(o.ctrl, buf, sizeof(buf));
                int n = 0;
                for (const char* c = buf; *c; c++)
                    if (TextCharOk(o, *c)) clean[n++] = *c;
                clean[n] = 0;
                if (strcmp(buf, clean)) {
                    DWORD at = 0;
                    SendMessageA(o.ctrl, EM_GETSEL, (WPARAM)&at, 0);
                    s_loading = true;
                    SetWindowTextA(o.ctrl, clean);
                    s_loading = false;
                    int back = (int)strlen(buf) - n;
                    SendMessageA(o.ctrl, EM_SETSEL, at > (DWORD)back ? at - back : 0, at > (DWORD)back ? at - back : 0);
                }
                Changed(o);
                if (!strcmp(o.key, "DumpFolder")) SetTimer(wnd, kFoundTimer, 400, NULL);  // the packages line, once typing pauses
            }
            if (code == BN_SETFOCUS || (IsText(o) && code == EN_SETFOCUS)) ShowHelpFor(o.ctrl);
        } else if (id >= ID_NAV0 && id < ID_NAV0 + kPageCount) {
            ShowPage(id - ID_NAV0);
        } else if (id >= ID_PRESET0 && id < ID_PRESET0 + kPresetCount && code == BN_CLICKED) {
            char q[200];
            sprintf(q, "Set every option to \"%s\"?", kPresets[id - ID_PRESET0].name);
            if (MessageBoxA(wnd, q, "glimpse_fix", MB_YESNO | MB_ICONQUESTION) == IDYES) ApplyPreset(kPresets[id - ID_PRESET0]);
        } else if (id == ID_LAUNCH && code == BN_CLICKED) {
            // Steam's own link for the game (app 297860); Steam starts it as from the library
            INT_PTR r = (INT_PTR)ShellExecuteA(wnd, "open", "steam://rungameid/297860", NULL, NULL, SW_SHOWNORMAL);
            Status(r > 32 ? "Starting Split/Second through Steam" : "Could not open Steam (is it installed?)");
        } else if (id == ID_LAUNCH && code == BN_SETFOCUS) {
            ShowHelpFor(s_launch);
        } else if (((id >= ID_DLCBROWSE && id <= ID_DLCUNINSTALL) || id == ID_DLCSETUP) && code == BN_SETFOCUS) {
            ShowHelpFor((HWND)lp);
        } else if (id == ID_DLCBROWSE && code == BN_CLICKED) {
            BrowseDlcFolder(wnd);
        } else if (id == ID_DLCSETUP && code == BN_CLICKED) {
            DlcSetup(wnd);
        } else if (id == ID_DLCBUILD && code == BN_CLICKED) {
            DlcBuild(wnd);
        } else if (id == ID_DLCINSTALL && code == BN_CLICKED) {
            DlcInstall(wnd, false);
        } else if (id == ID_DLCUNINSTALL && code == BN_CLICKED) {
            DlcUninstall(wnd, false);
        } else if (id == ID_DLCLOG && code == BN_CLICKED) {
            OpenDlcLog(wnd);
        } else if (id == ID_UNDO && code == BN_CLICKED) {
            RestoreUndo(wnd);
        } else if (id == ID_RESTORE) {
            RestoreBackup(wnd);
        } else if (id == ID_OPEN) {
            if (DlcBusy()) {
                Status("Not while a DLC task runs");
            } else {
                char picked[MAX_PATH];
                if (PickIni(wnd, picked) && Open(picked)) {
                    CheckLargeAddressAware(wnd);
                    if (s_page == kDlcPage) ShowPage(s_page);
                }
            }
        } else if (id == ID_CLOSE) {
            SendMessageA(wnd, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == kDlcTimer) DlcPoll(wnd);
        if (wp == kFoundTimer) {
            KillTimer(wnd, kFoundTimer);
            DlcShowFound();
        }
        return 0;
    case WM_CLOSE:
        // a DLC build, the download or a dry run may be stopped (a build keeps its finished steps); an install or
        // uninstall is let finish; the DirectX installer runs on in its own window
        if (s_task == DLC_INSTALL || s_task == DLC_UNINSTALL) {
            MessageBoxA(wnd, s_task == DLC_INSTALL ? "The DLC is being installed. Please wait until it is done."
                                                   : "The DLC is being removed. Please wait until it is done.",
                        "glimpse_fix", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (DlcBusy() && s_task != DLC_DEPS) {
            const char* q = s_task == DLC_BUILD      ? "The DLC build is still running. Stop it and close?"
                            : s_task == DLC_DOWNLOAD ? "The DLC tools are still being downloaded. Stop and close?"
                                                     : "The DLC install is still being checked (nothing has been changed yet). "
                                                       "Stop and close?";
            if (MessageBoxA(wnd, q, "glimpse_fix", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            // the task may have ended while the question was open, and Set up DLC gone on with the next one
            if (s_task == DLC_INSTALL || s_task == DLC_UNINSTALL) {
                MessageBoxA(wnd, "The DLC is being installed or removed now. Please wait until it is done.", "glimpse_fix",
                            MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            DlcStop();
            if (s_task == DLC_DOWNLOAD && WaitForSingleObject(s_taskProc, 10000) == WAIT_TIMEOUT) {
                // not in the middle of putting the tools in place (a Stop does not end that): wait for it
                EnterCriticalSection(&s_dl.cs);
                bool placing = s_dl.step == "Installing";
                LeaveCriticalSection(&s_dl.cs);
                if (placing) {
                    SetCursor(LoadCursor(NULL, IDC_WAIT));
                    WaitForSingleObject(s_taskProc, INFINITE);
                }
            }
        }
        DestroyWindow(wnd);
        return 0;
    case WM_HSCROLL:
        // while dragging only the value shown follows; the INI is written once the drag ends
        for (Option& o : kOptions)
            if (o.ctrl == (HWND)lp) {
                if (LOWORD(wp) == TB_ENDTRACK) Changed(o);
                else ShowValue(o, o.lo + SendMessageA(o.ctrl, TBM_GETPOS, 0, 0) * o.step);
                ShowHelpFor(o.ctrl);
            }
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, kText);
        SetBkColor(dc, kField);
        return (LRESULT)s_fieldBrush;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        HWND c = (HWND)lp;
        char cls[16];
        if (GetClassNameA(c, cls, sizeof(cls)) && !_stricmp(cls, "EDIT")) {  // a disabled text field
            SetTextColor(dc, kOff);
            SetBkColor(dc, kField);
            return (LRESULT)s_fieldBrush;
        }
        Role role = (Role)GetWindowLongPtrA(c, GWLP_USERDATA);
        bool side = role == R_BRAND || role == R_SIDEDIM;
        SetBkMode(dc, TRANSPARENT);
        COLORREF col = kText;
        if (role == R_BRAND) col = kAccent;
        else if (c == s_info && s_infoWarn) col = kWarn;
        else if (c == s_dlcState && s_dlcWarn) col = kWarn;
        else if (role == R_DIM || role == R_SIDEDIM) col = kDim;
        else if (role == R_VALUE || role == R_LABEL) {
            bool dim = false;
            for (int i = 0; i < kOptionCount; i++)
                if (kOptions[i].label == c || kOptions[i].value == c) dim = s_dim[i];
            col = dim ? kOff : role == R_VALUE ? kAccent : kText;
        }
        SetTextColor(dc, col);
        return (LRESULT)(side ? s_sideBrush : s_backBrush);
    }
    case WM_DPICHANGED: {
        s_dpi = HIWORD(wp);
        MakeFonts();
        EnumChildWindows(wnd, DestroyChild, 0);
        for (Option& o : kOptions) o.label = o.ctrl = o.value = NULL;
        s_undo = s_undoText = s_launch = NULL;
        s_dlcBrowse = s_dlcFound = s_dlcSetup = s_dlcBuild = s_dlcInstall = s_dlcUninstall = s_dlcLog = s_dlcState = NULL;
        CreateControls();
        DlcButtons();
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
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);  // the folder picker
    s_dpi = WindowDpi(NULL);
    EnableDarkMenus();
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    RegisterSlider(inst);
    s_backBrush = CreateSolidBrush(kBack);
    s_sideBrush = CreateSolidBrush(kSide);
    s_fieldBrush = CreateSolidBrush(kField);

    InitializeCriticalSection(&s_dl.cs);
    SetErrorMode(SEM_FAILCRITICALERRORS);  // no "insert a disk" windows while the DLC search looks at the drives

    // the INI next to this program when it is in the game folder or has an INI next to it (as before); else the one
    // in the game folder Steam has (only read here), else ask
    GetModuleFileNameA(NULL, s_path, MAX_PATH);
    char* slash = strrchr(s_path, '\\');
    strcpy(slash ? slash + 1 : s_path, "SplitSecondPad.ini");
    if (!FileExists(GameDir() + "\\SplitSecond.exe") && GetFileAttributesA(s_path) == INVALID_FILE_ATTRIBUTES) {
        std::string steam = SteamGameDir();
        if (!steam.empty() && steam.size() + 24 < MAX_PATH) {
            strcpy(s_path, (steam + "\\SplitSecondPad.ini").c_str());
            s_viaSteam = true;
        } else if (GetFileAttributesA(s_path) == INVALID_FILE_ATTRIBUTES && !PickIni(NULL)) {
            return 0;
        }
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
    FitHeight();
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
    UpdateWindow(s_wnd);
    CheckLargeAddressAware(s_wnd);

    MSG m;
    while (GetMessageA(&m, NULL, 0, 0)) {
        if (IsDialogMessageA(s_wnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
