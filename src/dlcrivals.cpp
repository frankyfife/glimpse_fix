// Split/Second PC fix - Deadline (TimeTrialPlus): the Xbox's rival standings and its time-left display on the race
// end screen and in the results list (with [DLC] Modes=1 or Pickups=1, wherever the countdown hook is in; the race
// time stands while the clock is frozen only with Pickups=1; see dlcpickups.cpp for the rest of the mode).
// A restart that does not pass the event objectives screen keeps the race info: the player's entry then holds the
// session's best run (0x7D5C40 keeps the better value), as in a stock Time Trial.
//
// The Xbox (Title Update 4, tools\dlc\mode\TU4_RULES.md section 4) does it in the game's own race info, the list of
// named times the time trial results are made of:
// - Before the race, its pre-race info builder (0x882E7C58 = PC 0x7D6770) adds the player's personal best and,
//   for the Deadline race kind 0xB, THE HAMMER, RAPTOR, VIXEN, LIVE WIRE, TORPEDO, BRAWN and RIGG with
//   (TimeTrialPlusCountdown - <Rival>Time) x 100 (hundredths; flag 1, time), the per-track values of the
//   RaceSetupOverlay/TimeTrialPlus keys. So a rival's value is the clock used, its <Rival>Time the time left.
// - At the finish (0x882E8980 = PC 0x799E50) the player's value is the race time record x 100 as in a Time Trial;
//   TU4's lap timer (0x88380670 = PC cLaps 0x440630) adds no time while the clock is frozen, so that too is the
//   clock used. The entries are sorted (the player ranked among the rivals by the clock used) and up to 8 of them
//   become result rows: entry j is written into player slot j's records (position j, time, not disqualified); the
//   rivals use the dummy player slots 1..7 (TimeTrialPlus.params: 'Fill remaining slots with dummy players').
// - Shown is the time left: the end screen (0x882D25B8 = PC 0x807730) and the rows (0x882E5F78 = PC 0x802420)
//   print Countdown x 100 - value, TEXT_DNF when the value is not below it. The end screen shows no position for
//   kind 0xB (0x882DA1D0, as for kind 1 = Time Trial).
// The PC has every piece but the kind-0xB lines. Here, with data writes and calls of the game's own functions only:
// - The rival entries: right after the event objectives screen built the race info (0x806820 -> 0x7D6FF0 ->
//   0x7D6770; dlcpickups.cpp's wrapper of its vtable slot 2), the 7 entries are added with the game's 0x7D5730
//   (thiscall(name, flag 1, type 0 = time, value), as 0x7D6770 adds the friends' times) and sorted as the game
//   sorts them (0x7D5AD0 with 0x7D53F0 over the 101 entries). 0x7D6FF0 clears the list first (0x7D5570), so they
//   are added once per build; never twice (checked by name). The values come from the overlay the PC reads for
//   the track (GameLogic/Tracks/<track>/Params/RaceSetupOverlay_<sub>.params: deadline_mode.py appends the Xbox
//   keys to the 11 tracks', Quarry's converted overlay carries them already); the PC's parser drops these keys,
//   the DLL reads them itself. The names: glimpse/deadline/RivalNames.txt in the mode pack (the official ones from
//   the title update, tu4text.py), else made from the key names (HammerTime -> HAMMER, LiveWire -> LIVE WIRE).
//   Only when the dummy players 1..7 have the records 0x799E50 writes, and not while the career's current event is
//   a championship (0x801440 would then copy the rows' positions into the season standings, as for any row).
// - The clock used as the player's time: dlcpickups.cpp's wrapper of cLaps' update holds the race time while the
//   clock is frozen (dt 0, as TU4).
// - The time left: the race end screen's time text (+0x814, from the value at +0x808 that 0x807730 got) is written
//   again before the screen shows it (cRaceEnd slot 2), and the rows' time records are turned into the time left
//   before the results screen builds its texts from them (cRaceResults vtable 0xC1F83C slot 2, 0x802E10, which
//   calls 0x802420 and 0x801500), once per finished race. The rows keep their order (they are sorted by the
//   position records).
// The stock writer requires a network catalogue key. dlcrecords.cpp handles eligible local records separately.
// Steam build only, every address checked; inert outside a Deadline race on one screen offline.
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

bool DlcOverlayRead(const char* path, std::vector<unsigned char>* out);  // dlcfs.cpp
bool DeadlineResultsNow(float* start);  // dlcpickups.cpp: a finished Deadline race on one screen, offline, kind 1
float DeadlineStartFor(float base);     // dlcpickups.cpp: the race's start time for the pack's (+ DeadlineBonus)

// ---------------------------------------------------------------- game addresses (Steam build)
static BYTE* const* const kRaceInfo = (BYTE* const*)0x00D719F8;
static const DWORD kInfoCount = 0x1948, kEntries = 8, kEntrySize = 0x40, kMaxEntries = 0x65;
static const int kSortHigh = (int)((kInfoCount - kEntries) / kEntrySize) - 1;  // 100: 0x7D5C40 / 0x7D6770's sort
typedef void(__thiscall* InfoAdd_t)(void* info, const char* name, int flag, int type, DWORD lo, DWORD hi);
typedef void(__cdecl* InfoSort_t)(void* base, int lo, int hi, const void* cmp, void* arg);
typedef void(__thiscall* TimeSet_t)(void* record, DWORD lo, DWORD hi);
typedef void(__cdecl* StrFormat_t)(void* str, const char* fmt, ...);
static const InfoAdd_t kInfoAdd = (InfoAdd_t)0x007D5730;      // ret 0x14
static const InfoSort_t kInfoSort = (InfoSort_t)0x007D5AD0;
static const void* const kInfoCompare = (const void*)0x007D53F0;
static const TimeSet_t kTimeSet = (TimeSet_t)0x00429650;      // cRecordTime: hundredths (+0x10) and seconds (+0xC)
static const StrFormat_t kStrFormat = (StrFormat_t)0x0052FAA0;
static BYTE* const* const kInGameResources = (BYTE* const*)0x00D66B28;
static const DWORD kGameLogicVt = 0x00BA9A74, kPlayerVt = 0x00BA63D4;
static const DWORD* const kRecordCollectionId = (const DWORD*)0x00D03C78;
static const DWORD* const kPositionId = (const DWORD*)0x00D0348C;  // the output whose +4 record 0x799E50 sets to j
static const DWORD kRecordCollectionOutVt = 0x00BA9810, kRecordIntVt = 0x00BA6738, kRecordTimeVt = 0x00BA66F0;
static BYTE* const* const kCareer = (BYTE* const*)0x00D64E40;
static void** const kResultsVt = (void**)0x00C1F83C;  // cRaceResults (COL 0xC972C0), slot 2 = 0x802E10

struct CodeCheck {
    DWORD addr;
    const char* bytes;
    size_t n;
};
static const CodeCheck kCode[] = {
    { 0x007D5730, "\x56\x8B\xF1\x8B\x86\x48\x19\x00\x00\x83\xF8\x65", 12 },  // add: count < 0x65
    { 0x007D57AF, "\x5E\xC2\x14\x00", 4 },                                  // ... ret 0x14
    { 0x007D5AD0, "\x6A\xFF\x68\x48\x19\xB4\x00", 7 },                      // sort
    { 0x007D53F0, "\x8B\x54\x24\x04\x8B\x44\x24\x08\x56\x8B\x72\x3C", 12 },  // its compare
    { 0x007D5D3E, "\x2B\xF0\x81\xC6\x48\x19\x00\x00\xC1\xFE\x06\x8B\xCE\x83\xF9\x02\x7C\x15\x50\x68\xF0\x53\x7D\x00", 24 },
    { 0x00429650, "\x8B\x54\x24\x04\x8B\x44\x24\x08\x89\x54\x24\x04\x89\x51\x10", 15 },  // time record setter
    { 0x0052FAA0, "\x8B\x4C\x24\x08\x8D\x44\x24\x0C\x50\x51", 10 },          // string format
    { 0x004467E0, "\x8B\x41\x1C\x8B\x4C\x24\x04\x8B\x04\x88\x83\xC0\x08", 13 },  // player n's outputs: logic+0x1C [n] + 8
    { 0x0079A0A0, "\x8B\x5F\x34\x8D\x4C\x24", 6 },                          // 0x799E50: the rows' records
    { 0x0079A0DA, "\x8B\x4F\x0C\x85\xC9\x74\x50\x8B\x56\x34", 10 },
    { 0x0080775F, "\x89\xBB\x08\x08\x00\x00\x89\x83\x0C\x08\x00\x00", 12 },  // 0x807730: the time at +0x808
    { 0x0080859D, "\x8B\x86\x14\x08\x00\x00", 6 },                          // 0x8082D0 shows +0x814
    { 0x008033A4, "\x8B\x8E\xA0\x04\x00\x00\xE8\x71\xF0\xFF\xFF", 11 },      // results enter: 0x802420 on +0x4A0
    { 0x008014CD, "\x8D\xBE\xF4\x02\x00\x00\x8B\x57\x38", 9 },              // the rows at list +0x2F4, 0x3C each
    { 0x00801473, "\x8B\x3D\x40\x4E\xD6\x00\x8B\x6F\x0C\x8B\x0F\x55", 12 },  // 0x801440's championship test
};

static bool s_ok;          // installed
static bool s_namesRead;
struct RivalName {
    char key[24];   // HammerTime
    char name[24];  // THE HAMMER
};
static RivalName s_names[16];
static int s_nameCount;
struct Rival {
    char key[24];
    float time;  // <Rival>Time: the time left on the clock when that rival finishes
};
static Rival s_rivals[8];
static int s_rivalCount;
static float s_countdown;  // TimeTrialPlusCountdown of the track
static char s_table[96];   // "<track>/<sub>" of the table ("" none)
static const BYTE* s_convertedTime[8];
static bool s_rowsDone;    // this race's result rows show the time left
static bool s_loggedSkip[4];

typedef void(__thiscall* ScreenEnter_t)(void* self);
static ScreenEnter_t s_origResultsEnter;

static bool Is(const BYTE* p, DWORD vt) { return p && *(const DWORD*)p == vt; }
static BYTE* Ptr(const BYTE* p, DWORD off) { return p ? *(BYTE* const*)(p + off) : NULL; }

static void SkipOnce(int i, const char* why)
{
    if (s_loggedSkip[i]) return;
    s_loggedSkip[i] = true;
    Log("DLC modes: Deadline rivals: %s", why);
}

// ---------------------------------------------------------------- data
static void ReadNames()
{
    if (s_namesRead) return;
    s_namesRead = true;
    std::vector<unsigned char> d;
    if (!DlcOverlayRead("glimpse/deadline/RivalNames.txt", &d) || d.empty()) {
        Log("DLC modes: Deadline rivals: no glimpse/deadline/RivalNames.txt in the mode pack, the names are made from the keys");
        return;
    }
    d.push_back(0);
    char* ctx = NULL;
    for (char* ln = strtok_s((char*)&d[0], "\r\n", &ctx); ln && s_nameCount < 16; ln = strtok_s(NULL, "\r\n", &ctx)) {
        char* eq = strchr(ln, '=');
        if (ln[0] == '#' || !eq || eq == ln || eq - ln >= 24 || strlen(eq + 1) >= 24 || !eq[1]) continue;
        RivalName& r = s_names[s_nameCount++];
        memcpy(r.key, ln, eq - ln);
        r.key[eq - ln] = 0;
        strcpy_s(r.name, eq + 1);
    }
    Log("DLC modes: Deadline rivals: %d names from glimpse/deadline/RivalNames.txt", s_nameCount);
}

// the shown name of a <Rival>Time key: the pack's, else HammerTime -> HAMMER, LiveWire -> LIVE WIRE
static void NameOf(const char* key, char* out, size_t n)
{
    for (int i = 0; i < s_nameCount; i++)
        if (!_stricmp(s_names[i].key, key)) {
            strcpy_s(out, n, s_names[i].name);
            return;
        }
    size_t len = strlen(key), o = 0;
    if (len > 4 && !_stricmp(key + len - 4, "Time")) len -= 4;
    for (size_t i = 0; i < len && o + 2 < n; i++) {
        char c = key[i];
        if (i && c >= 'A' && c <= 'Z' && key[i - 1] >= 'a' && key[i - 1] <= 'z') out[o++] = ' ';
        out[o++] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    out[o] = 0;
}

// the Xbox keys in the track overlay the PC reads: /RaceSetupOverlay/TimeTrialPlus <Rival>Time, /RaceSetupOverlay
// TimeTrialPlusCountdown. "name = 12.5 (0.0, 120.0);" lines; the last value of a key counts
static bool LoadTable(const char* track, const char* sub)
{
    char key[96];
    sprintf_s(key, "%s/%s", track, sub);
    if (!strcmp(key, s_table)) return s_rivalCount > 0 && s_countdown > 0.0f;
    strcpy_s(s_table, key);
    s_rivalCount = 0;
    s_countdown = 0.0f;
    char path[160];
    sprintf_s(path, "GameLogic/Tracks/%s/Params/RaceSetupOverlay_%s.params", track, sub);
    std::vector<unsigned char> d;
    if (!DlcOverlayRead(path, &d) || d.empty()) {
        Log("DLC modes: Deadline rivals: %s not readable, no rivals on this track", path);
        return false;
    }
    d.push_back(0);
    enum { NONE, RIVALS, OVERLAY } sec = NONE;
    char* ctx = NULL;
    for (char* ln = strtok_s((char*)&d[0], "\r\n", &ctx); ln; ln = strtok_s(NULL, "\r\n", &ctx)) {
        if (ln[0] == '/') {
            sec = !_stricmp(ln, "/RaceSetupOverlay/TimeTrialPlus:") ? RIVALS : !_stricmp(ln, "/RaceSetupOverlay:") ? OVERLAY : NONE;
            continue;
        }
        if (sec == NONE) continue;
        while (*ln == '\t' || *ln == ' ') ln++;
        char* eq = strchr(ln, '=');
        if (!eq) continue;
        char* end = eq;
        while (end > ln && end[-1] == ' ') end--;
        const size_t kl = end - ln;
        const float v = (float)atof(eq + 1);
        if (!(v >= 0.0f && v <= 3600.0f) || kl == 0 || kl >= sizeof(s_rivals[0].key)) continue;
        if (sec == OVERLAY && kl == 22 && !_strnicmp(ln, "TimeTrialPlusCountdown", kl)) {
            s_countdown = v;
        } else if (sec == RIVALS && kl > 4 && !_strnicmp(end - 4, "Time", 4)) {
            int i = 0;
            while (i < s_rivalCount && (strlen(s_rivals[i].key) != kl || _strnicmp(s_rivals[i].key, ln, kl))) i++;
            if (i == s_rivalCount) {
                if (s_rivalCount == 7) continue;  // the results show 8 rows: the player and 7 rivals
                s_rivalCount++;
                memcpy(s_rivals[i].key, ln, kl);
                s_rivals[i].key[kl] = 0;
            }
            s_rivals[i].time = v;
        }
    }
    Log("DLC modes: Deadline rivals: %s: %d rivals, TimeTrialPlusCountdown %.1f s", path, s_rivalCount, s_countdown);
    return s_rivalCount > 0 && s_countdown > 0.0f;
}

// ---------------------------------------------------------------- checks
// player slot n's outputs as 0x799E50 uses them for a row: the record collection (time, disqualified) and the
// output whose +4 record holds the row's position
static bool SlotReady(BYTE* logic, int n)
{
    BYTE* player = Ptr(Ptr(logic, 0x1C), n * 4);
    if (!Is(player, kPlayerVt)) return false;
    const DWORD rcId = *kRecordCollectionId, posId = *kPositionId;
    if (rcId > 63 || posId > 63) return false;
    BYTE* rc = Ptr(player, 0xC + rcId * 4);
    BYTE* pos = Ptr(player, 0xC + posId * 4);
    return Is(rc, kRecordCollectionOutVt) && Is(Ptr(rc, 0xC), kRecordTimeVt) && Is(Ptr(rc, 0x34), kRecordIntVt) && pos &&
           Is(pos + 4, kRecordIntVt);
}

// 0x801440 copies the rows' positions into the season standings while the career's current event is a
// championship ([0xD64E40]: +8 the career flag, +0 the season, +0xC the episode, +0x10 the event; event +0xE4 == 3)
static bool ChampionshipContext()
{
    BYTE* c = *kCareer;
    if (!c || !c[8]) return false;  // the career flag (0x7D5880, 0x803400): not in quick play
    BYTE* season = Ptr(c, 0);
    if (!season) return false;
    const DWORD ep = *(const DWORD*)(c + 0xC), ev = *(const DWORD*)(c + 0x10);
    if (ep >= *(const DWORD*)(season + 0x38)) return false;
    BYTE* episode = Ptr(Ptr(season, 0x3C), ep * 4);
    // the episode's events: count +4, array +8 (as the season's event getters check them, e.g. 0x775320)
    if (!episode || ev >= *(const DWORD*)(episode + 4)) return false;
    BYTE* event = Ptr(Ptr(episode, 8), ev * 4);
    return event && *(const int*)(event + 0xE4) == 3;
}

static bool Ready(BYTE** logicOut)
{
    BYTE* res = *kInGameResources;
    BYTE* logic = Ptr(res, 0xE4);
    if (!Is(logic, kGameLogicVt)) return false;
    const int n = *(const int*)(logic + 0x18);
    if (n < 8 || n > 16) {
        SkipOnce(0, "fewer than 8 player slots (no dummy players for the rival rows), no rivals");
        return false;
    }
    for (int i = 1; i < 8; i++)
        if (!SlotReady(logic, i)) {
            SkipOnce(1, "the dummy players' records are not as expected, no rivals");
            return false;
        }
    *logicOut = logic;
    return true;
}

// ---------------------------------------------------------------- the rival entries (event objectives screen)
// info: [0xD719F8] after 0x7D6FF0 built it for a Deadline race (dlcpickups.cpp checked mode, screens, online)
void RivalsObjectives(BYTE* info)
{
    if (!s_ok || !info) return;
    __try {
        BYTE* setup = Ptr(info, 0x1CF8);
        char track[64] = "", sub[16] = "";
        const char* t = setup ? (const char*)(setup + 0xE0) : NULL;
        const char* s = setup ? *(const char* const*)(setup + 0x1E0) : NULL;
        for (int i = 0; t && t[i] && i < 63; i++) track[i] = t[i];
        for (int i = 0; s && s[i] && i < 15; i++) sub[i] = s[i];
        if (!track[0] || !sub[0]) return;
        BYTE* logic = NULL;
        if (!Ready(&logic)) return;
        if (ChampionshipContext()) {
            SkipOnce(2, "the career's current event is a championship (the rows would reach its standings), no rivals");
            return;
        }
        ReadNames();
        if (!LoadTable(track, sub)) return;
        DWORD count = *(const DWORD*)(info + kInfoCount);
        if (count + s_rivalCount > kMaxEntries - 1) return;
        char names[8][24];
        for (int i = 0; i < s_rivalCount; i++) NameOf(s_rivals[i].key, names[i], sizeof(names[i]));
        for (DWORD e = 0; e < count; e++) {  // already there (the list was not rebuilt): leave it
            const BYTE* en = info + kEntries + e * kEntrySize;
            const char* nm = *(const char* const*)en;
            if (*(const int*)(en + 0x28) != 1 || !nm) continue;
            for (int i = 0; i < s_rivalCount; i++)
                if (!strcmp(nm, names[i])) return;
        }
        // the clock used against the race's own start time (with Pickups=0 the pack's + DeadlineBonus), so that the
        // results, which show every row's time left from that start, show each rival's <Rival>Time as it is
        const float start = DeadlineStartFor(s_countdown);
        for (int i = 0; i < s_rivalCount; i++) {
            // as TU4: (float)(Countdown - <Rival>Time) * 100.0, truncated, hundredths
            const float used = start - s_rivals[i].time;
            const __int64 v = used > 0.0f ? (__int64)((double)used * 100.0) : 0;
            kInfoAdd(info, names[i], 1, 0, (DWORD)v, (DWORD)(v >> 32));
        }
        kInfoSort(info + kEntries, 0, kSortHigh, kInfoCompare, info + kEntries);
        Log("DLC modes: Deadline rivals: %d added to the race's times (%s/%s, countdown %.1f s, start %.1f s), %lu entries",
            s_rivalCount, track, sub, s_countdown, start, *(const DWORD*)(info + kInfoCount));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SkipOnce(3, "reading the race info failed, no rivals");
    }
}

// ---------------------------------------------------------------- time left
// The end screen hands its time text to the movie as it is (0x8085A3..0x8085C7, no dictionary lookup, unlike the
// result rows' 0x802420), so the DNF text is looked up here: the game's dictionary ([0xD64E28], 0xA15BF0, which
// gives the key back when it has no text). Inside the caller's __try.
static const char* DnfText()
{
    typedef const char*(__thiscall* DictLookup_t)(void* dict, const char* key);
    void* dict = *(void* const*)0x00D64E28;
    const char* t = dict ? ((DictLookup_t)0x00A15BF0)(dict, "TEXT_DNF") : NULL;
    return t && *t && strcmp(t, "TEXT_DNF") ? t : "DNF";
}

static void FormatLeft(void* str, unsigned __int64 used, float start)
{
    const unsigned __int64 cd = (unsigned __int64)(int)start * 100;  // TU4: (int)Countdown * 100
    if (used >= cd) {
        kStrFormat(str, "%s", DnfText());
        return;
    }
    const unsigned __int64 left = cd - used;
    kStrFormat(str, "%.2d:%.2d:%.2d", (int)(left / 6000), (int)((left % 6000) / 100), (int)(left % 100));
}

// cRaceEnd's enter (dlcpickups.cpp's wrapper, before the game's): the time text 0x807730 wrote is the clock used
void RivalsRaceEnd(BYTE* screen)
{
    float start = 0.0f;
    if (!s_ok || !screen || !DeadlineResultsNow(&start)) return;
    __try {
        const char* text = *(const char* const*)(screen + 0x814);
        if (!text || !text[0]) return;  // disqualified (0x807730 emptied it)
        char old[32];
        strncpy_s(old, text, _TRUNCATE);  // the format may give the string new memory
        const unsigned __int64 used = *(const unsigned __int64*)(screen + 0x808);
        FormatLeft(screen + 0x814, used, start);
        Log("DLC modes: Deadline: end screen time %s -> %s (time left of %.1f s)", old,
            *(const char* const*)(screen + 0x814) ? *(const char* const*)(screen + 0x814) : "?", start);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// cRaceResults' enter: every row's time record (the clock used) becomes the time left, once per finished race
static void ResultsTimeLeft(BYTE* screen)
{
    float start = 0.0f;
    if (s_rowsDone || !DeadlineResultsNow(&start)) return;
    s_rowsDone = true;
    __try {
        BYTE* list = Ptr(screen, 0x4A0);
        if (!list) return;
        const unsigned __int64 cd = (unsigned __int64)(int)start * 100;
        int n = 0;
        for (int i = 0; i < 8; i++) {
            BYTE* row = list + 0x2F4 + i * 0x3C;
            if (!row[0x2D]) continue;  // +0x321 used
            BYTE* rc = Ptr(row, 0x34);  // +0x328 the record collection output
            if (!Is(rc, kRecordCollectionOutVt)) continue;
            BYTE* dq = Ptr(rc, 0x34);
            BYTE* tr = Ptr(rc, 0xC);
            if (!Is(tr, kRecordTimeVt)) continue;
            if (Is(dq, kRecordIntVt) && *(const int*)(dq + 0xC) == 1) {
                // disqualified (the clock ran out): as TU4 (0x882E8E60: a DNF row when the time left is 0), time 0 and
                // the row's DNF flag (+0x323), with which 0x802420 prints TEXT_DNF; 0x802870 then offers no best time
                kTimeSet(tr, 0, 0);
                row[0x2F] = 1;
                n++;
                continue;
            }
            const unsigned __int64 used = *(const unsigned __int64*)(tr + 0x10);
            const unsigned __int64 left = used < cd ? cd - used : 0;
            kTimeSet(tr, (DWORD)left, (DWORD)(left >> 32));
            s_convertedTime[i] = tr;
            n++;
        }
        Log("DLC modes: Deadline: %d result rows show the time left (of %.1f s) or DNF", n, start);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void __fastcall Hook_ResultsEnter(void* self, void*)
{
    ResultsTimeLeft((BYTE*)self);
    s_origResultsEnter(self);
}

// a new or restarted race
void RivalsRaceStart()
{
    s_rowsDone = false;
    memset(s_convertedTime, 0, sizeof(s_convertedTime));
    DlcRecordsRaceStart();
}

bool DeadlineRowShowsTimeLeft(const BYTE* time)
{
    for (const BYTE* row : s_convertedTime)
        if (row == time) return true;
    return false;
}

// from DlcInstallPickups (Steam build, Modes=1 or Pickups=1, the mode in the merged overlay)
bool RivalsInstall()
{
    __try {
        for (size_t i = 0; i < sizeof(kCode) / sizeof(kCode[0]); i++)
            if (memcmp((const void*)kCode[i].addr, kCode[i].bytes, kCode[i].n)) {
                Log("DLC modes: Deadline rivals: code at 0x%08lX not as expected, no rivals and no time left display", kCode[i].addr);
                return false;
            }
        if (!ClassIs(kResultsVt, 0x00C972C0, ".?AVcRaceResults@InGameUI@@") || (DWORD)kResultsVt[2] != 0x00802E10 ||
            memcmp((const void*)0x00802E10, "\x6A\xFF\x64\xA1\x00\x00\x00\x00\x68\x59\x36\xB4\x00", 13)) {
            Log("DLC modes: Deadline rivals: the results screen is not as expected, no rivals");
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    DWORD old;
    if (!VirtualProtect(&kResultsVt[2], 4, PAGE_READWRITE, &old)) return false;
    s_origResultsEnter = (ScreenEnter_t)kResultsVt[2];
    InterlockedExchangePointer(&kResultsVt[2], (void*)Hook_ResultsEnter);
    VirtualProtect(&kResultsVt[2], 4, old, &old);
    s_ok = true;
    Log("DLC modes: Deadline rivals and time left: results screen hooked (vtable 0xC1F83C slot 2)");
    return true;
}
