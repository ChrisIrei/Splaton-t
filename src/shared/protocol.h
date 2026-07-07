// Splaton't — wire protocol: message ids + little byte buffer reader/writer
#pragma once
#include "shared/defs.h"
#include <cstring>
#include <string>
#include <vector>

// channel 0 = reliable (auth, lobby, paint, events), channel 1 = unreliable (input, snapshots)

enum : u8 {
    // client -> server
    C_HELLO = 1,        // u32 protocolVersion
    C_REGISTER,         // str user, str pass
    C_LOGIN,            // str user, str pass
    C_SET_LOADOUT,      // u8 weapon, u8 skin, u8 sub
    C_QUEUE,            // u8 mode
    C_LEAVE_QUEUE,      //
    C_INPUT,            // f32 x, f32 y, f32 aimRad, u8 buttons
    C_LEAVE_MATCH,      // return to menu; slot is handed to a bot
    C_PING,             // u32 clientTimeMs (echoed back)
    C_QUICKCHAT,        // u8 msgId
    C_BUY_HAT,          // u8 hat
    C_RESUME,           // str sessionToken (re-auth without password)

    // server -> client
    S_HELLO = 64,       // str motd
    S_AUTH_OK,          // u32 accId, str name, u8 weapon, u8 skin, u8 sub, u32 kills, u32 deaths, u32 wins, u32 losses, u32 matches, u32 paint
    S_AUTH_FAIL,        // str reason
    S_LOADOUT_OK,       // u8 weapon, u8 skin, u8 sub
    S_QUEUE_STATE,      // u8 mode, u8 humansQueued, f32 countdown (<0 = waiting)
    S_MATCH_START,      // u8 mode, u8 colorPair, u8 mapId, u8 yourSlot, f32 timeLeft, u8 count, [u8 slot, str name, u8 team, u8 weapon, u8 skin, u8 flags]
    S_PLAYER_JOIN,      // same per-player tuple (mid-match join, replaces a bot slot)
    S_MATCH_END,        // u8 winnerTeam (0=draw), u16 scoreA, u16 scoreB, u8 count, [u8 slot, str name, u8 team, u16 kills, u16 deaths, u16 paint]
    S_SNAPSHOT,         // u32 tick, f32 timeLeft, u16 scoreA, u16 scoreB, u8 nPlayers, [players], u8 nProj, [f32 x, f32 y, u8 team, u8 kind, u8 aux]
    S_PAINT,            // u16 count, [u16 cellIdx, u8 team]
    S_EVENT,            // u8 eventType, ...
    S_ERROR,            // str message
    S_PONG,             // u32 clientTimeMs (echo of C_PING)
    S_BUY_RESULT,       // u8 ok, u32 coins, u16 hatsOwned
};

enum : u8 {
    EV_KILL = 1,        // u8 killerSlot, u8 victimSlot, f32 x, f32 y
    EV_RESPAWN,         // u8 slot
    EV_BOOM,            // u8 team, f32 x, f32 y, u8 radiusPx
    EV_CHAT,            // u8 slot, u8 msgId
    EV_SPECIAL,         // u8 slot, u8 specialKind (activation fanfare)
};

struct BufW {
    std::vector<u8> d;
    void u8_(u8 v) { d.push_back(v); }
    void u16_(u16 v) { d.push_back(v & 0xff); d.push_back(v >> 8); }
    void u32_(u32 v) { for (int i = 0; i < 4; i++) d.push_back((v >> (i * 8)) & 0xff); }
    void f32_(float v) { u32 b; memcpy(&b, &v, 4); u32_(b); }
    void str(const std::string& s) {
        u8 n = (u8)(s.size() > 255 ? 255 : s.size());
        u8_(n);
        d.insert(d.end(), s.begin(), s.begin() + n);
    }
};

struct BufR {
    const u8* d = nullptr;
    size_t n = 0, p = 0;
    bool ok = true;
    BufR(const u8* data, size_t len) : d(data), n(len) {}
    BufR(const std::vector<u8>& v) : d(v.data()), n(v.size()) {}
    bool has(size_t k) { if (p + k > n) { ok = false; return false; } return true; }
    u8 u8_() { if (!has(1)) return 0; return d[p++]; }
    u16 u16_() { if (!has(2)) return 0; u16 v = d[p] | (d[p + 1] << 8); p += 2; return v; }
    u32 u32_() { if (!has(4)) return 0; u32 v = 0; for (int i = 0; i < 4; i++) v |= (u32)d[p + i] << (i * 8); p += 4; return v; }
    float f32_() { u32 b = u32_(); float v; memcpy(&v, &b, 4); return v; }
    std::string str() {
        u8 k = u8_();
        if (!has(k)) return "";
        std::string s((const char*)d + p, k);
        p += k;
        return s;
    }
};
