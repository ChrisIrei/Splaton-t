// Splaton't — dedicated server: accounts, lobby, concurrent match rooms
#include "engine/net.h"
#include "engine/sha256.h"
#include "server/db.h"
#include "server/game.h"
#include "shared/protocol.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>

static void logf(const char* fmt, ...) {
    time_t t = time(nullptr);
    tm tmv;
    localtime_s(&tmv, &t);
    printf("[%02d:%02d:%02d] ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

struct Session {
    enum St { FRESH, AUTHED, QUEUED, INGAME } st = FRESH;
    Account acc;
    std::string token;
    u8 queuedMode = MODE_TURF;
    int room = -1;
    int slot = -1;
    double lastChat = 0;
};

struct Room {
    Match match;
    bool running = false;
};

// ---- server.cfg (created with defaults on first run) ----
struct ServerCfg {
    u16 port = DEFAULT_PORT;
    float turfTime = TURF_TIME;
    float tdmTime = TDM_TIME;
    int tdmKills = TDM_KILL_TARGET;
    float lobbyCountdown = LOBBY_COUNTDOWN;
};
static ServerCfg cfg;

static void loadOrCreateCfg() {
    FILE* f = fopen("server.cfg", "r");
    if (!f) {
        f = fopen("server.cfg", "w");
        if (f) {
            fprintf(f,
                "# Splaton't server configuration\n"
                "port %u\n"
                "turf_time %d\n"
                "tdm_time %d\n"
                "tdm_kills %d\n"
                "lobby_countdown %d\n",
                DEFAULT_PORT, (int)TURF_TIME, (int)TDM_TIME, TDM_KILL_TARGET, (int)LOBBY_COUNTDOWN);
            fclose(f);
        }
        return;
    }
    char key[64];
    int val;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%63s %d", key, &val) != 2) continue;
        if (!strcmp(key, "port")) cfg.port = (u16)val;
        else if (!strcmp(key, "turf_time")) cfg.turfTime = (float)val;
        else if (!strcmp(key, "tdm_time")) cfg.tdmTime = (float)val;
        else if (!strcmp(key, "tdm_kills")) cfg.tdmKills = val;
        else if (!strcmp(key, "lobby_countdown")) cfg.lobbyCountdown = (float)val;
    }
    fclose(f);
}

// ---- session tokens (in-memory; survive reconnects, not restarts) ----
struct TokenInfo { int64_t accId; double expires; };
static std::map<std::string, TokenInfo> tokens;

// ---- per-IP auth rate limiting ----
struct AuthLimit { int fails = 0; double resetAt = 0; };
static std::map<u32, AuthLimit> authLimits;

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static net::Host host;
static DB db;
static std::map<void*, Session> sessions;
static Room rooms[MAX_ROOMS];
static float countdown[MODE_COUNT] = { -1, -1 };
static float queueBroadcastT = 0;
static int mapRotation = 0;

static const char* modeName(u8 m) { return m == MODE_TURF ? "Turf War" : "Team Deathmatch"; }

static void sendTo(void* peer, BufW& w, bool reliable) { host.send(peer, w.d, reliable); }

static void broadcastRoom(int r, BufW& w, bool reliable) {
    for (auto& [peer, s] : sessions)
        if (s.st == Session::INGAME && s.room == r) host.send(peer, w.d, reliable);
}

static int freeRoom() {
    for (int i = 0; i < MAX_ROOMS; i++)
        if (!rooms[i].running) return i;
    return -1;
}

static void sendAuthOk(void* peer, Session& s) {
    const Account& a = s.acc;
    if (s.token.empty()) {
        s.token = random_hex(24);
        tokens[s.token] = { a.id, nowSec() + 86400.0 };
    }
    BufW w;
    w.u8_(S_AUTH_OK);
    w.u32_((u32)a.id);
    w.str(a.name);
    w.str(s.token);
    w.u8_(a.weapon);
    w.u8_(a.skin);
    w.u8_(a.sub);
    w.u8_(a.hat);
    w.u32_(a.coins);
    w.u16_(a.hatsOwned);
    w.u32_(a.kills);
    w.u32_(a.deaths);
    w.u32_(a.wins);
    w.u32_(a.losses);
    w.u32_(a.matches);
    w.u32_(a.paint);
    sendTo(peer, w, true);
}

static void sendAuthFail(void* peer, const std::string& reason) {
    BufW w;
    w.u8_(S_AUTH_FAIL);
    w.str(reason);
    sendTo(peer, w, true);
}

static void sendMatchStartTo(void* peer, Room& room, int yourSlot) {
    BufW w;
    w.u8_(S_MATCH_START);
    w.u8_(room.match.mode);
    w.u8_(room.match.colorPair);
    w.u8_((u8)room.match.map.mapId);
    w.u8_((u8)yourSlot);
    w.f32_(room.match.timeLeft);
    int n = 0;
    for (auto& p : room.match.players) if (p.active) n++;
    w.u8_((u8)n);
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (room.match.players[i].active) room.match.writeRosterEntry(w, i);
    sendTo(peer, w, true);
}

static void startMatch(u8 mode) {
    int r = freeRoom();
    if (r < 0) return;
    Room& room = rooms[r];
    int mapId = mapRotation++ % MAP_COUNT;
    room.match.start(mode, mapId, (u32)time(nullptr) + mapRotation * 7919,
                     mode == MODE_TURF ? cfg.turfTime : cfg.tdmTime, cfg.tdmKills);
    room.running = true;
    int humans = 0;
    for (auto& [peer, s] : sessions) {
        if (s.st != Session::QUEUED || s.queuedMode != mode) continue;
        int slot = room.match.addPlayer(s.acc.name, s.acc.weapon, s.acc.skin, s.acc.sub, s.acc.hat,
                                        false, peer, s.acc.id);
        if (slot >= 0) { s.st = Session::INGAME; s.room = r; s.slot = slot; humans++; }
    }
    room.match.fillWithBots();
    for (auto& [peer, s] : sessions)
        if (s.st == Session::INGAME && s.room == r) sendMatchStartTo(peer, room, s.slot);
    logf("room %d: %s started on %s, %d human(s), %d bot(s)",
        r, modeName(mode), MAP_NAMES[mapId], humans, MAX_PLAYERS - humans);
}

static void tryJoinInProgress(void* peer, Session& s) {
    for (int r = 0; r < MAX_ROOMS; r++) {
        Room& room = rooms[r];
        if (!room.running || room.match.ended || room.match.mode != s.queuedMode) continue;
        int slot = room.match.replaceBotWithHuman(s.acc.name, s.acc.weapon, s.acc.skin, s.acc.sub, s.acc.hat, peer, s.acc.id);
        if (slot < 0) continue;
        s.st = Session::INGAME;
        s.room = r;
        s.slot = slot;
        sendMatchStartTo(peer, room, slot);
        BufW paintW;
        room.match.writeFullPaint(paintW);
        sendTo(peer, paintW, true);
        BufW j;
        j.u8_(S_PLAYER_JOIN);
        room.match.writeRosterEntry(j, slot);
        for (auto& [op, os] : sessions)
            if (os.st == Session::INGAME && os.room == r && op != peer) host.send(op, j.d, true);
        logf("room %d: %s joined in progress (slot %d)", r, s.acc.name.c_str(), slot);
        return;
    }
}

static u32 coinsFor(const PlayerState& p, u8 winner) {
    int paintBonus = (int)p.paintCells / 40;
    if (paintBonus > 150) paintBonus = 150;
    int result = winner == TEAM_NONE ? 60 : (p.team == winner ? 150 : 40);
    return (u32)(80 + p.kills * 12 + paintBonus + result);
}

static void endMatch(int r) {
    Room& room = rooms[r];
    Match& match = room.match;
    BufW w;
    w.u8_(S_MATCH_END);
    w.u8_(match.winner);
    w.u16_(match.scoreA);
    w.u16_(match.scoreB);
    int n = 0;
    for (auto& p : match.players) if (p.active) n++;
    w.u8_((u8)n);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerState& p = match.players[i];
        if (!p.active) continue;
        w.u8_((u8)i);
        w.str(p.name);
        w.u8_(p.team);
        w.u16_(p.kills);
        w.u16_(p.deaths);
        w.u16_((u16)(p.paintCells > 65535 ? 65535 : p.paintCells));
        w.u16_((u16)(p.isBot ? 0 : coinsFor(p, match.winner)));
    }
    broadcastRoom(r, w, true);

    for (auto& [peer, s] : sessions) {
        if (s.st != Session::INGAME || s.room != r) continue;
        PlayerState& p = match.players[s.slot];
        bool won = match.winner != TEAM_NONE && p.team == match.winner;
        bool lost = match.winner != TEAM_NONE && !won;
        u32 coins = coinsFor(p, match.winner);
        db.addMatchStats(s.acc.id, p.kills, p.deaths, (int)p.paintCells, won, lost, (int)coins);
        s.acc.kills += p.kills;
        s.acc.deaths += p.deaths;
        s.acc.paint += p.paintCells;
        s.acc.coins += coins;
        s.acc.matches++;
        if (won) s.acc.wins++;
        if (lost) s.acc.losses++;
        s.st = Session::AUTHED;
        s.room = -1;
        s.slot = -1;
    }
    logf("room %d: %s ended, winner=%s, score %u-%u", r, modeName(match.mode),
        match.winner == TEAM_A ? "A" : match.winner == TEAM_B ? "B" : "draw",
        match.scoreA, match.scoreB);
    room.running = false;
}

static bool chatThrottled(Session& s) {
    double t = nowSec();
    if (t - s.lastChat < 1.0) return true;
    s.lastChat = t;
    return false;
}

static bool accountInUse(int64_t id) {
    for (auto& [peer, s] : sessions)
        if (s.st != Session::FRESH && s.acc.id == id) return true;
    return false;
}

static void handleMessage(void* peer, Session& s, BufR& r) {
    u8 id = r.u8_();
    switch (id) {
    case C_HELLO: {
        u32 ver = r.u32_();
        BufW w;
        if (ver != PROTOCOL_VERSION) {
            w.u8_(S_ERROR);
            w.str("Version mismatch - update your client");
        } else {
            w.u8_(S_HELLO);
            w.str("Splaton't local server");
        }
        sendTo(peer, w, true);
        break;
    }
    case C_REGISTER:
    case C_LOGIN: {
        std::string user = r.str(), pass = r.str();
        if (!r.ok || s.st != Session::FRESH) { sendAuthFail(peer, "Bad request"); break; }

        // per-IP rate limit on failed attempts
        u32 host32 = net::peerHost(peer);
        AuthLimit& lim = authLimits[host32];
        if (lim.fails >= 5 && nowSec() < lim.resetAt) {
            sendAuthFail(peer, "Too many attempts - wait a minute");
            break;
        }
        if (nowSec() >= lim.resetAt) lim.fails = 0;

        Account acc;
        std::string err;
        bool ok = (id == C_REGISTER)
            ? db.registerAccount(user, pass, acc, err)
            : db.login(user, pass, acc, err);
        if (ok && accountInUse(acc.id)) { ok = false; err = "Account already logged in"; }
        if (!ok) {
            lim.fails++;
            lim.resetAt = nowSec() + 60.0;
            sendAuthFail(peer, err);
            logf("auth failed for '%s' from %s: %s", user.c_str(), net::peerAddress(peer).c_str(), err.c_str());
            break;
        }
        authLimits.erase(host32);
        s.st = Session::AUTHED;
        s.acc = acc;
        sendAuthOk(peer, s);
        logf("%s %s from %s", acc.name.c_str(), id == C_REGISTER ? "registered" : "logged in",
            net::peerAddress(peer).c_str());
        break;
    }
    case C_RESUME: {
        std::string token = r.str();
        if (!r.ok || s.st != Session::FRESH) { sendAuthFail(peer, "Bad request"); break; }
        auto it = tokens.find(token);
        if (it == tokens.end() || nowSec() > it->second.expires) {
            sendAuthFail(peer, "Session expired - please log in");
            break;
        }
        Account acc;
        if (!db.loadAccount(it->second.accId, acc) || accountInUse(acc.id)) {
            sendAuthFail(peer, "Session invalid - please log in");
            break;
        }
        s.st = Session::AUTHED;
        s.acc = acc;
        s.token = token;
        sendAuthOk(peer, s);
        logf("%s resumed session from %s", acc.name.c_str(), net::peerAddress(peer).c_str());
        break;
    }
    case C_SET_LOADOUT: {
        u8 weapon = r.u8_(), skin = r.u8_(), sub = r.u8_(), hat = r.u8_();
        if (s.st == Session::FRESH || weapon >= W_COUNT || skin >= SKIN_COUNT ||
            sub >= SUB_COUNT || hat >= HAT_COUNT) break;
        if (!(s.acc.hatsOwned & (1 << hat))) hat = 0;
        s.acc.weapon = weapon;
        s.acc.skin = skin;
        s.acc.sub = sub;
        s.acc.hat = hat;
        db.setLoadout(s.acc.id, weapon, skin, sub, hat);
        BufW w;
        w.u8_(S_LOADOUT_OK);
        w.u8_(weapon);
        w.u8_(skin);
        w.u8_(sub);
        w.u8_(hat);
        sendTo(peer, w, true);
        break;
    }
    case C_BUY_HAT: {
        u8 hat = r.u8_();
        BufW w;
        w.u8_(S_BUY_RESULT);
        bool ok = s.st != Session::FRESH && hat > 0 && hat < HAT_COUNT &&
                  !(s.acc.hatsOwned & (1 << hat)) && s.acc.coins >= HAT_PRICES[hat];
        if (ok) {
            s.acc.coins -= HAT_PRICES[hat];
            s.acc.hatsOwned |= (u16)(1 << hat);
            db.updateCoinsHats(s.acc.id, s.acc.coins, s.acc.hatsOwned);
            logf("%s bought hat '%s'", s.acc.name.c_str(), HAT_NAMES[hat]);
        }
        w.u8_(ok ? 1 : 0);
        w.u32_(s.acc.coins);
        w.u16_(s.acc.hatsOwned);
        sendTo(peer, w, true);
        break;
    }
    case C_QUICKCHAT: {
        u8 msg = r.u8_();
        if (s.st != Session::INGAME || s.room < 0 || msg >= CHAT_COUNT) break;
        if (chatThrottled(s)) break;
        BufW w;
        w.u8_(S_EVENT);
        w.u8_(EV_CHAT);
        w.u8_((u8)s.slot);
        w.u8_(msg);
        broadcastRoom(s.room, w, true);
        break;
    }
    case C_QUEUE: {
        u8 mode = r.u8_();
        if (mode >= MODE_COUNT) break;
        if (s.st != Session::AUTHED && s.st != Session::QUEUED) break;
        s.st = Session::QUEUED;
        s.queuedMode = mode;
        logf("%s queued for %s", s.acc.name.c_str(), modeName(mode));
        tryJoinInProgress(peer, s);
        break;
    }
    case C_LEAVE_QUEUE:
        if (s.st == Session::QUEUED) s.st = Session::AUTHED;
        break;
    case C_INPUT: {
        if (s.st != Session::INGAME || s.room < 0) break;
        float x = r.f32_(), y = r.f32_(), aim = r.f32_();
        u8 buttons = r.u8_();
        if (r.ok) rooms[s.room].match.handleInput(s.slot, { x, y }, aim, buttons);
        break;
    }
    case C_LEAVE_MATCH:
        if (s.st == Session::INGAME && s.room >= 0 && rooms[s.room].running) {
            logf("room %d: %s left; slot handed to a bot", s.room, s.acc.name.c_str());
            rooms[s.room].match.humanLeft(s.slot);
            s.st = Session::AUTHED;
            s.room = -1;
            s.slot = -1;
        }
        break;
    case C_PING: {
        u32 t = r.u32_();
        BufW w;
        w.u8_(S_PONG);
        w.u32_(t);
        sendTo(peer, w, false);
        break;
    }
    default: break;
    }
}

int main(int argc, char** argv) {
    loadOrCreateCfg();
    u16 port = cfg.port;
    if (argc > 1) port = (u16)atoi(argv[1]);

    if (!net::init()) { logf("ENet init failed"); return 1; }
    std::string err;
    if (!db.open("splatont.db", err)) { logf("DB open failed: %s", err.c_str()); return 1; }
    if (!host.serve(port, 32)) { logf("Failed to bind UDP port %u", port); return 1; }
    logf("Splaton't server listening on UDP %u (db: splatont.db, cfg: server.cfg, %d rooms)", port, MAX_ROOMS);

    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    float acc = 0;
    std::vector<net::Event> events;

    for (;;) {
        events.clear();
        host.poll(events, 2);
        for (auto& e : events) {
            switch (e.type) {
            case net::Event::CONNECT:
                sessions[e.peer] = Session{};
                logf("connection from %s", net::peerAddress(e.peer).c_str());
                break;
            case net::Event::DISCONNECT: {
                auto it = sessions.find(e.peer);
                if (it != sessions.end()) {
                    Session& s = it->second;
                    if (s.st == Session::INGAME && s.room >= 0 && rooms[s.room].running) {
                        rooms[s.room].match.humanLeft(s.slot);
                        logf("room %d: %s disconnected; slot handed to a bot", s.room, s.acc.name.c_str());
                    } else if (s.st != Session::FRESH) {
                        logf("%s disconnected", s.acc.name.c_str());
                    } else {
                        logf("connection closed");
                    }
                    sessions.erase(it);
                }
                break;
            }
            case net::Event::DATA: {
                auto it = sessions.find(e.peer);
                if (it == sessions.end()) break;
                BufR r(e.data);
                handleMessage(e.peer, it->second, r);
                break;
            }
            }
        }

        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.25f) dt = 0.25f;
        acc += dt;

        while (acc >= TICK_DT) {
            acc -= TICK_DT;

            // lobby countdowns (a match starts only if a room is free)
            for (u8 m = 0; m < MODE_COUNT; m++) {
                int n = 0;
                for (auto& [peer, s] : sessions)
                    if (s.st == Session::QUEUED && s.queuedMode == m) n++;
                if (n == 0) { countdown[m] = -1; continue; }
                if (freeRoom() < 0) continue;        // hold until a room opens up
                if (countdown[m] < 0) countdown[m] = cfg.lobbyCountdown;
                countdown[m] -= TICK_DT;
                if (countdown[m] <= 0) {
                    countdown[m] = -1;
                    startMatch(m);
                }
            }

            // queue status updates, twice a second
            queueBroadcastT += TICK_DT;
            if (queueBroadcastT >= 0.5f) {
                queueBroadcastT = 0;
                for (auto& [peer, s] : sessions) {
                    if (s.st != Session::QUEUED) continue;
                    int n = 0;
                    for (auto& [p2, s2] : sessions)
                        if (s2.st == Session::QUEUED && s2.queuedMode == s.queuedMode) n++;
                    BufW w;
                    w.u8_(S_QUEUE_STATE);
                    w.u8_(s.queuedMode);
                    w.u8_((u8)n);
                    w.f32_(freeRoom() < 0 ? -1.0f : countdown[s.queuedMode]);
                    sendTo(peer, w, true);
                }
            }

            // feed peer RTTs into the sim for lag compensation
            for (auto& [peer, s] : sessions)
                if (s.st == Session::INGAME && s.room >= 0)
                    rooms[s.room].match.players[s.slot].rttS = net::peerRTT(peer) / 1000.0f;

            for (int r = 0; r < MAX_ROOMS; r++) {
                Room& room = rooms[r];
                if (!room.running) continue;

                if (room.match.humanCount() == 0) {
                    logf("room %d: no humans left; aborting match", r);
                    room.running = false;
                    continue;
                }

                room.match.update(TICK_DT);

                for (auto& ke : room.match.killEvents) {
                    BufW w;
                    w.u8_(S_EVENT);
                    w.u8_(EV_KILL);
                    w.u8_(ke.killer);
                    w.u8_(ke.victim);
                    w.f32_(ke.pos.x);
                    w.f32_(ke.pos.y);
                    broadcastRoom(r, w, true);
                }
                room.match.killEvents.clear();

                for (auto& be : room.match.boomEvents) {
                    BufW w;
                    w.u8_(S_EVENT);
                    w.u8_(EV_BOOM);
                    w.u8_(be.team);
                    w.f32_(be.pos.x);
                    w.f32_(be.pos.y);
                    w.u8_(be.radius);
                    broadcastRoom(r, w, true);
                }
                room.match.boomEvents.clear();

                for (auto& ce : room.match.chatEvents) {
                    BufW w;
                    w.u8_(S_EVENT);
                    w.u8_(EV_CHAT);
                    w.u8_(ce.slot);
                    w.u8_(ce.msg);
                    broadcastRoom(r, w, true);
                }
                room.match.chatEvents.clear();

                for (auto& se : room.match.specialEvents) {
                    BufW w;
                    w.u8_(S_EVENT);
                    w.u8_(EV_SPECIAL);
                    w.u8_(se.slot);
                    w.u8_(se.kind);
                    broadcastRoom(r, w, true);
                }
                room.match.specialEvents.clear();

                if (!room.match.paintDeltas.empty()) {
                    BufW w;
                    room.match.writePaintDeltas(w);
                    broadcastRoom(r, w, true);
                }

                BufW snap;
                room.match.writeSnapshot(snap);
                broadcastRoom(r, snap, false);

                if (room.match.ended) endMatch(r);
            }
        }
    }
}
