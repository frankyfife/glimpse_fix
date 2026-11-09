// Split/Second PC fix - post processing: motion blur strength, vignette
//
// The screen space motion blur (0x49B490) blurs along the camera motion of one frame. Each camera
// enables it (Cameras/Data/*/SSMotionBlurEnabled, needs graphics quality High or Very High) with
// a blur amount. At 60 fps a frame covers half the motion, so the amount is doubled to keep the
// look of the 30 fps game; MotionBlurStrength scales it further.
#include "common.h"
#include <stdlib.h>
#include <string.h>

// screen space motion blur pass (thiscall, 5 stack args, ret 14h); returns at once unless [[this+4]+8] is set.
// It is only called through its vtable slot, which is swapped instead of patching the code: code
// patches here set off SecuROM's integrity checks.
static BYTE* const kMotionBlurRender = (BYTE*)0x0049B490;
static void** const kMotionBlurSlot = (void**)0x00BB42B0;
typedef void(__thiscall* MotionBlurRender_t)(void* self, void* a, void* b, void* c, void* d, void* e);
static MotionBlurRender_t s_origMotionBlur;
static float s_blurScale = 1.0f;

static void __fastcall Hook_MotionBlurRender(BYTE* self, void*, void* a, void* b, void* c, void* d, void* e)
{
    BYTE* state = *(BYTE**)(self + 4);
    BYTE* params = *(BYTE**)(self + 0x14);
    float* amount = params ? (float*)(params + 0x14) : NULL;
    if (g_cfg.log) {
        static unsigned calls;
        if (calls++ % 600 == 0)
            Log("motion blur: enabled=%d amount=%.3f level=%d quality=%d", state ? state[8] : -1, amount ? *amount : -1.0f,
                params ? *(int*)(params + 0x20) : -1, *(int*)0x00D0F7E8);
    }
    float scale = s_blurScale * BoostBlurScale();
    BeginEdgeClamp();  // samples past the picture's edge would bring in a line at the bottom (diag.cpp)
    if (!amount || scale == 1.0f) {
        s_origMotionBlur(self, a, b, c, d, e);
    } else {
        float stock = *amount;
        *amount = stock * scale;
        s_origMotionBlur(self, a, b, c, d, e);
        *amount = stock;
    }
    EndEdgeClamp();
}

void PatchMotionBlur()
{
    s_blurScale = g_cfg.motionBlurStrength * (g_cfg.frameRate == 60 ? 2.0f : 1.0f);
    if (s_blurScale == 1.0f && !g_cfg.log && !g_cfg.boost) return;
    if (memcmp(kMotionBlurRender, "\x83\xEC\x68\x57\x8B\xF9", 6) || *kMotionBlurSlot != kMotionBlurRender) {
        Log("motion blur pass not recognised");
        return;
    }
    s_origMotionBlur = (MotionBlurRender_t)kMotionBlurRender;
    void* hook = (void*)Hook_MotionBlurRender;
    WriteMemory(kMotionBlurSlot, &hook, sizeof(hook));
    Log("motion blur scale %.2f", s_blurScale);
}

// Colour grading, gradient and vignette pass (0x49A7A0, same calling convention, vtable slot
// 0xBB4048). Its parameter block at this+0x14 holds the camera's vignette (Cameras/Data/*/Vignette*):
// colour rgb at +0x38, then U scale, V scale, inner and outer distance at +0x48..+0x54. With both
// scales at 0 every pixel sits at the centre, inside the inner distance, so nothing is darkened
// while the grading and gradient stay as they are. The gradient (the translucent blue band at the top
// of the screen) has its colour rgba at +0x1C..+0x2C; Gradient=0 sets its alpha to 0 for the call.
static BYTE* const kGradeRender = (BYTE*)0x0049A7A0;
static void** const kGradeSlot = (void**)0x00BB4048;
static MotionBlurRender_t s_origGrade;

static void __fastcall Hook_GradeRender(BYTE* self, void*, void* a, void* b, void* c, void* d, void* e)
{
    BYTE* params = *(BYTE**)(self + 0x14);
    float* vig = params ? (float*)(params + 0x48) : NULL;
    if (g_cfg.log && vig) {
        static unsigned calls;
        if (calls++ % 600 == 0)
            Log("vignette: colour %.2f %.2f %.2f  scale %.2f %.2f  inner %.2f outer %.2f | gradient colour %.2f %.2f %.2f %.2f"
                "  params %.2f %.2f", *(float*)(params + 0x38), *(float*)(params + 0x3C), *(float*)(params + 0x40), vig[0],
                vig[1], vig[2], vig[3], *(float*)(params + 0x1C), *(float*)(params + 0x20), *(float*)(params + 0x24),
                *(float*)(params + 0x2C), *(float*)(params + 0x30), *(float*)(params + 0x34));
    }
    if (!vig) {
        s_origGrade(self, a, b, c, d, e);
        return;
    }
    float* alpha = (float*)(params + 0x2C);
    float u = vig[0], v = vig[1], gradient = *alpha;
    if (!g_cfg.vignette) vig[0] = vig[1] = 0.0f;
    if (!g_cfg.gradient) *alpha = 0.0f;
    s_origGrade(self, a, b, c, d, e);
    vig[0] = u;
    vig[1] = v;
    *alpha = gradient;
}

void PatchVignette()
{
    if (g_cfg.vignette && g_cfg.gradient) return;
    if (memcmp(kGradeRender, "\x83\xEC\x08\x57\x8B\xF9", 6) || *kGradeSlot != kGradeRender) {
        Log("vignette pass not recognised");
        return;
    }
    s_origGrade = (MotionBlurRender_t)kGradeRender;
    void* hook = (void*)Hook_GradeRender;
    WriteMemory(kGradeSlot, &hook, sizeof(hook));
    Log("vignette %s, gradient %s", g_cfg.vignette ? "on" : "off", g_cfg.gradient ? "on" : "off");
}

// Bloom (0x492F50, cSSBloomEffect vtable slot 0xBB3678). The same pass also tone maps the frame; its
// parameter block at this+0x14 picks the shader with three flags: +0x38 bloom added, +0xC tone mapper,
// +0x14 filmic tone mapper. Bloom=0 clears +0x38 for the call, so the frame is still tone mapped.
// Particle flares (0x496C60, cSSParticleFlareEffect slot 0xBB39B8) run only while the effect's
// state flag [[this+4]+8] is set, as cameras switch it; ParticleFlare=0 clears it for the call.
static BYTE* const kBloomRender = (BYTE*)0x00492F50;
static void** const kBloomSlot = (void**)0x00BB3678;
static BYTE* const kParticleFlareRender = (BYTE*)0x00496C60;
static void** const kParticleFlareSlot = (void**)0x00BB39B8;
static MotionBlurRender_t s_origBloom, s_origParticleFlare;

// Bloom look: the pass feeds its shaders from the parameter block. [Graphics] BloomStrength scales
// the bright pass value at +0x3C (more or less glow on bright areas), BloomTone the one at +0x40,
// BloomCurve the add curve (+0x58, at least 1), Exposure the bright pass and tone mapper exposure
// (+0x20, the whole picture). They multiply the camera's values for the call and are read from the
// INI every 2 s, so they can be changed while the game runs.
static const DWORD kBloomOffsets[4] = { 0x3C, 0x40, 0x58, 0x20 };
static const char* const kBloomKeys[4] = { "BloomStrength", "BloomTone", "BloomCurve", "Exposure" };
static float s_bloomScale[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

static void ReadBloomSettings()
{
    static DWORD last;
    DWORD now = GetTickCount();
    if (last && now - last < 2000) return;
    last = now;
    char ini[MAX_PATH], buf[32];
    ModulePath(ini, "SplitSecondPad.ini");
    for (int i = 0; i < 4; i++) {
        GetPrivateProfileStringA("Graphics", kBloomKeys[i], "1.0", buf, sizeof(buf), ini);
        float v = (float)atof(buf);
        v = v < 0.0f ? 0.0f : v > 10.0f ? 10.0f : v;
        if (v != s_bloomScale[i]) Log("%s %.2f", kBloomKeys[i], v);
        s_bloomScale[i] = v;
    }
}

static void PhotoDofSwitch();  // below

static void __fastcall Hook_BloomRender(BYTE* self, void*, void* a, void* b, void* c, void* d, void* e)
{
    BYTE* params = *(BYTE**)(self + 0x14);
    ReadBloomSettings();
    PhotoDofSwitch();
    if (g_cfg.log && params) {
        static unsigned calls;
        if (calls++ % 600 == 0) {
            const float* f = (const float*)params;
            Log("bloom: add=%d tonemap=%d filmic=%d | exposure %.2f x %.2f  strength %.2f  tone %.2f  curve %.2f",
                params[0x38], params[0xC], params[0x14], f[0x20 / 4], f[0x74 / 4], f[0x3C / 4], f[0x40 / 4], f[0x58 / 4]);
        }
    }
    if (!params) {
        s_origBloom(self, a, b, c, d, e);
        return;
    }
    BYTE add = params[0x38];
    float stock[4];
    for (int i = 0; i < 4; i++) {
        float* v = (float*)(params + kBloomOffsets[i]);
        stock[i] = *v;
        *v = stock[i] * s_bloomScale[i] * (i == 0 ? g_photoBloom : i == 3 ? g_photoExposure : 1.0f);
    }
    // photo mode: bloom down to 0 switches it off, any other value than 1 switches it on
    if (g_photoBloom < 0.05f || (!g_cfg.bloom && g_photoBloom == 1.0f)) params[0x38] = 0;
    else if (g_photoBloom != 1.0f) params[0x38] = 1;
    s_origBloom(self, a, b, c, d, e);
    for (int i = 0; i < 4; i++) *(float*)(params + kBloomOffsets[i]) = stock[i];
    params[0x38] = add;
}

static void __fastcall Hook_ParticleFlareRender(BYTE* self, void*, void* a, void* b, void* c, void* d, void* e)
{
    BYTE* state = *(BYTE**)(self + 4);
    if (g_cfg.particleFlare || !state) {
        s_origParticleFlare(self, a, b, c, d, e);
        return;
    }
    BYTE on = state[8];
    state[8] = 0;
    s_origParticleFlare(self, a, b, c, d, e);
    state[8] = on;
}

void PatchBloomAndFlares()
{
    {
        if (memcmp(kBloomRender, "\x56\x8B\xF1\x8B\x46\x04\x80\x78\x08\x00", 10) || *kBloomSlot != kBloomRender) {
            Log("bloom pass not recognised");
        } else {
            s_origBloom = (MotionBlurRender_t)kBloomRender;
            void* hook = (void*)Hook_BloomRender;
            WriteMemory(kBloomSlot, &hook, sizeof(hook));
            Log(g_cfg.bloom ? "bloom pass hooked" : "bloom off");
        }
    }
    if (!g_cfg.particleFlare) {
        if (memcmp(kParticleFlareRender, "\x81\xEC\xD8\x00\x00\x00\x56\x8B\xF1\x8B", 10) ||
            *kParticleFlareSlot != kParticleFlareRender) {
            Log("particle flare pass not recognised");
        } else {
            s_origParticleFlare = (MotionBlurRender_t)kParticleFlareRender;
            void* hook = (void*)Hook_ParticleFlareRender;
            WriteMemory(kParticleFlareSlot, &hook, sizeof(hook));
            Log("particle flares off");
        }
    }
}

float g_photoBloom = 1.0f, g_photoExposure = 1.0f, g_photoDof = 0.0f, g_photoFocus = 15.0f;

// Depth of field (cDepthOfFieldEffect, render 0xADFE60 through its vtable slot 0xC645F8, same calling
// convention). It runs while its parameter block [this+4] has +8 set; the block holds the four focus
// distances in metres at +0x1B4 (blurred in front up to there, sharp from +0x1B8 to +0x1BC, blurred
// again from +0x1C0), the blend amount at +0x1C4 and the blur kernel from +0x10, which the game's own
// 0xADF430 (thiscall, the blur amount) fills. The post processing settings (0x48D8F0, "/DoF/a; Enabled"
// and so on) leave it off during races, and the chain (0x493BF0: bloom, colour cube, depth of field,
// ..., motion blur) only calls a pass whose +8 is set. So in photo mode the bloom hook, which the chain
// calls just before, sets +8 of the depth of field block (post processing manager [[0xD714D0]+0x120],
// its chain at +0xB50, the pass at chain+4, its block at pass+4); the hook fills the block for the call
// and puts all of the game's values back after it.
static BYTE* const kDofRender = (BYTE*)0x00ADFE60;
static void** const kDofSlot = (void**)0x00C645F8;
typedef void(__thiscall* DofSetBlur_t)(void* params, float amount);
static const DofSetBlur_t kDofSetBlur = (DofSetBlur_t)0x00ADF430;
static MotionBlurRender_t s_origDof;
static BYTE* s_dofSwitched;  // the block whose +8 the bloom hook set, with the game's value
static BYTE s_dofWas;

static bool PhotoDofWanted() { return g_photoDof > 0.0f && s_origDof && *(int*)0x00D0F7E8 != 0; }

static BYTE* DofParams()
{
    __try {
        BYTE* pp = *(BYTE**)0x00D714D0;
        BYTE* mgr = pp ? *(BYTE**)(pp + 0x120) : NULL;
        BYTE* chain = mgr ? *(BYTE**)(mgr + 0xB50) : NULL;
        BYTE* pass = chain ? *(BYTE**)(chain + 4) : NULL;
        if (!pass || *(DWORD*)pass != 0x00C645F0) return NULL;
        return *(BYTE**)(pass + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

static void PhotoDofSwitch()
{
    if (s_dofSwitched) {  // the pass did not run last time
        s_dofSwitched[8] = s_dofWas;
        s_dofSwitched = NULL;
    }
    if (!PhotoDofWanted()) return;
    BYTE* params = DofParams();
    if (!params || !*(void**)(params + 0x1CC)) {
        static bool logged;
        if (!logged) Log("depth of field: pass %s", params ? "has no view" : "not found");
        logged = true;
        return;
    }
    s_dofSwitched = params;
    s_dofWas = params[8];
    params[8] = 1;
}

static void __fastcall Hook_DofRender(BYTE* self, void*, void* a, void* b, void* c, void* d, void* e)
{
    BYTE* params = *(BYTE**)(self + 4);
    // not at the lowest graphics quality, where the game never runs it
    if (!PhotoDofWanted() || !params || !*(void**)(params + 0x1CC)) {
        s_origDof(self, a, b, c, d, e);
        if (params && params == s_dofSwitched) params[8] = s_dofWas;
        s_dofSwitched = NULL;
        return;
    }
    static BYTE saved[0x1C8];
    memcpy(saved, params + 8, sizeof(saved) - 8);
    float f = g_photoFocus;
    params[8] = 1;
    kDofSetBlur(params, 1.0f + 5.0f * g_photoDof);
    float* p = (float*)(params + 0x1B4);
    p[0] = f * 0.4f;
    p[1] = f * 0.85f;
    p[2] = f * 1.2f;
    p[3] = f * 3.0f;
    p[4] = g_photoDof;
    p[5] = 0.0f;
    static bool logged;
    if (!logged) Log("depth of field: on for the photo mode");
    logged = true;
    s_origDof(self, a, b, c, d, e);
    memcpy(params + 8, saved, sizeof(saved) - 8);
    if (params == s_dofSwitched) params[8] = s_dofWas;
    s_dofSwitched = NULL;
}

void PatchDepthOfField()
{
    if (!g_cfg.photoMode) return;
    if (memcmp(kDofRender, "\x56\x8B\xF1\x8B\x46\x04\x80\x78\x08\x00", 10) || *kDofSlot != kDofRender) {
        Log("depth of field pass not recognised");
        return;
    }
    s_origDof = (MotionBlurRender_t)kDofRender;
    void* hook = (void*)Hook_DofRender;
    WriteMemory(kDofSlot, &hook, sizeof(hook));
    Log("depth of field pass hooked (photo mode)");
}

// Lens flares: the screen space lens flare pass (0x499FB0) only runs while the tweakable
// "PostProcess/SSLensFlare/z; Enable" (0xD10A3C) is set, the sun and light flares while
// "LensFlare/Config/a; Enable" (0xD37D08) is. Levels may load them, so they are set every frame.
void ApplyLensFlare()
{
    if (g_cfg.lensFlare) return;
    *(BYTE*)0x00D10A3C = 0;
    *(BYTE*)0x00D37D08 = 0;
}
