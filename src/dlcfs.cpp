// Split/Second PC fix - converted content made visible to the game as part of data\resident.ark
// ([DLC] Overlays=1, experimental).
//
// The game reads all its resident content from one archive, data\resident.ark: one index of keys (the
// lower-case path hashed h*31+c) with offsets, and the data. Every PC-format archive in the folder
// glimpse_dlc next to the game is laid over it: the game is shown one archive with a merged index (an
// overlay entry replaces a base entry with the same key, new keys are added) whose data comes from the
// original file and from the overlays (vark.cpp). Nothing in the game folder changes.
//
// How: the game opens resident.ark with CreateFileA and asks for its size with GetFileSize right away;
// from then on that handle is served as the merged archive (iohook.cpp). The four functions it uses
// (ReadFile, GetFileSize, SetFilePointer, CloseHandle) are taken over by swapping the pointers in
// kernel32's own import table behind its exports (data in a Windows DLL); every other file passes
// through unchanged. It is done here, while the DLL loads, because the game reads resident.ark before
// its own import table is filled. The game's code is not touched; CreateFileA, whose slot in the game
// holds the copy protection's wrapper, is not hooked.
#include "common.h"
#include "iohook.h"
#include <algorithm>
#include <string>
#include <vector>

static VirtualArchive* s_resident;

static bool EndsWithArk(const char* name)
{
    size_t n = strlen(name);
    return n > 4 && !_stricmp(name + n - 4, ".ark");  // not "x.ark.bak" found through its 8.3 name
}

// ---------------------------------------------------------------- the DLC tracks this fix knows
//
// Each converted track (tools\dlc\assemble\assemble_track.py) brings its level archives, which the game opens
// below data (0x7A9580, 0x7A4F90 / 0x7A5100, 0x7C6140 through the "" device, <root>Data\...), the quick play
// movie (Bink, data|) and its streamed bank if it has one (Audio device 0x403200 sends the names in
// StreamedAudioFileList.txt to data(noassert)|). They may lie in glimpse_dlc\<dir>\data\ with the same layout
// instead of the game's data folder: an open of such a name that does not exist below data is sent there
// (kernel32's CreateFileA/W and FindFirstFileA slots, iohook.cpp). The file list only decides whether the track
// is complete (the first four are required); the redirect takes any file below the folder (Redirect).
// The track index is given here, in this order, from 15 on, to the tracks a merged directory.txt lists.
struct DlcTrack {
    const char* name;      // directory.txt TRACKS / track name table
    const char* letter;    // its one subtrack
    const char* path;      // subtrack path table
    const char* trackSub;  // track_subtrack table (lower-case track + '_' + letter, as the PC spells them)
    const char* movie;     // quick play movie table
    const char* dir;       // glimpse_dlc\<dir>\data
    const char* files[6];  // below data; [0..3] required
};
static const DlcTrack kDlcTracks[] = {
    { "Quarry", "A", "Environments/Levels/Quarry/Subtracks/A", "quarry_A",
      "data|Movies/Screens/QuickPlay/FE_QP_Track17.bik", "quarry",
      { "environments\\levels\\quarry.ark", "quarrypowerplays.ark",
        "environments\\levels\\quarry\\sectors\\quarry.static.ark",
        "environments\\levels\\quarry\\sectors\\quarry.stream.ark",
        "audio\\release\\locales\\quarry\\quarry_streamed.fsb", "movies\\screens\\quickplay\\fe_qp_track17.bik" } },
    // Survival At The Rock: a nemesis track (Survival on the PC; no level sound banks, as the PC's own nem_*)
    { "nem_quarry", "A", "Environments/Levels/nem_quarry/Subtracks/A", "nem_quarry_A",
      "data|Movies/Screens/QuickPlay/FE_QP_Track16.bik", "nem_quarry",
      { "environments\\levels\\nem_quarry.ark", "nem_quarrypowerplays.ark",
        "environments\\levels\\nem_quarry\\sectors\\nem_quarry.static.ark",
        "environments\\levels\\nem_quarry\\sectors\\nem_quarry.stream.ark",
        "movies\\screens\\quickplay\\fe_qp_track16.bik", NULL } },
};
static const int kNumDlcTracks = sizeof(kDlcTracks) / sizeof(kDlcTracks[0]);
static const int kStockTracks = 15;
static const int kRequiredFiles = 4;

struct DlcTrackState {
    bool listed;      // the merged directory.txt lists it in TRACKS
    bool complete;    // its required files are there (data or glimpse_dlc\<dir>\data)
    int elsewhere;    // how many of its files lie only below glimpse_dlc\<dir>\data
    int index;        // given track index, -1 = not registered
    int requiredElsewhere;  // how many of those files are required ones (the level archives)
};
static DlcTrackState s_track[kNumDlcTracks];
static int s_registered;          // DLC tracks registered (indices 15 .. 15 + s_registered - 1)
static bool s_trackMerged;        // a track overlay is part of the merged resident.ark
static std::vector<std::string> s_trackOverlays;  // the merged glimpse_dlc\track_*.ark, in merge order
static std::vector<std::string> s_overlays;       // every merged glimpse_dlc\*.ark, in merge order (online check)
static volatile LONG s_tracksDecided;  // DlcRegisterTracks has run (online.cpp waits for it)

static char s_dataDir[MAX_PATH];  // the game's data folder
// folders the redirect serves (glimpse_dlc\<dir>\data\), set while the DLL loads, read-only after that
static char s_altDir[kNumDlcTracks + 4][MAX_PATH];
static int s_altCount;
static volatile LONG s_redirected;

static bool Exists(const char* path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

static void AltDirOf(const char* dir, char* out)
{
    char rel[MAX_PATH];
    sprintf_s(rel, "glimpse_dlc\\%s\\data\\", dir);
    ModulePath(out, rel);
}

// every file of the track below data or below glimpse_dlc\<dir>\data (the required ones decide; without the
// bank or the movie the race runs without them)
static bool TrackFilesPresent(int t, bool logMissing)
{
    const DlcTrack& d = kDlcTracks[t];
    char alt[MAX_PATH];
    AltDirOf(d.dir, alt);
    s_track[t].elsewhere = s_track[t].requiredElsewhere = 0;
    for (int i = 0; i < 6 && d.files[i]; i++) {
        std::string inData = std::string(s_dataDir) + d.files[i], inAlt = std::string(alt) + d.files[i];
        if (Exists(inData.c_str())) continue;
        if (Exists(inAlt.c_str())) {
            s_track[t].elsewhere++;
            if (i < kRequiredFiles) s_track[t].requiredElsewhere++;
            continue;
        }
        if (logMissing)
            Log("DLC tracks: %s missing (neither in %s nor in %s)%s", d.files[i], s_dataDir, alt,
                i < kRequiredFiles ? ", track not registered" : "");
        if (i < kRequiredFiles) return false;
    }
    return true;
}

// Any file the game asks for below data that is not there but lies at the same relative path below one of the
// folders glimpse_dlc\<dir>\data is taken from there (the first that has it): besides the files above, the
// tracks' sound banks and music projects and the modes' movies, without a list to keep in step with the
// converters' output. A file in data always wins.
static bool Redirect(const char* name, char* out, size_t outSize)
{
    // every open and file search of the process comes here: names without "data" leave at once (the game's
    // data paths have it, as "data|" device names resolved to <game>\Data\...; logs and settings do not)
    bool candidate = false;
    for (const char* p = name; *p && !candidate; p++) candidate = !_strnicmp(p, "data", 4);
    if (!candidate) return false;
    char full[MAX_PATH];
    DWORD n = GetFullPathNameA(name, MAX_PATH, full, NULL);  // '/' to '\', "." and ".." resolved
    size_t d = strlen(s_dataDir);
    if (!n || n >= MAX_PATH || _strnicmp(full, s_dataDir, d)) return false;
    const char* rel = full + d;
    if (!*rel || strpbrk(rel, "*?") || Exists(full)) return false;  // wildcards pass; a file in data wins
    for (int i = 0; i < s_altCount; i++) {
        if (strlen(s_altDir[i]) + strlen(rel) + 1 > outSize) continue;
        strcpy_s(out, outSize, s_altDir[i]);
        strcat_s(out, outSize, rel);
        if (!Exists(out)) continue;
        if (InterlockedIncrement(&s_redirected) <= 32) Log("DLC: %s opened from %s", rel, out);
        return true;
    }
    return false;
}

static void AddAltDir(const char* dir)
{
    char p[MAX_PATH];
    AltDirOf(dir, p);
    for (int i = 0; i < s_altCount; i++)
        if (!_stricmp(s_altDir[i], p)) return;
    if (s_altCount < (int)(sizeof(s_altDir) / sizeof(s_altDir[0]))) strcpy_s(s_altDir[s_altCount++], p);
}

// one entry of a merged archive, as the game would read it
static bool VaRead(const VirtualArchive* va, const char* path, std::vector<unsigned char>* out);

// the names in directory.txt's TRACKS block ("-Environments/Levels/*", ":TRACKS", ":1", then one tab line each)
static std::vector<std::string> ListedTracks(const VirtualArchive* va)
{
    std::vector<std::string> names;
    std::vector<unsigned char> d;
    if (!VaRead(va, "directory.txt", &d)) return names;
    std::string t(d.begin(), d.end());
    size_t p = t.find("-Environments/Levels/*\r\n\t:TRACKS\r\n\t:1\r\n");
    if (p == std::string::npos) return names;
    p = t.find(":1\r\n", p) + 4;
    while (p < t.size() && t[p] == '\t') {
        size_t e = t.find("\r\n", p);
        if (e == std::string::npos) e = t.size();
        names.push_back(t.substr(p + 1, e - p - 1));
        p = e + 2;
    }
    return names;
}

// A track overlay (glimpse_dlc\track_*.ark) lists its tracks in directory.txt and TrackFiltration.xml. Shown
// without the track tables extended (DlcRegisterTracks), the quick play list offers them and picking one stores
// index -1 (0x485749 keeps 0x78E2A0's result unchecked; 0x78E600/630/660 then give NULL names, which 0x483AF1
// copies). So it is merged only when everything is in place here, before the game runs: [DLC] Tracks=1, not with
// SteamLAN=1 unless [DLC] TracksOnline=1, the Steam build, and every DLC track the merged directory.txt lists
// complete (Install). Without the track overlay the overlays before it (the car pack) stay as they are.
static const char* TrackOverlayBasics()
{
    return !g_cfg.dlcTracks ? "[DLC] Tracks=0"
         // online the track index travels to the other players as a byte (0x871C30 / 0x8717A0): one without the
         // track would look up an unknown index, so online only when the player asks for it; online.cpp then lets
         // only players with the same DLC tracks (DlcOnlineSignature) see each other's LAN games
         : g_cfg.steamLan && !g_cfg.dlcTracksOnline ? "SteamLAN=1 without [DLC] TracksOnline=1"
         : CurrentBuild() != BUILD_STEAM ? "not the Steam build"
         : NULL;
}

// the movies the DLL serves for the DLC modes (dlcmode.cpp), whose files may lie below glimpse_dlc\<dir>\data
struct DlcModeMovie {
    const char* file;  // below data
    const char* dir;   // glimpse_dlc\<dir>\data
};
static const DlcModeMovie kModeMovies[] = {
    { "movies\\screens\\quickplay\\fe_deadline.bik", "deadline" },
    { "movies\\screens\\quickplay\\fe_survival_race.bik", "nem_quarry" },
};

static VirtualArchive* BuildVa(const char* base, const std::vector<std::string>& overlays)
{
    VirtualArchive* va = new VirtualArchive();
    std::string err;
    if (!va->Build(base, overlays, &err)) {
        Log("DLC: overlays not used: %s", err.c_str());
        delete va;
        return NULL;
    }
    return va;
}

static void Install()
{
    char dir[MAX_PATH], base[MAX_PATH];
    ModulePath(dir, "glimpse_dlc");
    ModulePath(base, "data\\resident.ark");
    ModulePath(s_dataDir, "data\\");
    for (DlcTrackState& s : s_track) s = { false, false, 0, -1 };
    std::vector<std::string> overlays;
    WIN32_FIND_DATAA fd;
    std::string pattern = std::string(dir) + "\\*.ark";
    HANDLE f = FindFirstFileA(pattern.c_str(), &fd);
    const char* trackWhy = NULL;
    bool anyTrackOverlay = false;
    std::vector<bool> isTrack;
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !EndsWithArk(fd.cFileName)) continue;
            bool track = !_strnicmp(fd.cFileName, "track_", 6);
            if (track) {
                if (!anyTrackOverlay) trackWhy = TrackOverlayBasics();  // once, and only when a track overlay is there
                anyTrackOverlay = true;
                if (trackWhy) {
                    Log("DLC: %s left out: %s (a track overlay is only used when its tracks get registered)",
                        fd.cFileName, trackWhy);
                    continue;
                }
            }
            overlays.push_back(std::string(dir) + "\\" + fd.cFileName);
        } while (FindNextFileA(f, &fd));
        FindClose(f);
    }
    if (overlays.empty()) {
        Log("DLC: no overlay archives in %s", dir);
        return;
    }
    // later names override earlier ones, in alphabetical order whatever the case
    std::sort(overlays.begin(), overlays.end(), [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
    IoInitReal();
    VirtualArchive* va = BuildVa(base, overlays);
    if (!va) return;
    bool tracks = anyTrackOverlay && !trackWhy;
    if (tracks) {
        // which DLC tracks the merged directory.txt lists, and whether each is complete
        std::vector<std::string> listed = ListedTracks(va);
        std::string missing;
        // a TRACKS name that is neither the game's own (the base resident.ark's directory.txt) nor a DLC track this
        // DLL knows (a newer pack with an older DLL) would get no track index either: the same as a missing track
        {
            VirtualArchive* own = BuildVa(base, std::vector<std::string>());
            std::vector<std::string> stock = own ? ListedTracks(own) : std::vector<std::string>();
            delete own;
            if (stock.empty()) missing = "every track (the game's own track list is unreadable)";
            for (const std::string& n : listed) {
                bool known = stock.empty();
                for (const std::string& x : stock) known |= !_stricmp(n.c_str(), x.c_str());
                for (const DlcTrack& d : kDlcTracks) known |= !_stricmp(n.c_str(), d.name);
                if (!known) missing += std::string(missing.empty() ? "" : ", ") + n + " (a track this fix does not know)";
            }
        }
        for (int t = 0; t < kNumDlcTracks; t++) {
            for (const std::string& n : listed)
                if (!_stricmp(n.c_str(), kDlcTracks[t].name)) s_track[t].listed = true;
            if (!s_track[t].listed) continue;
            s_track[t].complete = TrackFilesPresent(t, true);
            if (!s_track[t].complete) missing += std::string(missing.empty() ? "" : ", ") + kDlcTracks[t].name;
        }
        if (!missing.empty()) {
            Log("DLC: the track overlays are left out: %s cannot be registered (missing level archives or unknown; a "
                "listed track the game cannot load would lead nowhere)", missing.c_str());
            delete va;
            std::vector<std::string> keep;
            for (const std::string& o : overlays) {
                const char* fn = strrchr(o.c_str(), '\\');
                if (_strnicmp(fn ? fn + 1 : o.c_str(), "track_", 6)) keep.push_back(o);
            }
            for (DlcTrackState& s : s_track) s = { false, false, 0, -1 };
            tracks = false;
            overlays.swap(keep);
            if (overlays.empty()) return;
            va = BuildVa(base, overlays);
            if (!va) return;
        }
    }
    // the redirect for files below glimpse_dlc\<dir>\data: the listed tracks with files there, the DLC modes'
    // movies. It goes in now, while the DLL loads: later in the run kernel32's CreateFileA/W exports are no
    // jmp-thunks any more (another module rewrites them after start; seen on Windows 11, while a plain process
    // has FF 25 thunks there). Set now, kernel32's own import slots stay the way in: a later hook of those exports
    // that keeps the jmp [slot] still ends in ours. Nothing of SecuROM or the game is touched.
    int elsewhere = 0;
    if (tracks)
        for (int t = 0; t < kNumDlcTracks; t++)
            if (s_track[t].listed && s_track[t].elsewhere) {
                AddAltDir(kDlcTracks[t].dir);
                elsewhere += s_track[t].elsewhere;
            }
    if (CurrentBuild() == BUILD_STEAM && g_cfg.dlcModes)
        for (const DlcModeMovie& m : kModeMovies) {
            char alt[MAX_PATH];
            AltDirOf(m.dir, alt);
            if (!Exists((std::string(s_dataDir) + m.file).c_str()) && Exists((std::string(alt) + m.file).c_str())) {
                AddAltDir(m.dir);
                elsewhere++;
            }
        }
    if (s_altCount) {
        if (IoInstallCreateFileRedirect(Redirect) == 0) {
            Log("DLC: kernel32's CreateFileA/W or FindFirstFileA are not import thunks, the files in glimpse_dlc\\<name>\\data "
                "cannot be used (put them below data)");
            s_altCount = 0;
            // only a level archive that lies there makes the track unusable: without its bank or movie (the optional
            // files) the race runs without them
            if (tracks)
                for (int t = 0; t < kNumDlcTracks; t++)
                    if (s_track[t].listed && s_track[t].requiredElsewhere) s_track[t].complete = false;
            bool broken = false;
            for (int t = 0; t < kNumDlcTracks; t++) broken |= s_track[t].listed && !s_track[t].complete;
            if (broken) {  // a listed track lost its files: no track overlay, as above
                delete va;
                std::vector<std::string> keep;
                for (const std::string& o : overlays) {
                    const char* fn = strrchr(o.c_str(), '\\');
                    if (_strnicmp(fn ? fn + 1 : o.c_str(), "track_", 6)) keep.push_back(o);
                }
                Log("DLC: the track overlays are left out");
                for (DlcTrackState& s : s_track) s = { false, false, 0, -1 };
                tracks = false;
                overlays.swap(keep);
                if (overlays.empty()) return;
                va = BuildVa(base, overlays);
                if (!va) return;
            }
        } else {
            for (int i = 0; i < s_altCount; i++) Log("DLC: files missing below data are taken from %s", s_altDir[i]);
            Log("DLC: %d files are found and opened from glimpse_dlc\\<name>\\data (CreateFileA/W, FindFirstFileA)",
                elsewhere);
        }
    }
    // on the two failures below the overlays are not used: the name redirect (if it went in) comes out again,
    // IoUninstall puts back every slot swapped so far
    if (!IoAddVirtualFile(base, va)) {
        Log("DLC: resident.ark cannot be opened or identified (no file id on this volume, or 4 GB and more), overlays not used");
        delete va;
        IoUninstall();
        s_altCount = 0;
        return;
    }
    if (IoInstallKernel32Slots() != 4) {
        Log("DLC: kernel32's file functions are not import thunks here, overlays not used");
        IoUninstall();
        s_altCount = 0;
        return;  // va stays registered but is never asked for: no hook is installed
    }
    s_resident = va;
    s_trackMerged = tracks;  // true only when the track overlays passed the checks above and are in the list
    for (const std::string& o : overlays) {
        Log("DLC: overlay %s", o.c_str());
        const char* fn = strrchr(o.c_str(), '\\');
        if (tracks && !_strnicmp(fn ? fn + 1 : o.c_str(), "track_", 6)) s_trackOverlays.push_back(o);  // for the online check
        s_overlays.push_back(o);
    }
    Log("DLC: resident.ark shown with %u extra bytes of index, %u bytes in all", va->Delta(), va->Size());
}

void DlcInstall()
{
    if (!g_cfg.dlcOverlays) return;
    // the converted data and every [DLC] part were made for the Steam build 1.0.0.1 (German/English depot): the
    // Russian depot's build and unknown exes get the game's own data only, like the rest of [DLC].
    // Deliberately left out for the Russian build: every [DLC] module (dlcfs, dlcmode, dlccars, dlcpickups,
    // dlcrivals, dlcmusic) uses raw Steam addresses (vtable slots, game functions, data) that are not mapped in
    // src\build_ru.inc. Enabling it there would mean mapping and checking all of them against the Russian dump,
    // as was done for the base features, and testing it in the game.
    if (CurrentBuild() != BUILD_STEAM) {
        Log("DLC: overlays not used: not the Steam build (%s)", BuildName());
        return;
    }
    try {  // while the DLL loads: nothing may leave this
        Install();
    } catch (...) {
        // as the failures inside Install: nothing of the overlays stays in (the name redirect may be in already, and
        // the kernel32 slots too when only the overlay lists at Install's end ran out of memory)
        IoUninstall();
        s_altCount = 0;
        s_resident = NULL;
        s_trackMerged = false;
        s_overlays.clear();
        s_trackOverlays.clear();
        Log("DLC: overlays not used (out of memory or a broken index)");
    }
}

// the DLL goes away: kernel32's slots get their own functions back (the name redirect too; nothing swapped =
// nothing to do)
void DlcUninstall()
{
    IoUninstall();
}

// ---------------------------------------------------------------- [DLC] Tracks: the DLC tracks as tracks 15, 16, ...
//
// The game knows its tracks by an index 0..14 through five enum tables {u32 index, char* name}, each with a
// descriptor {table, count, owns} that a static initialiser of the exe fills (0xB7AEC0, 0xB6B520, 0xB6B880;
// the destructor 0x5314D0 frees the table only when owns is set). The quick play screen turns the chosen
// track's "Environments/Levels/<track>/Subtracks/<letter>" into its index with 0x78E2A0 (0x4855F0 stores it
// at race setup +0x9A0). 0x78E2A0 is 0x5314F0 on the path table: it returns the position of the entry, the
// readers look up by the index field (0x531570, count-bounded: 0x78E600 name, 0x78E630 letter, 0x78E660 path,
// 0x485190 quick play movie), so index == position, the new entries go on at 15, 16, ... in kDlcTracks order.
// Each table is copied here with the registered DLC tracks appended and its descriptor pointed at the copy with
// the new count (data writes; owns stays 0, so nothing is freed at exit). Every reader of +0x9A0 (0x474C79,
// 0x4789CB, 0x483AAF, 0x51BA5B, 0x7D68C6/71/96; the session byte 0x871C30) only looks names up; the loop with
// an immediate 15 at 0x4128D0 serves only SPLITSECOND.PARAMS starts and simply does not find a DLC track.
// The track_subtrack enum (0xD6FB84, alphabetical, a numbering of its own) is read only by 0x47B9B0, which
// takes the found entry's index to the career event select movies 0xD6FB90 (16 entries, [15] =
// EpisodeStatic.bik; 16 and up: not found, "" - the DLC tracks have no career events). The lobby movie path
// 0x4751E0 serves track indices below 15 only (NULL: the previous movie keeps playing, 0x57C4D7).
// Save files are keyed by name (0xA38CB0 finds nodes by name), no saved array is indexed by the track.
// Online the index travels as a byte in the session (0x871C30 writes [0xD82528] to +0x5A, 0x8717A0 reads it):
// a peer without the tracks would look up an index and get no name, so nothing is registered with SteamLAN=1
// unless [DLC] TracksOnline=1; then online.cpp compares DlcOnlineSignature with the other players' and keeps
// players with other DLC tracks out of each other's LAN games (below). The game's own LAN mode (SteamLAN=0) is not
// guarded: there the DLC tracks are registered and the players must have the same ones.
// Only on the Steam build all these addresses were read from; the tables are checked before anything changes.
struct TrackEntry {
    DWORD index;
    const char* name;
};
struct TrackEnum {
    TrackEntry* table;
    int count;
    BYTE owns;
};
struct TrackTable {
    DWORD desc, table;     // descriptor and the exe's own table (Steam build)
    const char* last;      // the exe's entry 14, to recognise the table
    int field;             // which DlcTrack string goes in: 0 name, 1 letter, 2 path, 3 track_subtrack, 4 movie
    TrackEntry copy[kStockTracks + kNumDlcTracks];
};
static TrackTable s_tracks[] = {
    { 0x00D7E874, 0x00D368F8, "nem_graveyard", 0, {} },
    { 0x00D7E880, 0x00D36970, "A", 1, {} },
    { 0x00D7E88C, 0x00D369E8, "Environments/Levels/nem_graveyard/Subtracks/A", 2, {} },
    { 0x00D6FB84, 0x00D0F9F8, "nem_warehouse_A", 3, {} },
    { 0x00D6FC40, 0x00D10188, "data|Movies/Screens/QuickPlay/FE_QP_Track7.bik", 4, {} },
};

static const char* TrackField(const DlcTrack& d, int field)
{
    switch (field) {
    case 0: return d.name;
    case 1: return d.letter;
    case 2: return d.path;
    case 3: return d.trackSub;
    default: return d.movie;
    }
}

static bool TrackTableIntact(const TrackTable& t)
{
    __try {
        const TrackEnum* e = (const TrackEnum*)t.desc;
        if ((DWORD)e->table != t.table || e->count != kStockTracks || e->owns) return false;
        for (int i = 0; i < kStockTracks; i++) {
            const TrackEntry& x = e->table[i];
            if (x.index != (DWORD)i || !x.name || IsBadStringPtrA(x.name, 128)) return false;
            if (t.field != 1)  // there already
                for (const DlcTrack& d : kDlcTracks)
                    if (!_stricmp(x.name, TrackField(d, t.field))) return false;
        }
        return !strcmp(e->table[kStockTracks - 1].name, t.last);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// the registered DLC track with this track object (record +0 track: name at +0, letter at +0x48, char* or NULL)
static int DlcTrackOfRecord(const BYTE* track)
{
    if (!track) return -1;
    const char* name = *(const char* const*)track;
    const char* sub = *(const char* const*)(track + 0x48);
    if (!name || !sub) return -1;
    for (int t = 0; t < kNumDlcTracks; t++)
        if (s_track[t].index >= 0 && !_stricmp(name, kDlcTracks[t].name) && !_stricmp(sub, kDlcTracks[t].letter))
            return t;
    return -1;
}

// ---------------------------------------------------------------- quick play movies of the DLC tracks and modes
//
// The quick play screen (cNewQuickPlay, vtable 0xBAFD34, COL 0xC79BD0 at -4) plays movie ids through 0x456550
// (thiscall on the screen): unless the id equals the one playing (screen +0x128) it asks the screen's slot 24
// (+0x60 = 0x479680, thiscall, ret 4) for the path: ids 0..5 the mode movies (0xD6FB64), 6..20 the track
// movies (0x485190(id - 6)), 21 FE_Locked.bik, above 21 NULL (the previous movie keeps playing). The track list's
// item update (0x485C30 -> 0x4796F0 at 0x48646B) asks for "locked ? 21 : index + 6", the mode list's (0x4867B0
// -> 0x479720 at 0x486AF8) for "locked ? 21 : record movie id". So track 15 (Quarry) asks for id 21 and gets
// FE_Locked.bik although it is unlocked: the "locked" Quarry seen in quick play. Track 16 asks for 22 and gets
// NULL. Slot 24 is swapped: 22 and up to 6 + the last registered index give the DLC track's movie; 21 gives the
// DLC track's movie while the track item update of that (unlocked) DLC track runs, FE_Locked.bik otherwise; the
// DLC modes' ids from kModeMovieBase on give their movies (dlcmode.cpp sets the records' movie id). Because
// 0x456550 skips an id equal to the one playing, a switch between the two meanings of 21 clears +0x128 to -1
// first (what 0x456550 itself stores for no movie). Data writes and a vtable slot only.
typedef const char*(__thiscall* QpMoviePath_t)(void* self, int id);
static void** const kQpMovieSlot = (void**)0x00BAFD94;  // vtable 0xBAFD34 slot 24
static const DWORD kQpMoviePath = 0x00479680;
static QpMoviePath_t s_origQpMoviePath;
static int s_qpContext21 = -1;   // DLC track whose item update runs and asks for id 21
static bool s_shown21Dlc;        // what id 21 meant the last time it was asked for
static const int kLockedMovie = 21;
static const int kQpModeMovieBase = 64;  // DLC mode movies: far above the track ids (6 + 15 + the DLC tracks)
static const char* s_modeMovie[8];
static int s_modeMovies;
static bool s_movieLogged[kNumDlcTracks + 8];

static const char* __fastcall Hook_QpMoviePath(void* self, void*, int id)
{
    if (id >= kQpModeMovieBase && id < kQpModeMovieBase + s_modeMovies) {
        if (!s_movieLogged[kNumDlcTracks + id - kQpModeMovieBase]) {
            s_movieLogged[kNumDlcTracks + id - kQpModeMovieBase] = true;
            Log("DLC: quick play movie %d = %s", id, s_modeMovie[id - kQpModeMovieBase]);
        }
        return s_modeMovie[id - kQpModeMovieBase];
    }
    int t = -1;
    if (id == kLockedMovie) {
        s_shown21Dlc = s_qpContext21 >= 0;
        t = s_qpContext21;
    } else if (id > kLockedMovie && id < 6 + kStockTracks + s_registered) {
        for (int i = 0; i < kNumDlcTracks; i++)
            if (s_track[i].index == id - 6) t = i;
    }
    if (t >= 0) {
        if (!s_movieLogged[t]) {
            s_movieLogged[t] = true;
            Log("DLC tracks: quick play movie %d = %s (track %d %s)", id, kDlcTracks[t].movie, s_track[t].index,
                kDlcTracks[t].name);
        }
        return kDlcTracks[t].movie;
    }
    return s_origQpMoviePath(self, id);
}

// before a quick play item update of the selected movie item: dlc21 = it asks for 21 meaning a DLC track; state =
// the screen state (+0x240) in which that list's update asks for a movie at all (track list 1: 0x485C65, mode list
// 0: 0x4867DF). In the other state the update asks for nothing, and clearing +0x128 there would let the screen's
// tick (0x457661) stop the movie that plays.
void DlcQpBeforeMovieRequest(void* screen, bool dlc21, int state)
{
    if (!s_origQpMoviePath || !screen) return;
    __try {
        if (*(const DWORD*)screen != 0x00BAFD34) return;  // only cNewQuickPlay: +0x128 is its playing movie id
        if (*(const int*)((const BYTE*)screen + 0x240) != state) return;
        int* playing = (int*)((BYTE*)screen + 0x128);
        if (*playing == kLockedMovie && dlc21 != s_shown21Dlc) *playing = -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// dlcmode.cpp: a movie for a DLC mode; returns its id (kQpModeMovieBase + n) or -1
int DlcQpAddModeMovie(const char* path)
{
    if (s_modeMovies >= (int)(sizeof(s_modeMovie) / sizeof(s_modeMovie[0]))) return -1;
    for (int i = 0; i < s_modeMovies; i++)
        if (!strcmp(s_modeMovie[i], path)) return kQpModeMovieBase + i;
    s_modeMovie[s_modeMovies] = path;
    return kQpModeMovieBase + s_modeMovies++;
}

// the file of a "data|..." movie path is there (below data or in a folder the redirect serves)
bool DlcDataFileExists(const char* rel)
{
    if (!*s_dataDir) ModulePath(s_dataDir, "data\\");
    if (Exists((std::string(s_dataDir) + rel).c_str())) return true;
    for (int i = 0; i < s_altCount; i++)
        if (Exists((std::string(s_altDir[i]) + rel).c_str())) return true;
    return false;
}

// the class name behind the vtable's complete object locator {sig 0, offset, cdOffset, TypeDescriptor*, ...}
static bool QpScreenClassMatches()
{
    static const char kClass[] = ".?AVcNewQuickPlay@Frontend@@";
    const DWORD* col = (const DWORD*)0x00C79BD0;
    if (col[0] != 0) return false;
    const char* name = (const char*)(col[3] + 8);  // TypeDescriptor {vftable, spare, name[]}
    return !IsBadStringPtrA(name, sizeof(kClass)) && !strcmp(name, kClass);
}

// cNewQuickPlay's slot 24 swapped once (tracks or modes need it); false when the screen is not as expected
bool DlcQpInstallMovies()
{
    if (s_origQpMoviePath) return true;
    if (CurrentBuild() != BUILD_STEAM) return false;
    __try {
        if ((DWORD)kQpMovieSlot[0] != kQpMoviePath || (DWORD)kQpMovieSlot[-24 + 1] != 0x00479B30 ||
            (DWORD)kQpMovieSlot[-25] != 0x00C79BD0 || !QpScreenClassMatches() ||
            memcmp((const void*)kQpMoviePath, "\x8B\x44\x24\x04\x85\xC0\x7D\x05\x33\xC0\xC2\x04\x00\x83\xF8\x06", 16)) {
            Log("DLC: quick play screen not as expected, the DLC movies are not shown");
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    DWORD old;
    if (!VirtualProtect(kQpMovieSlot, 4, PAGE_READWRITE, &old)) return false;
    s_origQpMoviePath = (QpMoviePath_t)*kQpMovieSlot;
    InterlockedExchangePointer(kQpMovieSlot, (void*)Hook_QpMoviePath);
    VirtualProtect(kQpMovieSlot, 4, old, &old);
    Log("DLC: quick play movies hooked (vtable 0xBAFD34 slot 24)");
    return true;
}

// ---------------------------------------------------------------- [DLC] Tracks: the DLC tracks selectable in quick play
//
// The quick play track list (cQuickPlayTrackSelect..., at screen +0x140; made by 0x486570: capacity 16 at +4,
// records 16 x 8 bytes, the append 0x77ACB0 has no bound check, TrackFiltration.xml gives at most 16 tracks per
// mode, 0x77B620) is rebuilt by 0x4853B0 whenever the game mode changes (0x4854A0, from 0x47A2DF): 0x77ACB0
// appends one 8-byte record {track*, locked} per track of TrackFiltration.xml (count at list +8, records at list
// +0xC) and sets locked unless the mode flags force it open (online: flag 1, two players: flag 2) or a Season.xml
// event of that track is done in the profile. The DLC tracks have no season event, so their records stay locked.
// Every reader asks 0x4851C0 (record +4, opened only by UI/All Gamemodes Unlocked 0xD6FC54 and the Expressway
// cheat): the item update 0x485C30 (TEXT_LOCKED; it also shows TEXT_LOCKED for every track while the chosen mode
// is locked, 0x479670 -> 0x486F40), 0x4866A0 (the current selection, 0x479D83 / 0x47A1D0 / 0x47A268 / 0x47A471)
// and the race start 0x4855F0 (returns without starting). The track object keeps its name at +0 and the subtrack
// letter at +0x48 (both char*, NULL for ""; 0x4851C0 compares them the same way).
// The list's item update is the delegate cQuickPlayTrackSelectDynamicMenuConfigObject (vtable 0xBB224C, made at
// 0x4865E5 with the list at +4; the list has the screen at +0x18), slot 1 = 0x485C30 (index, selected, widget),
// called for the shown items whenever the menu refreshes (the rebuild marks the widget dirty, 0x48548C). Its slot
// is swapped: before the game's update, the records of the registered DLC tracks in the list are set unlocked (a
// data write into the freshly built list; the next rebuild locks them again and the next update opens them again),
// and the movie context for id 21 is set (above). Every other track keeps the game's own state; nothing is written
// to the profile.
static void** const kQpItemUpdateSlot = (void**)0x00BB2250;  // vtable 0xBB224C slot 1
static QpItemUpdate_t s_origQpItemUpdate;
static bool s_unlockLogged[kNumDlcTracks];

// the widget the item update gets is the movie (its name at +4, 0x485C30 compares it with "MMBinkMovie")
bool DlcQpIsMovieWidget(DWORD widget)
{
    __try {
        const char* n = widget ? *(const char* const*)(widget + 4) : NULL;
        return n && !IsBadStringPtrA(n, 32) && !_stricmp(n, "MMBinkMovie");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void UnlockDlcRecords(const BYTE* self, DWORD index, DWORD selected, DWORD widget)
{
    __try {
        const BYTE* list = *(const BYTE* const*)(self + 4);
        if (!list) return;
        int count = *(const int*)(list + 8);
        BYTE* records = *(BYTE* const*)(list + 0xC);
        if (count <= 0 || count > 64 || !records) return;
        for (int i = 0; i < count; i++) {
            BYTE* r = records + i * 8;
            int t = DlcTrackOfRecord(*(const BYTE* const*)r);
            if (t < 0) continue;
            if (r[4]) {
                r[4] = 0;
                if (!s_unlockLogged[t]) {
                    s_unlockLogged[t] = true;
                    Log("DLC tracks: %s unlocked in the quick play track list (record %d of %d, list %p)",
                        kDlcTracks[t].name, i, count, list);
                }
            }
            if ((DWORD)i == index && selected && s_track[t].index + 6 == kLockedMovie) s_qpContext21 = t;
        }
        // the movie is asked for only with the screen in state 1 and index < count (0x485C65, 0x48644E)
        if (s_origQpMoviePath && selected && index < (DWORD)count && DlcQpIsMovieWidget(widget))
            DlcQpBeforeMovieRequest(*(void* const*)(list + 0x18), s_qpContext21 >= 0, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void __fastcall Hook_QpItemUpdate(void* self, void*, DWORD index, DWORD selected, DWORD widget)
{
    s_qpContext21 = -1;
    if (s_registered) UnlockDlcRecords((const BYTE*)self, index, selected, widget);
    DlcRecordsUpdateTrack(s_origQpItemUpdate, self, index, selected, widget);
    s_qpContext21 = -1;
}

bool DlcInstallTrackListHook()
{
    if (s_origQpItemUpdate) return true;
    __try {
        if ((DWORD)kQpItemUpdateSlot[-2] != 0x00C7A3C0 || (DWORD)kQpItemUpdateSlot[0] != 0x00485C30 ||
            !BytesMatch((void*)0x00485C30, "6AFF68063DB300")) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    s_origQpItemUpdate = (QpItemUpdate_t)*kQpItemUpdateSlot;
    if (WriteSlot(kQpItemUpdateSlot, (void*)Hook_QpItemUpdate)) return true;
    s_origQpItemUpdate = NULL;
    return false;
}

static void RegisterTracks()
{
    if (!g_cfg.dlcTracks) return;
    if (CurrentBuild() != BUILD_STEAM) {
        Log("DLC tracks: not on this build (%s), no DLC track registered", BuildName());
        return;
    }
    if (!s_resident || !s_trackMerged) {
        // everything was decided while the DLL loaded (Install, logged there): without the track overlay the
        // game lists no DLC track, so there is nothing to register
        Log("DLC tracks: no track overlay in use (needs [DLC] Overlays=1 and glimpse_dlc\\track_*.ark), "
            "no DLC track registered");
        return;
    }
    int n = 0;
    for (int t = 0; t < kNumDlcTracks; t++)
        if (s_track[t].listed && s_track[t].complete) n++;
    if (!n) {
        Log("DLC tracks: the track overlay lists none of the DLC tracks this fix knows, nothing registered");
        return;
    }
    for (const TrackTable& t : s_tracks) {
        if (!TrackTableIntact(t)) {
            // the track overlay was merged at load (Install): its list entries would lead nowhere
            Log("DLC tracks: track table 0x%08lX not as expected, no DLC track registered - do not pick a DLC track "
                "(remove glimpse_dlc\\track_*.ark)", t.desc);
            return;
        }
    }
    int next = kStockTracks;
    for (int t = 0; t < kNumDlcTracks; t++)
        s_track[t].index = s_track[t].listed && s_track[t].complete ? next++ : -1;
    for (TrackTable& t : s_tracks) {
        TrackEnum* e = (TrackEnum*)t.desc;
        memcpy(t.copy, e->table, kStockTracks * sizeof(TrackEntry));
        for (int d = 0; d < kNumDlcTracks; d++) {
            if (s_track[d].index < 0) continue;
            t.copy[s_track[d].index].index = s_track[d].index;
            t.copy[s_track[d].index].name = TrackField(kDlcTracks[d], t.field);
        }
        // the copy is complete before the descriptor points at it, the count follows last
        InterlockedExchangePointer((void**)&e->table, t.copy);
        InterlockedExchange((volatile LONG*)&e->count, next);
        Log("DLC tracks: table 0x%08lX -> copy at %p, %d entries, [15] = %s%s%s", t.desc, t.copy, next,
            t.copy[kStockTracks].name, next > kStockTracks + 1 ? ", [16] = " : "",
            next > kStockTracks + 1 ? t.copy[kStockTracks + 1].name : "");
    }
    s_registered = next - kStockTracks;
    for (int t = 0; t < kNumDlcTracks; t++)
        if (s_track[t].index >= 0) Log("DLC tracks: %s registered as track %d", kDlcTracks[t].name, s_track[t].index);
    if (g_cfg.log) InstallDiagnostics();  // a converted track that fails to load shows where in the log
    if (DlcInstallTrackListHook())
        Log("DLC tracks: quick play track list hooked (vtable 0xBB224C slot 1), the DLC tracks will be selectable");
    else
        Log("DLC tracks: quick play track list not as expected, the DLC tracks stay locked there unless UnlockAll=1");
    DlcQpInstallMovies();
}

void DlcRegisterTracks()
{
    RegisterTracks();
    InterlockedExchange(&s_tracksDecided, 1);  // after every table write: online.cpp reads s_track from its thread
}

// ---------------------------------------------------------------- [DLC] TracksOnline: which DLC tracks the others need
//
// What the game sends: the host's game setup is the Net-Z well known object "gWellKnownGameSetup" (made at 0x872750);
// 0x871C30 fills it with three bytes: +0x59 the mode ([0xD82518]), +0x5A the track index ([0xD82528], set from race
// setup +0x9A0 by 0x4789B0 or chosen at random by 0x872340, "Choosing track %d", from the mode's TrackFiltration
// list, DLC tracks included), +0x58 two option nibbles ([0xD377D8], [0xD377D4]). A guest copies +0x5A into its own
// [0xD82528] (0x871D40), and at the race start every player reads it (0x8717A0) into its race setup +0x9A0 and
// looks the name, letter and level path up in its own track tables (0x4838B0 -> 0x78E600/630/660). No name and no
// list is sent: the indices 0..14 mean the same everywhere, 15 and up only when both registered the same DLC
// tracks in the same order from the same files. Which track comes next is the host's choice inside a running
// session (and Quazal migrates the host when it leaves), so "only the chosen track" cannot be checked in time; the
// players of one LAN game must all have the same DLC tracks or none.
//
// The cars travel by name, not by record index (static RE 2026-11-22): every player's cATVNetDOPlayerSetup carries
// the dataset cATVNetDSVehicleData {mVehicleName string, mColour int32} (DDL at 0xD44A3D; dataset 2 at DO +0x80,
// serialised by 0x87D220/0x87D2F0/0x87D3C0/0x87D4D0), filled by 0x883160 from a car struct: at the session start
// (0x8737A0) from the network manager [0xD64E18] +0x75C/+0x760, which is the profile's online car (slot 1: 0x41AA90
// when slot 1 changes, 0x41A840 when the profile loads), later from the race setup's car (lobby 0x477390 ->
// 0x870000). Slot 1 is chosen only on the online car select (game types 6/7, where dlccars.cpp adds nothing). Each player
// takes the name into his race setup (player entries of 200 bytes at +0x348, the car's name at entry +0x40) and looks
// it up in his own vehicle database (0x518FA0/0x518CA0 -> 0x51A1E0 -> 0x51BBE0); a name it lacks gives "Couldn't
// find vehicle info for %s" and a NULL vehicle record (+0x3C0). So the record indices (the Placeholder slots the car
// pack fills) do not matter, the names do: a player without the pack cannot race against a DLC car. The fix's own
// car hook never lists them online (dlccars.cpp); with UnlockAll=1 the game's all-records branch does, so then the
// cars are part of the signature, and only players with the same car pack (and the same DLC tracks) play together.
//
// The signature: "none" while no track overlay is merged (TracksOnline=0, no pack, SteamLAN=1 without it) and no DLC
// car can be chosen online (UnlockAll=0, or no overlay with DLC cars); else 16 hex digits of a 64-bit FNV-1a over the
// fix version, the registered tracks (index, name, letter, path), their four level archives (size, CRC-32 of the
// first MB and of the last 64 KB; data or glimpse_dlc\<dir>\data; not the sound bank or the movie, which are local)
// and every merged track_*.ark (size, the same CRCs; not its name); with DLC cars online also the car IDs and every
// merged overlay (size and CRCs: the car pack's records and models). Computed once, on the online thread, after
// DlcRegisterTracks.

bool DlcTracksDecided()
{
    return !g_cfg.dlcTracks || s_tracksDecided;
}

static uint32_t Crc32(uint32_t crc, const BYTE* p, size_t n)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// "<size> <crc of the first MB> <crc of the last 64 KB>", or "missing"
static std::string FileDigest(const char* path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return "missing";
    LARGE_INTEGER size = {};
    GetFileSizeEx(h, &size);
    std::vector<BYTE> buf(1 << 20);
    uint32_t crc[2] = {};
    for (int part = 0; part < 2; part++) {
        LONGLONG want = part == 0 ? (1 << 20) : (64 << 10);
        LARGE_INTEGER at = {};
        at.QuadPart = part == 0 ? 0 : (size.QuadPart > want ? size.QuadPart - want : 0);
        DWORD got = 0;
        if (SetFilePointerEx(h, at, NULL, FILE_BEGIN) && ReadFile(h, buf.data(), (DWORD)want, &got, NULL))
            crc[part] = Crc32(0, buf.data(), got);
    }
    CloseHandle(h);
    char t[64];
    sprintf_s(t, "%lld %08X %08X", size.QuadPart, crc[0], crc[1]);
    return t;
}

static std::string TrackFilePath(int t, const char* file)
{
    std::string inData = std::string(s_dataDir) + file;
    if (Exists(inData.c_str())) return inData;
    char alt[MAX_PATH];
    AltDirOf(kDlcTracks[t].dir, alt);
    return std::string(alt) + file;
}

void DlcOnlineSignature(char* sig, size_t sigSize, char* names, size_t namesSize)
{
    static char s_sig[24], s_names[96];
    static bool done;
    if (!done) {
        done = true;
        strcpy_s(s_sig, "none");
        strcpy_s(s_names, "none");
        // the DLC cars the game itself lists on the online car select (UnlockAll=1; the fix's hook never does)
        char carIds[200] = "";
        int cars = g_cfg.unlockAll && s_resident ? DlcCarsInOverlay(carIds, sizeof(carIds)) : 0;
        if (s_trackMerged || cars) {
            try {
                std::string m = "glimpse_fix " GLIMPSE_VERSION "\n", list;
                char line[512];
                for (int t = 0; s_trackMerged && t < kNumDlcTracks; t++) {
                    if (s_track[t].index < 0) continue;
                    const DlcTrack& d = kDlcTracks[t];
                    sprintf_s(line, "track %d %s %s %s\n", s_track[t].index, d.name, d.letter, d.path);
                    m += line;
                    sprintf_s(line, "%s%s=%d", list.empty() ? "" : ", ", d.name, s_track[t].index);
                    list += line;
                    for (int i = 0; i < kRequiredFiles; i++) {  // the level archives; bank and movie do not desync
                        sprintf_s(line, " file %s %s\n", d.files[i], FileDigest(TrackFilePath(t, d.files[i]).c_str()).c_str());
                        m += line;
                    }
                }
                if (s_trackMerged && !s_registered) {
                    m += "listed, no track index\n";  // TrackTableIntact failed: a pick would lead nowhere
                    list = "listed but not registered";
                }
                for (const std::string& o : s_trackOverlays) m += "merged track overlay " + FileDigest(o.c_str()) + "\n";
                if (cars) {
                    // the cars travel by name: the same IDs from the same records and models (every merged overlay)
                    m += std::string("cars online (UnlockAll=1) ") + carIds + "\n";
                    for (const std::string& o : s_overlays) m += "merged overlay " + FileDigest(o.c_str()) + "\n";
                    sprintf_s(line, "%s%d DLC cars (UnlockAll)", list.empty() ? "" : "; ", cars);
                    list += line;
                }
                uint64_t h = 0xCBF29CE484222325ull;
                for (unsigned char c : m) h = (h ^ c) * 0x100000001B3ull;
                sprintf_s(s_sig, "%016llX", (unsigned long long)h);
                strncpy_s(s_names, list.c_str(), _TRUNCATE);
                size_t p = 0;
                for (size_t e; (e = m.find('\n', p)) != std::string::npos; p = e + 1)
                    Log("DLC tracks online: %s", m.substr(p, e - p).c_str());
            } catch (...) {
                // out of memory: a value nobody else has, so no LAN game together with anybody
                sprintf_s(s_sig, "ERR%08lX%04lX", GetTickCount(), GetCurrentProcessId() & 0xFFFF);
                strcpy_s(s_names, "unreadable");
            }
        }
        Log("DLC tracks online: signature %s (%s)%s%s", s_sig, s_names,
            s_trackMerged ? " over the fix version, the registered tracks, their four level archives and the merged "
                            "track_*.ark (sizes, CRC-32 of the first MB and the last 64 KB)" : "",
            cars ? (s_trackMerged ? "; with the DLC cars the game lists online (UnlockAll=1) and every merged overlay"
                                  : " over the fix version, the DLC cars the game lists online (UnlockAll=1) and every "
                                    "merged overlay (sizes, CRC-32 of the first MB and the last 64 KB)")
                 : "");
    }
    strcpy_s(sig, sigSize, s_sig);
    strcpy_s(names, namesSize, s_names);
}

// Log=1: how the merged archive was read, with the 15 s state line, while it changes
void DlcLogStats()
{
    if (!s_resident || !g_cfg.log) return;
    static LONG last;
    LONG n = g_ioStats.synthetic + g_ioStats.translated;
    if (n == last) return;
    last = n;
    Log("DLC: %ld reads of resident.ark (%ld from the original at the shifted offset, %ld from the merged index "
        "or overlays), %ld opens",
        n, g_ioStats.translated, g_ioStats.synthetic, g_ioStats.registered);
}

static const ArkEntry* VaFind(const VirtualArchive* va, const char* path)
{
    const std::vector<uint8_t>& h = va->Head();
    if (h.size() < 24) return NULL;
    const uint32_t n = *(const uint32_t*)(h.data() + 8);
    if (h.size() < 24 + 20ull * n) return NULL;
    const ArkEntry* e = (const ArkEntry*)(h.data() + 24);
    const uint32_t k = ArkKey(path);
    // sorted by unsigned key (VirtualArchive::Build)
    const ArkEntry* it = std::lower_bound(e, e + n, k, [](const ArkEntry& a, uint32_t v) { return a.key < v; });
    return it != e + n && it->key == k ? it : NULL;
}

// the merged resident.ark is in use and holds this path (an overlay brought it or the game's own archive has it)
bool DlcOverlayHas(const char* path)
{
    return s_resident && VaFind(s_resident, path) != NULL;
}

// The game's LZSS (type 1 entries, arkio.lzss_n): u32 size, then flag bytes (bit set = literal) and 2-byte
// references into a 4 KiB window that starts at 0xFEE filled with 0.
static bool Lzss(const uint8_t* src, size_t n, std::vector<uint8_t>* out)
{
    if (n < 4) return false;
    const uint32_t size = *(const uint32_t*)src;
    if (size > (64u << 20)) return false;
    uint8_t win[4096] = {};
    unsigned r = 0xFEE, flags = 0;
    size_t i = 4;
    out->clear();
    out->reserve(size);
    while (out->size() < size) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            if (i >= n) return false;
            flags = src[i++] | 0xFF00;
        }
        if (flags & 1) {
            if (i >= n) return false;
            uint8_t c = src[i++];
            out->push_back(c);
            win[r] = c;
            r = (r + 1) & 0xFFF;
        } else {
            if (i + 1 >= n) return false;
            unsigned a = src[i], b = src[i + 1];
            i += 2;
            unsigned o = a | ((b & 0xF0) << 4);
            for (unsigned k = 0; k < (b & 0x0F) + 3u && out->size() < size; k++) {
                uint8_t c = win[(o + k) & 0xFFF];
                out->push_back(c);
                win[r] = c;
                r = (r + 1) & 0xFFF;
            }
        }
    }
    return true;
}

static bool VaRead(const VirtualArchive* va, const char* path, std::vector<unsigned char>* out)
{
    out->clear();
    if (!va) return false;
    try {
        const ArkEntry* it = VaFind(va, path);
        if (!it || it->csize > (64u << 20) || it->type > 1) return false;
        std::vector<uint8_t> raw(it->csize);
        uint32_t got = 0;
        if (!va->Read(it->offset, raw.data(), it->csize, &got) || got != it->csize) return false;
        if (it->type == 0) {
            out->swap(raw);
            return out->size() == it->size;
        }
        return Lzss(raw.data(), raw.size(), out) && out->size() == it->size;
    } catch (...) {
        out->clear();
        return false;
    }
}

// one entry of the merged resident.ark, as the game would read it (dlcpickups.cpp: the Deadline .pkps files)
bool DlcOverlayRead(const char* path, std::vector<unsigned char>* out)
{
    return VaRead(s_resident, path, out);
}

bool ResidentEntryMatches(const char* path, size_t size, uint32_t crc)
{
    try {
        VirtualArchive base;
        const VirtualArchive* archive = s_resident;
        if (!archive) {
            char file[MAX_PATH];
            ModulePath(file, "data\\resident.ark");
            std::string error;
            if (!base.Build(file, {}, &error)) return false;
            archive = &base;
        }
        const ArkEntry* entry = VaFind(archive, path);
        std::vector<unsigned char> data;
        return entry && entry->size == size && VaRead(archive, path, &data) &&
               Crc32(0, data.data(), data.size()) == crc;
    } catch (...) {
        return false;
    }
}
