// Split/Second PC fix - post processing: motion blur strength, vignette
//
// The screen space motion blur (0x49B490) blurs along the camera motion of one frame. Each camera
// enables it (Cameras/Data/*/SSMotionBlurEnabled, needs graphics quality High or Very High) with
// a blur amount. At 60 fps a frame covers half the motion, so the amount is doubled to keep the
// look of the 30 fps game; MotionBlurStrength scales it further.
#include "common.h"
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
    if (!amount || s_blurScale == 1.0f) {
        s_origMotionBlur(self, a, b, c, d, e);
        return;
    }
    float stock = *amount;
    *amount = stock * s_blurScale;
    s_origMotionBlur(self, a, b, c, d, e);
    *amount = stock;
}

void PatchMotionBlur()
{
    s_blurScale = g_cfg.motionBlurStrength * (g_cfg.frameRate == 60 ? 2.0f : 1.0f);
    if (s_blurScale == 1.0f && !g_cfg.log) return;
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
