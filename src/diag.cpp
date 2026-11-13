// Split/Second PC fix - Direct3D 9 hooks: device creation log and back buffer capture (Log=1),
// forced anisotropic filtering (Anisotropy=2..16), 60 fps pacing after Present (framerate.cpp)
#include "common.h"
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

typedef IDirect3D9*(WINAPI* Direct3DCreate9_t)(UINT);
typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);

typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(WINAPI* D3DXSaveSurfaceToFileA_t)(LPCSTR, DWORD, IDirect3DSurface9*, const PALETTEENTRY*, const RECT*);

static Direct3DCreate9_t s_origCreate9;
static CreateDevice_t s_origCreateDevice;
static Present_t s_origPresent;
static BYTE s_create9Prologue[5];

// ---------------------------------------------------------------- anisotropic filtering

// MINFILTER LINEAR becomes ANISOTROPIC on pixel samplers that hold a mipmapped texture which is not
// a render target or depth texture (shadow maps rely on plain linear filtering for PCF).
typedef HRESULT(STDMETHODCALLTYPE* SetTexture_t)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
typedef HRESULT(STDMETHODCALLTYPE* SetSamplerState_t)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
static SetTexture_t s_origSetTexture;
static SetSamplerState_t s_origSetSamplerState;

struct StageState {
    IDirect3DBaseTexture9* tex;
    bool eligible;
    DWORD minFilter;  // as the game set it
};
static StageState s_stages[16];

static bool Eligible(IDirect3DBaseTexture9* t)
{
    if (!t || t->GetLevelCount() < 2) return false;
    D3DSURFACE_DESC d;
    D3DRESOURCETYPE type = t->GetType();
    if (type == D3DRTYPE_TEXTURE) {
        if (FAILED(((IDirect3DTexture9*)t)->GetLevelDesc(0, &d))) return false;
    } else if (type == D3DRTYPE_CUBETEXTURE) {
        if (FAILED(((IDirect3DCubeTexture9*)t)->GetLevelDesc(0, &d))) return false;
    } else {
        return false;
    }
    return !(d.Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL));
}

static void ApplyFilter(IDirect3DDevice9* dev, DWORD stage)
{
    StageState& s = s_stages[stage];
    bool aniso = s.eligible && s.minFilter == D3DTEXF_LINEAR;
    s_origSetSamplerState(dev, stage, D3DSAMP_MINFILTER, aniso ? D3DTEXF_ANISOTROPIC : s.minFilter);
    if (aniso) s_origSetSamplerState(dev, stage, D3DSAMP_MAXANISOTROPY, (DWORD)g_cfg.anisotropy);
}

static HRESULT STDMETHODCALLTYPE Hook_SetTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex)
{
    HRESULT hr = s_origSetTexture(dev, stage, tex);
    if (stage < 16 && tex != s_stages[stage].tex) {
        s_stages[stage].tex = tex;
        bool eligible = Eligible(tex);
        if (eligible != s_stages[stage].eligible) {
            s_stages[stage].eligible = eligible;
            if (s_stages[stage].minFilter == D3DTEXF_LINEAR) ApplyFilter(dev, stage);
        }
    }
    return hr;
}

// The motion blur pass samples along the motion; at speed (and more so with the doubled blur at 60 fps
// and the boost) the samples near the bottom edge reach past the picture and bring in a thin line from
// outside it. While the pass runs, U and V addressing of the first stages is clamped to the edge. What
// the game sets meanwhile is kept and set when the pass ends, so the device ends up in the state the game
// expects (it skips settings it believes are already there).
static IDirect3DDevice9* s_device;
static bool s_clamp;
static DWORD s_wantAddress[8][2];  // U, V as the game wants them

void BeginEdgeClamp()
{
    if (!s_device || !s_origSetSamplerState || s_clamp) return;
    for (DWORD stage = 0; stage < 8; stage++)
        for (int i = 0; i < 2; i++) {
            D3DSAMPLERSTATETYPE type = i ? D3DSAMP_ADDRESSV : D3DSAMP_ADDRESSU;
            if (FAILED(s_device->GetSamplerState(stage, type, &s_wantAddress[stage][i]))) s_wantAddress[stage][i] = D3DTADDRESS_WRAP;
            s_origSetSamplerState(s_device, stage, type, D3DTADDRESS_CLAMP);
        }
    s_clamp = true;
}

void EndEdgeClamp()
{
    if (!s_clamp) return;
    s_clamp = false;
    for (DWORD stage = 0; stage < 8; stage++)
        for (int i = 0; i < 2; i++)
            s_origSetSamplerState(s_device, stage, i ? D3DSAMP_ADDRESSV : D3DSAMP_ADDRESSU, s_wantAddress[stage][i]);
}

static HRESULT STDMETHODCALLTYPE Hook_SetSamplerState(IDirect3DDevice9* dev, DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value)
{
    if (s_clamp && stage < 8 && (type == D3DSAMP_ADDRESSU || type == D3DSAMP_ADDRESSV)) {
        s_wantAddress[stage][type == D3DSAMP_ADDRESSV] = value;
        return s_origSetSamplerState(dev, stage, type, D3DTADDRESS_CLAMP);
    }
    if (stage < 16 && type == D3DSAMP_MINFILTER) {
        s_stages[stage].minFilter = value;
        ApplyFilter(dev, stage);
        return D3D_OK;
    }
    if (stage < 16 && type == D3DSAMP_MAXANISOTROPY && s_stages[stage].eligible && value < (DWORD)g_cfg.anisotropy)
        value = (DWORD)g_cfg.anisotropy;
    return s_origSetSamplerState(dev, stage, type, value);
}

static void PatchVtable(void** vt, int index, void* hook, void** orig)
{
    if (*orig) return;
    *orig = vt[index];
    DWORD old;
    VirtualProtect(&vt[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    vt[index] = hook;
    VirtualProtect(&vt[index], sizeof(void*), old, &old);
}

// ---------------------------------------------------------------- capture and log

// test feed: save the back buffer as JPG next to the game on request (exclusive fullscreen
// defeats desktop capture)
static void CaptureBackBuffer(IDirect3DDevice9* dev, DWORD index)
{
    static D3DXSaveSurfaceToFileA_t save;
    if (!save) {
        HMODULE x = GetModuleHandleA("d3dx9_42.dll");
        save = x ? (D3DXSaveSurfaceToFileA_t)GetProcAddress(x, "D3DXSaveSurfaceToFileA") : NULL;
        if (!save) return;
    }
    IDirect3DSurface9 *bb = NULL, *sys = NULL;
    D3DSURFACE_DESC d;
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && SUCCEEDED(bb->GetDesc(&d)) &&
        SUCCEEDED(dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, NULL)) &&
        SUCCEEDED(dev->GetRenderTargetData(bb, sys))) {
        char name[48], path[MAX_PATH];
        wsprintfA(name, "SplitSecondPad_shot%lu.jpg", index);
        ModulePath(path, name);
        HRESULT hr = save(path, 1 /* D3DXIFF_JPG */, sys, NULL, NULL);
        Log("capture %s -> %08lx", path, hr);
    }
    if (sys) sys->Release();
    if (bb) bb->Release();
}

// ---------------------------------------------------------------- frame dump (Log=1, F9)

// One frame, pass by pass: whenever the game switches render target 0, the target it drew into is
// saved (a JPG of all of it, a PNG of its bottom 64 rows) and the mean colour of its bottom rows is
// logged, with the targets and textures each pass reads. StretchRect copies are logged and their
// destination saved. Only reads; the device state the game set is never changed.
typedef HRESULT(STDMETHODCALLTYPE* SetRenderTarget_t)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
typedef HRESULT(STDMETHODCALLTYPE* StretchRect_t)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*,
                                                  const RECT*, D3DTEXTUREFILTERTYPE);
typedef HRESULT(STDMETHODCALLTYPE* DrawPrimitive_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitive_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawPrimitiveUP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitiveUP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*,
                                                             D3DFORMAT, const void*, UINT);
static SetRenderTarget_t s_origSetRenderTarget;
static StretchRect_t s_origStretchRect;
static DrawPrimitive_t s_origDrawPrimitive;
static DrawIndexedPrimitive_t s_origDrawIndexedPrimitive;
static DrawPrimitiveUP_t s_origDrawPrimitiveUP;
static DrawIndexedPrimitiveUP_t s_origDrawIndexedPrimitiveUP;

static bool s_dumping;     // the current frame is dumped
static DWORD s_dumpIndex;  // F9 presses so far
static int s_pass;         // render target switches in the dumped frame
static DWORD s_passDraws;  // draws into the current target
static char s_dumpDir[MAX_PATH];

static float Half(WORD h)
{
    DWORD sign = (h >> 15) & 1, exp = (h >> 10) & 31, man = h & 1023;
    float v = exp ? (1.0f + man / 1024.0f) * (float)(1 << exp) / 32768.0f : man / 1024.0f / 16384.0f;
    if (exp == 31) v = 65504.0f;
    return sign ? -v : v;
}

// mean colour of one row of a locked surface, false for formats not read here
static bool RowMean(const D3DLOCKED_RECT& lr, D3DFORMAT fmt, UINT width, UINT y, float out[4])
{
    const BYTE* row = (const BYTE*)lr.pBits + (size_t)y * lr.Pitch;
    double sum[4] = {};
    for (UINT x = 0; x < width; x++) {
        float c[4];
        if (fmt == D3DFMT_A8R8G8B8 || fmt == D3DFMT_X8R8G8B8) {
            DWORD p = ((const DWORD*)row)[x];
            c[0] = ((p >> 16) & 255) / 255.0f, c[1] = ((p >> 8) & 255) / 255.0f, c[2] = (p & 255) / 255.0f, c[3] = (p >> 24) / 255.0f;
        } else if (fmt == D3DFMT_A2R10G10B10) {
            DWORD p = ((const DWORD*)row)[x];
            c[0] = ((p >> 20) & 1023) / 1023.0f, c[1] = ((p >> 10) & 1023) / 1023.0f, c[2] = (p & 1023) / 1023.0f, c[3] = (p >> 30) / 3.0f;
        } else if (fmt == D3DFMT_A16B16G16R16F) {
            const WORD* p = (const WORD*)row + x * 4;
            for (int i = 0; i < 4; i++) c[i] = Half(p[i]);
        } else if (fmt == D3DFMT_R32F) {
            c[0] = ((const float*)row)[x], c[1] = c[2] = c[3] = 0.0f;
        } else if (fmt == D3DFMT_G16R16F) {
            const WORD* p = (const WORD*)row + x * 2;
            c[0] = Half(p[0]), c[1] = Half(p[1]), c[2] = c[3] = 0.0f;
        } else {
            return false;
        }
        for (int i = 0; i < 4; i++) sum[i] += c[i];
    }
    for (int i = 0; i < 4; i++) out[i] = (float)(sum[i] / width);
    return true;
}

static void DumpSurface(IDirect3DDevice9* dev, IDirect3DSurface9* surf, const char* what)
{
    static D3DXSaveSurfaceToFileA_t save;
    if (!save) {
        HMODULE x = GetModuleHandleA("d3dx9_42.dll");
        save = x ? (D3DXSaveSurfaceToFileA_t)GetProcAddress(x, "D3DXSaveSurfaceToFileA") : NULL;
    }
    D3DSURFACE_DESC d;
    if (!surf || FAILED(surf->GetDesc(&d))) return;
    IDirect3DSurface9 *resolved = NULL, *sys = NULL, *src = surf;
    HRESULT hr = D3D_OK;
    // a multisampled target is resolved into a plain one first
    if (d.MultiSampleType != D3DMULTISAMPLE_NONE) {
        hr = dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, NULL);
        if (SUCCEEDED(hr)) hr = s_origStretchRect(dev, surf, NULL, resolved, NULL, D3DTEXF_NONE);
        src = resolved;
    }
    if (SUCCEEDED(hr)) hr = dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, NULL);
    if (SUCCEEDED(hr)) hr = dev->GetRenderTargetData(src, sys);
    char name[96], path[MAX_PATH];
    wsprintfA(name, "f%lu_%02d_%s_%ux%u_fmt%u", s_dumpIndex, s_pass, what, d.Width, d.Height, d.Format);
    if (FAILED(hr)) {
        Log("dump %s: not readable (%08lx), multisample %d, usage %lx, pool %d", name, hr, d.MultiSampleType, d.Usage, d.Pool);
    } else {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
            char line[512];
            int n = wsprintfA(line, "dump %s: row means r g b a (x1000)", name);
            UINT rows[] = { d.Height - 1, d.Height - 2, d.Height - 3, d.Height - 4, d.Height - 6, d.Height - 8, d.Height - 16, d.Height / 2 };
            for (UINT y : rows) {
                float m[4];
                if (y >= d.Height || !RowMean(lr, d.Format, d.Width, y, m)) continue;
                n += wsprintfA(line + n, " | y%u %d %d %d %d", y, (int)(m[0] * 1000), (int)(m[1] * 1000), (int)(m[2] * 1000),
                               (int)(m[3] * 1000));
                if (n > 440) break;
            }
            sys->UnlockRect();
            Log("%s", line);
        }
        if (save) {
            wsprintfA(path, "%s\\%s.jpg", s_dumpDir, name);
            HRESULT h1 = save(path, 1 /* D3DXIFF_JPG */, sys, NULL, NULL);
            RECT bottom = { 0, d.Height > 64 ? (LONG)d.Height - 64 : 0, (LONG)d.Width, (LONG)d.Height };
            wsprintfA(path, "%s\\%s_bottom.png", s_dumpDir, name);
            HRESULT h2 = save(path, 3 /* D3DXIFF_PNG */, sys, NULL, &bottom);
            if (FAILED(h2)) {
                wsprintfA(path, "%s\\%s_bottom.dds", s_dumpDir, name);
                h2 = save(path, 4 /* D3DXIFF_DDS */, sys, NULL, &bottom);
            }
            if (FAILED(h1) || FAILED(h2)) Log("dump %s: saving failed (%08lx %08lx)", name, h1, h2);
        }
    }
    if (sys) sys->Release();
    if (resolved) resolved->Release();
}

static void DescribeSurface(IDirect3DSurface9* s, char* out)
{
    D3DSURFACE_DESC d;
    IDirect3DTexture9* tex = NULL;
    if (s && SUCCEEDED(s->GetContainer(IID_IDirect3DTexture9, (void**)&tex)) && tex) tex->Release();
    if (s && SUCCEEDED(s->GetDesc(&d)))
        wsprintfA(out, "%p (texture %p) %ux%u fmt %u ms %d", s, tex, d.Width, d.Height, d.Format, d.MultiSampleType);
    else
        wsprintfA(out, "%p", s);
}

// at the first draw into a target: what it is and what the pass reads
static void DumpPassStart(IDirect3DDevice9* dev)
{
    IDirect3DSurface9* rt = NULL;
    dev->GetRenderTarget(0, &rt);
    char desc[128];
    DescribeSurface(rt, desc);
    if (rt) rt->Release();
    D3DVIEWPORT9 vp = {};
    dev->GetViewport(&vp);
    DWORD scissor = 0;
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
    RECT sr = {};
    dev->GetScissorRect(&sr);
    IDirect3DPixelShader9* ps = NULL;
    dev->GetPixelShader(&ps);
    if (ps) ps->Release();
    Log("dump pass %02d: target %s, viewport %lu,%lu %lux%lu, scissor %lu (%ld,%ld-%ld,%ld), pixel shader %p", s_pass, desc, vp.X,
        vp.Y, vp.Width, vp.Height, scissor, sr.left, sr.top, sr.right, sr.bottom, ps);
    for (DWORD stage = 0; stage < 8; stage++) {
        IDirect3DBaseTexture9* t = NULL;
        if (FAILED(dev->GetTexture(stage, &t)) || !t) continue;
        D3DSURFACE_DESC d = {};
        if (t->GetType() == D3DRTYPE_TEXTURE) ((IDirect3DTexture9*)t)->GetLevelDesc(0, &d);
        DWORD u = 0, v = 0, mag = 0;
        dev->GetSamplerState(stage, D3DSAMP_ADDRESSU, &u);
        dev->GetSamplerState(stage, D3DSAMP_ADDRESSV, &v);
        dev->GetSamplerState(stage, D3DSAMP_MAGFILTER, &mag);
        Log("dump pass %02d:   stage %lu texture %p %ux%u fmt %u usage %lx, address %lu/%lu, filter %lu", s_pass, stage, t, d.Width,
            d.Height, d.Format, d.Usage, u, v, mag);
        t->Release();
    }
}

static void CountDraw(IDirect3DDevice9* dev)
{
    if (!s_dumping) return;
    if (!s_passDraws++) DumpPassStart(dev);
}

void DumpMark(const char* what)
{
    if (s_dumping) Log("dump pass %02d: -- %s (after %lu draws)", s_pass, what, s_passDraws);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawPrimitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE t, UINT a, UINT b)
{
    CountDraw(dev);
    return s_origDrawPrimitive(dev, t, a, b);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawIndexedPrimitive(IDirect3DDevice9* dev, D3DPRIMITIVETYPE t, INT a, UINT b, UINT c, UINT d, UINT e)
{
    CountDraw(dev);
    return s_origDrawIndexedPrimitive(dev, t, a, b, c, d, e);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawPrimitiveUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE t, UINT a, const void* b, UINT c)
{
    CountDraw(dev);
    return s_origDrawPrimitiveUP(dev, t, a, b, c);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawIndexedPrimitiveUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE t, UINT a, UINT b, UINT c, const void* d,
                                                             D3DFORMAT e, const void* f, UINT g)
{
    CountDraw(dev);
    return s_origDrawIndexedPrimitiveUP(dev, t, a, b, c, d, e, f, g);
}

static void EndPass(IDirect3DDevice9* dev, const char* why)
{
    if (s_passDraws) {
        IDirect3DSurface9* rt = NULL;
        if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt) {
            Log("dump pass %02d: %lu draws, ends (%s)", s_pass, s_passDraws, why);
            DumpSurface(dev, rt, "target");
            rt->Release();
        }
        s_pass++;
    }
    s_passDraws = 0;
}

// The full screen passes (bloom and tone mapping, motion blur, depth of field, colour grading ...) all
// draw through one helper (0x511BA0) that copies a quad template ([[0xD67758]+4], 4 vertices of 7
// floats: position xyz, texture coordinates) and moves it by half a pixel plus a fixed 0.0005 up and
// 0.00015 left in clip space. The fixed part is a fraction of a pixel at the resolutions the port was
// made for, but at graphics quality Very High on a 4K screen the passes render at 7680x4320, where it
// adds up to more than a pixel: every pass moves the picture up by a row and leaves its bottom row
// unwritten, so whatever was drawn there once stays (a line at the bottom of the screen). For targets
// above 2160 rows (3840 columns) the template is moved back by the part of the fixed offset that goes
// beyond what it is at 2160 rows (3840 columns), so it never exceeds the fraction of a pixel it is at
// native 4K. Only the template's position values are written; below that size nothing changes.
static float s_quadOrigY[4], s_quadOrigX[4], s_quadSetY[4], s_quadSetX[4];
static float* s_quad;

static void FitQuadOffset(IDirect3DSurface9* surf)
{
    D3DSURFACE_DESC d;
    if (!surf || FAILED(surf->GetDesc(&d))) return;
    BYTE* holder = *(BYTE**)0x00D67758;
    float* quad = holder ? *(float**)(holder + 4) : NULL;
    if (!quad || IsBadReadPtr(quad, 0x70)) return;
    // not inside the executable's code or constants
    if ((DWORD)quad >= 0x00401000 && (DWORD)quad < 0x00CF5000) return;
    if (quad != s_quad) {
        for (int i = 0; i < 4; i++) {
            float x = quad[i * 7], y = quad[i * 7 + 1];
            if (x < -1.01f || x > 1.01f || y < -1.01f || y > 1.01f) {
                static bool logged;
                if (!logged) Log("full screen quad template not recognised (%.4f %.4f)", x, y);
                logged = true;
                return;
            }
            s_quadOrigX[i] = s_quadSetX[i] = x;
            s_quadOrigY[i] = s_quadSetY[i] = y;
        }
        s_quad = quad;
        Log("full screen quad template at %p: %.4f %.4f / %.4f %.4f / %.4f %.4f / %.4f %.4f", quad, quad[0], quad[1], quad[7], quad[8],
            quad[14], quad[15], quad[21], quad[22]);
    }
    float dy = d.Height > 2160 ? 0.0005f * (1.0f - 2160.0f / d.Height) : 0.0f;
    float dx = d.Width > 3840 ? 0.00015f * (1.0f - 3840.0f / d.Width) : 0.0f;
    for (int i = 0; i < 4; i++) {
        float* x = &quad[i * 7];
        float* y = &quad[i * 7 + 1];
        if (*x != s_quadSetX[i]) s_quadOrigX[i] = *x;  // the game wrote it meanwhile
        if (*y != s_quadSetY[i]) s_quadOrigY[i] = *y;
        s_quadSetX[i] = s_quadOrigX[i] + dx;
        s_quadSetY[i] = s_quadOrigY[i] - dy;
        if (*x != s_quadSetX[i]) *x = s_quadSetX[i];
        if (*y != s_quadSetY[i]) *y = s_quadSetY[i];
    }
    static bool logged;
    if ((dx || dy) && !logged) {
        logged = true;
        Log("full screen passes at %ux%u: quad offset reduced by %.6f up, %.6f left", d.Width, d.Height, dy, dx);
    }
}

static HRESULT STDMETHODCALLTYPE Hook_SetRenderTarget(IDirect3DDevice9* dev, DWORD index, IDirect3DSurface9* surf)
{
    if (index == 0) FitQuadOffset(surf);
    if (s_dumping && s_pass < 120) {
        IDirect3DSurface9* cur = NULL;
        dev->GetRenderTarget(index, &cur);
        if (cur) cur->Release();
        if (cur != surf) {
            if (index == 0) {
                EndPass(dev, "new target");
            } else {
                char desc[128];
                DescribeSurface(surf, desc);
                Log("dump pass %02d: render target %lu = %s", s_pass, index, desc);
            }
        }
    }
    return s_origSetRenderTarget(dev, index, surf);
}

static HRESULT STDMETHODCALLTYPE Hook_StretchRect(IDirect3DDevice9* dev, IDirect3DSurface9* src, const RECT* sr, IDirect3DSurface9* dst,
                                                  const RECT* dr, D3DTEXTUREFILTERTYPE filter)
{
    HRESULT hr = s_origStretchRect(dev, src, sr, dst, dr, filter);
    if (s_dumping && s_pass < 120) {
        char a[128], b[128];
        DescribeSurface(src, a);
        DescribeSurface(dst, b);
        Log("dump pass %02d: StretchRect %s (%ld,%ld-%ld,%ld) -> %s (%ld,%ld-%ld,%ld) filter %d -> %08lx", s_pass, a, sr ? sr->left : -1,
            sr ? sr->top : -1, sr ? sr->right : -1, sr ? sr->bottom : -1, b, dr ? dr->left : -1, dr ? dr->top : -1, dr ? dr->right : -1,
            dr ? dr->bottom : -1, filter, hr);
        DumpSurface(dev, dst, "copy");
    }
    return hr;
}

static void DumpAtPresent(IDirect3DDevice9* dev)
{
    if (s_dumping) {
        EndPass(dev, "present");
        IDirect3DSurface9* bb = NULL;
        if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            DumpSurface(dev, bb, "backbuffer");
            bb->Release();
        }
        s_dumping = false;
        Log("dump %lu: done, %d passes, files in %s", s_dumpIndex, s_pass, s_dumpDir);
    }
    // F9 while the game has the focus dumps the next frame
    static bool was;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    bool down = pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_F9) & 0x8000);
    if (down && !was && s_origSetRenderTarget) {
        s_dumpIndex++;
        ModulePath(s_dumpDir, "glimpse_dump");
        CreateDirectoryA(s_dumpDir, NULL);
        s_dumping = true;
        s_pass = 0;
        s_passDraws = 0;
        Log("dump %lu: next frame, graphics quality %d", s_dumpIndex, *(int*)0x00D0F7E8);
    }
    was = down;
}

static HRESULT STDMETHODCALLTYPE Diag_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty)
{
    if (g_cfg.log) DumpAtPresent(dev);
    static DWORD frames, since;
    DWORD now = GetTickCount();
    frames++;
    if (!since) since = now;
    if (now - since >= 1000) {
        if (TestFeed* feed = GetTestFeed()) feed->fps = frames * 1000.0f / (now - since);
        frames = 0;
        since = now;
    }
    if (TestFeed* feed = GetTestFeed()) {
        if (feed->shotRequest) {
            CaptureBackBuffer(dev, feed->shotRequest);
            feed->shotRequest = 0;
        }
    }
    HRESULT hr = s_origPresent(dev, src, dst, wnd, dirty);
    FramePace();
    return hr;
}

static HRESULT STDMETHODCALLTYPE Diag_CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                                    D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
{
    Log("CreateDevice adapter=%u type=%d hwnd=%p fg=%p flags=%lx %ux%u fmt=%d windowed=%d refresh=%u interval=%x",
        adapter, type, focus, GetForegroundWindow(), flags, pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0,
        pp ? pp->BackBufferFormat : 0, pp ? pp->Windowed : -1, pp ? pp->FullScreen_RefreshRateInHz : 0,
        pp ? pp->PresentationInterval : 0);
    HRESULT hr = s_origCreateDevice(d3d, adapter, type, focus, flags, pp, out);
    Log("CreateDevice -> %08lx", hr);
    if (SUCCEEDED(hr) && out && *out) {
        Log("available texture memory %u MB", (*out)->GetAvailableTextureMem() >> 20);
        LogForeignModules("after device");
        void** vt = *(void***)*out;
        if (g_cfg.log || g_cfg.frameRate == 60) PatchVtable(vt, 17, (void*)Diag_Present, (void**)&s_origPresent);
        s_device = *out;
        if (g_cfg.anisotropy > 1) {
            PatchVtable(vt, 65, (void*)Hook_SetTexture, (void**)&s_origSetTexture);
            Log("anisotropic filtering %dx", g_cfg.anisotropy);
        }
        PatchVtable(vt, 69, (void*)Hook_SetSamplerState, (void**)&s_origSetSamplerState);  // filtering, edge clamp
        PatchVtable(vt, 37, (void*)Hook_SetRenderTarget, (void**)&s_origSetRenderTarget);  // quad offset, frame dump
        if (g_cfg.log) {  // frame dump on F9
            PatchVtable(vt, 34, (void*)Hook_StretchRect, (void**)&s_origStretchRect);
            PatchVtable(vt, 81, (void*)Hook_DrawPrimitive, (void**)&s_origDrawPrimitive);
            PatchVtable(vt, 82, (void*)Hook_DrawIndexedPrimitive, (void**)&s_origDrawIndexedPrimitive);
            PatchVtable(vt, 83, (void*)Hook_DrawPrimitiveUP, (void**)&s_origDrawPrimitiveUP);
            PatchVtable(vt, 84, (void*)Hook_DrawIndexedPrimitiveUP, (void**)&s_origDrawIndexedPrimitiveUP);
            Log("frame dump: F9");
        }
    }
    return hr;
}

static IDirect3D9* WINAPI Diag_Direct3DCreate9(UINT sdk)
{
    // restore the prologue and call through; one create is all we want to see
    DWORD old;
    VirtualProtect((void*)s_origCreate9, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)s_origCreate9, s_create9Prologue, 5);
    VirtualProtect((void*)s_origCreate9, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)s_origCreate9, 5);

    IDirect3D9* d3d = s_origCreate9(sdk);
    Log("Direct3DCreate9(%u) -> %p, %u adapters", sdk, d3d, d3d ? d3d->GetAdapterCount() : 0);
    if (d3d) {
        void** vt = *(void***)d3d;
        s_origCreateDevice = (CreateDevice_t)vt[16];
        VirtualProtect(&vt[16], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
        vt[16] = (void*)Diag_CreateDevice;
        VirtualProtect(&vt[16], sizeof(void*), old, &old);
        for (UINT a = 0; a < d3d->GetAdapterCount(); a++) {
            D3DADAPTER_IDENTIFIER9 id;
            D3DDISPLAYMODE dm;
            if (SUCCEEDED(d3d->GetAdapterIdentifier(a, 0, &id)) && SUCCEEDED(d3d->GetAdapterDisplayMode(a, &dm)))
                Log("  adapter %u: %s %ux%u@%u modes=%u, vendor %04lX device %04lX driver %u.%u.%u.%u", a, id.Description,
                    dm.Width, dm.Height, dm.RefreshRate, d3d->GetAdapterModeCount(a, D3DFMT_X8R8G8B8), id.VendorId,
                    id.DeviceId, HIWORD(id.DriverVersion.HighPart), LOWORD(id.DriverVersion.HighPart),
                    HIWORD(id.DriverVersion.LowPart), LOWORD(id.DriverVersion.LowPart));
        }
    }
    return d3d;
}

void InstallD3DHooks()
{
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9) d3d9 = LoadLibraryA("d3d9.dll");
    s_origCreate9 = d3d9 ? (Direct3DCreate9_t)GetProcAddress(d3d9, "Direct3DCreate9") : NULL;
    if (!s_origCreate9) return;
    memcpy(s_create9Prologue, (void*)s_origCreate9, 5);
    BYTE jmp[5] = { 0xE9 };
    *(DWORD*)(jmp + 1) = (DWORD)Diag_Direct3DCreate9 - ((DWORD)s_origCreate9 + 5);
    DWORD old;
    VirtualProtect((void*)s_origCreate9, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)s_origCreate9, jmp, 5);
    VirtualProtect((void*)s_origCreate9, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)s_origCreate9, 5);
}
