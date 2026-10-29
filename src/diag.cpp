// Split/Second PC fix - Direct3D 9 hooks: device creation log and back buffer capture (Log=1),
// forced anisotropic filtering (Anisotropy=2..16)
#include "common.h"
#include <d3d9.h>
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

static HRESULT STDMETHODCALLTYPE Hook_SetSamplerState(IDirect3DDevice9* dev, DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value)
{
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

static HRESULT STDMETHODCALLTYPE Diag_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty)
{
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
    return s_origPresent(dev, src, dst, wnd, dirty);
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
        void** vt = *(void***)*out;
        if (g_cfg.log) PatchVtable(vt, 17, (void*)Diag_Present, (void**)&s_origPresent);
        if (g_cfg.anisotropy > 1) {
            PatchVtable(vt, 65, (void*)Hook_SetTexture, (void**)&s_origSetTexture);
            PatchVtable(vt, 69, (void*)Hook_SetSamplerState, (void**)&s_origSetSamplerState);
            Log("anisotropic filtering %dx", g_cfg.anisotropy);
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
                Log("  adapter %u: %s %ux%u@%u modes=%u", a, id.Description, dm.Width, dm.Height, dm.RefreshRate,
                    d3d->GetAdapterModeCount(a, D3DFMT_X8R8G8B8));
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
