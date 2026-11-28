// Original Xbox DLC personal bests in Steam's native TrackRecords table.
// Keep the DLC board keys local: the game's network catalogue is unchanged.
#include "common.h"
#include <stdio.h>

enum class ScoreType { Time, Points, Deadline, QuarryRace, MultiplayerPoints };

struct Board {
    const char* key;  // spelling used by the native, case-sensitive record hash
    ScoreType type;
    const char* pickupTrack;
    char subtrack;
    int countdown;
    unsigned pickupSize;
    uint32_t pickupCrc;
};

#define DEADLINE(track, sub, seconds, size, crc) \
    { track "_" sub "_TIMETRIALPLUS", ScoreType::Deadline, track, sub[0], seconds, size, crc }
static const Board kBoards[] = {
    DEADLINE("AIRPORT_TEST_03", "A", 60, 13768, 0x55098128),
    DEADLINE("DOCKS", "A", 65, 13416, 0xbe01759f),
    DEADLINE("DOCKS", "B", 60, 12731, 0x86efca05),
    DEADLINE("DOCKS", "C", 60, 13425, 0x89ebf4b6),
    DEADLINE("GRAVEYARD", "A", 65, 14117, 0x1e6c2cb6),
    DEADLINE("GRAVEYARD", "B", 70, 17804, 0x711d01df),
    DEADLINE("DOWNTOWN", "A", 65, 22342, 0x11600f57),
    DEADLINE("DOWNTOWN", "B", 65, 20875, 0x63713c47),
    DEADLINE("DOWNTOWN", "C", 65, 16001, 0x483936ff),
    DEADLINE("POWERPLANT", "A", 60, 22066, 0xe231459d),
    DEADLINE("POWERPLANT", "B", 70, 18331, 0x7f8665c7),
    DEADLINE("QUARRY", "A", 60, 15644, 0xf2f64701),
    { "QUARRY_A_QUICKRACE", ScoreType::QuarryRace, NULL, 0, 0, 0, 0 },
    { "QUARRY_A_TIMETRIAL", ScoreType::Time, NULL, 0, 0, 0, 0 },
    { "QUARRY_A_HELICOPTERSURVIVAL", ScoreType::Points, NULL, 0, 0, 0, 0 },
    { "QUARRY_A_HELICOPTERREVENGE", ScoreType::Time, NULL, 0, 0, 0, 0 },
    { "NEM_QUARRY_A_NEMESIS", ScoreType::Points, NULL, 0, 0, 0, 0 },
    { "NEM_QUARRY_A_NEMESISMULTI", ScoreType::MultiplayerPoints, NULL, 0, 0, 0, 0 },
};
#undef DEADLINE

static const uint64_t kTimeBase = 100000000;
static const DWORD kRowSize = 0x1660, kRows = 100;
static const DWORD kCollectionVt = 0x00BA9810, kTimeVt = 0x00BA66F0, kIntVt = 0x00BA6738;
static const DWORD kLapsVt = 0x00BA8DE8;
static bool s_installed;
static bool s_originalPickups[_countof(kBoards)];
static bool s_trackBests;
static const BYTE* s_resultHandled;
static const BYTE* s_resultFault;

typedef void(__thiscall* ResultsUpdate_t)(void* self, float dt);
static ResultsUpdate_t s_origResultsUpdate;

static BYTE* Ptr(const BYTE* p, size_t offset)
{
    return p ? *(BYTE* const*)(p + offset) : NULL;
}

static bool Is(const BYTE* p, DWORD vt)
{
    return p && *(const DWORD*)p == vt;
}

static DWORD Hash(const char* key)
{
    DWORD value = 0;
    for (; *key; key++) value = value * 31 + (signed char)*key;  // 0x530450 with its second argument zero
    return value & 0x7fffffffu;
}

static const Board* FindBoardByTrack(const char* track, const char* sub, const char* mode)
{
    if (!track || !sub || !mode) return NULL;
    const size_t len = strnlen_s(mode, 40);
    if (len <= 7 || len == 40 || _stricmp(mode + len - 7, ".params")) return NULL;
    char key[96];
    if (_snprintf_s(key, _TRUNCATE, "%s_%s_%.*s", track, sub, (int)len - 7, mode) < 0) return NULL;
    for (const Board& board : kBoards)
        if (!_stricmp(key, board.key)) return &board;
    return NULL;
}

static const Board* FindBoard(const BYTE* setup)
{
    if (!s_installed || !setup || *(const int*)(setup + 0x9C) != 1 || setup[0x9B2]) return NULL;
    const BYTE* career = *(const BYTE* const*)0x00D64E40;
    if (!career || career[8]) return NULL;
    const Board* board = FindBoardByTrack((const char*)(setup + 0xE0), (const char*)Ptr(setup, 0x1E0),
        (const char*)Ptr(setup, 0x988));
    if (!board) return NULL;
    const int type = *(const int*)setup;
    const bool networkSetup = type == 6 || type == 7;
    if (!networkSetup && OnlineSession()) return NULL;
    if (board->type == ScoreType::QuarryRace) return board;  // one board for offline and multiplayer
    return (board->type == ScoreType::MultiplayerPoints) == networkSetup ? board : NULL;
}

static const BYTE* ViewportPlayer(const BYTE* setup)
{
    const BYTE* resources = *(BYTE* const*)0x00D66B28;
    if (Ptr(resources, 0x14) != setup) return NULL;
    const BYTE* logic = Ptr(resources, 0xE4);
    if (!Is(logic, 0x00BA9A74)) return NULL;
    const DWORD local = *(const DWORD*)(setup + 0xD0);
    const DWORD count = *(const DWORD*)(logic + 0x18);
    if (count > 16 || local >= count) return NULL;
    const BYTE* player = Ptr(Ptr(logic, 0x1C), local * 4);
    return Is(player, 0x00BA63D4) ? player : NULL;
}

static bool OriginalLaps(const BYTE* setup)
{
    const BYTE* player = ViewportPlayer(setup);
    const DWORD id = *(const DWORD*)0x00D0321C;
    const BYTE* laps = id < 29 ? Ptr(player, 0xA4 + id * 4) : NULL;
    const BYTE* out = Is(laps, kLapsVt) ? Ptr(laps, 0x2C) : NULL;
    return out && *(const int*)(out + 0xC) == 3;  // Onslaught Quarry A's default
}

static bool Eligible(const Board& board, const BYTE* setup)
{
    if (board.type == ScoreType::Deadline)
        return s_originalPickups[&board - kBoards] && DeadlineRecordRules(setup, board.countdown);
    if (board.type == ScoreType::QuarryRace) return OriginalLaps(setup);
    return true;
}

static bool IsPoints(const Board& board)
{
    return board.type == ScoreType::Points || board.type == ScoreType::MultiplayerPoints;
}

static uint64_t Encode(const Board& board, uint64_t value)
{
    if (!value) return 0;
    if (IsPoints(board)) return value <= INT_MAX ? value : 0;
    const uint64_t limit = board.countdown ? 100ull * board.countdown : kTimeBase;
    return value < limit ? kTimeBase - value : 0;
}

static BYTE* Record(const Board& board, BYTE** empty = NULL)
{
    BYTE* owner = *(BYTE* const*)0x00D64E38;
    if (!Is(owner, 0x00C0DA28)) return NULL;
    const DWORD count = *(const DWORD*)(owner + 0x2C4);
    if (!count || count > 100) return NULL;
    const DWORD hash = Hash(board.key);
    // A custom PC catalogue may already own the same hash. Leave all its records to the game.
    for (DWORD i = 0; i < count; i++) {
        const char* name = (const char*)Ptr(owner, 0x134 + i * 4);
        if (!name || !_strnicmp(name, board.key, 64) || Hash(name) == hash) return NULL;
    }
    for (DWORD i = 0; i < kRows; i++) {
        BYTE* row = owner + 0x2E0 + i * kRowSize;
        const DWORD rowHash = *(const DWORD*)row;
        const uint64_t score = *(const uint64_t*)(row + 0x30);
        const float local = *(const float*)(row + 0x38);
        const float occupied = *(const float*)(row + 0x1658);
        if (rowHash == hash && local == 0.0f && occupied == 0.0f) return row;
        if (rowHash == hash) {
            if (empty) *empty = NULL;
            return NULL;
        }
        if (empty && !*empty && !rowHash && !score && local == -1.0f && occupied == -1.0f)
            *empty = row;
    }
    return NULL;
}

static bool ScoreIsBest(const Board& board, uint64_t score, BYTE** destination)
{
    BYTE* empty = NULL;
    BYTE* current = Record(board, &empty);
    *destination = current ? current : empty;
    if (!*destination) return false;
    return !current || score > *(const uint64_t*)(current + 0x30);
}

void DlcRecordsRaceStart()
{
    s_resultHandled = NULL;
    s_resultFault = NULL;
}

static void StoreResult(BYTE* screen)
{
    __try {
        BYTE* list = Ptr(screen, 0x4A0);
        if (!list || !list[0x30]) {
            s_resultHandled = NULL;
            s_resultFault = NULL;
            return;  // native result pending
        }
        if (list == s_resultHandled) return;
        const BYTE* setup = Ptr(list, 0);
        const Board* board = FindBoard(setup);
        if (!board) return;
        const DWORD count = *(const DWORD*)(list + 0x18);
        const DWORD local = *(const DWORD*)(setup + 0xD0);  // viewport 0, as used by 0x78E270
        if (!count || count > 8) return;
        const BYTE* row = NULL;
        for (DWORD i = 0; i < count; i++) {
            const BYTE* entry = list + 0x2F4 + i * 0x3C;
            if (!entry[0x2D]) return;  // same completion gate as 0x802B84
            if (*(const DWORD*)entry == local) row = entry;
        }
        if (!row || !row[0x2C]) return;
        s_resultHandled = list;
        const BYTE* collection = Ptr(row, 0x34);
        // 0x79A265 takes the collection from this car's logic outputs before passing it to 0x803400.
        // SteamLAN's remote cars can also set the result's selected bit. Use the local viewport and its output.
        const BYTE* player = ViewportPlayer(setup);
        const DWORD id = *(const DWORD*)0x00D03C78;
        if (!player || id >= 29 || collection != Ptr(player, 0xC + id * 4)) {
            Log("DLC records: %s local result could not be identified", board->key);
            return;
        }
        const BYTE* dq = Ptr(collection, 0x34);
        if (row[0x2F] || !Is(collection, kCollectionVt) ||
            !Is(dq, kIntVt) || *(const int*)(dq + 0xC) == 1) {
            Log("DLC records: %s did not finish", board->key);
            return;
        }
        if (!Eligible(*board, setup)) {
            Log("DLC records: %s skipped (original rules required)", board->key);
            return;
        }

        const int type = *(const int*)(list + 0x2C);
        uint64_t value;
        if (IsPoints(*board)) {
            const BYTE* points = Ptr(collection, 0x24);
            if (type != 2 || !Is(points, kIntVt)) return;
            value = *(const DWORD*)(points + 0xC);
        } else {
            const BYTE* time = Ptr(collection, 0xC);
            if (type != 1 || !Is(time, kTimeVt)) return;
            value = *(const uint64_t*)(time + 0x10);
            // The native save can run before the results screen opens and changes the clock to time left.
            if (board->type == ScoreType::Deadline && DeadlineRowShowsTimeLeft(time)) {
                const uint64_t start = 100ull * board->countdown;
                if (!value || value >= start) return;
                value = start - value;
            }
        }
        const uint64_t score = Encode(*board, value);
        if (!score) return;
        BYTE* dest = NULL;
        if (!ScoreIsBest(*board, score, &dest)) {
            if (!dest) Log("DLC records: %s no available record row", board->key);
            return;
        }
        // Match the stock writer and loader flags. The native serializer owns the rest of the row.
        *(uint64_t*)(dest + 0x30) = score;
        *(DWORD*)dest = Hash(board->key);
        *(float*)(dest + 0x38) = 0.0f;
        *(float*)(dest + 0x1658) = 0.0f;
        Log("DLC records: %s new best %llu (native score %llu)", board->key, value, score);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (s_resultFault != screen) {
            s_resultFault = screen;
            Log("DLC records: result layout could not be read");
        }
    }
}

// Native Update calls 0x802870 and requests its autosave after the result's rows are ready.
static void __fastcall Hook_ResultsUpdate(BYTE* self, void*, float dt)
{
    StoreResult(self);
    s_origResultsUpdate(self, dt);
}

void DlcRecordsRaceEnd(BYTE* card)
{
    DlcRecordsRaceStart();
    __try {
        const BYTE* setup = Ptr(card, 0x390);
        if (!setup || *(const int*)setup == 6 || *(const int*)setup == 7) return;  // the stock card has no current time
        const Board* board = FindBoard(setup);
        const char* time = (const char*)Ptr(card, 0x814);
        if (!board || !time || !*time || !Eligible(*board, setup)) return;
        const uint64_t score = Encode(*board, *(const uint64_t*)(card + 0x808));
        const BYTE* record = Record(*board);
        if (score && record && score > *(const uint64_t*)(record + 0x30)) card[0x810] = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("DLC records: finish card layout could not be read");
    }
}

// The existing DialsInfo best-time element uses the game's localized TEXT_BEST label.
void DlcRecordsDeadlineHud(const BYTE* resources, float start)
{
    __try {
        const Board* board = FindBoard(Ptr(resources, 0x14));
        if (!board || board->type != ScoreType::Deadline || start != (float)board->countdown ||
            !s_originalPickups[board - kBoards]) return;
        const BYTE* row = Record(*board);
        if (!row) return;
        const uint64_t score = *(const uint64_t*)(row + 0x30);
        const unsigned countdown = board->countdown * 100;
        if (score >= kTimeBase || score <= kTimeBase - countdown) return;
        const unsigned left = countdown - (unsigned)(kTimeBase - score);

        // Same holder and methods as the stock HUD initializer at 0x4C357C / 0x4C374F.
        BYTE* timer = Ptr(Ptr(Ptr(resources, 0xCC), 0x128), 0x114);
        if (!timer || !timer[9] || !Ptr(Ptr(timer, 0x158), 4)) return;
        typedef void(__thiscall* Label_t)(void*, const char*);
        typedef void(__thiscall* Time_t)(void*, unsigned, unsigned, unsigned);
        ((Label_t)0x00560600)(timer, "TEXT_BEST");
        ((Time_t)0x00560570)(timer, left / 6000, (left / 100) % 60, left % 100);
        Log("DLC records: %s HUD best %u centiseconds left", board->key, left);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("DLC records: Deadline best-time display unavailable");
    }
}

struct TrackBest {
    const void* movie;
    const Board* board;
    char text[24];
};

// The stock item update invokes the movie synchronously through 0x536AC0.
static thread_local const TrackBest* s_trackBest;
static thread_local bool s_trackBestLogged[_countof(kBoards)];

static bool FormatTrackBest(const Board& board, uint64_t score, char (&text)[24])
{
    if (IsPoints(board)) {
        if (!score || score > INT_MAX) return false;
        const unsigned points = (unsigned)score;
        if (points >= 1000000)
            sprintf_s(text, "%u,%03u,%03u", points / 1000000, (points / 1000) % 1000, points % 1000);
        else if (points >= 1000)
            sprintf_s(text, "%u,%03u", points / 1000, points % 1000);
        else
            sprintf_s(text, "%u", points);
        return true;
    }
    if (!score || score >= kTimeBase) return false;
    unsigned time = (unsigned)(kTimeBase - score);
    if (board.type == ScoreType::Deadline) {
        const unsigned countdown = board.countdown * 100;
        if (time >= countdown) return false;
        time = countdown - time;
    }
    sprintf_s(text, "%02u:%02u.%02u", time / 6000, (time / 100) % 60, time % 100);
    return true;
}

static void PrepareTrackBest(TrackBest& best, const BYTE* self, DWORD index, DWORD widget)
{
    __try {
        const char* name = (const char*)Ptr((const BYTE*)widget, 4);
        if (!name || strcmp(name, "default")) return;  // the track card, at 0x485C7A
        const BYTE* list = Ptr(self, 4);
        const BYTE* screen = Ptr(list, 0x18);
        if (!screen || *(const DWORD*)(screen + 0x240) != 1) return;
        const BYTE* setup = (const BYTE*)0x00D6EE78;  // frontend setup returned by 0x453300
        const int type = *(const int*)setup;
        if (type == 6 || type == 7 || *(const int*)(setup + 0x9C) != 1) return;

        // Match the selected row used by the native board key builder (0x485A70).
        if (index >= *(const DWORD*)(list + 8)) return;
        const BYTE* menu = Ptr(list, 0);
        if (!menu) return;
        const DWORD count = *(const DWORD*)(menu + 0x60);
        const DWORD current = count ? ((const DWORD*)Ptr(menu, 0x64))[count - 1] : *(const DWORD*)(menu + 0x58);
        if (index != current) return;
        const BYTE* rows = Ptr(list, 0xC);
        if (!rows) return;
        const BYTE* item = rows + index * 8;
        const BYTE* track = Ptr(item, 0);
        const char* trackName = (const char*)Ptr(track, 0);
        const char* subtrack = (const char*)Ptr(track, 0x48);
        const Board* board = FindBoardByTrack(trackName, subtrack, (const char*)Ptr(setup, 0x988));
        if (!board || board->type == ScoreType::MultiplayerPoints ||
            (board->type == ScoreType::Deadline && !s_originalPickups[board - kBoards])) return;
        const BYTE* row = Record(*board);
        if (!row) return;

        // Use the same track/mode locks and NEW badge check as the stock card.
        using Locked_t = bool(__thiscall*)(const BYTE*);
        if (((Locked_t)0x004851C0)(item) || ((Locked_t)0x00479670)(screen)) return;
        const BYTE* career = *(const BYTE* const*)0x00D64E40;
        char newTrackKey[64];
        if (!career || _snprintf_s(newTrackKey, _TRUNCATE, "%s%s", trackName, subtrack) < 0) return;
        const char* key = newTrackKey;  // 0x772CB0 reads the string pointer at offset 0 of the game's string
        using IsNew_t = bool(__thiscall*)(const BYTE*, const char* const*);
        if (((IsNew_t)0x00772CB0)(career, &key)) return;
        if (!FormatTrackBest(*board, *(const uint64_t*)(row + 0x30), best.text)) return;
        best.board = board;
        best.movie = Ptr(Ptr((const BYTE*)widget, 0xC), 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("DLC records: Quick Play track card layout could not be read");
    }
}

void DlcRecordsUpdateTrack(QpItemUpdate_t original, void* self, DWORD index, DWORD selected, DWORD widget)
{
    if (!s_trackBests) {
        original(self, index, selected, widget);
        return;
    }
    TrackBest best = {};
    if ((BYTE)selected) PrepareTrackBest(best, (const BYTE*)self, index, widget);
    const TrackBest* previous = s_trackBest;
    s_trackBest = best.movie ? &best : NULL;
    __try {
        original(self, index, selected, widget);
    } __finally {
        s_trackBest = previous;
    }
}

bool DlcTrackBestArgs(void* movie, const char* path, const GFxValue* args, unsigned count, GFxValue* out)
{
    const TrackBest* best = s_trackBest;
    if (!best || best->movie != movie || count != 2 || path != (const char*)0x00BB22D8 ||  // PlaySelectedAnim
        !args || args[0].type != GFX_STRING || args[1].type != GFX_BOOL || args[1].boolean ||
        !args[0].str || *args[0].str) return false;
    out[0] = args[0];
    out[0].str = best->text;
    out[1] = args[1];
    out[1].boolean = 1;
    const size_t index = best->board - kBoards;
    if (!s_trackBestLogged[index]) {
        s_trackBestLogged[index] = true;
        Log("DLC records: Quick Play card %s = %s", best->board->key, best->text);
    }
    return true;
}

static void InstallTrackBests()
{
    __try {
        if (!BytesMatch((void*)0x00485C55, "8B45048B40183BC35657") ||
            !BytesMatch((void*)0x00485C73, "228424A402000068F42FBA00") ||
            !BytesMatch((void*)0x00486206, "8B4E0C578D9424940000005268D822BB00") ||
            !BytesMatch((void*)0x00453300, "B878EED600C3") ||
            !BytesMatch((void*)0x004851C0, "53558BE98B550085D25657") ||
            !BytesMatch((void*)0x00479670, "81C1B0010000E9C5D80000") ||
            !BytesMatch((void*)0x00486F40, "8B0183786000740C") ||
            !BytesMatch((void*)0x00772CB0, "535556578BE933FF397D70") ||
            !BytesMatch((void*)0x00536AC0, "83EC108B54241C8B49048B018B4048")) {
            Log("DLC records: Quick Play track card signature mismatch");
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("DLC records: Quick Play track card signature check failed");
        return;
    }
    const bool steamLan = g_cfg.steamLan && (g_cfg.patchMask & PATCH_ONLINE);
    if (!steamLan) InstallMenuHook(false, true);
    s_trackBests = (steamLan ? OnlineMenuHookInstalled() : MenuHookInstalled()) && DlcInstallTrackListHook();
    Log("DLC records: Quick Play track card %s", s_trackBests ? "enabled" : "hooks unavailable");
}

void DlcInstallRecords()
{
    if (!g_cfg.dlcPersonalBests) return;
    if (CurrentBuild() != BUILD_STEAM || !(g_cfg.patchMask & PATCH_DLC_RECORDS) || !g_cfg.dlcOverlays) {
        Log("DLC records: disabled by the build, PatchMask, or Overlays");
        return;
    }
    void** vt = (void**)0x00C1F83C;
    __try {
        if (!ClassIs(vt, 0x00C972C0, ".?AVcRaceResults@InGameUI@@") ||
            ((DWORD)vt[3] != 0x00802AF0 && !LapRaceHooksInstalled()) ||
            !BytesMatch((void*)0x00802B69, "8BBEA0040000807F3000742D8B5718") ||
            !BytesMatch((void*)0x00802B97, "8B8EA0040000E8CEFCFFFF") ||
            !BytesMatch((void*)0x0078E270, "8B54240433C081C1D0000000") ||
            !BytesMatch((void*)0x0079A34F, "8B4E4C505255E8A6900600") ||
            !BytesMatch((void*)0x004467E0, "8B411C8B4C24048B048883C008") ||
            !BytesMatch((void*)0x00419E19, "F30F104738") ||
            !BytesMatch((void*)0x00419FDF, "F30F118658160000F30F114638") ||
            !BytesMatch((void*)0x00807758, "8BBC243001000089BB08080000") ||
            !BytesMatch((void*)0x004C3850, "81C1CC0000008D6E0C") ||
            !BytesMatch((void*)0x004C3739, "8B560C8B8A280100008B8914010000") ||
            !BytesMatch((void*)0x00560570, "83EC58568BF1807E0900") ||
            !BytesMatch((void*)0x00560600, "83EC18568BF18B8658010000")) {
            Log("DLC records: results or native profile signature mismatch");
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("DLC records: results signature check failed");
        return;
    }
    if (!InstallDlcRaceEnd()) {
        Log("DLC records: finish card hook could not be installed");
        return;
    }
    for (size_t i = 0; i < _countof(kBoards); i++) {
        const Board& board = kBoards[i];
        if (!board.countdown || !PickupsWanted()) continue;
        char path[160];
        sprintf_s(path, "Environments/Levels/%s/Subtracks/%c/Pickups/%s.pkps",
            board.pickupTrack, board.subtrack, board.pickupTrack);
        s_originalPickups[i] = ResidentEntryMatches(path, board.pickupSize, board.pickupCrc);
    }
    s_origResultsUpdate = (ResultsUpdate_t)vt[3];
    s_installed = WriteSlot(&vt[3], (void*)Hook_ResultsUpdate);
    Log("DLC records: %s (cRaceResults slot 3)", s_installed ? "local personal bests enabled" : "results hook failed");
    if (s_installed) InstallTrackBests();
}
