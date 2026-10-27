// Split/Second controller fix - IDirectInput8A wrapper exposing XInput pads as
// "Controller (XBOX 360 For Windows)" joysticks, the only pad the game ships a mapping for.
#include "common.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// guidProduct of the wired Xbox 360 pad (VID 045E PID 028E); the game picks its pad preset from it
static const GUID kProduct360 = { 0x028E045E, 0x0000, 0x0000, { 0x00, 0x00, 'P', 'I', 'D', 'V', 'I', 'D' } };
// guidInstance base, last byte = XInput slot
static const GUID kInstanceBase = { 0x5353A3D0, 0x9C1E, 0x4B6D, { 0x8F, 0x52, 'S', 'S', 'P', 'A', 'D', 0 } };

static const char kProductName[] = "Controller (XBOX 360 For Windows)";

static bool IsVirtualGuid(REFGUID g, int* slot)
{
    if (memcmp(&g, &kInstanceBase, sizeof(GUID) - 1) != 0) return false;
    int s = g.Data4[7];
    if (s >= g_cfg.virtualPads) return false;
    if (slot) *slot = s;
    return true;
}

static GUID InstanceGuid(int slot)
{
    GUID g = kInstanceBase;
    g.Data4[7] = (BYTE)slot;
    return g;
}

// ---------------------------------------------------------------- XInput slot cache

struct PadSlot {
    bool connected;
    DWORD lastCheck;  // tick of the last XInputGetState on a disconnected slot
    XINPUT_STATE state;
};
static PadSlot s_pads[XUSER_MAX_COUNT];

TestFeed* GetTestFeed()
{
    static TestFeed* feed;
    static DWORD lastTry;
    if (feed || !g_cfg.testFeed) return feed;
    DWORD now = GetTickCount();
    if (lastTry && now - lastTry < 1000) return NULL;
    lastTry = now;
    HANDLE h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, kTestFeedName);
    if (h) {
        feed = (TestFeed*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(TestFeed));
        Log("test feed %s", feed ? "attached" : "failed");
    }
    return feed;
}

static void RefreshSlot(int slot)
{
    PadSlot& p = s_pads[slot];
    DWORD now = GetTickCount();
    bool was = p.connected;
    if (TestFeed* feed = GetTestFeed()) {
        p.connected = feed->connected[slot] != 0;
        p.state = feed->state[slot];
        if (was != p.connected) Log("pad %d %s (test feed)", slot, p.connected ? "connected" : "disconnected");
        return;
    }
    // XInputGetState on an empty slot is slow; probe those only twice a second
    if (!p.connected && p.lastCheck && now - p.lastCheck < 500) return;
    XINPUT_STATE st = {};
    p.connected = pXInputGetState((DWORD)slot, &st) == ERROR_SUCCESS;
    p.lastCheck = now;
    if (p.connected) p.state = st;
    else memset(&p.state, 0, sizeof(p.state));
    if (was != p.connected) Log("pad %d %s", slot, p.connected ? "connected" : "disconnected");
}

bool PadSlotConnected(int slot)
{
    return slot >= 0 && slot < XUSER_MAX_COUNT && s_pads[slot].connected;
}

// ---------------------------------------------------------------- object table

struct PadObject {
    const GUID* type;
    DWORD ofs;
    DWORD dwType;
    const char* name;
};

#define AXIS(n) (DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(n))
#define BTN(n) (DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(n))

static const PadObject kObjects[] = {
    { &GUID_XAxis,  DIJOFS_X,  AXIS(0), "X Axis" },
    { &GUID_YAxis,  DIJOFS_Y,  AXIS(1), "Y Axis" },
    { &GUID_ZAxis,  DIJOFS_Z,  AXIS(2), "Z Axis" },
    { &GUID_RxAxis, DIJOFS_RX, AXIS(3), "X Rotation" },
    { &GUID_RyAxis, DIJOFS_RY, AXIS(4), "Y Rotation" },
    { &GUID_RzAxis, DIJOFS_RZ, AXIS(5), "Z Rotation" },
    { &GUID_Button, DIJOFS_BUTTON(0), BTN(0), "Button 0" },
    { &GUID_Button, DIJOFS_BUTTON(1), BTN(1), "Button 1" },
    { &GUID_Button, DIJOFS_BUTTON(2), BTN(2), "Button 2" },
    { &GUID_Button, DIJOFS_BUTTON(3), BTN(3), "Button 3" },
    { &GUID_Button, DIJOFS_BUTTON(4), BTN(4), "Button 4" },
    { &GUID_Button, DIJOFS_BUTTON(5), BTN(5), "Button 5" },
    { &GUID_Button, DIJOFS_BUTTON(6), BTN(6), "Button 6" },
    { &GUID_Button, DIJOFS_BUTTON(7), BTN(7), "Button 7" },
    { &GUID_Button, DIJOFS_BUTTON(8), BTN(8), "Button 8" },
    { &GUID_Button, DIJOFS_BUTTON(9), BTN(9), "Button 9" },
    { &GUID_POV,    DIJOFS_POV(0), DIDFT_POV | DIDFT_MAKEINSTANCE(0), "Hat Switch" },
};
static const int kNumAxes = 6, kNumButtons = 10, kNumPovs = 1;
static const int kNumObjects = sizeof(kObjects) / sizeof(kObjects[0]);

static void FillObjectInstance(const PadObject& o, LPDIDEVICEOBJECTINSTANCEA out)
{
    DWORD size = out->dwSize;
    memset(out, 0, size);
    out->dwSize = size;
    out->guidType = *o.type;
    out->dwOfs = o.ofs;
    out->dwType = o.dwType;
    out->dwFlags = (o.dwType & DIDFT_AXIS) ? DIDOI_ASPECTPOSITION : 0;
    strcpy_s(out->tszName, o.name);
    if (size >= sizeof(DIDEVICEOBJECTINSTANCEA) && (o.dwType & DIDFT_AXIS)) {
        out->wUsagePage = 1;  // generic desktop
        static const WORD usages[] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x35 };
        out->wUsage = usages[DIDFT_GETINSTANCE(o.dwType)];
    }
}

static const PadObject* FindObject(DWORD obj, DWORD how)
{
    for (const PadObject& o : kObjects) {
        if (how == DIPH_BYOFFSET && o.ofs == obj) return &o;
        if (how == DIPH_BYID && (o.dwType & 0x00FFFFFF) == (obj & 0x00FFFFFF)) return &o;
    }
    return NULL;
}

static void FillDeviceInstance(int slot, LPDIDEVICEINSTANCEA out)
{
    DWORD size = out->dwSize;
    memset(out, 0, size);
    out->dwSize = size;
    out->guidInstance = InstanceGuid(slot);
    out->guidProduct = kProduct360;
    out->dwDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | DIDEVTYPE_HID;
    sprintf_s(out->tszInstanceName, "XInput Controller %d", slot + 1);
    strcpy_s(out->tszProductName, kProductName);
    if (size >= sizeof(DIDEVICEINSTANCEA)) {
        out->wUsagePage = 1;
        out->wUsage = 5;  // game pad
    }
}

// ---------------------------------------------------------------- virtual device

class VirtualPad : public IDirectInputDevice8A {
public:
    explicit VirtualPad(int slot) : m_ref(1), m_slot(slot), m_formatSize(sizeof(DIJOYSTATE2))
    {
        for (int i = 0; i < kNumAxes; i++) {
            m_min[i] = 0;
            m_max[i] = 65535;
            m_deadzone[i] = 0;
            m_saturation[i] = 10000;
        }
        Log("pad %d: device created", slot);
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == IID_IUnknown || riid == IID_IDirectInputDevice8A) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override
    {
        if (!caps || (caps->dwSize != sizeof(DIDEVCAPS) && caps->dwSize != sizeof(DIDEVCAPS_DX3)))
            return DIERR_INVALIDPARAM;
        DWORD size = caps->dwSize;
        memset(caps, 0, size);
        caps->dwSize = size;
        caps->dwFlags = DIDC_ATTACHED | DIDC_EMULATED;
        caps->dwDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | DIDEVTYPE_HID;
        caps->dwAxes = kNumAxes;
        caps->dwButtons = kNumButtons;
        caps->dwPOVs = kNumPovs;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ref, DWORD flags) override
    {
        if (!cb) return DIERR_INVALIDPARAM;
        DWORD types = flags & 0xFF, attrs = flags & 0xFF000000;
        for (const PadObject& o : kObjects) {
            if (types && !(o.dwType & types)) continue;
            if (attrs && (o.dwType & attrs) != attrs) continue;  // no force feedback actuators here
            DIDEVICEOBJECTINSTANCEA inst;
            inst.dwSize = sizeof(inst);
            FillObjectInstance(o, &inst);
            if (cb(&inst, ref) == DIENUM_STOP) break;
        }
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID prop, LPDIPROPHEADER hdr) override
    {
        if (!hdr) return DIERR_INVALIDPARAM;
        DWORD_PTR id = (DWORD_PTR)&prop;
        int axis = AxisIndex(hdr);
        if (id == (DWORD_PTR)&DIPROP_RANGE) {
            if (axis < 0) return DIERR_OBJECTNOTFOUND;
            ((LPDIPROPRANGE)hdr)->lMin = m_min[axis];
            ((LPDIPROPRANGE)hdr)->lMax = m_max[axis];
            return DI_OK;
        }
        if (id == (DWORD_PTR)&DIPROP_DEADZONE || id == (DWORD_PTR)&DIPROP_SATURATION) {
            if (axis < 0) return DIERR_OBJECTNOTFOUND;
            ((LPDIPROPDWORD)hdr)->dwData = id == (DWORD_PTR)&DIPROP_DEADZONE ? m_deadzone[axis] : m_saturation[axis];
            return DI_OK;
        }
        if (id == (DWORD_PTR)&DIPROP_VIDPID) {
            ((LPDIPROPDWORD)hdr)->dwData = kProduct360.Data1;
            return DI_OK;
        }
        if (id == (DWORD_PTR)&DIPROP_PRODUCTNAME || id == (DWORD_PTR)&DIPROP_INSTANCENAME) {
            wchar_t* dst = ((LPDIPROPSTRING)hdr)->wsz;
            if (id == (DWORD_PTR)&DIPROP_PRODUCTNAME) swprintf_s(dst, MAX_PATH, L"%hs", kProductName);
            else swprintf_s(dst, MAX_PATH, L"XInput Controller %d", m_slot + 1);
            return DI_OK;
        }
        if (id == (DWORD_PTR)&DIPROP_GUIDANDPATH) {
            LPDIPROPGUIDANDPATH gp = (LPDIPROPGUIDANDPATH)hdr;
            gp->guidClass = GUID_NULL;
            swprintf_s(gp->wszPath, MAX_PATH, L"\\\\?\\splitsecondpad#xinput%d", m_slot);
            return DI_OK;
        }
        if (id == (DWORD_PTR)&DIPROP_JOYSTICKID) {
            ((LPDIPROPDWORD)hdr)->dwData = m_slot;
            return DI_OK;
        }
        if (id == (DWORD_PTR)&DIPROP_AUTOCENTER) {
            ((LPDIPROPDWORD)hdr)->dwData = DIPROPAUTOCENTER_OFF;
            return DI_OK;
        }
        if (id == (DWORD_PTR)&DIPROP_BUFFERSIZE) {
            ((LPDIPROPDWORD)hdr)->dwData = 0;
            return DI_OK;
        }
        return DIERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID prop, LPCDIPROPHEADER hdr) override
    {
        if (!hdr) return DIERR_INVALIDPARAM;
        DWORD_PTR id = (DWORD_PTR)&prop;
        if (id == (DWORD_PTR)&DIPROP_RANGE || id == (DWORD_PTR)&DIPROP_DEADZONE || id == (DWORD_PTR)&DIPROP_SATURATION) {
            int first = 0, last = kNumAxes - 1;
            if (hdr->dwHow != DIPH_DEVICE) {
                int axis = AxisIndex(hdr);
                if (axis < 0) return DIERR_OBJECTNOTFOUND;
                first = last = axis;
            }
            for (int i = first; i <= last; i++) {
                if (id == (DWORD_PTR)&DIPROP_RANGE) {
                    m_min[i] = ((LPCDIPROPRANGE)hdr)->lMin;
                    m_max[i] = ((LPCDIPROPRANGE)hdr)->lMax;
                } else if (id == (DWORD_PTR)&DIPROP_DEADZONE) {
                    m_deadzone[i] = min(((LPCDIPROPDWORD)hdr)->dwData, 10000UL);
                } else {
                    m_saturation[i] = min(((LPCDIPROPDWORD)hdr)->dwData, 10000UL);
                }
            }
            if (id == (DWORD_PTR)&DIPROP_RANGE)
                Log("pad %d: range obj=%lu how=%lu [%ld,%ld]", m_slot, hdr->dwObj, hdr->dwHow,
                    ((LPCDIPROPRANGE)hdr)->lMin, ((LPCDIPROPRANGE)hdr)->lMax);
            else
                Log("pad %d: %s obj=%lu how=%lu %lu", m_slot, id == (DWORD_PTR)&DIPROP_DEADZONE ? "deadzone" : "saturation",
                    hdr->dwObj, hdr->dwHow, ((LPCDIPROPDWORD)hdr)->dwData);
            return DI_OK;
        }
        // autocenter, buffer size, ff gain etc. have nothing to act on
        return DI_OK;
    }

    // Always succeeds: the game retries Acquire in a tight loop for as long as it returns
    // DIERR_INPUTLOST. An empty slot reports itself through Poll/GetDeviceState instead.
    HRESULT STDMETHODCALLTYPE Acquire() override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Unacquire() override { return DI_OK; }

    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, LPVOID data) override
    {
        if (!data || (size != sizeof(DIJOYSTATE) && size != sizeof(DIJOYSTATE2))) return DIERR_INVALIDPARAM;
        if (!s_pads[m_slot].connected) return DIERR_INPUTLOST;
        DIJOYSTATE2 js;
        BuildState(s_pads[m_slot].state.Gamepad, &js);
        memcpy(data, &js, size);
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD count, DWORD) override
    {
        if (count) *count = 0;
        return DIERR_NOTBUFFERED;
    }

    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT fmt) override
    {
        if (!fmt) return DIERR_INVALIDPARAM;
        Log("pad %d: data format size=%lu objs=%lu", m_slot, fmt->dwDataSize, fmt->dwNumObjs);
        m_formatSize = fmt->dwDataSize;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override { return DI_OK; }

    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA out, DWORD obj, DWORD how) override
    {
        if (!out) return DIERR_INVALIDPARAM;
        const PadObject* o = FindObject(obj, how);
        if (!o) return DIERR_OBJECTNOTFOUND;
        FillObjectInstance(*o, out);
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA out) override
    {
        if (!out) return DIERR_INVALIDPARAM;
        FillDeviceInstance(m_slot, out);
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT* out, LPUNKNOWN) override
    {
        if (out) *out = NULL;
        return DIERR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA, LPVOID, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA, REFGUID) override { return DIERR_DEVICENOTREG; }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE) override { return DIERR_UNSUPPORTED; }

    HRESULT STDMETHODCALLTYPE Poll() override
    {
        if (m_slot == 0) OnInputFrame();
        RefreshSlot(m_slot);
        return s_pads[m_slot].connected ? DI_OK : DIERR_INPUTLOST;
    }

    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR, DWORD, LPDIFILEEFFECT, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA) override { return DIERR_UNSUPPORTED; }

private:
    int AxisIndex(LPCDIPROPHEADER hdr) const
    {
        if (hdr->dwHow == DIPH_DEVICE) return 0;
        const PadObject* o = FindObject(hdr->dwObj, hdr->dwHow);
        if (!o || !(o->dwType & DIDFT_AXIS)) return -1;
        return DIDFT_GETINSTANCE(o->dwType);
    }

    // n in [-1,1] -> axis range, honouring the game's DirectInput deadzone/saturation
    LONG AxisValue(int axis, float n) const
    {
        float dz = m_deadzone[axis] / 10000.0f, sat = m_saturation[axis] / 10000.0f;
        float a = fabsf(n);
        if (a <= dz) a = 0;
        else if (a >= sat || sat <= dz) a = 1;
        else a = (a - dz) / (sat - dz);
        n = n < 0 ? -a : a;
        double center = (m_min[axis] + (double)m_max[axis]) / 2;
        double half = (m_max[axis] - (double)m_min[axis]) / 2;
        return (LONG)floor(center + n * half + 0.5);
    }

    void BuildState(const XINPUT_GAMEPAD& g, DIJOYSTATE2* js)
    {
        memset(js, 0, sizeof(*js));
        float lx, ly, rx, ry;
        Stick(g.sThumbLX, g.sThumbLY, &lx, &ly);
        Stick(g.sThumbRX, g.sThumbRY, &rx, &ry);
        float lt = Trigger(g.bLeftTrigger), rt = Trigger(g.bRightTrigger);

        js->lX = AxisValue(0, lx);
        js->lY = AxisValue(1, -ly);  // DirectInput Y grows downwards
        js->lRx = AxisValue(3, rx);
        js->lRy = AxisValue(4, -ry);
        if (g_cfg.separateTriggers) {
            js->lZ = AxisValue(2, lt);
            js->lRz = AxisValue(5, rt);
        } else {
            // what the stock 360 driver reports: both triggers share Z
            js->lZ = AxisValue(2, lt - rt);
            js->lRz = AxisValue(5, 0);
        }

        static const WORD buttons[kNumButtons] = {
            XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
            XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
            XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
            XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
        };
        for (int i = 0; i < kNumButtons; i++)
            js->rgbButtons[i] = (g.wButtons & buttons[i]) ? 0x80 : 0;

        js->rgdwPOV[0] = PovFromDpad(g.wButtons);
        js->rgdwPOV[1] = js->rgdwPOV[2] = js->rgdwPOV[3] = 0xFFFFFFFF;

        WORD prev = m_lastButtons;
        m_lastButtons = g.wButtons;
        if ((g.wButtons & ~prev) || fabsf(lx) > 0.5f || fabsf(ly) > 0.5f || fabsf(rx) > 0.5f || fabsf(ry) > 0.5f ||
            lt > 0.5f || rt > 0.5f)
            NotePadActivity();
    }

    static void Stick(SHORT x, SHORT y, float* ox, float* oy)
    {
        float fx = x / 32767.0f, fy = y / 32767.0f;
        if (fx < -1) fx = -1;
        if (fy < -1) fy = -1;
        float mag = sqrtf(fx * fx + fy * fy);
        float dz = g_cfg.stickDeadzone;
        if (mag <= dz || mag == 0) {
            *ox = *oy = 0;
            return;
        }
        float scaled = (mag - dz) / (1 - dz);
        if (scaled > 1) scaled = 1;
        *ox = fx / mag * scaled;
        *oy = fy / mag * scaled;
    }

    static float Trigger(BYTE v)
    {
        float f = v / 255.0f, dz = g_cfg.triggerDeadzone;
        if (f <= dz) return 0;
        return (f - dz) / (1 - dz);
    }

    static DWORD PovFromDpad(WORD b)
    {
        int x = ((b & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0) - ((b & XINPUT_GAMEPAD_DPAD_LEFT) ? 1 : 0);
        int y = ((b & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0) - ((b & XINPUT_GAMEPAD_DPAD_UP) ? 1 : 0);
        static const DWORD table[3][3] = {
            // x = -1,  0,     1
            { 31500, 0,     4500 },   // y = -1 (up)
            { 27000, 0xFFFFFFFF, 9000 },  // y = 0
            { 22500, 18000, 13500 },  // y = 1 (down)
        };
        return table[y + 1][x + 1];
    }

    LONG m_ref;
    int m_slot;
    DWORD m_formatSize;
    LONG m_min[kNumAxes], m_max[kNumAxes];
    DWORD m_deadzone[kNumAxes], m_saturation[kNumAxes];
    WORD m_lastButtons = 0;
};

// ---------------------------------------------------------------- IDirectInput8A wrapper

struct FilterCtx {
    IDirectInput8A* di;
    LPDIENUMDEVICESCALLBACKA cb;
    LPVOID ref;
    int passed;  // real game controllers handed to the game
};

static bool IsXInputDevice(IDirectInput8A* di, const DIDEVICEINSTANCEA* inst)
{
    IDirectInputDevice8A* dev = NULL;
    if (FAILED(di->CreateDevice(inst->guidInstance, &dev, NULL)) || !dev) return false;
    DIPROPGUIDANDPATH gp = {};
    gp.diph.dwSize = sizeof(gp);
    gp.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    gp.diph.dwHow = DIPH_DEVICE;
    bool xinput = false;
    if (SUCCEEDED(dev->GetProperty(DIPROP_GUIDANDPATH, &gp.diph))) {
        _wcsupr_s(gp.wszPath, MAX_PATH);
        xinput = wcsstr(gp.wszPath, L"IG_") != NULL;
    }
    dev->Release();
    return xinput;
}

static BOOL CALLBACK FilterCallback(LPCDIDEVICEINSTANCEA inst, LPVOID p)
{
    FilterCtx* ctx = (FilterCtx*)p;
    if (g_cfg.hideXInputDevices && IsXInputDevice(ctx->di, inst)) {
        Log("hiding XInput device '%s' from DirectInput", inst->tszProductName);
        return DIENUM_CONTINUE;
    }
    Log("passing through device '%s'", inst->tszProductName);
    ctx->passed++;
    return ctx->cb(inst, ctx->ref);
}

class DI8Wrap : public IDirectInput8A {
public:
    explicit DI8Wrap(IDirectInput8A* real) : m_ref(1), m_real(real) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == IID_IUnknown || riid == IID_IDirectInput8A) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return m_real->QueryInterface(riid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_ref);
        if (!r) {
            m_real->Release();
            delete this;
        }
        return r;
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN outer) override
    {
        int slot;
        if (IsVirtualGuid(guid, &slot)) {
            if (!out) return DIERR_INVALIDPARAM;
            *out = new VirtualPad(slot);
            return DI_OK;
        }
        return m_real->CreateDevice(guid, out, outer);
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD type, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD flags) override
    {
        if (!cb) return DIERR_INVALIDPARAM;
        bool padClass = type == DI8DEVCLASS_ALL || type == DI8DEVCLASS_GAMECTRL || type == DI8DEVTYPE_GAMEPAD;
        Log("EnumDevices type=%lu flags=%lx", type, flags);
        if (!padClass) return m_real->EnumDevices(type, cb, ref, flags);

        // virtual pads first, so the game's joystick index equals the XInput slot (it rumbles by that index)
        if (!(flags & DIEDFL_FORCEFEEDBACK)) {
            for (int i = 0; i < g_cfg.virtualPads; i++) {
                RefreshSlot(i);
                DIDEVICEINSTANCEA inst;
                inst.dwSize = sizeof(inst);
                FillDeviceInstance(i, &inst);
                if (cb(&inst, ref) == DIENUM_STOP) return DI_OK;
            }
        }
        FilterCtx ctx = { m_real, cb, ref, 0 };
        HRESULT hr = m_real->EnumDevices(type, FilterCallback, &ctx, flags);
        // the joystick binding names are shared by all devices; only rename them when every
        // joystick the game sees is one of ours
        static bool named;
        if (!named && !ctx.passed) {
            named = true;
            PatchPadNames();
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID guid) override
    {
        int slot;
        if (IsVirtualGuid(guid, &slot)) return PadSlotConnected(slot) ? DI_OK : DI_NOTATTACHED;
        return m_real->GetDeviceStatus(guid);
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND w, DWORD f) override { return m_real->RunControlPanel(w, f); }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD v) override { return m_real->Initialize(h, v); }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID cls, LPCSTR name, LPGUID out) override { return m_real->FindDevice(cls, name, out); }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR user, LPDIACTIONFORMATA af, LPDIENUMDEVICESBYSEMANTICSCBA cb, LPVOID ref, DWORD flags) override
    {
        return m_real->EnumDevicesBySemantics(user, af, cb, ref, flags);
    }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK cb, LPDICONFIGUREDEVICESPARAMSA p, DWORD flags, LPVOID ref) override
    {
        return m_real->ConfigureDevices(cb, p, flags, ref);
    }

private:
    LONG m_ref;
    IDirectInput8A* m_real;
};

IDirectInput8A* WrapDirectInput8A(IDirectInput8A* real)
{
    Log("wrapping IDirectInput8A");
    return new DI8Wrap(real);
}
