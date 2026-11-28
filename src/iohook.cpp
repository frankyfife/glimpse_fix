// File I/O hooks for virtual archives (see iohook.h, DESIGN.md)
#include "iohook.h"
#include <stdio.h>
#include <string.h>
#include <unordered_map>

IoReal g_ioReal;
IoStats g_ioStats;

namespace {

struct FileId {
    DWORD vol, hi, lo;
    bool operator==(const FileId& o) const { return vol == o.vol && hi == o.hi && lo == o.lo; }
};

struct Target {
    FileId id;
    DWORD size;  // the real file's size: only a handle of that size is asked for its file id
    const VirtualArchive* va;
};

struct VHandle {
    const VirtualArchive* va;
    uint64_t pos;  // file pointer for synchronous handles (overlapped ones carry it in OVERLAPPED)
    FileId id;     // the file it was recognised as (a handle value closed past this hook can come back for another)
};

SRWLOCK s_lock = SRWLOCK_INIT;
// on the heap and never freed: at process exit, CloseHandle calls of other modules can still arrive after
// this DLL's static objects are gone
std::vector<Target>& s_targets = *new std::vector<Target>();
std::unordered_map<HANDLE, VHandle>& s_handles = *new std::unordered_map<HANDLE, VHandle>();

struct Saved {
    void** slot;
    void* old;
};
std::vector<Saved>& s_saved = *new std::vector<Saved>();

bool GetId(HANDLE h, FileId* id)
{
    BY_HANDLE_FILE_INFORMATION fi;
    if (!GetFileInformationByHandle(h, &fi)) return false;
    id->vol = fi.dwVolumeSerialNumber;
    id->hi = fi.nFileIndexHigh;
    id->lo = fi.nFileIndexLow;
    return true;
}

bool Lookup(HANDLE h, VHandle* out)
{
    AcquireSRWLockShared(&s_lock);
    auto it = s_handles.find(h);
    bool found = it != s_handles.end();
    if (found) *out = it->second;
    ReleaseSRWLockShared(&s_lock);
    return found;
}

void SetPos(HANDLE h, uint64_t pos)
{
    AcquireSRWLockExclusive(&s_lock);
    auto it = s_handles.find(h);
    if (it != s_handles.end()) it->second.pos = pos;
    ReleaseSRWLockExclusive(&s_lock);
}

bool Swap(void** slot, void* hook)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

}  // namespace

void IoInitReal()
{
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    g_ioReal.ReadFile = (ReadFile_t)GetProcAddress(k32, "ReadFile");
    g_ioReal.SetFilePointer = (SetFilePointer_t)GetProcAddress(k32, "SetFilePointer");
    g_ioReal.GetFileSize = (GetFileSize_t)GetProcAddress(k32, "GetFileSize");
    g_ioReal.CloseHandle = (CloseHandle_t)GetProcAddress(k32, "CloseHandle");
}

bool IoAddVirtualFile(const char* path, const VirtualArchive* va)
{
    HANDLE f = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    Target t;
    bool ok = GetId(f, &t.id) && (t.id.vol || t.id.hi || t.id.lo);  // a volume without file ids: not used
    LARGE_INTEGER sz = {};
    ok = ok && GetFileSizeEx(f, &sz) && sz.QuadPart < 0xFFFFFFFF;
    t.size = ok ? (DWORD)sz.QuadPart : 0;
    (g_ioReal.CloseHandle ? g_ioReal.CloseHandle : CloseHandle)(f);
    if (!ok) return false;
    t.va = va;
    AcquireSRWLockExclusive(&s_lock);
    s_targets.push_back(t);
    ReleaseSRWLockExclusive(&s_lock);
    return true;
}

size_t IoVirtualHandleCount()
{
    AcquireSRWLockShared(&s_lock);
    size_t n = s_handles.size();
    ReleaseSRWLockShared(&s_lock);
    return n;
}

void Log(const char* fmt, ...);  // main.cpp

DWORD WINAPI Hook_GetFileSize(HANDLE h, LPDWORD high)
{
    VHandle v;
    bool known = Lookup(h, &v);
    if (known) {
        // still the file it was recognised as? A handle closed past CloseHandle (NtClose, another module's own
        // import of it) leaves its entry behind, and the same value may come back for another file: the game asks
        // for the size right after every open, so the entry is checked here before any read
        FileId id;
        if (!GetId(h, &id) || !(id == v.id)) {
            AcquireSRWLockExclusive(&s_lock);
            s_handles.erase(h);
            ReleaseSRWLockExclusive(&s_lock);
            static volatile LONG logged;
            if (InterlockedIncrement(&logged) <= 4) Log("DLC: a stale archive handle entry dropped (the handle is another file now)");
            known = false;
        }
    }
    if (!known) {
        // the game asks for the size right after opening (0x5734E1): the moment to recognise the archive.
        // Only a file of the registered size is asked for its id, so other handles cost one call.
        DWORD realHigh = 0;
        const DWORD real = g_ioReal.GetFileSize(h, &realHigh);
        const DWORD err = GetLastError();
        bool candidate = false;
        AcquireSRWLockShared(&s_lock);
        for (const Target& t : s_targets)
            if (real == t.size && realHigh == 0) candidate = true;
        ReleaseSRWLockShared(&s_lock);
        FileId id = {};
        const VirtualArchive* va = NULL;
        if (candidate && GetId(h, &id)) {
            AcquireSRWLockShared(&s_lock);
            for (const Target& t : s_targets)
                if (t.id == id && t.size == real) va = t.va;
            ReleaseSRWLockShared(&s_lock);
        }
        if (!va) {
            if (high) *high = realHigh;
            SetLastError(err);
            return real;
        }
        AcquireSRWLockExclusive(&s_lock);
        s_handles[h] = VHandle{va, 0, id};
        ReleaseSRWLockExclusive(&s_lock);
        InterlockedIncrement(&g_ioStats.registered);
        v.va = va;
    }
    if (high) *high = 0;
    SetLastError(NO_ERROR);
    return v.va->Size();
}

BOOL WINAPI Hook_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov)
{
    VHandle v;
    if (!Lookup(h, &v)) {
        InterlockedIncrement(&g_ioStats.passthrough);
        return g_ioReal.ReadFile(h, buf, n, got, ov);
    }
    const VirtualArchive* va = v.va;
    const uint64_t pos = ov ? ((uint64_t)ov->OffsetHigh << 32 | ov->Offset) : v.pos;
    if (pos >= va->Size()) {
        if (got) *got = 0;
        if (ov) {
            SetLastError(ERROR_HANDLE_EOF);
            return FALSE;
        }
        return TRUE;
    }
    if (pos >= va->BaseDataStart() && pos + n <= va->BaseDataEnd()) {
        // entirely in the base file's data: the real read at the shifted offset (stays asynchronous)
        InterlockedIncrement(&g_ioStats.translated);
        const uint64_t real = pos - va->Delta();
        if (ov) {
            const DWORD so = ov->Offset, sh = ov->OffsetHigh;
            ov->Offset = (DWORD)real;  // read by the kernel when the request is issued
            ov->OffsetHigh = (DWORD)(real >> 32);
            BOOL r = g_ioReal.ReadFile(h, buf, n, got, ov);
            DWORD e = GetLastError();
            ov->Offset = so;  // the game keeps its own (virtual) position there
            ov->OffsetHigh = sh;
            SetLastError(e);
            return r;
        }
        LONG hi = (LONG)(real >> 32);
        if (g_ioReal.SetFilePointer(h, (LONG)(DWORD)real, &hi, FILE_BEGIN) == INVALID_SET_FILE_POINTER &&
            GetLastError() != NO_ERROR)
            return FALSE;
        DWORD g = 0;
        BOOL r = g_ioReal.ReadFile(h, buf, n, &g, NULL);
        if (got) *got = g;
        if (r) SetPos(h, pos + g);
        return r;
    }
    // header, merged index, overlay data or a read across a border: served from the virtual archive
    InterlockedIncrement(&g_ioStats.synthetic);
    uint32_t g = 0;
    if (!va->Read((uint32_t)pos, buf, n, &g)) {
        SetLastError(ERROR_READ_FAULT);
        return FALSE;
    }
    if (got) *got = g;
    if (ov) {
        // completed at once, as a cached read would; GetOverlappedResult finds Internal != STATUS_PENDING
        ov->Internal = 0;  // STATUS_SUCCESS
        ov->InternalHigh = g;
        HANDLE ev = (HANDLE)((ULONG_PTR)ov->hEvent & ~(ULONG_PTR)1);
        if (ev) SetEvent(ev);
    } else {
        SetPos(h, pos + g);
    }
    return TRUE;
}

DWORD WINAPI Hook_SetFilePointer(HANDLE h, LONG dist, PLONG high, DWORD method)
{
    VHandle v;
    if (!Lookup(h, &v)) return g_ioReal.SetFilePointer(h, dist, high, method);
    int64_t d = high ? ((int64_t)*high << 32) | (uint32_t)dist : (int64_t)dist;
    int64_t from = method == FILE_BEGIN ? 0 : method == FILE_CURRENT ? (int64_t)v.pos : (int64_t)v.va->Size();
    int64_t to = from + d;
    if (to < 0) {
        SetLastError(ERROR_NEGATIVE_SEEK);
        return INVALID_SET_FILE_POINTER;
    }
    SetPos(h, (uint64_t)to);
    if (high) *high = (LONG)(to >> 32);
    SetLastError(NO_ERROR);
    return (DWORD)to;
}

BOOL WINAPI Hook_CloseHandle(HANDLE h)
{
    // every CloseHandle of the process comes here: only a known handle takes the exclusive lock
    VHandle v;
    if (Lookup(h, &v)) {
        AcquireSRWLockExclusive(&s_lock);
        s_handles.erase(h);
        ReleaseSRWLockExclusive(&s_lock);
    }
    return g_ioReal.CloseHandle(h);
}

int IoInstallKernel32Slots()
{
    static const char* const names[4] = {"ReadFile", "SetFilePointer", "GetFileSize", "CloseHandle"};
    void* const hooks[4] = {(void*)Hook_ReadFile, (void*)Hook_SetFilePointer, (void*)Hook_GetFileSize,
                            (void*)Hook_CloseHandle};
    void** real[4] = {(void**)&g_ioReal.ReadFile, (void**)&g_ioReal.SetFilePointer, (void**)&g_ioReal.GetFileSize,
                      (void**)&g_ioReal.CloseHandle};
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    void** slots[4];
    for (int i = 0; i < 4; i++) {
        const BYTE* p = (const BYTE*)GetProcAddress(k32, names[i]);
        if (!p || p[0] != 0xFF || p[1] != 0x25) return 0;  // not a jmp [slot] thunk: leave everything alone
        slots[i] = *(void***)(p + 2);
        // the slot must be kernel32's own (its import table), and still hold a function outside kernel32 (an empty
        // slot or one that points back into kernel32 would become the "real" function every read and close goes to)
        HMODULE owner = NULL, target = NULL;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)slots[i], &owner) || owner != k32 || !*slots[i])
            return 0;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)*slots[i], &target) && target == k32)
            return 0;
    }
    // a second call: the slots hold our hooks already, which must not become the "real" functions (endless recursion)
    int ours = 0;
    for (int i = 0; i < 4; i++)
        if (*slots[i] == hooks[i]) ours++;
    if (ours) return ours == 4 ? 4 : 0;
    s_saved.reserve(s_saved.size() + 4);  // before any slot changes: a swapped slot is always in the list
    int done = 0;
    for (int i = 0; i < 4; i++) {
        *real[i] = *slots[i];  // KernelBase's function
        void* old = *slots[i];
        if (!Swap(slots[i], hooks[i])) break;
        s_saved.push_back({slots[i], old});
        done++;
    }
    if (done != 4) IoUninstall();
    return done == 4 ? 4 : 0;
}

typedef HANDLE(WINAPI* CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileA_t s_realCreateFileA;
static CreateFileW_t s_realCreateFileW;
static IoRedirect_t s_redirect;

static HANDLE WINAPI Hook_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                      DWORD flags, HANDLE tmpl)
{
    char other[MAX_PATH];
    if (name && s_redirect && s_redirect(name, other, sizeof(other))) name = other;
    return s_realCreateFileA(name, access, share, sa, disp, flags, tmpl);
}

static HANDLE WINAPI Hook_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                      DWORD flags, HANDLE tmpl)
{
    // Bink and FMOD may open their files wide: names the ANSI code page holds exactly are offered for redirection
    // (a game folder with umlauts included; the redirect and the rest of the fix work with ANSI paths)
    char a[MAX_PATH], other[MAX_PATH];
    WCHAR w[MAX_PATH];
    if (name && s_redirect) {
        BOOL lossy = FALSE;
        if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, name, -1, a, MAX_PATH, NULL, &lossy) && !lossy &&
            s_redirect(a, other, sizeof(other)) && MultiByteToWideChar(CP_ACP, 0, other, -1, w, MAX_PATH))
            name = w;
    }
    return s_realCreateFileW(name, access, share, sa, disp, flags, tmpl);
}

// The game asks whether a file exists through its "" device's file finder (0x402300 -> WinFileFinder 0x573550),
// which calls FindFirstFileA: the level constructor 0x7C6140 builds its sector texture set only when
// <level>.Static.ark is found that way (0x7C2FE0). Names are redirected there as for CreateFile. The search name
// is a plain path (no wildcard) for these checks; a pattern is not one of the redirect's known names anyway.
typedef HANDLE(WINAPI* FindFirstFileA_t)(LPCSTR, LPWIN32_FIND_DATAA);
static FindFirstFileA_t s_realFindFirstFileA;

static HANDLE WINAPI Hook_FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA data)
{
    char other[MAX_PATH];
    if (name && s_redirect && s_redirect(name, other, sizeof(other))) {
        // its own line: the test log shows that the game's existence check (0x573550) was sent there too
        static volatile LONG logged;
        HANDLE h = s_realFindFirstFileA(other, data);
        if (InterlockedIncrement(&logged) <= 8)
            Log("DLC tracks: existence check of %s found it at %s: %s", name, other,
                h != INVALID_HANDLE_VALUE ? "yes" : "no");
        return h;
    }
    return s_realFindFirstFileA(name, data);
}

int IoInstallCreateFileRedirect(IoRedirect_t fn)
{
    static const char* const names[3] = {"CreateFileA", "CreateFileW", "FindFirstFileA"};
    void* const hooks[3] = {(void*)Hook_CreateFileA, (void*)Hook_CreateFileW, (void*)Hook_FindFirstFileA};
    void** real[3] = {(void**)&s_realCreateFileA, (void**)&s_realCreateFileW, (void**)&s_realFindFirstFileA};
    if (s_redirect) return 0;  // once
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    void** slots[3];
    for (int i = 0; i < 3; i++) {
        const BYTE* p = (const BYTE*)GetProcAddress(k32, names[i]);
        if (!p || p[0] != 0xFF || p[1] != 0x25) return 0;  // not a jmp [slot] thunk: leave everything alone
        slots[i] = *(void***)(p + 2);
        HMODULE owner = NULL;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)slots[i], &owner) || owner != k32 || !*slots[i])
            return 0;
    }
    s_saved.reserve(s_saved.size() + 3);  // before any slot changes: a swapped slot is always in the list
    s_redirect = fn;
    int done = 0;
    for (int i = 0; i < 3; i++) {
        *real[i] = *slots[i];  // KernelBase's function, set before the slot points at the hook
        void* old = *slots[i];
        if (!Swap(slots[i], hooks[i])) break;
        s_saved.push_back({slots[i], old});
        done++;
    }
    if (done != 3) {  // all three or none: a level found by CreateFile but not by FindFirstFile would crash
        for (int i = 0; i < done; i++) {
            Swap(s_saved.back().slot, s_saved.back().old);
            s_saved.pop_back();
        }
        s_redirect = NULL;
        return 0;
    }
    return done;
}

void IoUninstall()
{
    for (size_t i = s_saved.size(); i-- > 0;) Swap(s_saved[i].slot, s_saved[i].old);
    s_saved.clear();
}

// Diagnostic (#16, Wine/Proton): what kernel32's file exports look like where the checks above fail. Reads only:
// no VirtualProtect, no write, no slot swapped; every read is checked (VirtualQuery) and/or inside __try.
namespace {

#define DIAG_ADDR(p) ((unsigned)(uintptr_t)(p))

bool DiagReadable(const void* p, size_t n)
{
    uintptr_t cur = (uintptr_t)p, end = cur + n;
    if (!p || end < cur) return false;
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)cur, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= cur) return false;
        cur = next;
    }
    return true;
}

bool DiagRead(void* dst, const void* src, size_t n)
{
    if (!DiagReadable(src, n)) return false;
    __try {
        memcpy(dst, src, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// the file name (no folder) of the module that holds addr
const char* DiagModule(const void* addr, char* out, size_t size)
{
    HMODULE m = NULL;
    char path[MAX_PATH];
    DWORD len = 0;
    if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)addr, &m))
        len = GetModuleFileNameA(m, path, MAX_PATH);
    if (!len) {
        strcpy_s(out, size, m ? "(module without a name)" : "(no module)");
        return out;
    }
    path[MAX_PATH - 1] = 0;
    const char* name = path;
    for (const char* c = path; *c; c++)
        if (*c == '\\' || *c == '/') name = c + 1;
    strcpy_s(out, size, name);
    return out;
}

// n bytes in hex
const char* DiagHex(const BYTE* b, size_t n, char* out, size_t size)
{
    size_t o = 0;
    strcpy_s(out, size, n ? "" : "(not readable)");
    for (size_t i = 0; i < n && o + 4 <= size; i++) o += (size_t)sprintf_s(out + o, size - o, i ? " %02X" : "%02X", b[i]);
    return out;
}

DWORD DiagImageSize(HMODULE mod)
{
    __try {
        const BYTE* base = (const BYTE*)mod;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE ? nt->OptionalHeader.SizeOfImage : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// mod's export directory entry for name: 1 = code at *rva, 2 = a forwarder (its string in fwd), 0 = no such name,
// -1 = headers or export directory not readable
int DiagExport(HMODULE mod, const char* name, DWORD* rva, char* fwd, size_t fwdSize)
{
    __try {
        const BYTE* base = (const BYTE*)mod;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return -1;
        const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
            return -1;
        DWORD dirRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        DWORD dirSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        if (!dirRva) return -1;
        const IMAGE_EXPORT_DIRECTORY* ed = (const IMAGE_EXPORT_DIRECTORY*)(base + dirRva);
        const DWORD* names = (const DWORD*)(base + ed->AddressOfNames);
        const WORD* ords = (const WORD*)(base + ed->AddressOfNameOrdinals);
        const DWORD* funcs = (const DWORD*)(base + ed->AddressOfFunctions);
        for (DWORD i = 0; i < ed->NumberOfNames; i++) {
            if (strcmp((const char*)base + names[i], name)) continue;
            if (ords[i] >= ed->NumberOfFunctions) return -1;
            *rva = funcs[ords[i]];
            if (*rva < dirRva || *rva >= dirRva + dirSize) return 1;
            const char* s = (const char*)base + *rva;  // inside the export directory: "DLL.Function"
            size_t k = 0;
            for (; k + 1 < fwdSize && s[k]; k++) fwd[k] = (s[k] >= 32 && s[k] < 127) ? s[k] : '?';
            fwd[k] = 0;
            return 2;
        }
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// the check of IoInstallKernel32Slots (withTarget) or IoInstallCreateFileRedirect, as it would decide now: NULL
// when it passes, else the first condition that fails
const char* DiagWhyNot(HMODULE k32, const void* exp, bool withTarget)
{
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    BYTE b[6];
    if (!exp) return "GetProcAddress gives NULL";
    if (!DiagRead(b, exp, 2)) return "the export is not readable";
    if (b[0] != 0xFF || b[1] != 0x25) return "no FF 25 at the export";
    if (!DiagRead(b, exp, 6)) return "the FF 25 operand is not readable";
    void* slot;
    memcpy(&slot, b + 2, sizeof(slot));
    HMODULE owner = NULL;
    if (!GetModuleHandleExA(flags, (LPCSTR)slot, &owner) || owner != k32) return "the slot is not in kernel32";
    void* target = NULL;
    if (!DiagRead(&target, slot, sizeof(target)) || !target) return "the slot is empty or not readable";
    HMODULE tmod = NULL;
    if (withTarget && GetModuleHandleExA(flags, (LPCSTR)target, &tmod) && tmod == k32)
        return "the slot points back into kernel32";
    return NULL;
}

void DiagVerdict(HMODULE k32, const void* exp, bool withTarget, char* out, size_t size)
{
    const char* why = DiagWhyNot(k32, exp, withTarget);
    if (why)
        sprintf_s(out, size, "not an import thunk (%s)", why);
    else
        strcpy_s(out, size, "import thunk ok");
}

// one line per address from the export on: its bytes, then the jump there (followed, at most 3) or "body"
void DiagSteps(const char* name, const BYTE* p)
{
    for (int step = 0;; step++) {
        char mod[MAX_PATH], hex[64], pro[64] = "", jump[MAX_PATH * 2 + 96];
        DiagModule(p, mod, sizeof(mod));
        BYTE b[16];
        size_t have = sizeof(b);  // 16 bytes, fewer when the readable memory ends before
        while (have && !DiagRead(b, p, have)) have--;
        DiagHex(b, have, hex, sizeof(hex));
        size_t at = 0;
        if (have >= 2 && b[0] == 0x8B && b[1] == 0xFF) {
            at = 2;
            strcpy_s(pro, "8B FF (mov edi,edi), then ");
            if (have >= 5 && b[2] == 0x55 && b[3] == 0x8B && b[4] == 0xEC) {
                at = 5;
                strcpy_s(pro, "8B FF 55 8B EC (mov edi,edi; push ebp; mov ebp,esp), then ");
            }
        }
        const BYTE* next = NULL;
        if (!have) {
            strcpy_s(jump, "not readable");
        } else if (have >= at + 6 && b[at] == 0xFF && b[at + 1] == 0x25) {
            void* slot;
            void* target = NULL;
            char smod[MAX_PATH], tmod[MAX_PATH];
            memcpy(&slot, b + at + 2, sizeof(slot));
            DiagModule(slot, smod, sizeof(smod));
            if (DiagRead(&target, slot, sizeof(target))) {
                sprintf_s(jump, "FF 25 jmp [%08X] (slot in %s) -> %08X (%s)", DIAG_ADDR(slot), smod, DIAG_ADDR(target),
                          DiagModule(target, tmod, sizeof(tmod)));
                next = (const BYTE*)target;
            } else {
                sprintf_s(jump, "FF 25 jmp [%08X] (slot in %s), the slot is not readable", DIAG_ADDR(slot), smod);
            }
        } else if (have >= at + 5 && b[at] == 0xE9) {
            LONG rel;
            char tmod[MAX_PATH];
            memcpy(&rel, b + at + 1, sizeof(rel));
            next = p + at + 5 + rel;
            sprintf_s(jump, "E9 jmp rel32 -> %08X (%s)", DIAG_ADDR(next), DiagModule(next, tmod, sizeof(tmod)));
        } else if (have >= at + 2 && b[at] == 0xEB) {
            char tmod[MAX_PATH];
            next = p + at + 2 + (signed char)b[at + 1];
            sprintf_s(jump, "EB jmp rel8 -> %08X (%s)", DIAG_ADDR(next), DiagModule(next, tmod, sizeof(tmod)));
        } else {
            strcpy_s(jump, "body");
        }
        if (next && step == 3) {  // three jumps followed already: this one is only shown
            Log("K32 diag %s [%d] %08X %s: %s | %s%s, not followed", name, step, DIAG_ADDR(p), mod, hex, pro, jump);
            return;
        }
        Log("K32 diag %s [%d] %08X %s: %s | %s%s", name, step, DIAG_ADDR(p), mod, hex, pro, jump);
        if (!next) return;
        p = next;
    }
}

// a string returned by Wine's ntdll (wine_get_version, wine_get_build_id), copied with every read checked
void DiagWineString(const char* fn, char* out, size_t size)
{
    typedef const char*(__cdecl * WineStr_t)();
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    WineStr_t f = nt ? (WineStr_t)GetProcAddress(nt, fn) : NULL;
    out[0] = 0;
    if (!f) return;
    const char* s = NULL;
    __try {
        s = f();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s = NULL;
    }
    size_t k = 0;
    char c = 0;
    for (; s && k + 1 < size && DiagRead(&c, s + k, 1) && c; k++) out[k] = (c >= 32 && c < 127) ? c : '?';
    out[k] = 0;
    if (!k) strcpy_s(out, size, s ? "(empty)" : "(no string)");
}

}  // namespace

void IoLogKernel32Exports()
{
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    char mod[MAX_PATH], wine[128], build[128];
    DiagWineString("wine_get_version", wine, sizeof(wine));
    DiagWineString("wine_get_build_id", build, sizeof(build));
    bool isWine = nt && GetProcAddress(nt, "wine_get_version");
    if (isWine)
        Log("K32 diag: ntdll exports wine_get_version: %s (build id: %s)", wine, build[0] ? build : "not exported");
    else
        Log("K32 diag: ntdll does not export wine_get_version (not Wine)");
    if (!k32) {
        Log("K32 diag: no kernel32.dll module");
        return;
    }
    Log("K32 diag: kernel32 module %s at %08X, image size %08X", DiagModule(k32, mod, sizeof(mod)), DIAG_ADDR(k32),
        DiagImageSize(k32));
    static const char* const names[7] = {"ReadFile",    "SetFilePointer", "GetFileSize",   "CloseHandle",
                                         "CreateFileA", "CreateFileW",    "FindFirstFileA"};
    for (int i = 0; i < 7; i++) {
        const BYTE* p = (const BYTE*)GetProcAddress(k32, names[i]);
        DWORD rva = 0;
        char fwd[128] = "", entry[192], verdict[96];
        switch (DiagExport(k32, names[i], &rva, fwd, sizeof(fwd))) {
        case 2: sprintf_s(entry, "export entry is a forwarder to \"%s\"", fwd); break;
        case 1: sprintf_s(entry, "export entry rva %08X (%08X), not a forwarder", rva, DIAG_ADDR(k32) + rva); break;
        case 0: strcpy_s(entry, "no export entry of that name"); break;
        default: strcpy_s(entry, "export directory not readable"); break;
        }
        DiagVerdict(k32, p, i < 4, verdict, sizeof(verdict));
        Log("K32 diag %s: GetProcAddress %08X (%s); %s; current check (%s): %s", names[i], DIAG_ADDR(p),
            p ? DiagModule(p, mod, sizeof(mod)) : "-", entry, i < 4 ? "overlay slots" : "name redirect", verdict);
        if (p) DiagSteps(names[i], p);
    }
}
