// Split/Second PC fix - post processing: motion blur strength
//
// The screen space motion blur (0x49B490) blurs along the camera motion of one frame. Each camera
// enables it (Cameras/Data/*/SSMotionBlurEnabled, needs graphics quality High or Very High) with
// a blur amount. At 60 fps a frame covers half the motion, so the amount is doubled to keep the
// look of the 30 fps game; MotionBlurStrength scales it further.
#include "common.h"
#include <string.h>

// screen space motion blur pass (thiscall, 5 stack args, ret 14h); returns at once unless [[this+4]+8] is set
static BYTE* const kMotionBlurRender = (BYTE*)0x0049B490;
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
    if (memcmp(kMotionBlurRender, "\x83\xEC\x68\x57\x8B\xF9", 6)) {
        Log("motion blur pass not recognised");
        return;
    }
    s_origMotionBlur = (MotionBlurRender_t)Detour(kMotionBlurRender, 6, (void*)Hook_MotionBlurRender);
    Log("motion blur scale %.2f", s_blurScale);
}
