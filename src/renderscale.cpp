// Split/Second PC fix - internal render resolution independent of the graphics quality option.
//
// "Very High" is exactly "High" plus 2x supersampling: nine places check quality == 3 and then
// double the screen size (render targets at start-up and after a device reset, post effect
// viewports, HUD icon bounds and size) and downsample with a linear StretchRect at present.
// With RenderScale set, those places test our own flag instead of the quality level and use the
// configured factor instead of 2.
#include "common.h"
#include <math.h>
#include <string.h>

static const int* const kScreenW = (const int*)0x00D56648;
static const int* const kScreenH = (const int*)0x00D5664C;

static BYTE s_supersample;  // replaces "quality == 3"
static float s_scale = 1.0f, s_invScale = 1.0f;
static void* s_origCreateTargets;  // 0x9F8DA0
static void* s_origRecreateTargets;  // 0x9F8AD0

// The 2x paths pass twice the screen size; turn that into the configured size.
static void __stdcall AdjustTargetSize(int* w, int* h)
{
    if (*w == *kScreenW * 2 && *h == *kScreenH * 2) {
        *w = (int)floorf(*kScreenW * s_scale + 0.5f);
        *h = (int)floorf(*kScreenH * s_scale + 0.5f);
    }
}

__declspec(naked) static void Stub_CreateTargets()
{
    __asm {
        pushad
        lea eax, [esp + 44]  // &height (32 saved + return + object + width)
        push eax
        lea eax, [esp + 44]  // &width
        push eax
        call AdjustTargetSize
        popad
        jmp s_origCreateTargets
    }
}

__declspec(naked) static void Stub_RecreateTargets()
{
    __asm {
        pushad
        lea eax, [esp + 44]
        push eax
        lea eax, [esp + 44]
        push eax
        call AdjustTargetSize
        popad
        jmp s_origRecreateTargets
    }
}

// post effect viewports: in edi/ebx the screen size, in cl the mip shift; eax/edx already hold
// the unscaled size >> cl and become the scaled size >> cl
__declspec(naked) static void Stub_ViewportSize()
{
    __asm {
        cmp s_supersample, 0
        je done
        push ecx
        push edi
        fild dword ptr [esp]
        fmul s_scale
        fistp dword ptr [esp]
        pop eax
        push ebx
        fild dword ptr [esp]
        fmul s_scale
        fistp dword ptr [esp]
        pop edx
        pop ecx
        sar eax, cl
        sar edx, cl
    done:
        ret
    }
}

// HUD icon bounds: eax/edx = screen size, scaled like the render target
__declspec(naked) static void Stub_HudBounds()
{
    __asm {
        cmp s_supersample, 0
        je done
        push eax
        fild dword ptr [esp]
        fmul s_scale
        fistp dword ptr [esp]
        pop eax
        push edx
        fild dword ptr [esp]
        fmul s_scale
        fistp dword ptr [esp]
        pop edx
    done:
        ret
    }
}

// HUD icon size: the width it is derived from already includes the scale, so divide it out
__declspec(naked) static void Stub_HudIconSize()
{
    __asm {
        cmp s_supersample, 0
        je done
        mulss xmm4, s_invScale
    done:
        ret
    }
}

static const char kCmpQuality3[] = "833DE8F7D00003";  // cmp dword ptr [0D0F7E8h], 3

// cmp dword ptr [quality], 3  ->  cmp byte ptr [s_supersample], 1  (both 7 bytes, same ZF meaning)
static bool RetargetCompare(DWORD at)
{
    if (!BytesMatch((void*)at, kCmpQuality3)) return false;
    BYTE code[7] = { 0x80, 0x3D, 0, 0, 0, 0, 0x01 };
    *(DWORD*)(code + 2) = (DWORD)&s_supersample;
    WriteMemory((void*)at, code, sizeof(code));
    return true;
}

// replace 'len' bytes at 'at' with a call to 'stub' and NOPs
static void CallStub(DWORD at, int len, void* stub)
{
    BYTE code[32];
    memset(code, 0x90, sizeof(code));
    code[0] = 0xE8;
    *(DWORD*)(code + 1) = (DWORD)stub - (at + 5);
    WriteMemory((void*)at, code, len);
}

void PatchRenderScale()
{
    if (g_cfg.renderScale <= 0) return;  // auto: the game's quality option decides

    // everything must look as expected before anything is touched
    static const DWORD viewport[] = { 0x0049225E, 0x00496D69, 0x0049847E };
    static const DWORD targets[] = { 0x00A06FE1, 0x00A07099, 0x00A072AA, 0x00A07314 };
    bool ok = BytesMatch((void*)0x009F8DA0, "6AFF68B8EEB500") && BytesMatch((void*)0x009F8AD0, "8B44241083EC0C") &&
              BytesMatch((void*)0x0050E46D, kCmpQuality3) &&
              BytesMatch((void*)0x0055E74F, "833DE8F7D00003750403C003D2") &&
              BytesMatch((void*)0x0055E8B3, kCmpQuality3) && BytesMatch((void*)0x0055E8D6, "7504F30F59E0");
    for (DWORD a : viewport) ok = ok && BytesMatch((void*)a, "833DE8F7D00003750A8D043F8D141BD3F8D3FA");
    for (DWORD a : targets) ok = ok && BytesMatch((void*)a, kCmpQuality3);
    if (!ok) {
        Log("render scale: code not recognised, left to the quality option");
        return;
    }

    s_scale = g_cfg.renderScale;
    s_invScale = 1.0f / s_scale;
    s_supersample = s_scale != 1.0f;

    for (DWORD a : targets) RetargetCompare(a);
    RetargetCompare(0x0050E46D);  // linear StretchRect filter when supersampling
    for (DWORD a : viewport) CallStub(a, 19, (void*)Stub_ViewportSize);
    CallStub(0x0055E74F, 13, (void*)Stub_HudBounds);
    static const BYTE nop7[7] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    WriteMemory((void*)0x0055E8B3, nop7, sizeof(nop7));  // compare no longer needed
    CallStub(0x0055E8D6, 6, (void*)Stub_HudIconSize);
    s_origCreateTargets = Detour((BYTE*)0x009F8DA0, 7, (void*)Stub_CreateTargets);
    s_origRecreateTargets = Detour((BYTE*)0x009F8AD0, 7, (void*)Stub_RecreateTargets);
    Log("render scale %.2f (independent of the quality option)", s_scale);
}
