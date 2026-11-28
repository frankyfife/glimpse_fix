// Split/Second controller fix - SteamLAN=1: the game's LAN mode over Steam (PROTOTYPE)
//
// The game's network code is Quazal's: Net-Z sessions over PRUDP (reliable UDP). In LAN mode the
// host answers session discovery broadcasts on UDP port 9100 (to 255.255.255.255, 0xAF8170), and
// the players connect to the host's PRUDP port (9104-9199, 0x5D33C0). Quazal's sockets go through a
// replaceable transport, its SocketDriver: when Network starts (0x5C33E0) it takes the driver from
// [0xD77734] (null by default: plain Winsock) into [0xD779FC]; every socket then asks the driver for
// its own socket object (0x5D3360) and opens, binds, sends, receives and waits through it
// (0x5D35C0, 0x5D33C0, 0x5D3700, 0x5D3840, 0x5E1870).
//
// This puts a driver there that carries the datagrams through Steam (ISteamNetworkingMessages,
// relayed by Steam, so no ports have to be opened) to the members of a Steam lobby. Every player
// has a virtual address 10.x.y.z made from the Steam account id; a broadcast goes to every member.
// Quazal learns its own address from the interface list (WSAIoctl SIO_GET_INTERFACE_LIST, 0x5E1A80);
// that call is answered with the virtual address through the import slot, so the address the host
// advertises is one the others can reach. A data write and an import slot change, no game code.
//
// steam_api.dll (Steamworks SDK, flat API) is loaded from the game folder at run time; the game runs
// as its own app 297860, which has Steam lobbies and the relay network.
//
// With DLC tracks registered online ([DLC] TracksOnline=1), or DLC cars the game lists online (UnlockAll=1 with a car
// pack), only players with the same DLC tracks and cars see each other's LAN games (the DLC check below
// SendDatagram); players without either are not affected by each other.
#include "common.h"
#include <d3d9.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <deque>
#include <string>
#include <vector>

// ---------------------------------------------------------------- Quazal's SocketDriver interface

// address as Quazal passes it: IPv4 in host byte order, then the port
struct Endpoint {
    uint32_t ip;
    uint16_t port;
};

enum { Q_OK = 0, Q_ERROR = 1, Q_WOULDBLOCK = 2 };  // results of the socket calls (0x5D34A0)

// vtable order of Quazal's SocketDriver::Socket (BerkeleySocket 0xBDE06C); thiscall, the callee
// pops its arguments, exactly what MSVC makes of these virtual functions
class QSocket {
public:
    virtual bool Open(int type);                               // 0 = UDP, 1 = TCP
    virtual void Close();
    virtual bool Bind(unsigned port);
    virtual int RecvFrom(char* buf, int len, Endpoint* from, int* got);
    virtual int SendTo(const char* buf, int len, const Endpoint* to, int* sent);
    virtual int Connect(const Endpoint* to);
    virtual int Recv(char* buf, int len, int* got);
    virtual int Send(const char* buf, int len, int* sent);
    virtual bool SetOption(int value);
    virtual QSocket* Destroy(unsigned flags);

    struct Packet {
        Endpoint from;
        std::vector<char> data;
    };
    uint16_t port = 0;
    bool udp = false;
    std::deque<Packet> queue;
};

struct SelectEntry {
    QSocket* socket;
    int want;   // 1 readable, 2 writable
    int ready;
};

// vtable order of Quazal's SocketDriver (BerkeleySocketDriver 0xBDD270)
class QDriver {
public:
    virtual QDriver* Destroy(unsigned) { return this; }  // static object, never freed
    virtual QSocket* CreateSocket();
    virtual void DeleteSocket(QSocket* s);
    virtual bool Select(SelectEntry* entries, unsigned count, unsigned timeoutMs);
};

static QDriver s_driver;
static CRITICAL_SECTION s_lock;          // sockets, their queues, the peers
static HANDLE s_arrived;                 // set when a datagram was queued
static std::vector<QSocket*> s_sockets;
static uint32_t s_myIp;                  // 10.x.y.z, host byte order; 0 until Steam is up
static uint16_t s_nextPort = 50000;

// Log=1: traffic counters, summed up every 10 seconds
static volatile LONG s_sentDirect, s_sentBroadcast, s_sentLocal, s_received, s_selects, s_dropped;
static volatile LONG s_sendFailed, s_lastSendResult, s_queueFull, s_truncated, s_badWire, s_tooBig;

// Log=1: each kind of event logged the first time only (per key), so a fault shows without flooding
static CRITICAL_SECTION s_diagLock;
static std::vector<uint64_t> s_seen;
static bool FirstTime(uint64_t key)
{
    EnterCriticalSection(&s_diagLock);
    bool first = s_seen.size() < 512;
    for (uint64_t k : s_seen)
        if (k == key) {
            first = false;
            break;
        }
    if (first) s_seen.push_back(key);
    LeaveCriticalSection(&s_diagLock);
    return first;
}
enum DiagKind : uint64_t {
    DIAG_NO_SOCKET = 1ull << 56, DIAG_FIRST_IN = 2ull << 56, DIAG_FIRST_OUT = 3ull << 56, DIAG_QUEUE_FULL = 4ull << 56,
    DIAG_TRUNCATED = 5ull << 56, DIAG_BAD_WIRE = 6ull << 56, DIAG_TOO_BIG = 7ull << 56, DIAG_UNSUPPORTED = 8ull << 56,
    DIAG_WINSOCK_SOCKET = 9ull << 56, DIAG_SEND_FAILED = 10ull << 56, DIAG_IP_CLASH = 11ull << 56,
    DIAG_DLC = 12ull << 56,
};
static uint64_t Key(DiagKind kind, uint32_t ip, uint16_t port) { return kind | ((uint64_t)ip << 16) | port; }

// ---------------------------------------------------------------- Steam (flat API)

typedef uint64_t SteamAPICall_t;
struct SteamNetworkingIdentity { BYTE raw[136]; };
struct SteamNetworkingMessage_t { void* data; int size; };  // the leading fields, the rest is not read
#pragma pack(push, 8)
struct LobbyCreated_t { int result; uint64_t lobby; };                                    // 513
struct LobbyEnter_t { uint64_t lobby; uint32_t permissions; bool locked; uint32_t response; };  // 504
struct LobbyMatchList_t { uint32_t count; };                                              // 510
#pragma pack(pop)

static struct {
    int (__cdecl* InitFlat)(char* err);
    void (__cdecl* RunCallbacks)();
    void* (__cdecl* User)();
    void* (__cdecl* Utils)();
    void* (__cdecl* Matchmaking)();
    void* (__cdecl* Messages)();
    void* (__cdecl* NetUtils)();
    uint64_t (__cdecl* GetSteamID)(void*);
    bool (__cdecl* IsDone)(void*, SteamAPICall_t, bool*);
    bool (__cdecl* Result)(void*, SteamAPICall_t, void*, int, int, bool*);
    SteamAPICall_t (__cdecl* CreateLobby)(void*, int, int);
    SteamAPICall_t (__cdecl* JoinLobby)(void*, uint64_t);
    void (__cdecl* LeaveLobby)(void*, uint64_t);
    SteamAPICall_t (__cdecl* RequestLobbyList)(void*);
    void (__cdecl* StringFilter)(void*, const char*, const char*, int);
    void (__cdecl* DistanceFilter)(void*, int);
    uint64_t (__cdecl* LobbyByIndex)(void*, int);
    bool (__cdecl* SetLobbyData)(void*, uint64_t, const char*, const char*);
    int (__cdecl* NumMembers)(void*, uint64_t);
    uint64_t (__cdecl* MemberByIndex)(void*, uint64_t, int);
    int (__cdecl* SendToUser)(void*, const SteamNetworkingIdentity*, const void*, uint32_t, int, int);
    int (__cdecl* ReceiveOnChannel)(void*, int, SteamNetworkingMessage_t**, int);
    bool (__cdecl* AcceptSession)(void*, const SteamNetworkingIdentity*);
    void (__cdecl* ReleaseMessage)(SteamNetworkingMessage_t*);
    void (__cdecl* IdentitySetSteamID)(SteamNetworkingIdentity*, uint64_t);
    void (__cdecl* IdentityClear)(SteamNetworkingIdentity*);
    void (__cdecl* InitRelay)(void*);
    int (__cdecl* SessionInfo)(void*, const SteamNetworkingIdentity*, void* info, void* status);
    void (__cdecl* SlotsFilter)(void*, int);
    const char* (__cdecl* GetLobbyData)(void*, uint64_t, const char*);
    uint64_t (__cdecl* LobbyOwner)(void*, uint64_t);
    void (__cdecl* SetMemberData)(void*, uint64_t, const char*, const char*);
    const char* (__cdecl* GetMemberData)(void*, uint64_t, uint64_t, const char*);
    bool (__cdecl* SetJoinable)(void*, uint64_t, bool);
    void* (__cdecl* Friends)();
    int (__cdecl* FriendCount)(void*, int);
    uint64_t (__cdecl* FriendByIndex)(void*, int, int);
    bool (__cdecl* FriendGamePlayed)(void*, uint64_t, void*);
    bool (__cdecl* SetRichPresence)(void*, const char*, const char*);
    void (__cdecl* InviteDialog)(void*, uint64_t);
    void (__cdecl* RegisterCallback)(void* callback, int id);
} S;

// SteamNetConnectionInfo_t (pack 8), the fields read here
#pragma pack(push, 8)
struct ConnectionInfo {
    SteamNetworkingIdentity remote;  // 0
    int64_t userData;                // 136
    uint32_t listenSocket;           // 144
    BYTE addrRemote[18];             // 148
    uint16_t pad;                    // 166
    uint32_t popRemote;              // 168
    uint32_t popRelay;               // 172, non-zero: through a Steam relay
    int state;                       // 176
    int endReason;                   // 180
    char endDebug[128];              // 184
    char description[128];           // 312
    int flags;                       // 440
    uint32_t reserved[63];
};
// SteamNetConnectionRealTimeStatus_t, the leading fields
struct ConnectionStatus {
    int state;
    int ping;
    float qualityLocal, qualityRemote;
    float outPackets, outBytes, inPackets, inBytes;
    int sendRate, pendingUnreliable, pendingReliable, sentUnacked;
    int64_t queueTime;
    uint32_t reserved[16];
};
#pragma pack(pop)
static_assert(offsetof(ConnectionInfo, state) == 176 && offsetof(ConnectionInfo, flags) == 440, "SteamNetConnectionInfo_t");

static void *s_user, *s_utils, *s_mm, *s_msg;
static CRITICAL_SECTION s_steamLock;     // Steam calls from the game's network threads and ours
static uint64_t s_lobby, s_mySteamID;
static volatile LONG s_lobbySerial;      // counts joins and creates: the own member data is published again after each

struct Peer {
    uint64_t steamID;
    uint32_t ip;
    SteamNetworkingIdentity identity;
    int state;      // Steam connection state last logged
    bool relayed;
    bool dlcKnown;        // its DLC tracks are known (lobby member data glimpse_dlc); kept once known
    bool dlcCached;       // ... only from the cache of an earlier membership (s_dlcSeen) until its member data comes
    char dlc[24];         // their signature (DlcOnlineSignature), "none" without DLC tracks
    char dlcNames[96];    // which, for the log and the banner
};
static std::vector<Peer> s_peers;        // the other lobby members

// the last signature seen per Steam ID while the game runs (s_lock): a member that leaves the lobby and comes back
// (Steam drops its member data on leave, it publishes again after its join) is taken by it until its new member
// data arrives, instead of being "not known yet" for a second or two, which would hold back all game traffic
// between it and a player with DLC tracks, also in a running race (NotePeersDlc replaces it if it differs)
struct SeenDlc {
    uint64_t steamID;
    char dlc[24];
    char dlcNames[96];
};
static std::vector<SeenDlc> s_dlcSeen;

static const uint32_t kMagic = 0x314C4C47;  // "GLL1"
static const int kSendFlags = 1 | 32;       // unreliable without Nagle delay, restart broken sessions
static const char kLobbyVersion[] = "3";     // players of other versions do not meet (lobby rules 2, host rules 3)
static const char kDefaultLobby[] = "glimpse";  // the public lobby when LobbyName is empty

#pragma pack(push, 1)
struct Wire {
    uint32_t magic;
    uint32_t srcIp;
    uint16_t srcPort;
    uint16_t dstPort;  // 0: hello, opens the session
};
#pragma pack(pop)

static uint32_t VirtualIp(uint64_t steamID)
{
    uint32_t ip = 0x0A000000 | ((uint32_t)steamID & 0x00FFFFFF);
    if ((ip & 0xFF) == 0 || (ip & 0xFF) == 0xFF) ip ^= 0x01;
    return ip;
}

static void IpText(uint32_t ip, char* out, size_t n)
{
    _snprintf_s(out, n, _TRUNCATE, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

template <class T> static bool Load(HMODULE dll, T& fn, const char* name)
{
    fn = (T)GetProcAddress(dll, name);
    if (!fn) Log("online: steam_api.dll has no %s", name);
    return fn != NULL;
}

static bool LoadSteam()
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    strcpy_s(slash ? slash + 1 : path, MAX_PATH - (slash ? slash + 1 - path : 0), "steam_api.dll");
    HMODULE dll = LoadLibraryA(path);
    if (!dll) {
        Log("online: %s not found (error %lu), SteamLAN off", path, GetLastError());
        return false;
    }
    bool ok = Load(dll, S.InitFlat, "SteamAPI_InitFlat") & Load(dll, S.RunCallbacks, "SteamAPI_RunCallbacks") &
              Load(dll, S.User, "SteamAPI_SteamUser_v023") & Load(dll, S.Utils, "SteamAPI_SteamUtils_v010") &
              Load(dll, S.Matchmaking, "SteamAPI_SteamMatchmaking_v009") &
              Load(dll, S.Messages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002") &
              Load(dll, S.NetUtils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004") &
              Load(dll, S.GetSteamID, "SteamAPI_ISteamUser_GetSteamID") &
              Load(dll, S.IsDone, "SteamAPI_ISteamUtils_IsAPICallCompleted") &
              Load(dll, S.Result, "SteamAPI_ISteamUtils_GetAPICallResult") &
              Load(dll, S.CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby") &
              Load(dll, S.JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby") &
              Load(dll, S.LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby") &
              Load(dll, S.RequestLobbyList, "SteamAPI_ISteamMatchmaking_RequestLobbyList") &
              Load(dll, S.StringFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter") &
              Load(dll, S.DistanceFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter") &
              Load(dll, S.LobbyByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyByIndex") &
              Load(dll, S.SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData") &
              Load(dll, S.NumMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers") &
              Load(dll, S.MemberByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex") &
              Load(dll, S.SendToUser, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser") &
              Load(dll, S.ReceiveOnChannel, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel") &
              Load(dll, S.AcceptSession, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser") &
              Load(dll, S.ReleaseMessage, "SteamAPI_SteamNetworkingMessage_t_Release") &
              Load(dll, S.IdentitySetSteamID, "SteamAPI_SteamNetworkingIdentity_SetSteamID64") &
              Load(dll, S.IdentityClear, "SteamAPI_SteamNetworkingIdentity_Clear") &
              Load(dll, S.InitRelay, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess") &
              Load(dll, S.SessionInfo, "SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo") &
              Load(dll, S.SlotsFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListFilterSlotsAvailable") &
              Load(dll, S.GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData") &
              Load(dll, S.LobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner") &
              Load(dll, S.SetMemberData, "SteamAPI_ISteamMatchmaking_SetLobbyMemberData") &
              Load(dll, S.GetMemberData, "SteamAPI_ISteamMatchmaking_GetLobbyMemberData") &
              Load(dll, S.SetJoinable, "SteamAPI_ISteamMatchmaking_SetLobbyJoinable") &
              Load(dll, S.Friends, "SteamAPI_SteamFriends_v018") &
              Load(dll, S.SetRichPresence, "SteamAPI_ISteamFriends_SetRichPresence") &
              Load(dll, S.InviteDialog, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog") &
              Load(dll, S.RegisterCallback, "SteamAPI_RegisterCallback");
    if (!ok) {
        Log("online: this steam_api.dll is too old (needs SteamNetworkingMessages002), SteamLAN off");
        return false;
    }
    S.FriendCount = (int(__cdecl*)(void*, int))GetProcAddress(dll, "SteamAPI_ISteamFriends_GetFriendCount");
    S.FriendByIndex = (uint64_t(__cdecl*)(void*, int, int))GetProcAddress(dll, "SteamAPI_ISteamFriends_GetFriendByIndex");
    S.FriendGamePlayed = (bool(__cdecl*)(void*, uint64_t, void*))GetProcAddress(dll, "SteamAPI_ISteamFriends_GetFriendGamePlayed");
    if (g_cfg.friendsOnly && !(S.FriendCount && S.FriendByIndex && S.FriendGamePlayed)) {
        Log("online: this steam_api.dll cannot list friends' games, FriendsOnly needs a newer one; the lobby name is used");
        g_cfg.friendsOnly = false;
    }
    char app[16];
    if (!GetEnvironmentVariableA("SteamAppId", app, sizeof(app))) SetEnvironmentVariableA("SteamAppId", "297860");
    char err[1024] = "";
    int init = S.InitFlat(err);
    if (init != 0) {
        Log("online: SteamAPI_InitFlat failed (%d: %s), SteamLAN off", init, err);
        return false;
    }
    s_user = S.User();
    s_utils = S.Utils();
    s_mm = S.Matchmaking();
    s_msg = S.Messages();
    s_mySteamID = S.GetSteamID(s_user);
    S.InitRelay(S.NetUtils());
    return s_user && s_utils && s_mm && s_msg && s_mySteamID;
}

// waits for a call result on the online thread
static bool Wait(SteamAPICall_t call, void* out, int size, int id)
{
    bool failed = false;
    for (int i = 0; i < 400 && call; i++) {
        S.RunCallbacks();
        if (S.IsDone(s_utils, call, &failed)) break;
        Sleep(25);
    }
    return call && S.Result(s_utils, call, out, size, id, &failed) && !failed;
}

// ---------------------------------------------------------------- datagrams

static QSocket* SocketOnPort(uint16_t port)
{
    for (QSocket* s : s_sockets)
        if (s->udp && s->port == port) return s;
    return NULL;
}

static void Deliver(uint32_t srcIp, uint16_t srcPort, uint16_t dstPort, const char* data, int size)
{
    bool queued = false, full = false;
    EnterCriticalSection(&s_lock);
    if (QSocket* s = SocketOnPort(dstPort)) {
        if (s->queue.size() >= 256) {
            s->queue.pop_front();
            full = true;
        }
        QSocket::Packet p;
        p.from.ip = srcIp;
        p.from.port = srcPort;
        p.data.assign(data, data + size);
        s->queue.push_back(std::move(p));
        SetEvent(s_arrived);
        queued = true;
    }
    LeaveCriticalSection(&s_lock);
    char t[20];
    IpText(srcIp, t, sizeof(t));
    if (!queued) {
        InterlockedIncrement(&s_dropped);  // nobody listens on that port (yet)
        if (FirstTime(Key(DIAG_NO_SOCKET, srcIp, dstPort)))
            Log("online: datagram from %s:%u for port %u, but no socket there (%d bytes)", t, srcPort, dstPort, size);
        return;
    }
    if (full) {
        InterlockedIncrement(&s_queueFull);
        if (FirstTime(Key(DIAG_QUEUE_FULL, 0, dstPort)))
            Log("online: receive queue of port %u full (256), the oldest datagram dropped: Quazal reads too slowly", dstPort);
    }
    if (FirstTime(Key(DIAG_FIRST_IN, srcIp, dstPort)))
        Log("online: first datagram from %s:%u to port %u (%d bytes)", t, srcPort, dstPort, size);
}

static const char* SteamResultText(int r)
{
    switch (r) {
    case 1: return "OK";
    case 2: return "fail";
    case 3: return "no connection";
    case 8: return "invalid parameter";
    case 10: return "busy";
    case 16: return "timeout";
    case 25: return "limit exceeded";
    case 37: return "connect failed";
    case 41: return "ignored";
    default: return "?";
    }
}

static void SendWire(const SteamNetworkingIdentity* to, uint32_t toIp, uint16_t srcPort, uint16_t dstPort, const char* data,
                     int size)
{
    char buf[2048];
    if (size < 0 || size > (int)(sizeof(buf) - sizeof(Wire))) {
        InterlockedIncrement(&s_tooBig);
        if (FirstTime(Key(DIAG_TOO_BIG, 0, dstPort)))
            Log("online: datagram to port %u too big for the tunnel (%d bytes, at most %u), dropped", dstPort, size,
                (unsigned)(sizeof(buf) - sizeof(Wire)));
        return;
    }
    Wire w = { kMagic, s_myIp, srcPort, dstPort };
    memcpy(buf, &w, sizeof(w));
    if (size) memcpy(buf + sizeof(w), data, size);
    EnterCriticalSection(&s_steamLock);
    int r = S.SendToUser(s_msg, to, buf, (uint32_t)(sizeof(w) + size), kSendFlags, 0);
    LeaveCriticalSection(&s_steamLock);
    if (r != 1) {
        InterlockedIncrement(&s_sendFailed);
        InterlockedExchange(&s_lastSendResult, r);
        if (FirstTime(Key(DIAG_SEND_FAILED, toIp, (uint16_t)r))) {
            char t[20];
            IpText(toIp, t, sizeof(t));
            Log("online: Steam did not take a datagram to %s:%u: result %d (%s)", t, dstPort, r, SteamResultText(r));
        }
    }
}

// This player hosts the game's LAN session: it answered a session discovery broadcast, which the host
// does with a datagram straight to the searcher's discovery port (players looking for a game only
// broadcast there). Cleared when the LAN mode ends (OnlineFrame).
static volatile bool s_hosting;
static const uint16_t kDiscoveryPort = 9100;
// The host of the LAN game this player joined: a guest connects to the host first, so the first player it
// sends a datagram to outside the discovery port is the host (several games may run in one lobby).
static volatile uint32_t s_sessionHost;
// When Quazal bound a game port (besides the discovery port): a player that created a LAN game does so and
// then waits, a guest connects to its host in the same moment. A game port open for 1.5 s without a
// connection to a host means this player created the game (SyncRules).
static volatile DWORD s_gamePortSince;

// ---------------------------------------------------------------- DLC tracks: who may play with whom
//
// The game sends the chosen track as its index only (a byte of the game setup object, dlcfs.cpp's DlcOnlineSignature
// has the details): with [DLC] TracksOnline=1 the indices 15 and up are DLC tracks, which a player without the same
// pack does not have or has as another track. The host chooses the next track inside a running session (also at
// random), so the players of one LAN game must all have the same DLC tracks, or none. Every member publishes its
// signature (lobby member data glimpse_dlc, "<sig>|<names>", published again after every join); between two players
// whose signatures differ (for a player with DLC tracks also: whose signature is not known yet; one without treats an
// unknown player as before) only broadcasts pass: the game's session searches go out, but the host's answer, which
// lists its game, and every datagram to a game port are held back, on the sending and on the receiving side. So
// neither sees the other's LAN game in the game's list, and nobody ends up in a race whose track one of them lacks.
// Players without DLC tracks all have "none" and play together as before; so do players with the same pack and fix
// version. The cars travel by name (cATVNetDSVehicleData, dlcfs.cpp): with UnlockAll=1 the game lists the car pack's
// cars on the online car select itself, so then the signature covers the car pack too. A member that leaves and comes
// back keeps its last signature (s_dlcSeen) until its new member data arrives. Nothing of the game is changed for it.

static char s_myDlc[24];           // this player's signature, set before the first lobby or later (online thread); "" = not yet
static char s_myDlcNames[96];
static volatile LONG s_dlcHeld;    // datagrams held back (10 s counter)
static volatile LONG s_dlcHidden;  // this player searched for games while a player with other DLC tracks is in the lobby

// this player has no DLC tracks online: it behaves as before towards a player whose tracks are not known (yet) or
// that is not a member yet; a player with DLC tracks holds such traffic back itself, in both directions
static bool MyDlcNone() { return !strcmp(s_myDlc, "none"); }

enum DlcMatch { DLC_SAME, DLC_OTHER, DLC_UNKNOWN };
static DlcMatch PeerDlc(const Peer& p)
{
    if (!s_myDlc[0]) return DLC_UNKNOWN;
    if (!p.dlcKnown) return MyDlcNone() ? DLC_SAME : DLC_UNKNOWN;
    return strcmp(p.dlc, s_myDlc) ? DLC_OTHER : DLC_SAME;
}

// a datagram between this player and the one at ip that is held back: logged the first time per player and way
// (send: this player's, receive: the other's)
static void DlcHeld(uint32_t ip, const Peer* p, bool send, uint16_t port, int size)
{
    InterlockedIncrement(&s_dlcHeld);
    bool answer = port == kDiscoveryPort;
    if (!FirstTime(Key(DIAG_DLC, ip, (uint16_t)((send ? 1 : 0) | (answer ? 2 : 0))))) return;
    char t[20];
    IpText(ip, t, sizeof(t));
    char why[200];
    if (!p) _snprintf_s(why, _TRUNCATE, "%s is not a lobby member yet", t);
    else if (!p->dlcKnown) _snprintf_s(why, _TRUNCATE, "%s has not reported its DLC tracks yet", t);
    else _snprintf_s(why, _TRUNCATE, "DLC tracks differ: %s has %s (%s), this player %s (%s)", t, p->dlcNames, p->dlc,
                     s_myDlcNames, s_myDlc);
    if (answer && send) Log("online: DLC check: this player's LAN game is not shown to %s (answer to its search held back, %d bytes): %s", t, size, why);
    else if (answer) Log("online: DLC check: the LAN game of %s is not shown here (its answer held back, %d bytes): %s", t, size, why);
    else Log("online: DLC check: game traffic %s %s port %u held back (%d bytes): %s", send ? "to" : "from", t, port, size, why);
}

// receiving side (Pump): a datagram from ip may be delivered; a search (a short datagram to the discovery port) always
static bool DlcLetIn(uint32_t ip, uint16_t dstPort, int size)
{
    if (dstPort == kDiscoveryPort && size < 100) return true;
    EnterCriticalSection(&s_lock);
    const Peer* found = NULL;
    for (const Peer& p : s_peers)
        if (p.ip == ip) found = &p;
    bool ok = found ? PeerDlc(*found) == DLC_SAME : MyDlcNone();
    if (ok) {
        LeaveCriticalSection(&s_lock);
        return true;
    }
    Peer copy = {};
    if (found) copy = *found;
    LeaveCriticalSection(&s_lock);
    DlcHeld(ip, found ? &copy : NULL, false, dstPort, size);
    return false;
}

static void SendDatagram(uint16_t srcPort, const Endpoint* to, const char* data, int size)
{
    bool broadcast = to->ip == 0xFFFFFFFF || (to->ip & 0xFF) == 0xFF;
    // the host's answer to a search carries the game's description (320 bytes seen), a search itself is short
    // (35 bytes); a player that joined a game is not a host
    if (!broadcast && to->port == kDiscoveryPort && size >= 100 && to->ip != s_myIp && (to->ip >> 24) != 127 &&
        !s_hosting && !s_sessionHost) {
        s_hosting = true;
        Log("online: this player answered a session search: it hosts the LAN game");
    }
    if (broadcast) InterlockedIncrement(&s_sentBroadcast);
    if (broadcast || to->ip == s_myIp || (to->ip >> 24) == 127) {
        if (!broadcast) InterlockedIncrement(&s_sentLocal);
        Deliver(s_myIp ? s_myIp : 0x7F000001, srcPort, to->port, data, size);  // a broadcast reaches us too
    }
    if (!s_myIp) return;
    EnterCriticalSection(&s_lock);
    std::vector<std::pair<SteamNetworkingIdentity, uint32_t>> targets;
    bool held = false, otherDlc = false;
    Peer heldPeer = {};
    for (const Peer& p : s_peers)
        if (broadcast || p.ip == to->ip) {
            DlcMatch m = PeerDlc(p);
            otherDlc |= m == DLC_OTHER;
            if (!broadcast && m != DLC_SAME) {  // other or unknown DLC tracks: only broadcasts pass
                held = true;
                heldPeer = p;
                continue;
            }
            targets.push_back(std::make_pair(p.identity, p.ip));
        }
    LeaveCriticalSection(&s_lock);
    // this player looks for LAN games while a player with other DLC tracks is in the lobby: the banner says why
    // that one's game is not listed (online thread; the host's held answers are only logged)
    if (broadcast && to->port == kDiscoveryPort && otherDlc) InterlockedExchange(&s_dlcHidden, 1);
    if (held) {
        DlcHeld(to->ip, &heldPeer, true, to->port, size);
        return;
    }
    // game traffic that really goes out to another player: this player joined that one's game (after the DLC check:
    // a datagram held back above reached nobody, and its target's rules must not become the session's)
    if (!broadcast && to->port != kDiscoveryPort && !s_hosting && !s_sessionHost && to->ip != s_myIp &&
        (to->ip >> 24) != 127) {
        s_sessionHost = to->ip;
        char h[20];
        IpText(to->ip, h, sizeof(h));
        Log("online: this player joined the LAN game of %s", h);
    }
    char t[20];
    IpText(to->ip, t, sizeof(t));
    if (targets.empty() && !broadcast && to->ip != s_myIp && (to->ip >> 24) != 127) {
        InterlockedIncrement(&s_dropped);
        if (FirstTime(Key(DIAG_NO_SOCKET, to->ip, 0)))
            Log("online: no lobby member has the address %s (datagram to port %u dropped)", t, to->port);
        return;
    }
    if (!broadcast && !targets.empty() && FirstTime(Key(DIAG_FIRST_OUT, to->ip, to->port)))
        Log("online: first datagram to %s:%u from port %u (%d bytes)", t, to->port, srcPort, size);
    if (broadcast && FirstTime(Key(DIAG_FIRST_OUT, 0xFFFFFFFF, to->port)))
        Log("online: first broadcast to port %u from port %u (%d bytes, %u players)", to->port, srcPort, size,
            (unsigned)targets.size());
    for (const auto& target : targets) {
        SendWire(&target.first, target.second, srcPort, to->port, data, size);
        InterlockedIncrement(&s_sentDirect);
    }
}

// ---------------------------------------------------------------- the driver

QSocket* QDriver::CreateSocket()
{
    QSocket* s = new QSocket();
    EnterCriticalSection(&s_lock);
    s_sockets.push_back(s);
    size_t n = s_sockets.size();
    LeaveCriticalSection(&s_lock);
    Log("online: Quazal made a socket through the driver (%u open)", (unsigned)n);
    return s;
}

void QDriver::DeleteSocket(QSocket* s)
{
    if (s) s->Destroy(1);
}

bool QDriver::Select(SelectEntry* entries, unsigned count, unsigned timeoutMs)
{
    InterlockedIncrement(&s_selects);
    DWORD start = GetTickCount();
    for (;;) {
        bool any = false;
        EnterCriticalSection(&s_lock);
        for (unsigned i = 0; i < count; i++) {
            SelectEntry& e = entries[i];
            e.ready = 0;
            if (!e.socket) {  // made by Winsock before the driver was there: never served
                if (FirstTime(Key(DIAG_WINSOCK_SOCKET, 0, 0)))
                    Log("online: Quazal waits on a socket made before the driver, it will not get anything");
                continue;
            }
            if ((e.want & 1) && !e.socket->queue.empty()) e.ready |= 1;
            if (e.want & 2) e.ready |= 2;  // sending never waits
            any |= e.ready != 0;
        }
        LeaveCriticalSection(&s_lock);
        if (any) return true;
        DWORD spent = GetTickCount() - start;
        if (spent >= timeoutMs) return false;
        DWORD left = timeoutMs - spent;
        WaitForSingleObject(s_arrived, left < 5 ? left : 5);  // several threads may wait: poll as well
    }
}

bool QSocket::Open(int type)
{
    if (type != 0) {
        Log("online: Quazal asked for a TCP socket, not carried over Steam");
        return false;
    }
    udp = true;
    return true;
}

void QSocket::Close()
{
    EnterCriticalSection(&s_lock);
    uint16_t was = port;
    size_t unread = queue.size();
    port = 0;
    queue.clear();
    LeaveCriticalSection(&s_lock);
    if (was) Log("online: socket on port %u closed (%u datagrams unread)", was, (unsigned)unread);
}

bool QSocket::Bind(unsigned wanted)
{
    uint16_t p = (uint16_t)wanted;
    EnterCriticalSection(&s_lock);
    if (!p) {
        while (SocketOnPort(s_nextPort)) s_nextPort = s_nextPort >= 60000 ? 50000 : s_nextPort + 1;
        p = s_nextPort++;
    }
    bool ok = !SocketOnPort(p);
    if (ok) port = p;
    if (ok && wanted && p != kDiscoveryPort && !s_gamePortSince) s_gamePortSince = GetTickCount() | 1;
    LeaveCriticalSection(&s_lock);
    if (wanted) Log(ok ? "online: socket bound to port %u" : "online: port %u already in use", p);
    else Log("online: socket bound to free port %u", p);
    return ok;
}

int QSocket::RecvFrom(char* buf, int len, Endpoint* from, int* got)
{
    EnterCriticalSection(&s_lock);
    if (queue.empty()) {
        LeaveCriticalSection(&s_lock);
        return Q_WOULDBLOCK;
    }
    Packet p = std::move(queue.front());
    queue.pop_front();
    LeaveCriticalSection(&s_lock);
    int n = (int)p.data.size() < len ? (int)p.data.size() : len;
    if (n > 0) memcpy(buf, p.data.data(), n);
    if (from) *from = p.from;
    if (got) *got = n;
    InterlockedIncrement(&s_received);
    if (n < (int)p.data.size()) {
        InterlockedIncrement(&s_truncated);
        if (FirstTime(Key(DIAG_TRUNCATED, 0, port)))
            Log("online: datagram on port %u cut to Quazal's buffer (%u of %d bytes)", port, (unsigned)p.data.size(), len);
    }
    return Q_OK;
}

int QSocket::SendTo(const char* buf, int len, const Endpoint* to, int* sent)
{
    if (!to || len < 0) return Q_ERROR;
    if (!port) Bind(0);  // like an unbound UDP socket, which gets a port on its first send
    SendDatagram(port, to, buf, len);
    if (sent) *sent = len;
    return Q_OK;
}

static int Unsupported(const char* what)
{
    if (FirstTime(Key(DIAG_UNSUPPORTED, 0, what[0] | (what[1] << 8))))
        Log("online: Quazal called %s on a socket, which the Steam driver does not carry (TCP)", what);
    return Q_ERROR;
}
int QSocket::Connect(const Endpoint*) { return Unsupported("Connect"); }
int QSocket::Recv(char*, int, int*) { return Unsupported("Recv"); }
int QSocket::Send(const char*, int, int*) { return Unsupported("Send"); }
bool QSocket::SetOption(int) { return true; }

QSocket* QSocket::Destroy(unsigned flags)
{
    EnterCriticalSection(&s_lock);
    for (size_t i = 0; i < s_sockets.size(); i++)
        if (s_sockets[i] == this) {
            s_sockets.erase(s_sockets.begin() + i);
            break;
        }
    bool none = s_sockets.empty();
    LeaveCriticalSection(&s_lock);
    // the last socket gone: this player left the LAN game, so it neither hosts nor is a guest any more
    // (otherwise a guest that hosts its own game right after would keep its old host)
    if (none) s_gamePortSince = 0;
    if (none && (s_hosting || s_sessionHost)) {
        s_hosting = false;
        s_sessionHost = 0;
        Log("online: Quazal closed its last socket, the LAN game role ends");
    }
    if (flags & 1) {
        delete this;
        return NULL;
    }
    return this;
}

// ---------------------------------------------------------------- own address

// INTERFACE_INFO: flags, then address, broadcast address and net mask (sockaddr_gen, 24 bytes each)
#pragma pack(push, 1)
struct InterfaceInfo {
    uint32_t flags;
    struct { uint16_t family, port; uint32_t addr; BYTE pad[16]; } address, broadcast, mask;
};
#pragma pack(pop)
static_assert(sizeof(InterfaceInfo) == 76, "INTERFACE_INFO is 76 bytes");

typedef int(WINAPI* WSAIoctl_t)(UINT_PTR, DWORD, void*, DWORD, void*, DWORD, DWORD*, void*, void*);
static WSAIoctl_t s_origIoctl;

static int WINAPI Hook_WSAIoctl(UINT_PTR s, DWORD code, void* in, DWORD inLen, void* out, DWORD outLen, DWORD* returned,
                                void* overlapped, void* completion)
{
    const DWORD kGetInterfaceList = 0x4004747F;
    if (code == kGetInterfaceList && s_myIp && out && outLen >= sizeof(InterfaceInfo) && !overlapped) {
        InterfaceInfo info = {};
        info.flags = 1 | 2;  // up, broadcast
        info.address.family = info.broadcast.family = info.mask.family = 2;
        info.address.addr = _byteswap_ulong(s_myIp);
        info.broadcast.addr = 0xFFFFFFFF;
        info.mask.addr = _byteswap_ulong(0xFF000000);
        memcpy(out, &info, sizeof(info));
        if (returned) *returned = sizeof(info);
        static bool logged;
        if (!logged) {
            char t[20];
            IpText(s_myIp, t, sizeof(t));
            Log("online: interface list answered with %s", t);
            logged = true;
        }
        return 0;
    }
    return s_origIoctl(s, code, in, inLen, out, outLen, returned, overlapped, completion);
}

// ---------------------------------------------------------------- lobby

static void LobbyKeys(char* name, size_t n)
{
    const char* src = g_cfg.lobbyName[0] ? g_cfg.lobbyName : kDefaultLobby;
    size_t i = 0;
    for (; src[i] && i + 1 < n; i++) name[i] = src[i] >= 'A' && src[i] <= 'Z' ? (char)(src[i] + 32) : src[i];
    name[i] = 0;
}

// FriendsOnly: the lobbies Steam friends of this player are in while they play Split/Second; the oldest one,
// so friends who start at the same time end up together (it is checked to be a glimpse_fix lobby on joining)
#pragma pack(push, 8)
struct FriendGameInfo_t { uint64_t game; uint32_t ip; uint16_t gamePort, queryPort; uint64_t lobby; };
#pragma pack(pop)
// lobbies that are gone, full or of another version: not tried again for 5 minutes (a join that only timed
// out is tried again)
static std::vector<std::pair<uint64_t, DWORD>> s_badLobbies;
static void NoteBadLobby(uint64_t id)
{
    for (auto& b : s_badLobbies)
        if (b.first == id) {
            b.second = GetTickCount();
            return;
        }
    if (s_badLobbies.size() < 64) s_badLobbies.push_back(std::make_pair(id, GetTickCount()));
}
static bool BadLobby(uint64_t id)
{
    for (size_t i = 0; i < s_badLobbies.size(); i++)
        if (s_badLobbies[i].first == id) {
            if (GetTickCount() - s_badLobbies[i].second < 300000) return true;
            s_badLobbies.erase(s_badLobbies.begin() + i);
            return false;
        }
    return false;
}

static uint64_t FindFriendLobby()
{
    void* friends = S.Friends();
    const int kImmediate = 4;  // k_EFriendFlagImmediate: real friends
    int n = friends ? S.FriendCount(friends, kImmediate) : 0;
    uint64_t best = 0;
    for (int i = 0; i < n; i++) {
        uint64_t id = S.FriendByIndex(friends, i, kImmediate);
        FriendGameInfo_t g = {};
        if (!id || !S.FriendGamePlayed(friends, id, &g)) continue;
        if ((g.game & 0xFFFFFF) != 297860 || !g.lobby || g.lobby == s_lobby || BadLobby(g.lobby)) continue;
        if (!best || g.lobby < best) best = g.lobby;
    }
    return best;
}

static uint64_t FindLobby()
{
    if (g_cfg.friendsOnly) return FindFriendLobby();
    char name[64];
    LobbyKeys(name, sizeof(name));
    S.StringFilter(s_mm, "glimpse_lan", kLobbyVersion, 0);
    S.StringFilter(s_mm, "glimpse_name", name, 0);
    S.SlotsFilter(s_mm, 1);     // a full lobby (8) is left out, the next player opens another
    S.DistanceFilter(s_mm, 3);  // worldwide
    LobbyMatchList_t list = {};
    if (!Wait(S.RequestLobbyList(s_mm), &list, sizeof(list), 510)) return 0;
    uint64_t best = 0;
    for (uint32_t i = 0; i < list.count; i++) {
        uint64_t id = S.LobbyByIndex(s_mm, (int)i);
        if (id && id != s_lobby && !BadLobby(id) && (!best || id < best)) best = id;  // the oldest, everybody picks the same
    }
    return best;
}

static bool Join(uint64_t lobby)
{
    LobbyEnter_t entered = {};
    if (!Wait(S.JoinLobby(s_mm, lobby), &entered, sizeof(entered), 504) || entered.response != 1) {
        Log("online: could not join lobby %llu (response %u)", (unsigned long long)lobby, entered.response);
        if (entered.response == 2 || entered.response == 4) NoteBadLobby(lobby);  // does not exist, full
        return false;
    }
    s_lobby = lobby;
    InterlockedIncrement(&s_lobbySerial);  // SyncRules publishes the member data again
    Log("online: joined lobby %llu", (unsigned long long)lobby);
    return true;
}

static bool Create()
{
    LobbyCreated_t created = {};
    // public: found by its name; friends only: not listed, reached through friends (FindFriendLobby, "Join game")
    int type = g_cfg.friendsOnly ? 1 : 2;
    if (!Wait(S.CreateLobby(s_mm, type, 8), &created, sizeof(created), 513) || created.result != 1) {
        Log("online: could not create a lobby (result %d)", created.result);
        return false;
    }
    s_lobby = created.lobby;
    InterlockedIncrement(&s_lobbySerial);
    char name[64];
    LobbyKeys(name, sizeof(name));
    S.SetLobbyData(s_mm, s_lobby, "glimpse_lan", kLobbyVersion);
    S.SetLobbyData(s_mm, s_lobby, "glimpse_name", name);
    S.SetJoinable(s_mm, s_lobby, true);  // Steam friends see "Join game"
    if (g_cfg.friendsOnly) S.SetLobbyData(s_mm, s_lobby, "glimpse_name", "friends");
    Log("online: created %s lobby %llu ('%s'), waiting for players", g_cfg.friendsOnly ? "a friends only" : "a public",
        (unsigned long long)s_lobby, g_cfg.friendsOnly ? "friends" : name);
    return true;
}

// ---------------------------------------------------------------- session rules for the extras
//
// In a LAN session the extras that change the race (boost, handbrake, power bar fill, AI boost, wreck time)
// follow the rules of the player who hosts the game's LAN session, from the host's own INI: everybody
// has the same, what the host does not allow is off for all. Every member publishes its own rules and
// whether it hosts; until a host is known, the Steam lobby owner's rules stand in. The photo mode is a
// free camera in a session and does not hold the world still (camera.cpp), since the others race on.
// Outside a session the own INI applies.

// laps: Race's laps (the host's LapSelect pick, 0 = the game's). The game works the count out on every PC itself
// (cLaps::Reset, 0x43FFA5: the track's own count, or the lap override) and sends none, so all must use the same;
// lap_menu.cpp sets it for each car's reset. The game caps a network race at 8 (0x43FFCB). Last in the text: a
// version without it reads the 14 fields before and ignores it (and then reports other settings, see SyncRules).
struct Rules {
    bool boost = false, powerFill = false, aiBoost = false, aiUnlimited = false, aiStraight = true, aiChaos = false;
    bool handbrake = false;
    int aiLevel = 2, handbrakeMode = 1, laps = 0;
    float boostAccel = 15, boostMax = 90, wreckTime = 0, handbrakeStrength = 15, handbrakeDrift = 2.5f;
};

static const int kNetworkMaxLaps = 8;  // the game's cap for a network race (0x43FFD5)

static void Encode(const Rules& r, char* out, size_t n)
{
    _snprintf_s(out, n, _TRUNCATE, "b=%d;ba=%g;bm=%g;p=%d;a=%d;al=%d;au=%d;as=%d;ac=%d;w=%g;h=%d;hm=%d;hs=%g;hd=%g;l=%d", r.boost,
                r.boostAccel, r.boostMax, r.powerFill, r.aiBoost, r.aiLevel, r.aiUnlimited, r.aiStraight, r.aiChaos, r.wreckTime,
                r.handbrake, r.handbrakeMode, r.handbrakeStrength, r.handbrakeDrift, r.laps);
}

static bool Decode(const char* s, Rules* r)
{
    if (!s || !*s) return false;
    Rules x;
    int b, p, a, au, as, ac, h;
    // 14 fields from a version before the laps (they stay the game's), 15 with them
    const int got = sscanf_s(s, "b=%d;ba=%g;bm=%g;p=%d;a=%d;al=%d;au=%d;as=%d;ac=%d;w=%g;h=%d;hm=%d;hs=%g;hd=%g;l=%d", &b,
                             &x.boostAccel, &x.boostMax, &p, &a, &x.aiLevel, &au, &as, &ac, &x.wreckTime, &h, &x.handbrakeMode,
                             &x.handbrakeStrength, &x.handbrakeDrift, &x.laps);
    if (got != 14 && got != 15) return false;
    if (got == 14) x.laps = 0;
    if (x.laps < 0 || x.laps > 16) x.laps = 0;
    x.handbrake = h != 0;
    if (x.handbrakeMode != 2) x.handbrakeMode = 1;
    x.handbrakeStrength = x.handbrakeStrength < 0 ? 0 : x.handbrakeStrength > 40 ? 40 : x.handbrakeStrength;
    x.handbrakeDrift = x.handbrakeDrift < 0 ? 0 : x.handbrakeDrift > 5 ? 5 : x.handbrakeDrift;
    x.boost = b != 0;
    x.powerFill = p != 0;
    x.aiBoost = a != 0;
    x.aiUnlimited = au != 0;
    x.aiStraight = as != 0;
    x.aiChaos = ac != 0;
    if (x.aiLevel < 1 || x.aiLevel > 3) x.aiLevel = 2;
    *r = x;
    return true;
}

static void Describe(const Rules& r, char* out, size_t n)
{
    char ai[48] = "off";
    if (r.aiBoost) {
        static const char* const levels[] = { "easy", "normal", "hard" };
        if (r.aiChaos) strcpy_s(ai, "CHAOS");
        else _snprintf_s(ai, _TRUNCATE, "%s%s", levels[r.aiLevel - 1], r.aiUnlimited ? ", unlimited" : "");
    }
    char wreck[24] = "as the game";
    if (r.wreckTime > 0) _snprintf_s(wreck, _TRUNCATE, "%.0f s", r.wreckTime);
    char laps[24] = "as the game";
    if (r.laps > 0) _snprintf_s(laps, _TRUNCATE, "%d", r.laps > kNetworkMaxLaps ? kNetworkMaxLaps : r.laps);
    _snprintf_s(out, n, _TRUNCATE, "Boost %s, handbrake %s, power bar fill %s, AI boost %s, wreck time %s, Race laps %s",
                r.boost ? "ON" : "off", r.handbrake ? (r.handbrakeMode == 2 ? "ON (brake)" : "ON") : "off",
                r.powerFill ? "ON" : "off", ai, wreck, laps);
}

static Rules OwnRules()
{
    Rules r;
    r.boost = g_cfg.boost;
    r.boostAccel = g_cfg.boostAcceleration;
    r.boostMax = g_cfg.boostMaxSpeed;
    r.powerFill = g_cfg.powerFill;
    r.aiBoost = g_cfg.aiBoost;
    r.aiLevel = g_cfg.aiBoostLevel;
    r.aiUnlimited = g_cfg.aiBoostUnlimited;
    r.aiStraight = g_cfg.aiBoostStraightOnly;
    r.aiChaos = g_cfg.aiBoostChaos;
    r.wreckTime = g_cfg.wreckTime;
    r.handbrake = g_cfg.handbrake;
    r.handbrakeMode = g_cfg.handbrakeMode;
    r.handbrakeStrength = g_cfg.handbrakeStrength;
    r.handbrakeDrift = g_cfg.handbrakeDrift;
    r.laps = HostLaps();
    return r;
}

static Rules s_own;                 // from this player's INI, taken at start
static volatile bool s_lapsHeld;    // a member cannot take the lap pick (glimpse_lapok): the game's laps for all
static bool s_ownPhotoMode;
static WORD s_ownHandbrakeButton;    // from the INI; ApplyRules clears it when it clashes with the boost
static Rules s_lobbyRules;          // the session's: the host's, else the lobby owner's (online thread)
static bool s_lobbyRulesKnown;
static char s_rulesFrom[48];        // whose they are, for the log and the banner
static char s_lobbyName[64];        // shown name of the lobby we are in

static void ApplyRules(const Rules& r, bool photoMode)
{
    g_cfg.boost = r.boost;
    g_cfg.boostAcceleration = r.boostAccel;
    g_cfg.boostMaxSpeed = r.boostMax;
    g_cfg.powerFill = r.powerFill;
    g_cfg.aiBoost = r.aiBoost;
    g_cfg.aiBoostLevel = r.aiLevel;
    g_cfg.aiBoostUnlimited = r.aiUnlimited;
    g_cfg.aiBoostStraightOnly = r.aiStraight;
    g_cfg.aiBoostChaos = r.aiChaos;
    g_cfg.wreckTime = r.wreckTime;
    g_cfg.handbrake = r.handbrake;
    g_cfg.handbrakeMode = r.handbrakeMode;
    g_cfg.handbrakeStrength = r.handbrakeStrength;
    g_cfg.handbrakeDrift = r.handbrakeDrift;
    g_cfg.sessionLaps = r.laps;
    // this player's own buttons: when the host allows both and they are the same, the handbrake gets none
    // (as at the start, main.cpp)
    g_cfg.handbrakeButton = r.boost && r.handbrake && s_ownHandbrakeButton == g_cfg.boostButton ? 0 : s_ownHandbrakeButton;
    g_cfg.photoMode = photoMode;
}

static void RefreshMembers()
{
    std::vector<Peer> now;
    int n = S.NumMembers(s_mm, s_lobby);
    for (int i = 0; i < n; i++) {
        uint64_t id = S.MemberByIndex(s_mm, s_lobby, i);
        if (!id || id == s_mySteamID) continue;
        Peer p = {};
        p.steamID = id;
        p.ip = VirtualIp(id);
        p.state = -1;
        S.IdentityClear(&p.identity);
        S.IdentitySetSteamID(&p.identity, id);
        now.push_back(p);
    }
    EnterCriticalSection(&s_lock);
    std::vector<Peer> before = s_peers;
    std::vector<uint64_t> fromCache;  // members back in the lobby, taken by their last signature
    for (Peer& p : now) {
        bool was = false;
        for (const Peer& b : before)
            if (b.steamID == p.steamID) {
                was = true;
                p.state = b.state;
                p.relayed = b.relayed;
                p.dlcKnown = b.dlcKnown;
                p.dlcCached = b.dlcCached;
                memcpy(p.dlc, b.dlc, sizeof(p.dlc));
                memcpy(p.dlcNames, b.dlcNames, sizeof(p.dlcNames));
            }
        if (was) continue;
        for (const SeenDlc& s : s_dlcSeen)
            if (s.steamID == p.steamID) {
                p.dlcKnown = p.dlcCached = true;
                memcpy(p.dlc, s.dlc, sizeof(p.dlc));
                memcpy(p.dlcNames, s.dlcNames, sizeof(p.dlcNames));
                fromCache.push_back(p.steamID);
            }
    }
    s_peers = now;
    LeaveCriticalSection(&s_lock);
    for (uint64_t id : fromCache) {
        char t[20];
        IpText(VirtualIp(id), t, sizeof(t));
        for (const Peer& p : now)
            if (p.steamID == id)
                Log("online: DLC check: player %s is back in the lobby, taken by its last DLC signature (%s, %s) until "
                    "its member data comes", t, p.dlcNames, p.dlc);
    }
    for (size_t i = 0; i < now.size(); i++)
        for (size_t j = i + 1; j < now.size(); j++)
            if (now[i].ip == now[j].ip && FirstTime(Key(DIAG_IP_CLASH, now[i].ip, 0))) {
                char t[20];
                IpText(now[i].ip, t, sizeof(t));
                Log("online: two players got the same address %s, one of them cannot be reached", t);
            }
    for (const Peer& p : now)
        if (p.ip == s_myIp && FirstTime(Key(DIAG_IP_CLASH, p.ip, 1))) Log("online: a player got this player's own address");
    for (const Peer& p : now) {
        bool known = false;
        for (const Peer& b : before) known |= b.steamID == p.steamID;
        if (known) continue;
        char t[20];
        IpText(p.ip, t, sizeof(t));
        Log("online: player %s joined the lobby (%d in it)", t, n);
        EnterCriticalSection(&s_steamLock);
        S.AcceptSession(s_msg, &p.identity);
        LeaveCriticalSection(&s_steamLock);
        SendWire(&p.identity, p.ip, 0, 0, NULL, 0);  // hello: our first message opens the session both ways
    }
    for (const Peer& b : before) {
        bool still = false;
        for (const Peer& p : now) still |= b.steamID == p.steamID;
        if (still) continue;
        char t[20];
        IpText(b.ip, t, sizeof(t));
        Log("online: player %s left the lobby", t);
    }
}

static void Pump()
{
    SteamNetworkingMessage_t* msgs[32];
    for (;;) {
        EnterCriticalSection(&s_steamLock);
        int n = S.ReceiveOnChannel(s_msg, 0, msgs, 32);
        LeaveCriticalSection(&s_steamLock);
        if (n <= 0) return;
        for (int i = 0; i < n; i++) {
            const SteamNetworkingMessage_t* m = msgs[i];
            Wire w = {};
            if (m->size >= (int)sizeof(Wire)) memcpy(&w, m->data, sizeof(w));
            if (w.magic != kMagic) {
                InterlockedIncrement(&s_badWire);
                if (FirstTime(Key(DIAG_BAD_WIRE, 0, 0)))
                    Log("online: message from Steam that is not ours (%d bytes): other version of the fix?", m->size);
            } else if (!w.dstPort) {
                char t[20];
                IpText(w.srcIp, t, sizeof(t));
                if (FirstTime(Key(DIAG_FIRST_IN, w.srcIp, 0))) Log("online: hello from %s, the Steam session is open", t);
            } else if (DlcLetIn(w.srcIp, w.dstPort, m->size - (int)sizeof(w))) {
                Deliver(w.srcIp, w.srcPort, w.dstPort, (const char*)m->data + sizeof(w), m->size - (int)sizeof(w));
            }
            S.ReleaseMessage(msgs[i]);
        }
    }
}

// what kind of address Steam talks to, without the address itself (logs end up in public issues)
static const char* AddressKind(const BYTE* addr18)
{
    static const BYTE v4mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
    if (memcmp(addr18, v4mapped, 12)) {
        static const BYTE zero[16] = {};
        return memcmp(addr18, zero, 16) ? "IPv6" : "no address";
    }
    BYTE a = addr18[12], b = addr18[13];
    if (a == 10 || (a == 172 && (b & 0xF0) == 16) || (a == 192 && b == 168)) return "home network (private address)";
    if (a == 100 && (b & 0xC0) == 64) return "carrier NAT (shared address)";
    if (a == 169 && b == 254) return "link-local";
    if (a == 127) return "this computer";
    if (a == 0) return "no address";
    return "internet (public address)";
}

static const char* StateText(int s)
{
    switch (s) {
    case 0: return "no session";
    case 1: return "connecting";
    case 2: return "finding a route";
    case 3: return "connected";
    case 4: return "closed by the other player";
    case 5: return "lost (problem on this side)";
    default: return "?";
    }
}

// Steam's connection to each player: logged when it changes, with ping and route every 10 s (full)
static void PeerConnections(bool full)
{
    EnterCriticalSection(&s_lock);
    std::vector<Peer> peers = s_peers;
    LeaveCriticalSection(&s_lock);
    for (Peer& p : peers) {
        ConnectionInfo info;
        ConnectionStatus status;
        memset(&info, 0, sizeof(info));
        memset(&status, 0, sizeof(status));
        EnterCriticalSection(&s_steamLock);
        int state = S.SessionInfo(s_msg, &p.identity, &info, &status);
        LeaveCriticalSection(&s_steamLock);
        bool infoOk = info.state == state;  // the structure read as laid out above
        bool relayed = infoOk && info.popRelay != 0;
        char t[20];
        IpText(p.ip, t, sizeof(t));
        if (state != p.state || relayed != p.relayed) {
            if ((state == 4 || state == 5) && infoOk) {
                info.endDebug[sizeof(info.endDebug) - 1] = 0;
                Log("online: connection to %s %s, reason %d: %s", t, StateText(state), info.endReason, info.endDebug);
            } else if (state == 3) {
                Log("online: connection to %s connected, %s, other side: %s, ping %d ms", t,
                    infoOk ? (relayed ? "through a Steam relay" : "direct") : "route unknown",
                    infoOk ? AddressKind(info.addrRemote) : "?", status.ping);
            } else {
                Log("online: connection to %s: %s", t, StateText(state));
            }
            EnterCriticalSection(&s_lock);
            for (Peer& q : s_peers)
                if (q.steamID == p.steamID) {
                    q.state = state;
                    q.relayed = relayed;
                }
            LeaveCriticalSection(&s_lock);
        } else if (full && state == 3) {
            Log("online: %s: ping %d ms, quality %.0f%%/%.0f%%, out %.0f/s, in %.0f/s, %d bytes waiting to be sent", t,
                status.ping, status.qualityLocal * 100, status.qualityRemote * 100, status.outPackets, status.inPackets,
                status.pendingUnreliable + status.pendingReliable);
        }
    }
}

static QDriver* const* s_activeDriver;  // Quazal's driver in use, [0xD779FC]

// Log=1: what went through the driver in the last 10 seconds
static void LogTraffic()
{
    static bool wasActive;
    bool active = *s_activeDriver == &s_driver;
    if (active != wasActive) {
        Log(active ? "online: Quazal's network runs on the Steam driver" : "online: Quazal's network does not use the driver (yet)");
        wasActive = active;
    }
    LONG direct = InterlockedExchange(&s_sentDirect, 0), bcast = InterlockedExchange(&s_sentBroadcast, 0);
    LONG local = InterlockedExchange(&s_sentLocal, 0), recv = InterlockedExchange(&s_received, 0);
    LONG sel = InterlockedExchange(&s_selects, 0), drop = InterlockedExchange(&s_dropped, 0);
    if (direct || bcast || local || recv || drop) {
        EnterCriticalSection(&s_lock);
        size_t peers = s_peers.size(), sockets = s_sockets.size();
        LeaveCriticalSection(&s_lock);
        Log("online: 10 s: %ld to players, %ld broadcasts, %ld to itself, %ld received, %ld without a socket, "
            "%ld waits; %u sockets, %u players", direct, bcast, local, recv, drop, sel, (unsigned)sockets, (unsigned)peers);
    }
    LONG failed = InterlockedExchange(&s_sendFailed, 0), full = InterlockedExchange(&s_queueFull, 0);
    LONG cut = InterlockedExchange(&s_truncated, 0), bad = InterlockedExchange(&s_badWire, 0), big = InterlockedExchange(&s_tooBig, 0);
    if (failed || full || cut || bad || big)
        Log("online: 10 s PROBLEMS: %ld sends refused by Steam (last result %ld), %ld dropped from full queues, %ld cut short, "
            "%ld foreign messages, %ld too big", failed, s_lastSendResult, full, cut, bad, big);
    if (LONG held = InterlockedExchange(&s_dlcHeld, 0))
        Log("online: 10 s: %ld datagrams between this player and players with other (or not yet known) DLC tracks held back", held);
    PeerConnections(true);
}

// ---------------------------------------------------------------- crashes and the game's debug text

static LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter;

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* r = ep->ExceptionRecord;
    HMODULE mod = NULL;
    char name[MAX_PATH] = "?";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)r->ExceptionAddress, &mod);
    if (mod) GetModuleFileNameA(mod, name, sizeof(name));
    const char* file = strrchr(name, '\\');
    Log("CRASH: exception %08lX at %p (%s+0x%lX), thread %lu, access %s %p", r->ExceptionCode, r->ExceptionAddress,
        file ? file + 1 : name, (unsigned long)((BYTE*)r->ExceptionAddress - (BYTE*)mod), GetCurrentThreadId(),
        r->NumberParameters >= 2 ? (r->ExceptionInformation[0] ? "write" : "read") : "-",
        r->NumberParameters >= 2 ? (void*)r->ExceptionInformation[1] : NULL);
    const CONTEXT* c = ep->ContextRecord;
    Log("CRASH: eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX esp %08lX", c->Eax, c->Ebx, c->Ecx,
        c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    // the call chain, as far as a raw stack scan shows it: words that point into the exe's code section (return
    // addresses and some stale values), the first 24 found within 4 KB
    char line[512];
    int len = _snprintf_s(line, _TRUNCATE, "CRASH: stack code addresses:");
    int found = 0;
    __try {
        const DWORD* sp = (const DWORD*)c->Esp;
        for (int i = 0; i < 1024 && found < 24 && len > 0 && len < (int)sizeof(line) - 12; i++) {
            DWORD v = sp[i];
            if (v >= 0x00401000 && v < 0x00B9B000) {
                len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " %08lX", v);
                found++;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Log("%s", line);
    return s_prevFilter ? s_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

// OutputDebugStringA through the import slot: Quazal's log device (LogDeviceDebugOutput, 0xAF7700) and the
// game's own debug text, into the log; at most 100 lines per 10 seconds
typedef void(WINAPI* OutputDebugStringA_t)(LPCSTR);
static OutputDebugStringA_t s_origOds;
static void WINAPI Hook_OutputDebugStringA(LPCSTR text)
{
    s_origOds(text);
    if (!text) return;
    static DWORD windowStart;
    static LONG lines, skipped;
    DWORD now = GetTickCount();
    if (now - windowStart > 10000) {
        if (skipped) Log("debug text: %ld more lines left out", skipped);
        windowStart = now;
        lines = skipped = 0;
    }
    if (InterlockedIncrement(&lines) > 100) {
        InterlockedIncrement(&skipped);
        return;
    }
    char line[512];
    lstrcpynA(line, text, sizeof(line));
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (n) Log("debug text: %s", line);
}

void InstallDiagnostics()
{
    static bool done;  // online start and the DLC tracks may both ask
    if (done) return;
    done = true;
    s_prevFilter = SetUnhandledExceptionFilter(CrashFilter);
    void** slot = (void**)G(0x00B9B174);  // KERNEL32!OutputDebugStringA import
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (k32 && *slot == (void*)GetProcAddress(k32, "OutputDebugStringA")) {
        s_origOds = (OutputDebugStringA_t)*slot;
        void* hook = (void*)Hook_OutputDebugStringA;
        WriteMemory(slot, &hook, sizeof(hook));
        Log("online: crash handler set, the game's debug text goes to the log");
    } else {
        Log("online: crash handler set; OutputDebugStringA import not as expected, debug text not logged");
    }
}

// ---------------------------------------------------------------- what is shown (banner, Steam status)

static CRITICAL_SECTION s_showLock;
static char s_bannerLines[4][192];
static int s_bannerCount;
static DWORD s_bannerUntil;
static char s_appliedText[170];      // "S|" or "M|" (in a LAN session or not) and what applies (member data)
static bool s_inSession;
static char s_warning[160];          // a player with other rules or another version
static char s_presence[128];         // Steam status text
static unsigned s_presencePlayers;   // players in the lobby, for Steam's player group
static char s_mode[48];              // the LAN game's mode as the game's lobby screen names it (UTF-8), "" = none yet
static char s_lobbyMode[48];         // the mode of a game hosted in this Steam lobby, from its host's member data
static bool s_noLaa;                 // 2 GB address space only: Steam's libraries make running out likelier

static void ShowBanner(DWORD ms)
{
    EnterCriticalSection(&s_lock);
    size_t players = s_peers.size() + 1;
    LeaveCriticalSection(&s_lock);
    EnterCriticalSection(&s_showLock);
    Rules shown = s_inSession ? (s_lobbyRulesKnown ? s_lobbyRules : Rules()) : s_own;
    char rules[160];
    Describe(shown, rules, sizeof(rules));
    char mode[64] = "";
    if (s_inSession) _snprintf_s(mode, _TRUNCATE, ", mode: %s", s_mode[0] ? s_mode : "not chosen yet");
    else if (s_lobbyMode[0]) _snprintf_s(mode, _TRUNCATE, ", game open: %s", s_lobbyMode);
    char dlc[48] = "";  // with DLC tracks: LAN games only with players whose code (signature) is the same
    if (s_myDlc[0] && strcmp(s_myDlc, "none")) _snprintf_s(dlc, _TRUNCATE, ", DLC tracks %.8s", s_myDlc);
    _snprintf_s(s_bannerLines[0], _TRUNCATE, "glimpse_fix online  -  lobby \"%s\", %u/8 players%s%s", s_lobbyName,
                (unsigned)players, mode, dlc);
    char whose[80];
    _snprintf_s(whose, _TRUNCATE, "Rules from %s, for everybody", s_rulesFrom[0] ? s_rulesFrom : "the host");
    _snprintf_s(s_bannerLines[1], _TRUNCATE, "%s: %s", s_inSession ? whose : "Your settings (no LAN session)", rules);
    s_bannerCount = 2;
    if (s_inSession && s_ownPhotoMode)
        strcpy_s(s_bannerLines[s_bannerCount++], "Photo mode: free camera only, the race goes on");
    if (s_warning[0]) strcpy_s(s_bannerLines[s_bannerCount++], s_warning);
    else if (s_noLaa) strcpy_s(s_bannerLines[s_bannerCount++], "WARNING: SplitSecond.exe without LAA flag - run setlaa.exe, the game may run out of memory");
    s_bannerUntil = GetTickCount() + ms;
    const char* shownMode = s_inSession ? s_mode : s_lobbyMode;
    _snprintf_s(s_presence, _TRUNCATE, "Lobby %s (%u/8)%s%s", s_lobbyName, (unsigned)players, shownMode[0] ? " - " : "",
                shownMode);
    s_presencePlayers = (unsigned)players;
    LeaveCriticalSection(&s_showLock);
}

// the Steam status text, set on the online thread (Steam is called from there only, besides sends)
static void UpdatePresence()
{
    char text[128];
    EnterCriticalSection(&s_showLock);
    strcpy_s(text, s_presence);
    LeaveCriticalSection(&s_showLock);
    static char set[128];
    if (strcmp(text, set) && S.Friends) {
        S.SetRichPresence(S.Friends(), "status", text);
        strcpy_s(set, text);
    }
    // Steam shows friends with the same player group together in the friends list, with the group's size
    static uint64_t groupSet;
    static unsigned sizeSet;
    EnterCriticalSection(&s_showLock);
    unsigned players = s_presencePlayers;
    LeaveCriticalSection(&s_showLock);
    if (S.Friends && s_lobby && (groupSet != s_lobby || sizeSet != players) && players) {
        char group[24], size[8];
        _snprintf_s(group, _TRUNCATE, "%llu", (unsigned long long)s_lobby);
        _snprintf_s(size, _TRUNCATE, "%u", players);
        S.SetRichPresence(S.Friends(), "steam_player_group", group);
        S.SetRichPresence(S.Friends(), "steam_player_group_size", size);
        groupSet = s_lobby;
        sizeSet = players;
    }
}

// the other members' DLC tracks from their member data (online thread, every second): taken once per member, as
// they do not change while the game runs. A member of an older release publishes none and cannot have DLC tracks
// (they came with this version): known from its version alone. One of this version without the value yet waits.
static void NotePeersDlc(const std::vector<Peer>& peers)
{
    for (const Peer& p : peers) {
        if (p.dlcKnown && !p.dlcCached) continue;  // a cached one is read again until its own member data is there
        const char* v = S.GetMemberData(s_mm, s_lobby, p.steamID, "glimpse_dlc");
        const char* ver = S.GetMemberData(s_mm, s_lobby, p.steamID, "glimpse_ver");
        char sig[24] = "", names[96] = "";
        if (v && *v) {
            const char* bar = strchr(v, '|');
            size_t n = bar ? (size_t)(bar - v) : strlen(v);
            strncpy_s(sig, v, n < sizeof(sig) - 1 ? n : sizeof(sig) - 1);
            strncpy_s(names, bar ? bar + 1 : sig, _TRUNCATE);
        } else if (ver && *ver && strcmp(ver, GLIMPSE_VERSION)) {
            strcpy_s(sig, "none");
            strcpy_s(names, "none (older glimpse_fix)");
        } else {
            continue;
        }
        EnterCriticalSection(&s_lock);
        for (Peer& q : s_peers)
            if (q.steamID == p.steamID) {
                strcpy_s(q.dlc, sig);
                strcpy_s(q.dlcNames, names);
                q.dlcKnown = true;
                q.dlcCached = false;
            }
        bool seen = false;  // the cache for a later return of this member
        for (SeenDlc& s : s_dlcSeen)
            if (s.steamID == p.steamID) {
                seen = true;
                strcpy_s(s.dlc, sig);
                strcpy_s(s.dlcNames, names);
            }
        if (!seen && s_dlcSeen.size() < 256) {  // a bound for a long run with many lobbies (POD, no throw path)
            SeenDlc s = {};
            s.steamID = p.steamID;
            strcpy_s(s.dlc, sig);
            strcpy_s(s.dlcNames, names);
            try {
                s_dlcSeen.push_back(s);
            } catch (...) {
            }
        }
        LeaveCriticalSection(&s_lock);
        char t[20];
        IpText(p.ip, t, sizeof(t));
        if (p.dlcCached && !strcmp(sig, p.dlc)) {
            Log("online: DLC check: player %s confirmed its DLC signature (%s, %s)", t, names, sig);
            continue;
        }
        if (p.dlcCached)
            Log("online: DLC check: player %s now has another DLC signature than when it left (%s, was %s)", t, sig,
                p.dlc);
        if (!strcmp(sig, s_myDlc))
            Log("online: DLC check: player %s has the same DLC tracks/cars (%s, %s): LAN games together", t, names, sig);
        else
            Log("online: DLC check: player %s has other DLC tracks/cars: %s (%s), this player %s (%s): no LAN game "
                "together (the track travels as a number, the car as a name); same packs, UnlockAll and fix version, "
                "or no DLC online on both (TracksOnline=0 or no track pack, and UnlockAll=0 or no car pack)",
                t, names, sig, s_myDlcNames, s_myDlc);
    }
}

// every member publishes its own rules and whether it hosts the LAN session; the session's rules are the
// host's (the lobby owner's until a host is known); everybody reports its version and what it applies,
// and the others' reports are compared (online thread, every second)
static bool TryTakeOwnDlc();  // below

static void SyncRules()
{
    if (!s_lobby) return;
    char mine[160];
    Encode(s_own, mine, sizeof(mine));
    uint64_t owner = S.LobbyOwner(s_mm, s_lobby);
    const char* current = S.GetLobbyData(s_mm, s_lobby, "glimpse_rules");
    if (owner == s_mySteamID && (!current || strcmp(current, mine))) S.SetLobbyData(s_mm, s_lobby, "glimpse_rules", mine);

    EnterCriticalSection(&s_lock);
    std::vector<Peer> peers = s_peers;
    LeaveCriticalSection(&s_lock);
    NotePeersDlc(peers);
    EnterCriticalSection(&s_lock);
    peers = s_peers;  // with the DLC tracks just learned
    LeaveCriticalSection(&s_lock);
    EnterCriticalSection(&s_showLock);
    bool inSession = s_inSession;
    LeaveCriticalSection(&s_showLock);
    DWORD portSince = s_gamePortSince;
    if (inSession && portSince && !s_hosting && !s_sessionHost && GetTickCount() - portSince >= 1500) {
        s_hosting = true;
        Log("online: this player created the LAN game: it hosts");
    }
    bool hosting = s_hosting && inSession;

    // whose rules: this player's when it hosts, else those of the host of the game it joined, else the owner's
    Rules r;
    bool known = false;
    char from[48] = "the lobby owner";
    if (hosting) {
        r = s_own;
        known = true;
        strcpy_s(from, "this player, the host");
    }
    for (size_t i = 0; i < peers.size() && !known; i++) {
        const char* host = S.GetMemberData(s_mm, s_lobby, peers[i].steamID, "glimpse_host");
        const char* applied = S.GetMemberData(s_mm, s_lobby, peers[i].steamID, "glimpse_applied");
        if (peers[i].ip == s_sessionHost && host && !strcmp(host, "1") && applied && applied[0] == 'S' &&
            Decode(S.GetMemberData(s_mm, s_lobby, peers[i].steamID, "glimpse_own"), &r)) {
            known = true;
            char t[20];
            IpText(peers[i].ip, t, sizeof(t));
            _snprintf_s(from, _TRUNCATE, "the host %s", t);
        }
    }
    if (!known && inSession && s_sessionHost) {
        // a guest whose host's rules have not come yet: everything off until they do (not the lobby owner's,
        // who may be in another game)
        char t[20];
        IpText(s_sessionHost, t, sizeof(t));
        _snprintf_s(from, _TRUNCATE, "the host %s (waiting)", t);
    } else if (!known) {
        known = Decode(S.GetLobbyData(s_mm, s_lobby, "glimpse_rules"), &r);
        if (owner == s_mySteamID) strcpy_s(from, "this player, the lobby owner");
    }
    char now[160] = "", before[160] = "";
    if (known) Encode(r, now, sizeof(now));
    EnterCriticalSection(&s_showLock);
    if (s_lobbyRulesKnown) Encode(s_lobbyRules, before, sizeof(before));
    bool fromChanged = strcmp(from, s_rulesFrom) != 0;
    s_lobbyRules = r;
    s_lobbyRulesKnown = known;
    strcpy_s(s_rulesFrom, from);
    char applied[170];
    strcpy_s(applied, s_appliedText);
    LeaveCriticalSection(&s_showLock);
    if (strcmp(now, before) || fromChanged) {
        char d[160];
        Describe(r, d, sizeof(d));
        Log("online: session rules from %s: %s", from, known ? d : "none yet");
        ShowBanner(10000);
    }

    // this player's DLC tracks, when TakeOwnDlc could not decide them yet: published with everything below
    if (!s_myDlc[0] && TryTakeOwnDlc()) InterlockedIncrement(&s_lobbySerial);

    // own report
    // everything again after every join or create (Steam drops a member's data when it leaves, also when it comes
    // back to the same lobby), and when Steam no longer shows our version
    static LONG reportedSerial = -1;
    static char reported[170], reportedOwn[160], reportedMode[48] = "-";
    static int reportedHost = -1;
    const char* ownVer = S.GetMemberData(s_mm, s_lobby, s_mySteamID, "glimpse_ver");
    if (reportedSerial != s_lobbySerial || (reportedSerial >= 0 && (!ownVer || !*ownVer))) {
        static LONG goneLogged = -1;
        if (reportedSerial == s_lobbySerial && goneLogged != s_lobbySerial) {
            goneLogged = s_lobbySerial;
            Log("online: this player's member data is gone from the lobby, published again");
        }
        if (s_myDlc[0]) {  // not decided yet (TakeOwnDlc): nothing, the others wait for it
            char dlc[128];
            _snprintf_s(dlc, _TRUNCATE, "%s|%s", s_myDlc, s_myDlcNames);
            S.SetMemberData(s_mm, s_lobby, "glimpse_dlc", dlc);  // before the version: a member that has it knows ours
        }
        S.SetMemberData(s_mm, s_lobby, "glimpse_ver", GLIMPSE_VERSION);
        // whether this PC applies the session's laps (lap_menu.cpp's race hooks; not on the Russian depot)
        S.SetMemberData(s_mm, s_lobby, "glimpse_lapok", LapRaceHooksInstalled() ? "1" : "0");
        reportedSerial = s_lobbySerial;
        reported[0] = reportedOwn[0] = 0;
        strcpy_s(reportedMode, "-");
        reportedHost = -1;
    }
    if (strcmp(reportedOwn, mine)) {
        S.SetMemberData(s_mm, s_lobby, "glimpse_own", mine);
        strcpy_s(reportedOwn, mine);
    }
    if (reportedHost != (int)hosting) {
        S.SetMemberData(s_mm, s_lobby, "glimpse_host", hosting ? "1" : "0");
        reportedHost = hosting;
    }
    // the host's mode, so the others in the Steam lobby see which game is open
    char myMode[48];
    EnterCriticalSection(&s_showLock);
    strcpy_s(myMode, hosting ? s_mode : "");
    LeaveCriticalSection(&s_showLock);
    if (strcmp(reportedMode, myMode)) {
        S.SetMemberData(s_mm, s_lobby, "glimpse_mode", myMode);
        strcpy_s(reportedMode, myMode);
    }
    char lobbyMode[48] = "";
    for (const Peer& p : peers) {
        const char* host = S.GetMemberData(s_mm, s_lobby, p.steamID, "glimpse_host");
        const char* m = S.GetMemberData(s_mm, s_lobby, p.steamID, "glimpse_mode");
        if (host && !strcmp(host, "1") && m && *m) strcpy_s(lobbyMode, m);
    }
    EnterCriticalSection(&s_showLock);
    bool modeChanged = strcmp(lobbyMode, s_lobbyMode) != 0;
    strcpy_s(s_lobbyMode, lobbyMode);
    LeaveCriticalSection(&s_showLock);
    if (modeChanged) ShowBanner(6000);
    if (strcmp(reported, applied)) {
        S.SetMemberData(s_mm, s_lobby, "glimpse_applied", applied);
        strcpy_s(reported, applied);
    }

    // the others' reports
    char warning[160] = "";
    bool lapsBlocked = false;
    for (const Peer& p : peers) {
        char t[20];
        IpText(p.ip, t, sizeof(t));
        const char* ver = S.GetMemberData(s_mm, s_lobby, p.steamID, "glimpse_ver");
        const char* theirs = S.GetMemberData(s_mm, s_lobby, p.steamID, "glimpse_applied");
        const char* lapok = S.GetMemberData(s_mm, s_lobby, p.steamID, "glimpse_lapok");
        lapsBlocked |= !lapok || strcmp(lapok, "1") != 0;  // an older version publishes none
        if (ver && *ver && strcmp(ver, GLIMPSE_VERSION)) {
            if (FirstTime(Key(DIAG_IP_CLASH, p.ip, 2))) Log("online: player %s runs glimpse_fix %s, this one %s", t, ver, GLIMPSE_VERSION);
            _snprintf_s(warning, _TRUNCATE, "Note: player %s runs glimpse_fix %s (you: %s)", t, ver, GLIMPSE_VERSION);
        }
        if (!theirs || !*theirs) continue;
        bool theirSession = theirs[0] == 'S' && theirs[1] == '|';
        const char* theirRules = theirs[0] && theirs[1] == '|' ? theirs + 2 : theirs;
        Rules other;
        char d[160] = "?";
        if (Decode(theirRules, &other)) Describe(other, d, sizeof(d));
        // every change of what a player applies goes to the log
        static std::vector<std::pair<uint64_t, std::string>> last;
        bool changed = true;
        for (auto& l : last)
            if (l.first == p.steamID) {
                changed = l.second != theirs;
                l.second = theirs;
            }
        if (changed) {
            bool listed = false;
            for (auto& l : last) listed |= l.first == p.steamID;
            if (!listed) last.push_back(std::make_pair(p.steamID, std::string(theirs)));
            Log("online: player %s %s: %s", t, theirSession ? "in the LAN session" : "not in a LAN session", d);
        }
        // a guest takes the host's rules a few seconds after it joined: only a difference that lasts is shown
        static std::vector<std::pair<uint64_t, DWORD>> differs;
        bool differ = theirSession && known && strcmp(theirRules, now);
        DWORD since = 0;
        for (size_t k = 0; k < differs.size(); k++)
            if (differs[k].first == p.steamID) {
                if (!differ) differs.erase(differs.begin() + k);
                else since = differs[k].second;
                break;
            }
        if (differ && !since) differs.push_back(std::make_pair(p.steamID, since = GetTickCount()));
        if (differ && GetTickCount() - since >= 8000)
            _snprintf_s(warning, _TRUNCATE, "WARNING: player %s plays with other settings: %s", t, d);
    }
    // the laps go only into a lobby where everybody applies them: a player that keeps the game's laps (an older
    // version, the Russian depot) would end its race at another lap than the others
    if (lapsBlocked != s_lapsHeld) {
        s_lapsHeld = lapsBlocked;
        if (lapsBlocked && HostLaps())
            Log("online: a player cannot take a lap pick (older glimpse_fix, the Russian depot, or not known yet): this "
                "player's pick is held back, the game's laps");
        else if (!lapsBlocked && HostLaps())
            Log("online: every player takes a lap pick, this player's (%d) is offered", HostLaps());
    }
    // other DLC tracks: the one warning that says why a game cannot be joined, so it goes before the others
    for (const Peer& p : peers)
        if (PeerDlc(p) == DLC_OTHER) {
            char t[20];
            IpText(p.ip, t, sizeof(t));
            _snprintf_s(warning, _TRUNCATE, "WARNING: player %s has other DLC tracks/cars (%s; you: %s) - no LAN game together",
                        t, p.dlcNames, s_myDlcNames);
        }
    EnterCriticalSection(&s_showLock);
    bool changed = strcmp(warning, s_warning) != 0;
    strcpy_s(s_warning, warning);
    LeaveCriticalSection(&s_showLock);
    // a LAN game was just kept out of the game's list (or this player's from another's): shown again, at most every 20 s
    static DWORD hiddenShown;
    bool hidden = InterlockedExchange(&s_dlcHidden, 0) != 0;
    if (hidden && warning[0] && GetTickCount() - hiddenShown >= 20000) {
        hiddenShown = GetTickCount();
        ShowBanner(12000);
    } else if (changed && warning[0]) {
        ShowBanner(15000);
    }
}

static volatile bool s_started;

bool OnlineSession()
{
    if (!s_started) return false;
    EnterCriticalSection(&s_showLock);
    bool in = s_inSession;
    LeaveCriticalSection(&s_showLock);
    return in;
}

// game thread, every frame (OnInputFrame): the lobby rules in a LAN session, the own INI outside
void OnlineFrame()
{
    if (!s_started) return;
    EnterCriticalSection(&s_lock);
    // Quazal has sockets only while the LAN mode is in use; between closing the search socket and making
    // the session's there is a gap, so the session ends only 3 s after the last socket went
    static DWORD lastSockets;
    static bool hadSockets;
    if (!s_sockets.empty()) {
        lastSockets = GetTickCount();
        hadSockets = true;
    }
    bool active = hadSockets && GetTickCount() - lastSockets < 3000;
    LeaveCriticalSection(&s_lock);
    if (!active && (s_hosting || s_sessionHost)) {
        s_hosting = false;
        s_sessionHost = 0;
        Log("online: the LAN game ended");
    }
    EnterCriticalSection(&s_showLock);
    s_own.laps = s_lapsHeld ? 0 : HostLaps();  // the lap choice can change it on the track select (game thread, as here)
    Rules target = active ? (s_lobbyRulesKnown ? s_lobbyRules : Rules()) : s_own;  // all off until the rules are known
    if (!LapRaceHooksInstalled()) target.laps = 0;  // cannot be applied here: reported as the game's laps
    bool wasActive = s_inSession;
    s_inSession = active;
    LeaveCriticalSection(&s_showLock);
    bool photo = s_ownPhotoMode;  // in a session a free camera only, the world keeps running (camera.cpp)
    char enc[160];
    Encode(target, enc, sizeof(enc));
    static char applied[160];
    static bool appliedPhoto = true, first = true;
    if (first || strcmp(enc, applied) || photo != appliedPhoto) {
        ApplyRules(target, photo);
        char d[160];
        Describe(target, d, sizeof(d));
        Log("online: %s: %s%s", active ? "LAN session, the session rules apply" : "no LAN session, own settings apply", d,
            active && s_ownPhotoMode ? "; photo mode as a free camera, the world keeps running" : "");
        strcpy_s(applied, enc);
        appliedPhoto = photo;
        first = false;
    }
    // reported to the others: in a session ("S|") or not ("M|"), and what applies
    char report[170];
    _snprintf_s(report, _TRUNCATE, "%s|%s", active ? "S" : "M", enc);
    EnterCriticalSection(&s_showLock);
    strcpy_s(s_appliedText, report);
    LeaveCriticalSection(&s_showLock);
    if (active != wasActive && !active) {
        EnterCriticalSection(&s_showLock);
        s_mode[0] = 0;
        LeaveCriticalSection(&s_showLock);
    }
    if (active != wasActive && active) ShowBanner(10000);
}

// ---------------------------------------------------------------- the banner
//
// A few lines at the top left for some seconds (lobby joined, players coming and going, a LAN session
// starting, other rules or versions), drawn with ID3DXFont from d3dx9_42.dll, which the game loads,
// before the frame is presented. Only on the back buffer; the viewport is put back afterwards.

void OnlinePresent(IDirect3DDevice9* dev)
{
    if (!s_started || !dev) return;
    char lines[4][192];
    int count;
    DWORD until;
    EnterCriticalSection(&s_showLock);
    count = s_bannerCount;
    until = s_bannerUntil;
    memcpy(lines, s_bannerLines, sizeof(lines));
    LeaveCriticalSection(&s_showLock);
    if (!count || (int)(GetTickCount() - until) >= 0) return;
    const char* text[4];
    D3DCOLOR colors[4];
    for (int i = 0; i < count; i++) {
        text[i] = lines[i];
        colors[i] = i == 0 ? D3DCOLOR_ARGB(255, 255, 120, 20)
                    : !strncmp(lines[i], "WARNING", 7) ? D3DCOLOR_ARGB(255, 255, 85, 75)
                                                         : D3DCOLOR_ARGB(255, 235, 235, 238);
    }
    OverlayDraw(dev, text, colors, count, OVERLAY_TOP_LEFT, 1.0f);
}

// Steam: a friend's "Join game" while the game runs (GameLobbyJoinRequested_t, 333). The layout of
// Steam's CCallbackBase: vtable (Run, Run with the call result, GetCallbackSizeBytes), flags, id.
class SteamCallbackBase {
public:
    virtual void Run(void* param) = 0;
    virtual void Run(void* param, bool ioFailure, SteamAPICall_t call) = 0;
    virtual int GetCallbackSizeBytes() = 0;
    BYTE flags = 0;
    int id = 0;
};
#pragma pack(push, 8)
struct GameLobbyJoinRequested_t { uint64_t lobby; uint64_t friendID; };
#pragma pack(pop)
static volatile uint64_t s_joinRequest;
class JoinRequested : public SteamCallbackBase {
public:
    void Run(void* param) override { s_joinRequest = ((GameLobbyJoinRequested_t*)param)->lobby; }
    void Run(void* param, bool, SteamAPICall_t) override { Run(param); }
    int GetCallbackSizeBytes() override { return sizeof(GameLobbyJoinRequested_t); }
};
static JoinRequested s_joinCallback;

// "+connect_lobby <id>": Steam started the game from a friend's "Join game"
static uint64_t LobbyFromCommandLine()
{
    const char* cmd = GetCommandLineA();
    const char* at = cmd ? strstr(cmd, "+connect_lobby") : NULL;
    if (!at) return 0;
    at += strlen("+connect_lobby");
    while (*at == ' ' || *at == '"') at++;
    return _strtoui64(at, NULL, 10);
}

static void NoteLobbyName()
{
    const char* name = S.GetLobbyData(s_mm, s_lobby, "glimpse_name");
    EnterCriticalSection(&s_showLock);
    strcpy_s(s_lobbyName, name && *name ? name : "?");
    LeaveCriticalSection(&s_showLock);
}

// a friend's lobby: joined first, and only then the one this player was in is left, so a failed join does
// not cost the own lobby (Steam ends a lobby when its last member leaves)
static bool JoinFriendLobby(uint64_t lobby)
{
    uint64_t old = s_lobby;
    if (!Join(lobby)) return false;  // s_lobby stays the old one
    const char* ver = S.GetLobbyData(s_mm, lobby, "glimpse_lan");
    if (!ver || strcmp(ver, kLobbyVersion)) {
        Log("online: the friend's lobby is not a glimpse_fix LAN lobby of this version (%s), left again", ver && *ver ? ver : "none");
        S.LeaveLobby(s_mm, lobby);
        NoteBadLobby(lobby);
        s_lobby = old;
        return false;
    }
    if (old && old != lobby) S.LeaveLobby(s_mm, old);
    Log("online: joined a friend's lobby through Steam");
    return true;
}

// this player's DLC tracks once DlcRegisterTracks has decided them (online thread); false while it has not
static bool TryTakeOwnDlc()
{
    if (!DlcTracksDecided()) return false;
    char sig[24], names[96];
    DlcOnlineSignature(sig, sizeof(sig), names, sizeof(names));
    InterlockedExchange(&s_dlcHidden, 0);
    EnterCriticalSection(&s_lock);  // PeerDlc reads them from the network threads (DlcLetIn holds the lock)
    strcpy_s(s_myDlcNames, names);
    strcpy_s(s_myDlc, sig);
    LeaveCriticalSection(&s_lock);
    if (!strcmp(sig, "none"))
        Log("online: DLC check: no DLC tracks registered online and no DLC cars offered online, LAN games with every "
            "player without them");
    else
        Log("online: DLC check: DLC online: %s (signature %s): LAN games only with players who have the same (same "
            "track pack, with UnlockAll=1 the same car pack, and glimpse_fix version)", names, sig);
    return true;
}

// before the first lobby: DlcRegisterTracks runs right after OnlineStart on the game thread. Not decided within
// 30 s: s_myDlc stays empty and nothing is published (players without DLC tracks treat this one as before; this
// one lets no game traffic through, as for a player whose tracks are not known); SyncRules tries again every second
static void TakeOwnDlc()
{
    for (int i = 0; i < 3000 && !DlcTracksDecided(); i++) Sleep(10);
    if (!TryTakeOwnDlc())
        Log("online: DLC check: the DLC tracks were not decided within 30 s: nothing published yet, LAN game traffic "
            "held back until they are (checked again every second)");
}

static DWORD WINAPI OnlineThread(void*)
{
    TakeOwnDlc();
    uint64_t invited = LobbyFromCommandLine();
    if (invited) Log("online: started by Steam to join lobby %llu", (unsigned long long)invited);
    bool in = invited && JoinFriendLobby(invited);
    if (!in) {
        uint64_t lobby = FindLobby();
        in = (lobby && (g_cfg.friendsOnly ? JoinFriendLobby(lobby) : Join(lobby))) || Create();
    }
    if (!in) {
        Log("online: no lobby, SteamLAN stays local");
        return 0;
    }
    NoteLobbyName();
    ShowBanner(10000);
    DWORD lastMembers = 0, lastMatch = GetTickCount(), lastTraffic = GetTickCount();
    for (;;) {
        S.RunCallbacks();
        Pump();
        if (uint64_t req = s_joinRequest) {
            s_joinRequest = 0;
            if (req != s_lobby) {
                Log("online: a friend's Join game, switching lobbies");
                if (!JoinFriendLobby(req) && !s_lobby) Create();
                NoteLobbyName();
                ShowBanner(10000);
            }
        }
        DWORD now = GetTickCount();
        if (now - lastMembers >= 1000) {
            lastMembers = now;
            static int lostFor;
            if (!s_lobby || S.LobbyOwner(s_mm, s_lobby) == 0) {
                if (++lostFor >= 3) {
                    if (s_lobby) {
                        Log("online: Steam no longer has this player in lobby %llu, looking for a lobby again",
                            (unsigned long long)s_lobby);
                        S.LeaveLobby(s_mm, s_lobby);
                        s_lobby = 0;
                    }
                    uint64_t lobby = FindLobby();
                    if (!(lobby && (g_cfg.friendsOnly ? JoinFriendLobby(lobby) : Join(lobby))) && !Create())
                        Log("online: no lobby for now, trying again");
                    NoteLobbyName();
                    ShowBanner(10000);
                    lostFor = 0;
                }
            } else {
                lostFor = 0;
            }
            size_t before = s_peers.size();
            RefreshMembers();
            PeerConnections(false);
            SyncRules();
            if (s_peers.size() != before) ShowBanner(8000);
            UpdatePresence();
        }
        if (now - lastTraffic >= 10000) {
            lastTraffic = now;
            LogTraffic();
        }
        // two players who started at the same time each made a lobby: while alone, look for an older one
        if (now - lastMatch >= 15000) {
            lastMatch = now;
            bool alone;
            EnterCriticalSection(&s_lock);
            alone = s_peers.empty();
            LeaveCriticalSection(&s_lock);
            if (alone) {
                uint64_t other = FindLobby();
                if (other && other < s_lobby) {
                    uint64_t mine = s_lobby;
                    if (g_cfg.friendsOnly) {
                        JoinFriendLobby(other);
                    } else if (Join(other)) {  // joined first, the own lobby left only then
                        S.LeaveLobby(s_mm, mine);
                    }
                    NoteLobbyName();
                }
            }
        }
        Sleep(1);
    }
}

// The game's calls into its Scaleform menus (GFxMovieRoot vtable 0xC41CF4, slot 18 Invoke): the lobby
// screen's title gives the game mode (NoteTitle). Only the vtable slot is swapped, no game code.
typedef bool(__fastcall* GfxInvokeHook_t)(void* movie, void*, const char* path, GFxValue* result, const GFxValue* args,
                                          unsigned count);
static GFxInvoke_t s_origInvoke;

// The game's lobby screen of a LAN game sets its title to "LOBBY - <mode>" in the game's language (seen on
// the host and the guests alike); the part after the last " - " is the mode, kept until the LAN game ends.
static void NoteTitle(const char* path, const GFxValue* args, unsigned count)
{
    if (!path || strcmp(path, "main.SetTitleText") || !args || count < 1 || args[0].type != GFX_STRING || !args[0].str ||
        IsBadStringPtrA(args[0].str, 128) || !OnlineSession())
        return;
    const char* dash = NULL;
    for (const char* t = args[0].str; (t = strstr(t, " - ")) != NULL; t += 3) dash = t;
    if (!dash || !dash[3]) return;
    char mode[48];
    strncpy_s(mode, dash + 3, _TRUNCATE);
    EnterCriticalSection(&s_showLock);
    bool changed = strcmp(mode, s_mode) != 0;
    if (changed) strcpy_s(s_mode, mode);
    LeaveCriticalSection(&s_showLock);
    if (changed) {
        Log("online: game mode %s", mode);
        ShowBanner(6000);
    }
}

static bool __fastcall Hook_GfxInvoke(void* movie, void*, const char* path, GFxValue* result, const GFxValue* args,
                                      unsigned count)
{
    NoteTitle(path, args, count);
    GFxValue mine[8];
    if (DlcTrackBestArgs(movie, path, args, count, mine) || LapMenuInvokeArgs(movie, path, args, count, mine)) args = mine;
    return s_origInvoke(movie, path, result, args, count);
}

bool OnlineMenuHookInstalled()
{
    return s_origInvoke != NULL;
}

void* OnlineInvokeHook()
{
    return s_origInvoke ? (void*)Hook_GfxInvoke : NULL;
}

static void InstallUiHook()
{
    void** vt = (void**)G(0x00C41CF4);
    if (IsBadReadPtr(vt, 19 * 4) || (DWORD)vt[18] != G(0x0098D2E0)) {
        Log("online: Scaleform movie class not as expected, the game mode is not shown");
        return;
    }
    s_origInvoke = (GFxInvoke_t)vt[18];
    if (!WriteSlot(&vt[18], (void*)Hook_GfxInvoke)) {
        s_origInvoke = NULL;
        Log("online: Scaleform Invoke slot could not be written");
        return;
    }
    Log("online: the game mode is read from the lobby screen");
}

void OnlineStart()
{
    if (CurrentBuild() == BUILD_UNKNOWN) {
        Log("online: SteamLAN only on the known Steam builds");
        return;
    }
    InitializeCriticalSection(&s_lock);
    InitializeCriticalSection(&s_steamLock);
    InitializeCriticalSection(&s_diagLock);
    InitializeCriticalSection(&s_showLock);
    s_arrived = CreateEventA(NULL, FALSE, FALSE, NULL);
    InstallUiHook();
    s_own = OwnRules();
    s_ownHandbrakeButton = g_cfg.handbrakeButtonIni;
    s_ownPhotoMode = g_cfg.photoMode;
    // the transport Network takes when it starts (it has not yet in the main menu)
    QDriver** setting = (QDriver**)G(0x00D77734);
    QDriver* const* active = (QDriver* const*)G(0x00D779FC);
    s_activeDriver = active;
    if (*active || *setting) {
        Log("online: Quazal already has a socket driver, SteamLAN off");
        return;
    }
    // the interface list Quazal reads for its own address
    void** ioctlSlot = (void**)G(0x00B9B368);  // WS2_32!WSAIoctl import
    HMODULE ws2 = GetModuleHandleA("ws2_32.dll");
    if (!ws2 || *ioctlSlot != (void*)GetProcAddress(ws2, "WSAIoctl")) {
        Log("online: WSAIoctl import not as expected, SteamLAN off");
        return;
    }
    if (!LoadSteam()) return;
    s_myIp = VirtualIp(s_mySteamID);
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GlobalMemoryStatusEx(&ms);
    s_noLaa = ms.ullTotalVirtual <= 0x80000000ull;
    if (s_noLaa) Log("online: WARNING: no 4 GB address space (SplitSecond.exe without LAA flag); with Steam's libraries "
                     "loaded as well the game may run out of memory when loading - run setlaa.exe on SplitSecond.exe");

    s_origIoctl = (WSAIoctl_t)*ioctlSlot;
    void* hook = (void*)Hook_WSAIoctl;
    WriteMemory(ioctlSlot, &hook, sizeof(hook));
    QDriver* driver = &s_driver;
    WriteMemory(setting, &driver, sizeof(driver));

    char t[20];
    IpText(s_myIp, t, sizeof(t));
    Log("online: SteamLAN on, this player is %s, socket driver set", t);
    if (g_cfg.log) InstallDiagnostics();
    S.RegisterCallback(&s_joinCallback, 333);  // GameLobbyJoinRequested_t
    s_started = true;
    CloseHandle(CreateThread(NULL, 0, OnlineThread, NULL, 0, NULL));
}
