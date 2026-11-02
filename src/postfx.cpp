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
    if (!amount || scale == 1.0f) {
        s_origMotionBlur(self, a, b, c, d, e);
        return;
    }
    float stock = *amount;
    *amount = stock * scale;
    s_origMotionBlur(self, a, b, c, d, e);
    *amount = stock;
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
// while the grading and gradient stay as they are.
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
    float u = vig[0], v = vig[1];
    vig[0] = vig[1] = 0.0f;
    s_origGrade(self, a, b, c, d, e);
    vig[0] = u;
    vig[1] = v;
}

void PatchVignette()
{
    if (g_cfg.vignette) return;
    if (memcmp(kGradeRender, "\x83\xEC\x08\x57\x8B\xF9", 6) || *kGradeSlot != kGradeRender) {
        Log("vignette pass not recognised");
        return;
    }
    s_origGrade = (MotionBlurRender_t)kGradeRender;
    void* hook = (void*)Hook_GradeRender;
    WriteMemory(kGradeSlot, &hook, sizeof(hook));
    Log("vignette off");
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

static void __fastcall Hook_BloomRender(BYTE* self, void*, void* a, void* b, void* c, void* d, void* e)
{
    BYTE* params = *(BYTE**)(self + 0x14);
    ReadBloomSettings();
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
        *v = stock[i] * s_bloomScale[i];
    }
    if (!g_cfg.bloom) params[0x38] = 0;
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

// Lens flares: the screen space lens flare pass (0x499FB0) only runs while the tweakable
// "PostProcess/SSLensFlare/z; Enable" (0xD10A3C) is set, the sun and light flares while
// "LensFlare/Config/a; Enable" (0xD37D08) is. Levels may load them, so they are set every frame.
void ApplyLensFlare()
{
    if (g_cfg.lensFlare) return;
    *(BYTE*)0x00D10A3C = 0;
    *(BYTE*)0x00D37D08 = 0;
}
