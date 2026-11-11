// Split/Second PC fix - Log=1: the system, the game executable and foreign modules, for bug reports
//
// Bug reports should show at a glance whether the game is the supported Steam build, whether other mods
// or proxy DLLs are in play, and on what hardware it runs. Only read, nothing in the game is touched.
#include "common.h"
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>

// CRC-32 of a file (IEEE), 0 if it cannot be read
static DWORD FileCrc(const char* path, DWORD* size)
{
    static DWORD table[256];
    if (!table[1])
        for (DWORD i = 0; i < 256; i++) {
            DWORD c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    *size = 0;
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    static BYTE buf[1 << 16];
    DWORD crc = 0xFFFFFFFF, got;
    while (ReadFile(f, buf, sizeof(buf), &got, NULL) && got) {
        for (DWORD i = 0; i < got; i++) crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
        *size += got;
    }
    CloseHandle(f);
    return crc ^ 0xFFFFFFFF;
}

static void LogSystem()
{
    typedef LONG(WINAPI * RtlGetVersion_t)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    OSVERSIONINFOW os = { sizeof(os) };
    RtlGetVersion_t getVersion = ntdll ? (RtlGetVersion_t)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    if (getVersion) getVersion(&os);
    typedef const char*(__cdecl * WineVersion_t)();
    WineVersion_t wine = ntdll ? (WineVersion_t)GetProcAddress(ntdll, "wine_get_version") : NULL;
    // the real version from the registry; the game may be told an older one (compatibility mode)
    char product[96] = "Windows", display[32] = "", build[16] = "";
    DWORD major = 0, minor = 0, ubr = 0, n;
    HKEY cv;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ | KEY_WOW64_64KEY,
                      &cv) == ERROR_SUCCESS) {
        n = sizeof(product);
        RegQueryValueExA(cv, "ProductName", NULL, NULL, (BYTE*)product, &n);
        n = sizeof(display);
        RegQueryValueExA(cv, "DisplayVersion", NULL, NULL, (BYTE*)display, &n);
        n = sizeof(build);
        RegQueryValueExA(cv, "CurrentBuildNumber", NULL, NULL, (BYTE*)build, &n);
        n = sizeof(major);
        RegQueryValueExA(cv, "CurrentMajorVersionNumber", NULL, NULL, (BYTE*)&major, &n);
        n = sizeof(minor);
        RegQueryValueExA(cv, "CurrentMinorVersionNumber", NULL, NULL, (BYTE*)&minor, &n);
        n = sizeof(ubr);
        RegQueryValueExA(cv, "UBR", NULL, NULL, (BYTE*)&ubr, &n);
        RegCloseKey(cv);
    }
    Log("system: %s %s (%lu.%lu build %s.%lu)%s%s", product, display, major, minor, build[0] ? build : "?", ubr,
        wine ? ", Wine " : "", wine ? wine() : "");
    if (major && (os.dwMajorVersion != major || os.dwBuildNumber != (DWORD)atoi(build)))
        Log("system: the game is told Windows %lu.%lu build %lu (compatibility mode)", os.dwMajorVersion, os.dwMinorVersion,
            os.dwBuildNumber);

    int regs[4];
    char cpu[49] = {};
    __cpuid(regs, 0x80000000);
    if ((unsigned)regs[0] >= 0x80000004)
        for (int i = 0; i < 3; i++) __cpuid((int*)(cpu + i * 16), 0x80000002 + i);
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    MEMORYSTATUSEX mem = { sizeof(mem) };
    GlobalMemoryStatusEx(&mem);
    const char* name = cpu;
    while (*name == ' ') name++;
    for (char* e = cpu + strlen(cpu); e > cpu && e[-1] == ' ';) *--e = 0;
    Log("system: %s, %lu threads, %llu MB RAM", name, si.dwNumberOfProcessors, mem.ullTotalPhys >> 20);

    // graphics cards with their memory, from the display adapters' driver keys
    HKEY cls;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
                      0, KEY_READ, &cls) == ERROR_SUCCESS) {
        for (DWORD i = 0; i < 16; i++) {
            char sub[16];
            sprintf(sub, "%04lu", i);
            HKEY k;
            if (RegOpenKeyExA(cls, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
            char desc[128] = {}, driver[64] = {};
            n = sizeof(desc);
            RegQueryValueExA(k, "DriverDesc", NULL, NULL, (BYTE*)desc, &n);
            n = sizeof(driver);
            RegQueryValueExA(k, "DriverVersion", NULL, NULL, (BYTE*)driver, &n);
            ULONGLONG vram = 0;
            n = sizeof(vram);
            if (RegQueryValueExA(k, "HardwareInformation.qwMemorySize", NULL, NULL, (BYTE*)&vram, &n) != ERROR_SUCCESS) {
                DWORD v32 = 0;
                n = sizeof(v32);
                if (RegQueryValueExA(k, "HardwareInformation.MemorySize", NULL, NULL, (BYTE*)&v32, &n) == ERROR_SUCCESS) vram = v32;
            }
            RegCloseKey(k);
            if (desc[0]) Log("system: GPU %s, %llu MB video memory, driver %s", desc, vram >> 20, driver[0] ? driver : "?");
        }
        RegCloseKey(cls);
    }
}

static void LogExe()
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    DWORD size;
    DWORD crc = FileCrc(path, &size);
    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    const char* file = strrchr(path, '\\');
    Log("exe: %s, %lu bytes, CRC32 %08lX, built %08lX, large address aware %d", file ? file + 1 : path, size, crc,
        nt->FileHeader.TimeDateStamp, (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0);
    Log("exe: %s", path);
}

// modules loaded from outside the Windows folder (the game's own, overlays, other mods and proxies)
void LogForeignModules(const char* when)
{
    if (!g_cfg.log) return;
    char windir[MAX_PATH];
    UINT wl = GetWindowsDirectoryA(windir, MAX_PATH);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32 me = { sizeof(me) };
    int n = 0;
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
        if (wl && !_strnicmp(me.szExePath, windir, wl)) continue;
        Log("modules %s: %s (%lu KB)", when, me.szExePath, me.modBaseSize >> 10);
        n++;
    }
    CloseHandle(snap);
    Log("modules %s: %d outside the Windows folder", when, n);
}

// DLL and ASI files next to the game, loaded or not (proxies and mod loaders)
static void LogGameFolderDlls()
{
    char dir[MAX_PATH], pattern[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\');
    if (!slash) return;
    slash[1] = 0;
    const char* kinds[] = { "*.dll", "*.asi" };
    for (const char* kind : kinds) {
        sprintf(pattern, "%s%s", dir, kind);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do Log("game folder: %s (%lu KB)", fd.cFileName, fd.nFileSizeLow >> 10);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}

void LogEnvironment()
{
    if (!g_cfg.log) return;
    Log("glimpse_fix %s", GLIMPSE_VERSION);
    LogSystem();
    LogExe();
    LogGameFolderDlls();
    LogForeignModules("at start");
}
