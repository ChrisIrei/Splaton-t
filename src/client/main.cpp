// Splaton't — game client: screens, netcode, match rendering
#include "raylib.h"

#include "client/assets.h"
#include "client/launch_server.h"
#include "client/ui.h"
#include "engine/net.h"
#include "shared/protocol.h"
#include "shared/sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

constexpr int VW = 480, VH = 270;      // virtual pixel resolution

enum Screen { SC_CONNECT, SC_LOGIN, SC_MENU, SC_LOADOUT, SC_LOBBY, SC_MATCH, SC_RESULTS, SC_SETTINGS };

// ---------------- display settings (persisted next to the exe) ----------------

struct DisplayCfg { int scale = 2; bool fullscreen = false; int music = 55, sfx = 80, showFps = 0; };
static DisplayCfg g_disp;

static std::string settingsPath() { return std::string(GetApplicationDirectory()) + "settings.txt"; }

static void applyVolumes() {
    g_musVol = g_disp.music / 100.0f;
    g_sfxVol = g_disp.sfx / 100.0f;
}

static void loadDisplayCfg() {
    FILE* f = fopen(settingsPath().c_str(), "r");
    if (f) {
        char key[32];
        int val;
        while (fscanf(f, "%31s %d", key, &val) == 2) {
            if (!strcmp(key, "scale")) g_disp.scale = val < 1 ? 1 : (val > 3 ? 3 : val);
            else if (!strcmp(key, "fullscreen")) g_disp.fullscreen = val != 0;
            else if (!strcmp(key, "music")) g_disp.music = val < 0 ? 0 : (val > 100 ? 100 : val);
            else if (!strcmp(key, "sfx")) g_disp.sfx = val < 0 ? 0 : (val > 100 ? 100 : val);
            else if (!strcmp(key, "fps")) g_disp.showFps = val != 0;
        }
        fclose(f);
    }
    applyVolumes();
}

static void saveDisplayCfg() {
    FILE* f = fopen(settingsPath().c_str(), "w");
    if (!f) return;
    fprintf(f, "scale %d\nfullscreen %d\nmusic %d\nsfx %d\nfps %d\n",
        g_disp.scale, g_disp.fullscreen ? 1 : 0, g_disp.music, g_disp.sfx, g_disp.showFps);
    fclose(f);
}

static void applyDisplay() {
    bool borderless = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    if (g_disp.fullscreen && !borderless) {
        ToggleBorderlessWindowed();
    } else if (!g_disp.fullscreen) {
        if (borderless) ToggleBorderlessWindowed();
        SetWindowSize(VW * g_disp.scale, VH * g_disp.scale);
        int mon = GetCurrentMonitor();
        SetWindowPosition((GetMonitorWidth(mon) - VW * g_disp.scale) / 2,
                          (GetMonitorHeight(mon) - VH * g_disp.scale) / 2);
    }
    saveDisplayCfg();
}

// integer upscale with letterboxing (used for the blit and mouse mapping)
static void blitParams(int& s, int& ox, int& oy) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    s = sw / VW < sh / VH ? sw / VW : sh / VH;
    if (s < 1) s = 1;
    ox = (sw - VW * s) / 2;
    oy = (sh - VH * s) / 2;
}

struct RosterEntry {
    bool active = false;
    std::string name;
    u8 team = TEAM_NONE, weapon = 0, skin = 0, hat = 0;
    bool isBot = false;
};
struct SnapPlayer {
    bool present = false;
    float x = 0, y = 0, aim = 0;
    u8 hp = 0, ink = 0, flags = 0;
    float respawn = 0;
    u8 special = 0, kills = 0, deaths = 0;
};
struct ProjView { float x, y; u8 team, kind, aux; };
struct Snapshot {
    u32 tick = 0;
    double tRecv = 0;
    float timeLeft = 0;
    u16 scoreA = 0, scoreB = 0;
    SnapPlayer pl[MAX_PLAYERS];
    std::vector<ProjView> projs;
};
struct FeedItem { std::string text; u8 team; double t; };
struct Particle { Vec2 pos, vel; float life, maxLife, size; Color col; };
struct ResultRow { u8 slot; std::string name; u8 team; u16 kills, deaths, paint, coins; };

struct AutoPilot {
    bool on = false;
    std::string user, pass;
    u8 mode = MODE_TURF;
    float dur = 40;
    double t0 = 0;
    bool sentRegister = false, triedLogin = false, menuActed = false, loadoutSent = false;
    float wpT = 0;
    Vec2 wp{};
    float fireT = 0, ssT = 2.5f;
    int ssN = 0;
    bool reachedMatch = false;
    int myKills = 0;
};

struct LocalInput {
    Vec2 move{};
    float aim = 0;
    bool fire = false, swim = false, bomb = false, special = false;
};

struct App {
    Screen screen = SC_CONNECT;
    bool quit = false;
    double now = 0;

    net::Host host;
    bool netUp = false;
    std::string ip = "127.0.0.1";
    std::string user, pass, status;
    float hostConnectT = -1;            // >=0: waiting to connect to freshly launched server

    // account
    std::string accName;
    std::string token;                  // session resume token
    u8 weapon = 0, skin = 0, sub = 0, hat = 0;
    u32 coins = 0;
    u16 hatsOwned = 1;
    u32 stKills = 0, stDeaths = 0, stWins = 0, stLosses = 0, stMatches = 0, stPaint = 0;
    u8 selWeapon = 0, selSkin = 0, selSub = 0, selHat = 0;
    Screen settingsFrom = SC_CONNECT;

    // lobby
    u8 queueMode = MODE_TURF;
    int queueN = 0;
    float queueCd = -1;

    // match
    GameMap map;
    PaintGrid paint;
    bool paintDirty = false;
    u8 mode = MODE_TURF, colorPair = 0, mapId = 0;
    int mySlot = -1;
    int pingMs = -1;
    float pingTimer = 0;
    float localBarrage = 0;             // splatling prediction
    bool prevBombHeld = false;
    bool escMenu = false;
    u8 intro = 0;                       // 3-2-1-GO deciseconds from the snapshot
    u8 prevIntro = 0;
    float goFlash = 0;
    int lastKillerSlot = -1;            // death cam target
    Vec2 camFocus{};
    float dmgFlash[MAX_PLAYERS] = {};   // white-flash timers on hp drops
    u8 hpPrev[MAX_PLAYERS] = {};
    u8 chatMsg[MAX_PLAYERS];            // speech bubbles (255 = none)
    double chatT[MAX_PLAYERS] = {};
    bool specialWasFull = false;
    RosterEntry roster[MAX_PLAYERS];
    std::deque<Snapshot> snaps;
    Snapshot latest;
    bool haveSnap = false, needPosSync = true;
    u8 prevFlags[MAX_PLAYERS] = {};
    Vec2 myPos;
    float myAim = 0;
    float localCharge = 0;
    bool localFireHeld = false, localSwimHeld = false;
    float inputAcc = 0;
    float matchClock = 0;
    float camShake = 0;
    std::vector<FeedItem> feed;
    std::vector<Particle> parts;

    // results
    u8 resWinner = 0;
    u16 resScoreA = 0, resScoreB = 0;
    std::vector<ResultRow> resRows;
    double resT = 0;

    Assets as;
    AutoPilot ap;
};
static App g;

// ---------------- helpers ----------------

static void sendMsg(BufW& w, bool reliable) {
    if (g.netUp) g.host.send(g.host.clientPeer, w.d, reliable);
}

static u8 myTeam() {
    return (g.mySlot >= 0 && g.roster[g.mySlot].active) ? g.roster[g.mySlot].team : TEAM_NONE;
}

static void resetToConnect(const char* msg) {
    g.host.close();
    g.netUp = false;
    g.screen = SC_CONNECT;
    g.status = msg;
    g.accName.clear();
}

static void spawnBurst(Vec2 pos, Color col, int n, float speed) {
    for (int i = 0; i < n; i++) {
        float a = (float)GetRandomValue(0, 628) / 100.0f;
        float s = speed * (0.4f + (float)GetRandomValue(0, 100) / 160.0f);
        Particle p;
        p.pos = pos;
        p.vel = { cosf(a) * s, sinf(a) * s };
        p.maxLife = p.life = 0.35f + (float)GetRandomValue(0, 100) / 280.0f;
        p.size = 1.0f + (float)GetRandomValue(0, 20) / 10.0f;
        p.col = col;
        g.parts.push_back(p);
    }
}

static void addFeed(const std::string& text, u8 team) {
    g.feed.push_back({ text, team, g.now });
    if (g.feed.size() > 5) g.feed.erase(g.feed.begin());
}

// ---------------- network actions ----------------

static bool doConnect(const std::string& ip) {
    if (!g.host.connectTo(ip, DEFAULT_PORT)) {
        g.status = "Could not reach server at " + ip;
        return false;
    }
    g.netUp = true;
    BufW w;
    w.u8_(C_HELLO);
    w.u32_(PROTOCOL_VERSION);
    sendMsg(w, true);
    g.status = "Connecting...";
    return true;
}

static void sendAuth(u8 msgId) {
    BufW w;
    w.u8_(msgId);
    w.str(g.user);
    w.str(g.pass);
    sendMsg(w, true);
    g.status = "...";
}

static void sendLoadout(u8 weapon, u8 skin, u8 sub, u8 hat) {
    BufW w;
    w.u8_(C_SET_LOADOUT);
    w.u8_(weapon);
    w.u8_(skin);
    w.u8_(sub);
    w.u8_(hat);
    sendMsg(w, true);
}

static void sendQuickChat(u8 msg) {
    BufW w;
    w.u8_(C_QUICKCHAT);
    w.u8_(msg);
    sendMsg(w, true);
}

static void queueFor(u8 mode) {
    BufW w;
    w.u8_(C_QUEUE);
    w.u8_(mode);
    sendMsg(w, true);
    g.queueMode = mode;
    g.queueN = 1;
    g.queueCd = -2;
    g.screen = SC_LOBBY;
    g.as.playS(g.as.sQueue);
}

static void leaveMatchToMenu() {
    BufW w;
    w.u8_(C_LEAVE_MATCH);
    sendMsg(w, true);
    g.screen = SC_MENU;
}

// ---------------- server message handling ----------------

static void readRosterEntry(BufR& r) {
    u8 slot = r.u8_();
    std::string name = r.str();
    u8 team = r.u8_(), weapon = r.u8_(), skin = r.u8_(), hat = r.u8_(), flags = r.u8_();
    if (slot >= MAX_PLAYERS) return;
    g.roster[slot] = { true, name, team, weapon, skin, hat, (flags & PF_BOT) != 0 };
}

static void handleServerMsg(BufR& r) {
    u8 id = r.u8_();
    switch (id) {
    case S_HELLO:
        r.str();
        if (g.screen == SC_CONNECT) {
            g.screen = SC_LOGIN;
            g.status = "";
            if (!g.token.empty()) {     // try resuming the previous session
                BufW w;
                w.u8_(C_RESUME);
                w.str(g.token);
                sendMsg(w, true);
                g.status = "Resuming session...";
            }
        }
        break;
    case S_ERROR:
        g.status = r.str();
        break;
    case S_AUTH_OK: {
        r.u32_();
        g.accName = r.str();
        g.token = r.str();
        g.weapon = r.u8_();
        g.skin = r.u8_();
        g.sub = r.u8_();
        g.hat = r.u8_();
        g.coins = r.u32_();
        g.hatsOwned = r.u16_();
        g.stKills = r.u32_();
        g.stDeaths = r.u32_();
        g.stWins = r.u32_();
        g.stLosses = r.u32_();
        g.stMatches = r.u32_();
        g.stPaint = r.u32_();
        g.selWeapon = g.weapon;
        g.selSkin = g.skin;
        g.selSub = g.sub;
        g.selHat = g.hat;
        g.screen = SC_MENU;
        g.status = "";
        break;
    }
    case S_AUTH_FAIL:
        g.status = r.str();
        g.token.clear();                       // resume failed / bad creds: full login
        if (g.ap.on && !g.ap.triedLogin) {     // name taken from a previous run -> log in
            g.ap.triedLogin = true;
            sendAuth(C_LOGIN);
        }
        break;
    case S_LOADOUT_OK:
        g.weapon = r.u8_();
        g.skin = r.u8_();
        g.sub = r.u8_();
        g.hat = r.u8_();
        if (g.screen == SC_LOADOUT) g.status = "Loadout saved!";
        break;
    case S_BUY_RESULT: {
        u8 ok = r.u8_();
        g.coins = r.u32_();
        g.hatsOwned = r.u16_();
        if (g.screen == SC_LOADOUT)
            g.status = ok ? "Purchased! Click it again to equip, then SAVE." : "Not enough coins";
        if (ok) g.as.playS(g.as.sQueue);
        break;
    }
    case S_PONG: {
        u32 t = r.u32_();
        g.pingMs = (int)((u32)(g.now * 1000.0) - t);
        if (g.pingMs < 0) g.pingMs = 0;
        break;
    }
    case S_QUEUE_STATE:
        r.u8_();
        g.queueN = r.u8_();
        g.queueCd = r.f32_();
        break;
    case S_MATCH_START: {
        g.mode = r.u8_();
        g.colorPair = r.u8_();
        g.mapId = r.u8_();
        g.mySlot = r.u8_();
        float timeLeft = r.f32_();
        for (auto& e : g.roster) e = RosterEntry{};
        u8 n = r.u8_();
        for (int i = 0; i < n; i++) readRosterEntry(r);
        if (!r.ok || g.mySlot >= MAX_PLAYERS) break;
        g.map.load(g.mapId);
        g.paint.clear();
        g.paintDirty = true;
        g.as.buildMatchAssets(g.colorPair, g.map);
        g.snaps.clear();
        g.haveSnap = false;
        g.needPosSync = true;
        g.feed.clear();
        g.parts.clear();
        memset(g.prevFlags, 0, sizeof g.prevFlags);
        g.localCharge = 0;
        g.localBarrage = 0;
        g.pingMs = -1;
        g.matchClock = 0;
        g.escMenu = false;
        g.intro = g.prevIntro = 0;
        g.lastKillerSlot = -1;
        g.specialWasFull = false;
        memset(g.dmgFlash, 0, sizeof g.dmgFlash);
        memset(g.hpPrev, 100, sizeof g.hpPrev);
        memset(g.chatMsg, 255, sizeof g.chatMsg);
        g.latest = Snapshot{};
        g.latest.timeLeft = timeLeft;
        u8 t = myTeam();
        g.myPos = g.map.spawns[t == TEAM_B ? 1 : 0][0];
        g.screen = SC_MATCH;
        g.as.playS(g.as.sStart);
        g.ap.reachedMatch = true;
        break;
    }
    case S_PLAYER_JOIN: {
        size_t at = r.p;
        u8 slot = r.d[at];
        readRosterEntry(r);
        if (slot < MAX_PLAYERS && g.roster[slot].active)
            addFeed(g.roster[slot].name + " joined", g.roster[slot].team);
        break;
    }
    case S_PAINT: {
        u16 n = r.u16_();
        for (int i = 0; i < n && r.ok; i++) {
            u16 idx = r.u16_();
            u8 team = r.u8_();
            g.paint.set(idx, team);
        }
        g.paintDirty = true;
        break;
    }
    case S_EVENT: {
        u8 ev = r.u8_();
        if (ev == EV_KILL) {
            u8 killer = r.u8_(), victim = r.u8_();
            float x = r.f32_(), y = r.f32_();
            u8 kTeam = killer < MAX_PLAYERS ? g.roster[killer].team : TEAM_NONE;
            spawnBurst({ x, y }, teamColor(g.colorPair, kTeam), 16, 70);
            if (killer < MAX_PLAYERS && victim < MAX_PLAYERS)
                addFeed(g.roster[killer].name + " splatted " + g.roster[victim].name, kTeam);
            if (victim == g.mySlot) {
                g.as.playS(g.as.sDeath);
                g.camShake = 5;
                g.lastKillerSlot = killer;
            }
            else if (killer == g.mySlot) { g.as.playS(g.as.sKill); g.ap.myKills++; }
            else g.as.sSplat.play(0.9f + (float)GetRandomValue(0, 20) / 100.0f, 0.6f);
        } else if (ev == EV_CHAT) {
            u8 slot = r.u8_(), msg = r.u8_();
            if (slot < MAX_PLAYERS && msg < CHAT_COUNT && g.roster[slot].active) {
                g.chatMsg[slot] = msg;
                g.chatT[slot] = g.now;
                addFeed(g.roster[slot].name + ": " + CHAT_MSGS[msg], g.roster[slot].team);
                g.as.playS(g.as.sChat, slot == g.mySlot ? 1.1f : 0.95f);
            }
        } else if (ev == EV_SPECIAL) {
            u8 slot = r.u8_(), kind = r.u8_();
            if (slot < MAX_PLAYERS && kind < SP_COUNT && g.roster[slot].active) {
                addFeed(g.roster[slot].name + " used " + SPECIAL_NAMES[kind] + "!", g.roster[slot].team);
                g.as.playS(g.as.sReady, 0.8f);
            }
        } else if (ev == EV_BOOM) {
            u8 team = r.u8_();
            float x = r.f32_(), y = r.f32_();
            u8 radius = r.u8_();
            Color c = teamColor(g.colorPair, team);
            spawnBurst({ x, y }, c, 22, 90);
            spawnBurst({ x, y }, WHITE, 6, 50);
            float d = vlen(Vec2{ x, y } - g.myPos);
            float vol = 1.0f - d / 380.0f;
            if (vol > 0.05f && g.as.audio) {
                SetSoundVolume(g.as.sBoom, vol);
                SetSoundPitch(g.as.sBoom, 0.9f + (float)GetRandomValue(0, 20) / 100.0f);
                PlaySound(g.as.sBoom);
            }
            if (d < radius + 30) g.camShake = std::max(g.camShake, 4.0f);
        }
        break;
    }
    case S_SNAPSHOT: {
        if (g.screen != SC_MATCH) break;
        Snapshot s;
        s.tick = r.u32_();
        s.tRecv = g.now;
        s.timeLeft = r.f32_();
        s.scoreA = r.u16_();
        s.scoreB = r.u16_();
        g.prevIntro = g.intro;
        g.intro = r.u8_();
        u8 n = r.u8_();
        for (int i = 0; i < n && r.ok; i++) {
            u8 slot = r.u8_();
            SnapPlayer p;
            p.present = true;
            p.x = r.f32_();
            p.y = r.f32_();
            p.aim = r.f32_();
            p.hp = r.u8_();
            p.ink = r.u8_();
            p.flags = r.u8_();
            p.respawn = r.u8_() / 10.0f;
            p.special = r.u8_();
            p.kills = r.u8_();
            p.deaths = r.u8_();
            if (slot < MAX_PLAYERS) {
                // white damage flash when someone loses hp
                if (p.hp + 1 < g.hpPrev[slot] && !(p.flags & PF_DEAD)) g.dmgFlash[slot] = 0.18f;
                g.hpPrev[slot] = p.hp;
                s.pl[slot] = p;
            }
        }
        u8 np = r.u8_();
        for (int i = 0; i < np && r.ok; i++) {
            ProjView pv;
            pv.x = r.f32_();
            pv.y = r.f32_();
            pv.team = r.u8_();
            pv.kind = r.u8_();
            pv.aux = r.u8_();
            s.projs.push_back(pv);
        }
        if (!r.ok || g.mySlot < 0) break;
        // fire sounds for other players on flag transitions
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!s.pl[i].present) continue;
            bool fired = (s.pl[i].flags & PF_FIRING) && !(g.prevFlags[i] & PF_FIRING);
            if (fired && i != g.mySlot) {
                float d = vlen(Vec2{ s.pl[i].x, s.pl[i].y } - g.myPos);
                float vol = 1.0f - d / 320.0f;
                if (vol > 0.05f) g.as.sShoot.play(0.9f + (float)GetRandomValue(0, 25) / 100.0f, vol * 0.8f);
            }
            g.prevFlags[i] = s.pl[i].flags;
        }
        g.snaps.push_back(s);
        while (g.snaps.size() > 12) g.snaps.pop_front();
        g.latest = g.snaps.back();
        g.haveSnap = true;

        SnapPlayer& me = g.latest.pl[g.mySlot];
        if (me.present) {
            if (g.needPosSync || (me.flags & PF_DEAD)) {
                g.myPos = { me.x, me.y };
                g.needPosSync = false;
            } else if (vlen(Vec2{ me.x, me.y } - g.myPos) > 48.0f) {
                g.myPos = { me.x, me.y };   // server rejected our movement, snap back
            }
        }
        break;
    }
    case S_MATCH_END: {
        g.resWinner = r.u8_();
        g.resScoreA = r.u16_();
        g.resScoreB = r.u16_();
        g.resRows.clear();
        u8 n = r.u8_();
        for (int i = 0; i < n && r.ok; i++) {
            ResultRow row;
            row.slot = r.u8_();
            row.name = r.str();
            row.team = r.u8_();
            row.kills = r.u16_();
            row.deaths = r.u16_();
            row.paint = r.u16_();
            row.coins = r.u16_();
            g.resRows.push_back(row);
        }
        std::sort(g.resRows.begin(), g.resRows.end(), [](const ResultRow& a, const ResultRow& b) {
            if (a.team != b.team) return a.team < b.team;
            return a.kills > b.kills;
        });
        for (auto& row : g.resRows) {
            if ((int)row.slot != g.mySlot) continue;
            g.stKills += row.kills;
            g.stDeaths += row.deaths;
            g.stPaint += row.paint;
            g.coins += row.coins;
            g.stMatches++;
            if (g.resWinner != TEAM_NONE) {
                if (row.team == g.resWinner) g.stWins++;
                else g.stLosses++;
            }
        }
        g.resT = g.now;
        g.screen = SC_RESULTS;
        g.as.playS(g.as.sEnd);
        break;
    }
    default: break;
    }
}

static void netPump() {
    if (!g.netUp) return;
    std::vector<net::Event> events;
    g.host.poll(events, 0);
    for (auto& e : events) {
        if (e.type == net::Event::DISCONNECT) {
            resetToConnect("Disconnected from server");
        } else if (e.type == net::Event::DATA) {
            BufR r(e.data);
            handleServerMsg(r);
        }
    }
}

// ---------------- match: input + local sim ----------------

static Vector2 virtualMouse() {
    int s, ox, oy;
    blitParams(s, ox, oy);
    Vector2 m = GetMousePosition();
    float x = (m.x - ox) / s, y = (m.y - oy) / s;
    if (x < 0) x = 0; if (x > VW) x = VW;
    if (y < 0) y = 0; if (y > VH) y = VH;
    return { x, y };
}

static bool interpPlayer(int slot, SnapPlayer& out);

static Vec2 camOffset() {
    float cx = g.camFocus.x - VW / 2.0f, cy = g.camFocus.y - VH / 2.0f;
    cx = std::max(0.0f, std::min((float)(WORLD_W - VW), cx));
    cy = std::max(0.0f, std::min((float)(WORLD_H - VH), cy));
    if (g.camShake > 0.2f) {
        cx += (float)GetRandomValue(-100, 100) / 100.0f * g.camShake * 0.4f;
        cy += (float)GetRandomValue(-100, 100) / 100.0f * g.camShake * 0.4f;
    }
    return { cx, cy };
}

static LocalInput realInput() {
    LocalInput li;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) li.move.y -= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) li.move.y += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) li.move.x -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) li.move.x += 1;
    li.move = vnorm(li.move);
    Vec2 cam = camOffset();
    Vector2 vm = virtualMouse();
    li.aim = atan2f(cam.y + vm.y - g.myPos.y, cam.x + vm.x - g.myPos.x);
    li.fire = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    li.swim = IsKeyDown(KEY_LEFT_SHIFT);
    li.bomb = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsKeyDown(KEY_Q);
    li.special = IsKeyDown(KEY_F) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    return li;
}

static LocalInput apInput(float dt) {
    LocalInput li;
    AutoPilot& a = g.ap;
    a.wpT -= dt;
    if (a.wpT <= 0 || vlen(a.wp - g.myPos) < 12.0f) {
        a.wpT = 1.5f + (float)GetRandomValue(0, 150) / 100.0f;
        for (int tries = 0; tries < 20; tries++) {
            Vec2 c = { (float)GetRandomValue(24, WORLD_W - 24), (float)GetRandomValue(24, WORLD_H - 24) };
            if (!g.map.solidAtPx(c.x, c.y) && vlen(c - g.myPos) < 220.0f) { a.wp = c; break; }
        }
    }
    li.move = vnorm(a.wp - g.myPos);
    li.aim = atan2f(li.move.y, li.move.x);
    // aim at nearest visible enemy instead, if any
    if (g.haveSnap) {
        float best = 200;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (i == g.mySlot || !g.latest.pl[i].present || !g.roster[i].active) continue;
            if (g.roster[i].team == myTeam() || (g.latest.pl[i].flags & PF_DEAD)) continue;
            Vec2 e = { g.latest.pl[i].x, g.latest.pl[i].y };
            float d = vlen(e - g.myPos);
            if (d < best && lineOfSight(g.map, g.myPos, e)) {
                best = d;
                li.aim = atan2f(e.y - g.myPos.y, e.x - g.myPos.x);
            }
        }
    }
    a.fireT += dt;
    li.fire = fmodf(a.fireT, 1.3f) < 0.7f;
    li.bomb = fmodf(a.fireT, 4.7f) < 0.1f;   // lob a bomb every ~5s
    li.special = g.haveSnap && g.mySlot >= 0 && g.latest.pl[g.mySlot].special >= 100;
    u8 under = g.paint.atPx(g.myPos.x, g.myPos.y);
    bool lowInk = g.haveSnap && g.mySlot >= 0 && g.latest.pl[g.mySlot].ink < 30;
    li.swim = lowInk && under == myTeam();
    if (li.swim) li.fire = false;
    return li;
}

static void updateMatch(float dt) {
    g.matchClock += dt;
    if (g.camShake > 0) g.camShake = std::max(0.0f, g.camShake - dt * 14);
    for (auto& f : g.dmgFlash)
        if (f > 0) f -= dt;

    if (IsKeyPressed(KEY_ESCAPE) && !g.ap.on) g.escMenu = !g.escMenu;

    bool dead = g.haveSnap && g.mySlot >= 0 && (g.latest.pl[g.mySlot].flags & PF_DEAD);
    LocalInput li = g.ap.on ? apInput(dt) : realInput();
    if (g.escMenu || g.intro > 0) {     // pause overlay / 3-2-1-GO: freeze inputs
        LocalInput frozen;
        frozen.aim = li.aim;
        li = frozen;
    }
    g.localFireHeld = li.fire;
    g.localSwimHeld = li.swim;
    g.myAim = li.aim;

    // intro countdown blips
    if (g.intro != g.prevIntro && g.intro > 0 && (g.intro / 10) != (g.prevIntro / 10))
        g.as.playS(g.as.sQueue, 1.2f);
    if (g.prevIntro > 0 && g.intro == 0) {
        g.as.playS(g.as.sStart);
        g.prevIntro = 0;
        g.camShake = std::max(g.camShake, 2.0f);
        g.goFlash = 0.9f;
    }
    if (g.goFlash > 0) g.goFlash -= dt;

    // quick chat
    if (!g.ap.on && !g.escMenu) {
        if (IsKeyPressed(KEY_C)) sendQuickChat(0);
        if (IsKeyPressed(KEY_X)) sendQuickChat(1);
        if (IsKeyPressed(KEY_V)) sendQuickChat(2);
        if (IsKeyPressed(KEY_B)) sendQuickChat(3);
    }

    // "special ready!" jingle on the rising edge
    if (g.haveSnap && g.mySlot >= 0) {
        bool full = g.latest.pl[g.mySlot].special >= 100;
        if (full && !g.specialWasFull) g.as.playS(g.as.sReady);
        g.specialWasFull = full;
    }

    // camera: follow your killer while dead, otherwise yourself
    g.camFocus = g.myPos;
    if (dead && g.lastKillerSlot >= 0 && g.lastKillerSlot != g.mySlot) {
        SnapPlayer kp;
        if (interpPlayer(g.lastKillerSlot, kp) && !(kp.flags & PF_DEAD))
            g.camFocus = { kp.x, kp.y };
    }

    if (!dead) {
        u8 under = g.paint.atPx(g.myPos.x, g.myPos.y);
        bool localFiring = false;
        if (g.weapon == W_CHARGER) {
            if (li.fire && !li.swim) {
                g.localCharge = std::min(1.0f, g.localCharge + dt / CHARGER_CHARGE_TIME);
                localFiring = true;
            } else {
                if (g.localCharge > 0.15f) g.as.sShoot.play(0.6f, 0.9f);
                g.localCharge = 0;
            }
        } else if (g.weapon == W_SPLATLING) {
            // mirror the server's rev/barrage state for movement + meter
            if (g.localBarrage > 0) {
                g.localBarrage -= dt;
                localFiring = true;
            } else if (li.fire && !li.swim) {
                g.localCharge = std::min(1.0f, g.localCharge + dt / SPLATLING_REV_TIME);
                localFiring = true;
            } else {
                if (g.localCharge > 0.2f) g.localBarrage = g.localCharge * SPLATLING_BARRAGE_MAX;
                g.localCharge = 0;
            }
        } else {
            localFiring = li.fire && !li.swim;
        }
        float sp = speedFor(myTeam(), under, li.swim, localFiring);
        if (!li.swim) sp *= WEAPONS[g.weapon].moveMult;   // weapon weight class
        g.myPos = moveAndSlide(g.map, g.myPos, li.move * (sp * dt));

        // bomb throw feedback (server is authoritative about ink)
        bool bombEdge = li.bomb && !g.prevBombHeld;
        if (bombEdge && !li.swim && g.haveSnap && g.latest.pl[g.mySlot].ink >= SUBS[g.sub].inkCost)
            g.as.playS(g.as.sThrow);

        // local muzzle feedback for rapid-fire weapons
        bool rapid = g.weapon == W_SPLATTERSHOT || g.weapon == W_AEROSPRAY ||
                     (g.weapon == W_SPLATLING && g.localBarrage > 0);
        if (rapid && localFiring && g.haveSnap && g.latest.pl[g.mySlot].ink > 3) {
            static float clickT = 0;
            clickT += dt;
            float interval = WEAPONS[g.weapon].fireInterval;
            if (clickT > interval) {
                clickT = 0;
                g.as.sShoot.play(0.95f + (float)GetRandomValue(0, 15) / 100.0f, 0.9f);
                Vec2 mz = g.myPos + Vec2{ cosf(g.myAim), sinf(g.myAim) } * 10.0f;
                spawnBurst(mz, teamColor(g.colorPair, myTeam()), 2, 30);
            }
        }
    } else {
        g.localCharge = 0;
        g.localBarrage = 0;
    }
    g.prevBombHeld = li.bomb;

    // ping measurement, every 2s
    g.pingTimer -= dt;
    if (g.pingTimer <= 0) {
        g.pingTimer = 2.0f;
        BufW w;
        w.u8_(C_PING);
        w.u32_((u32)(g.now * 1000.0));
        sendMsg(w, false);
    }

    // send input at a fixed rate
    g.inputAcc += dt;
    while (g.inputAcc >= 1.0f / INPUT_RATE) {
        g.inputAcc -= 1.0f / INPUT_RATE;
        if (!dead) {
            BufW w;
            w.u8_(C_INPUT);
            w.f32_(g.myPos.x);
            w.f32_(g.myPos.y);
            w.f32_(g.myAim);
            u8 b = 0;
            if (li.fire) b |= BTN_FIRE;
            if (li.swim) b |= BTN_SWIM;
            if (li.bomb) b |= BTN_BOMB;
            if (li.special) b |= BTN_SPECIAL;
            w.u8_(b);
            sendMsg(w, false);
        }
    }

    for (size_t i = 0; i < g.parts.size();) {
        Particle& p = g.parts[i];
        p.life -= dt;
        p.pos = p.pos + p.vel * dt;
        p.vel = p.vel * (1.0f - 4.0f * dt);
        if (p.life <= 0) { g.parts[i] = g.parts.back(); g.parts.pop_back(); }
        else i++;
    }

    if (g.paintDirty) {
        g.as.updatePaintTex(g.paint, g.colorPair);
        g.paintDirty = false;
    }
}

// interpolated remote player state (render ~120ms in the past)
static bool interpPlayer(int slot, SnapPlayer& out) {
    double rt = g.now - 0.12;
    const Snapshot* s0 = nullptr;
    const Snapshot* s1 = nullptr;
    for (auto& s : g.snaps) {
        if (s.tRecv <= rt) s0 = &s;
        else { s1 = &s; break; }
    }
    if (!s0) s0 = s1;
    if (!s1) s1 = s0;
    if (!s0 || !s0->pl[slot].present) return false;
    if (!s1->pl[slot].present || s1 == s0) { out = s0->pl[slot]; return true; }
    float t = (float)((rt - s0->tRecv) / std::max(0.001, s1->tRecv - s0->tRecv));
    t = std::max(0.0f, std::min(1.0f, t));
    const SnapPlayer& a = s0->pl[slot];
    const SnapPlayer& b = s1->pl[slot];
    out = b;
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    float da = fmodf(b.aim - a.aim + 9.42477f, 6.28318f) - 3.14159f;
    out.aim = a.aim + da * t;
    return true;
}

// ---------------- match rendering ----------------

static void drawTimerAndScore() {
    int secs = (int)std::max(0.0f, g.latest.timeLeft);
    const char* ts = TextFormat("%d:%02d", secs / 60, secs % 60);
    Color tc = secs <= 15 ? Color{ 255, 90, 90, 255 } : WHITE;
    DrawText(ts, VW / 2 - MeasureText(ts, 20) / 2, 4, 20, tc);

    Color ca = teamColor(g.colorPair, TEAM_A), cb = teamColor(g.colorPair, TEAM_B);
    if (g.mode == MODE_TURF) {
        int bw = 130, bx = VW / 2 - bw / 2, by = 26;
        DrawRectangle(bx, by, bw, 7, Color{ 50, 48, 66, 255 });
        int wa = bw * g.latest.scoreA / 1000, wb = bw * g.latest.scoreB / 1000;
        DrawRectangle(bx, by, wa, 7, ca);
        DrawRectangle(bx + bw - wb, by, wb, 7, cb);
        DrawRectangleLines(bx - 1, by - 1, bw + 2, 9, Color{ 20, 18, 32, 255 });
        DrawText(TextFormat("%.1f%%", g.latest.scoreA / 10.0f), bx - 38, by - 1, 10, ca);
        DrawText(TextFormat("%.1f%%", g.latest.scoreB / 10.0f), bx + bw + 6, by - 1, 10, cb);
    } else {
        const char* s = TextFormat("%u", g.latest.scoreA);
        DrawText(s, VW / 2 - 24 - MeasureText(s, 20), 26, 20, ca);
        DrawText("-", VW / 2 - 3, 26, 20, WHITE);
        DrawText(TextFormat("%u", g.latest.scoreB), VW / 2 + 24, 26, 20, cb);
        DrawText(TextFormat("first to %d", TDM_KILL_TARGET), VW / 2 - 26, 46, 10, GRAY);
    }
}

static void drawMatch() {
    Vec2 cam = camOffset();
    ClearBackground(Color{ 16, 14, 26, 255 });

    DrawTextureRec(g.as.mapTex, { cam.x, cam.y, VW, VH }, { 0, 0 }, WHITE);
    DrawTexturePro(g.as.paintTex,
        { cam.x / PAINT_CELL, cam.y / PAINT_CELL, (float)VW / PAINT_CELL, (float)VH / PAINT_CELL },
        { 0, 0, VW, VH }, { 0, 0 }, 0, Fade(WHITE, 0.92f));

    // players
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g.roster[i].active) continue;
        SnapPlayer sp;
        float x, y, aim;
        u8 flags;
        if (i == g.mySlot) {
            if (!g.haveSnap || !g.latest.pl[i].present) continue;
            x = g.myPos.x; y = g.myPos.y; aim = g.myAim;
            flags = g.latest.pl[i].flags;
            if (g.localSwimHeld) flags |= PF_SWIM; else flags &= ~PF_SWIM;
        } else {
            if (!interpPlayer(i, sp)) continue;
            x = sp.x; y = sp.y; aim = sp.aim; flags = sp.flags;
        }
        if (flags & PF_DEAD) continue;
        float sx = x - cam.x, sy = y - cam.y;
        if (sx < -20 || sy < -20 || sx > VW + 20 || sy > VH + 20) continue;

        u8 team = g.roster[i].team;
        int ti = team == TEAM_B ? 1 : 0;
        bool swimming = (flags & PF_SWIM) != 0;
        u8 under = g.paint.atPx(x, y);

        if (swimming && under == team && team != myTeam() && i != g.mySlot) {
            // enemy hidden in their ink: just a ripple
            float r = 3.5f + sinf((float)g.now * 9 + i) * 1.2f;
            DrawCircleLines((int)sx, (int)sy, r, Fade(teamColorDark(g.colorPair, team), 0.6f));
            continue;
        }
        if (swimming && under == team && GetRandomValue(0, 99) < 35)     // swim wake droplets
            spawnBurst({ x, y }, teamColorDark(g.colorPair, team), 1, 18);

        DrawEllipse((int)sx, (int)(sy + 5), 6, 3, Fade(BLACK, 0.25f));    // shadow
        Texture2D tex = swimming ? g.as.squid[ti] : g.as.kid[ti][g.roster[i].weapon][g.roster[i].skin];
        Color tint = swimming && under == team ? Fade(WHITE, 0.85f) : WHITE;
        if (g.dmgFlash[i] > 0) tint = Color{ 255, 140, 140, 255 };        // hit flash
        DrawTexturePro(tex, { 0, 0, 16, 16 }, { sx, sy, 16, 16 }, { 8, 8 }, aim * RAD2DEG, tint);
        if (!swimming && g.roster[i].hat > 0 && g.roster[i].hat < HAT_COUNT)
            DrawTexturePro(g.as.hats[g.roster[i].hat], { 0, 0, 16, 16 },
                           { sx, sy, 16, 16 }, { 8, 8 }, aim * RAD2DEG, tint);

        if (flags & PF_SHIELD) {                                          // bubbler / spawn shield
            float r = 11.0f + sinf((float)g.now * 8 + i) * 1.2f;
            DrawCircle((int)sx, (int)sy, r, Fade(teamColor(g.colorPair, team), 0.18f));
            DrawCircleLines((int)sx, (int)sy, r, Fade(WHITE, 0.7f));
        }
        if (flags & PF_ZOOKA) {                                           // inkzooka aura
            float r = 10.0f + fmodf((float)g.now * 26 + i * 3, 6.0f);
            DrawCircleLines((int)sx, (int)sy, r, Fade(teamColor(g.colorPair, team), 0.8f));
        }
        if (g.chatMsg[i] != 255) {                                        // speech bubble
            if (g.now - g.chatT[i] > 2.5) g.chatMsg[i] = 255;
            else {
                const char* msg = CHAT_MSGS[g.chatMsg[i]];
                int tw = MeasureText(msg, 10);
                DrawRectangle((int)sx - tw / 2 - 3, (int)sy - 26, tw + 6, 13, Fade(WHITE, 0.92f));
                DrawTriangle({ sx - 3, sy - 13 }, { sx + 3, sy - 13 }, { sx, sy - 9 }, Fade(WHITE, 0.92f));
                DrawText(msg, (int)sx - tw / 2, (int)sy - 24, 10, Color{ 30, 26, 44, 255 });
            }
        }

        if (flags & PF_FIRING) {
            Vec2 mz = { x + cosf(aim) * 11, y + sinf(aim) * 11 };
            DrawCircle((int)(mz.x - cam.x), (int)(mz.y - cam.y), 2.2f, Fade(teamColor(g.colorPair, team), 0.9f));
        }
        if (flags & PF_CHARGING) {
            float len = i == g.mySlot ? (CHARGER_MIN_RANGE + (WEAPONS[W_CHARGER].range - CHARGER_MIN_RANGE) * g.localCharge) : 70.0f;
            Vec2 e = { x + cosf(aim) * len, y + sinf(aim) * len };
            DrawLineEx({ sx, sy }, { e.x - cam.x, e.y - cam.y }, 1.0f, Fade(teamColor(g.colorPair, team), 0.4f));
        }
        if (team == myTeam() && i != g.mySlot) {
            const char* nm = g.roster[i].name.c_str();
            DrawText(nm, (int)(sx - MeasureText(nm, 10) / 2), (int)(sy - 18), 10, Fade(WHITE, 0.65f));
        }
    }

    // projectiles + grenades
    for (auto& pv : g.latest.projs) {
        int ti = pv.team == TEAM_B ? 1 : 0;
        float sx = pv.x - cam.x, sy = pv.y - cam.y;
        if (pv.kind == PK_BLOB) {
            DrawTexturePro(g.as.blob[ti], { 0, 0, 8, 8 },
                { sx, sy, 8, 8 }, { 4, 4 }, (float)((int)(g.now * 700) % 360), WHITE);
        } else if (pv.kind == PK_SPLAT_BOMB) {
            DrawEllipse((int)sx, (int)(sy + 4), 4, 2, Fade(BLACK, 0.3f));
            DrawTexturePro(g.as.bomb[ti], { 0, 0, 8, 8 }, { sx, sy, 10, 10 }, { 5, 5 }, 0, WHITE);
            // armed bombs blink faster as the fuse runs out
            if (pv.aux != 255 && pv.aux <= 4 && ((int)(g.now * 12) & 1))
                DrawCircle((int)sx, (int)sy, 5, Fade(WHITE, 0.8f));
        } else if (pv.kind == PK_STORM) {
            Color c = teamColor(g.colorPair, pv.team);
            DrawCircle((int)sx, (int)sy, STORM_RADIUS, Fade(c, 0.10f));
            DrawCircleLines((int)sx, (int)sy, STORM_RADIUS, Fade(c, 0.45f));
            // cloud puffs + falling drips
            for (int k = 0; k < 3; k++) {
                float ox = sinf((float)g.now * 1.3f + k * 2.1f) * 10.0f;
                DrawCircle((int)(sx + ox + (k - 1) * 12), (int)(sy - 4 + (k & 1) * 5), 8.0f,
                           Fade(Color{ 230, 230, 240, 255 }, 0.35f));
            }
            for (int k = 0; k < 4; k++) {
                u32 h = (u32)(g.now * 9) * 31u + k * 977u;
                float rx = (float)(h % (u32)(STORM_RADIUS * 2)) - STORM_RADIUS;
                float ry = (float)((h >> 9) % (u32)(STORM_RADIUS * 2)) - STORM_RADIUS;
                if (rx * rx + ry * ry < STORM_RADIUS * STORM_RADIUS)
                    DrawRectangle((int)(sx + rx), (int)(sy + ry), 2, 3, Fade(c, 0.6f));
            }
        } else { // burst bomb
            DrawCircle((int)sx, (int)sy, 3.5f, teamColor(g.colorPair, pv.team));
            DrawCircle((int)sx, (int)sy, 1.5f, Fade(WHITE, 0.9f));
        }
    }

    for (auto& p : g.parts)
        DrawCircleV({ p.pos.x - cam.x, p.pos.y - cam.y }, p.size * (p.life / p.maxLife), p.col);

    // ---- HUD ----
    drawTimerAndScore();

    // kill feed (below the score readout)
    int fy = 42;
    for (auto& f : g.feed) {
        if (g.now - f.t > 6) continue;
        Color c = teamColor(g.colorPair, f.team);
        DrawText(f.text.c_str(), VW - 4 - MeasureText(f.text.c_str(), 10), fy, 10, Fade(c, 0.95f));
        fy += 12;
    }

    if (g.haveSnap && g.mySlot >= 0 && g.latest.pl[g.mySlot].present) {
        const SnapPlayer& me = g.latest.pl[g.mySlot];

        // ink tank (with a notch at the sub-weapon cost)
        int ix = VW - 20, iy = VH - 64;
        DrawRectangle(ix - 1, iy - 1, 12, 52, Color{ 20, 18, 32, 255 });
        DrawRectangleLines(ix - 2, iy - 2, 14, 54, Color{ 150, 146, 190, 255 });
        int fh = 50 * me.ink / 100;
        DrawRectangle(ix, iy + 50 - fh, 10, fh, teamColor(g.colorPair, myTeam()));
        int notch = iy + 50 - (int)(50 * SUBS[g.sub].inkCost / 100.0f);
        DrawRectangle(ix - 2, notch, 14, 1, me.ink >= SUBS[g.sub].inkCost ? WHITE : Color{ 120, 116, 150, 255 });
        DrawText("INK", ix - 4, iy + 54, 10, GRAY);

        // special meter
        int spx = ix - 56, spy = iy + 44;
        bool spFull = me.special >= 100;
        DrawRectangle(spx - 1, spy - 1, 48, 8, Color{ 20, 18, 32, 255 });
        DrawRectangle(spx, spy, 46 * me.special / 100, 6,
            spFull && ((int)(g.now * 6) & 1) ? WHITE : teamColor(g.colorPair, myTeam()));
        DrawRectangleLines(spx - 2, spy - 2, 50, 10, Color{ 150, 146, 190, 255 });
        DrawText(spFull ? "SPECIAL [F]!" : SPECIAL_NAMES[WEAPON_SPECIAL[g.weapon]],
                 spx - 2, spy - 12, 10, spFull ? WHITE : GRAY);

        // damage vignette
        if (me.hp < 95 && !(me.flags & PF_DEAD)) {
            float a = (1.0f - me.hp / 100.0f) * 0.5f;
            DrawRectangle(0, 0, VW, 8, Fade(RED, a));
            DrawRectangle(0, VH - 8, VW, 8, Fade(RED, a));
            DrawRectangle(0, 0, 8, VH, Fade(RED, a));
            DrawRectangle(VW - 8, 0, 8, VH, Fade(RED, a));
        }

        // charge / rev meter
        if ((g.weapon == W_CHARGER || g.weapon == W_SPLATLING) && g.localCharge > 0.01f) {
            Vector2 vm = virtualMouse();
            DrawRectangle((int)vm.x - 11, (int)vm.y + 10, 22, 4, Color{ 20, 18, 32, 220 });
            DrawRectangle((int)vm.x - 10, (int)vm.y + 11, (int)(20 * g.localCharge), 2,
                g.localCharge >= 1 ? WHITE : teamColor(g.colorPair, myTeam()));
        }

        // respawn overlay (dimmed less so the death cam stays visible)
        if (me.flags & PF_DEAD) {
            DrawRectangle(0, 0, VW, VH, Fade(BLACK, 0.35f));
            const char* t1 = "SPLATTED!";
            DrawText(t1, VW / 2 - MeasureText(t1, 30) / 2, VH / 2 - 40, 30, Color{ 255, 90, 90, 255 });
            if (g.lastKillerSlot >= 0 && g.roster[g.lastKillerSlot].active && g.lastKillerSlot != g.mySlot) {
                const char* by = TextFormat("by %s (%s)", g.roster[g.lastKillerSlot].name.c_str(),
                                            WEAPONS[g.roster[g.lastKillerSlot].weapon].name);
                DrawText(by, VW / 2 - MeasureText(by, 10) / 2, VH / 2 - 6, 10,
                         teamColor(g.colorPair, g.roster[g.lastKillerSlot].team));
            }
            const char* t2 = TextFormat("respawning in %.1f", me.respawn);
            DrawText(t2, VW / 2 - MeasureText(t2, 10) / 2, VH / 2 + 10, 10, WHITE);
        }
    }

    // 3-2-1-GO
    if (g.intro > 0) {
        DrawRectangle(0, 0, VW, VH, Fade(BLACK, 0.35f));
        int num = (g.intro + 9) / 10;
        const char* t = TextFormat("%d", num);
        DrawText(t, VW / 2 - MeasureText(t, 40) / 2, VH / 2 - 24, 40, WHITE);
        const char* mn = TextFormat("%s on %s", g.mode == MODE_TURF ? "TURF WAR" : "TEAM DEATHMATCH",
                                    MAP_NAMES[g.mapId % MAP_COUNT]);
        DrawText(mn, VW / 2 - MeasureText(mn, 10) / 2, VH / 2 + 22, 10, GRAY);
    } else if (g.prevIntro > 0 && g.matchClock < 60.0f) {
        // brief GO! right after the countdown (prevIntro clears on the sound trigger)
    }

    // TAB scoreboard (autopilot flashes it periodically so tests can screenshot it)
    bool showBoard = IsKeyDown(KEY_TAB) || (g.ap.on && fmodf((float)g.now, 10.0f) > 7.5f);
    if (showBoard && g.haveSnap) {
        Rectangle panel = { VW / 2.0f - 110, 50, 220, 130 };
        DrawRectangleRec(panel, Fade(Color{ 16, 14, 28, 255 }, 0.92f));
        DrawRectangleLinesEx(panel, 1, Color{ 110, 106, 150, 255 });
        DrawText("K", (int)panel.x + 158, (int)panel.y + 6, 10, GRAY);
        DrawText("D", (int)panel.x + 188, (int)panel.y + 6, 10, GRAY);
        int y = (int)panel.y + 20;
        for (int pass = 0; pass < 2; pass++) {
            u8 team = pass == 0 ? TEAM_A : TEAM_B;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!g.roster[i].active || g.roster[i].team != team || !g.latest.pl[i].present) continue;
                Color c = teamColor(g.colorPair, team);
                DrawRectangle((int)panel.x + 8, y + 2, 6, 6, c);
                DrawText(g.roster[i].name.c_str(), (int)panel.x + 20, y, 10,
                         i == g.mySlot ? WHITE : Color{ 205, 202, 224, 255 });
                DrawText(TextFormat("%u", g.latest.pl[i].kills), (int)panel.x + 156, y, 10, WHITE);
                DrawText(TextFormat("%u", g.latest.pl[i].deaths), (int)panel.x + 186, y, 10, WHITE);
                y += 13;
            }
        }
    }

    // minimap
    {
        int mx = 6, my = VH - 38;
        DrawRectangle(mx - 2, my - 2, 52, 36, Color{ 20, 18, 32, 220 });
        DrawTextureEx(g.as.mapTex, { (float)mx, (float)my }, 0, 1.0f / 16.0f, Fade(WHITE, 0.9f));
        DrawTextureEx(g.as.paintTex, { (float)mx, (float)my }, 0, 0.25f, Fade(WHITE, 0.85f));
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!g.roster[i].active || !g.latest.pl[i].present || (g.latest.pl[i].flags & PF_DEAD)) continue;
            float px = i == g.mySlot ? g.myPos.x : g.latest.pl[i].x;
            float py = i == g.mySlot ? g.myPos.y : g.latest.pl[i].y;
            Color c = i == g.mySlot ? WHITE : teamColor(g.colorPair, g.roster[i].team);
            DrawRectangle(mx + (int)(px / 16), my + (int)(py / 16), 2, 2, c);
        }
        DrawRectangleLines(mx - 2, my - 2, 52, 36, Color{ 110, 106, 150, 255 });
    }

    if (g.goFlash > 0) {
        int size = 40;
        DrawText("GO!", VW / 2 - MeasureText("GO!", size) / 2, VH / 2 - 20, size,
                 Fade(WHITE, std::min(1.0f, g.goFlash * 2)));
    }

    if (g.matchClock < 6.0f) {
        const char* mapLabel = TextFormat("- %s -", MAP_NAMES[g.mapId % MAP_COUNT]);
        DrawText(mapLabel, VW / 2 - MeasureText(mapLabel, 10) / 2, VH - 28, 10, Fade(WHITE, 0.9f));
        if (!g.ap.on) {
            const char* hint = "WASD move · LMB shoot · RMB/Q bomb · F special · SHIFT swim · C/X/V/B chat · ESC menu";
            DrawText(hint, VW / 2 - MeasureText(hint, 10) / 2, VH - 14, 10, Fade(WHITE, 0.8f));
        }
    }

    if (g.pingMs >= 0)
        DrawText(TextFormat("%d ms", g.pingMs), 6, 4, 10, Fade(GRAY, 0.9f));
    if (g_disp.showFps)
        DrawText(TextFormat("%d fps", GetFPS()), 6, 16, 10, Fade(GREEN, 0.8f));

    // ESC pause overlay (the match keeps running server-side)
    if (g.escMenu) {
        DrawRectangle(0, 0, VW, VH, Fade(BLACK, 0.6f));
        Rectangle panel = { VW / 2.0f - 70, 80, 140, 106 };
        uiPanel(panel);
        DrawText("PAUSED", VW / 2 - MeasureText("PAUSED", 20) / 2, (int)panel.y + 8, 20, WHITE);
        if (uiButton({ panel.x + 10, panel.y + 32, 120, 20 }, "RESUME"))
            g.escMenu = false;
        if (uiButton({ panel.x + 10, panel.y + 56, 120, 20 }, "SETTINGS")) {
            g.settingsFrom = SC_MATCH;
            g.screen = SC_SETTINGS;
        }
        if (uiButton({ panel.x + 10, panel.y + 80, 120, 20 }, "LEAVE MATCH")) {
            g.escMenu = false;
            leaveMatchToMenu();
        }
    } else {
        // crosshair
        Vector2 vm = virtualMouse();
        Color cc = Fade(WHITE, 0.9f);
        DrawLine((int)vm.x - 4, (int)vm.y, (int)vm.x + 4, (int)vm.y, cc);
        DrawLine((int)vm.x, (int)vm.y - 4, (int)vm.x, (int)vm.y + 4, cc);
        DrawCircleLines((int)vm.x, (int)vm.y, 6, Fade(teamColor(g.colorPair, myTeam()), 0.7f));
    }
}

// ---------------- menu screens ----------------

static void drawMenuBackdrop(const char* subtitle) {
    ClearBackground(Color{ 18, 16, 30, 255 });
    // drifting ink blobs
    for (int i = 0; i < 5; i++) {
        float t = (float)g.now * 0.12f + i * 1.7f;
        float x = fmodf(i * 137.0f + sinf(t) * 40 + (float)g.now * (6 + i), VW + 160.0f) - 80;
        float y = 40 + (i * 53) % 200 + cosf(t * 0.7f) * 12;
        Color c = i % 2 ? Color{ 46, 80, 246, 18 } : Color{ 250, 114, 20, 18 };
        DrawCircle((int)x, (int)y, 34 + (i % 3) * 14, c);
    }
    const char* title = "SPLATON'T";
    int tw = MeasureText(title, 40);
    DrawText(title, VW / 2 - tw / 2 + 2, 22, 40, Color{ 20, 18, 32, 255 });
    DrawText(title, VW / 2 - tw / 2, 20, 40, Color{ 250, 114, 20, 255 });
    // drips under the logo
    DrawCircle(VW / 2 - tw / 2 + 12, 58, 3, Color{ 250, 114, 20, 255 });
    DrawRectangle(VW / 2 - tw / 2 + 10, 50, 4, 8, Color{ 250, 114, 20, 255 });
    DrawCircle(VW / 2 + tw / 2 - 30, 62, 2, Color{ 250, 114, 20, 255 });
    DrawRectangle(VW / 2 + tw / 2 - 31, 52, 3, 10, Color{ 250, 114, 20, 255 });
    if (subtitle)
        DrawText(subtitle, VW / 2 - MeasureText(subtitle, 10) / 2, 66, 10, GRAY);
}

static void screenConnect() {
    drawMenuBackdrop("a very unofficial ink-em-up");
    Rectangle panel = { VW / 2.0f - 90, 92, 180, 152 };
    uiPanel(panel);
    DrawText("SERVER ADDRESS", (int)panel.x + 10, (int)panel.y + 10, 10, GRAY);
    uiTextBox({ panel.x + 10, panel.y + 24, 160, 18 }, g.ip, 1, false, 32, "127.0.0.1");
    if (uiButton({ panel.x + 10, panel.y + 50, 160, 20 }, "CONNECT"))
        doConnect(g.ip);
    if (uiButton({ panel.x + 10, panel.y + 74, 160, 20 }, "HOST + PLAY (local server)")) {
        if (launchLocalServer()) {
            g.status = "Starting local server...";
            g.hostConnectT = 1.1f;
        } else {
            g.status = "Could not start splatont_server.exe";
        }
    }
    if (uiButton({ panel.x + 10, panel.y + 98, 77, 20 }, "SETTINGS")) {
        g.settingsFrom = SC_CONNECT;
        g.screen = SC_SETTINGS;
    }
    if (uiButton({ panel.x + 93, panel.y + 98, 77, 20 }, "QUIT"))
        g.quit = true;
    if (!g.status.empty())
        DrawText(g.status.c_str(), VW / 2 - MeasureText(g.status.c_str(), 10) / 2, (int)panel.y + 128, 10,
                 Color{ 255, 170, 90, 255 });

    if (g.hostConnectT >= 0) {
        g.hostConnectT -= GetFrameTime();
        if (g.hostConnectT < 0 && !g.netUp)
            if (!doConnect("127.0.0.1"))
                g.status = "Local server did not come up - check splatont_server.exe";
    }
}

static void screenLogin() {
    drawMenuBackdrop("log in or create an account");
    Rectangle panel = { VW / 2.0f - 90, 95, 180, 140 };
    uiPanel(panel);
    DrawText("NAME", (int)panel.x + 10, (int)panel.y + 8, 10, GRAY);
    uiTextBox({ panel.x + 10, panel.y + 20, 160, 18 }, g.user, 1, false, 16, "SquidKid42");
    DrawText("PASSWORD", (int)panel.x + 10, (int)panel.y + 42, 10, GRAY);
    uiTextBox({ panel.x + 10, panel.y + 54, 160, 18 }, g.pass, 2, true, 32, "");
    bool canGo = g.user.size() >= 3 && g.pass.size() >= 3;
    if (uiButton({ panel.x + 10, panel.y + 80, 77, 20 }, "LOG IN", canGo) ||
        (canGo && IsKeyPressed(KEY_ENTER)))
        sendAuth(C_LOGIN);
    if (uiButton({ panel.x + 93, panel.y + 80, 77, 20 }, "REGISTER", canGo))
        sendAuth(C_REGISTER);
    if (uiButton({ panel.x + 10, panel.y + 104, 160, 18 }, "BACK"))
        resetToConnect("");
    if (!g.status.empty())
        DrawText(g.status.c_str(), VW / 2 - MeasureText(g.status.c_str(), 10) / 2, (int)panel.y + 128, 10,
                 Color{ 255, 170, 90, 255 });
}

static void screenMenu() {
    drawMenuBackdrop(nullptr);
    const char* hello = TextFormat("welcome back, %s", g.accName.c_str());
    DrawText(hello, VW / 2 - MeasureText(hello, 10) / 2, 66, 10, Color{ 200, 200, 220, 255 });

    Rectangle panel = { VW / 2.0f - 90, 84, 180, 152 };
    uiPanel(panel);
    if (uiButton({ panel.x + 10, panel.y + 10, 160, 22 }, "PLAY - TURF WAR  (4v4)", true, Color{ 150, 70, 20, 255 }))
        queueFor(MODE_TURF);
    if (uiButton({ panel.x + 10, panel.y + 38, 160, 22 }, "PLAY - TEAM DEATHMATCH", true, Color{ 30, 60, 140, 255 }))
        queueFor(MODE_TDM);
    if (uiButton({ panel.x + 10, panel.y + 66, 160, 22 }, "LOADOUT")) {
        g.selWeapon = g.weapon;
        g.selSkin = g.skin;
        g.selSub = g.sub;
        g.status = "";
        g.screen = SC_LOADOUT;
    }
    if (uiButton({ panel.x + 10, panel.y + 94, 77, 22 }, "SETTINGS")) {
        g.settingsFrom = SC_MENU;
        g.screen = SC_SETTINGS;
    }
    if (uiButton({ panel.x + 93, panel.y + 94, 77, 22 }, "LOG OUT")) {
        g.token.clear();
        resetToConnect("");
    }

    const char* coins = TextFormat("$ %u coins", g.coins);
    DrawText(coins, VW / 2 - MeasureText(coins, 10) / 2, (int)panel.y - 12, 10, Color{ 252, 206, 48, 255 });
    const char* stats = TextFormat("splats %u   deaths %u   W/L %u/%u   matches %u",
        g.stKills, g.stDeaths, g.stWins, g.stLosses, g.stMatches);
    DrawText(stats, VW / 2 - MeasureText(stats, 10) / 2, (int)panel.y + 128, 10, GRAY);
    const char* turf = TextFormat("lifetime turf inked: %u cells", g.stPaint);
    DrawText(turf, VW / 2 - MeasureText(turf, 10) / 2, VH - 22, 10, Color{ 120, 118, 150, 255 });
}

static void screenLoadout() {
    drawMenuBackdrop("pick your kit");
    // 6 weapons, 2 rows x 3 columns
    for (int i = 0; i < W_COUNT; i++) {
        int col = i % 3, row = i / 3;
        Rectangle card = { 10.0f + col * 156, 76.0f + row * 66, 148, 62 };
        bool sel = g.selWeapon == i;
        DrawRectangleRec(card, sel ? Color{ 46, 40, 72, 245 } : Color{ 24, 22, 38, 235 });
        DrawRectangleLinesEx(card, sel ? 2.0f : 1.0f, sel ? Color{ 255, 200, 60, 255 } : Color{ 90, 86, 130, 255 });
        DrawTextureEx(g.as.kidPreview[i][g.selSkin], { card.x + 4, card.y + 15 }, 0, 2, WHITE);
        const WeaponDef& wd = WEAPONS[i];
        DrawText(wd.name, (int)card.x + 40, (int)card.y + 5, 10, WHITE);
        struct { const char* n; float v; } bars[3] = {
            { "PWR", wd.dmg / 120.0f },
            { "SPD", 1.0f - wd.fireInterval / 0.9f },
            { "RNG", wd.range / 300.0f },
        };
        for (int bidx = 0; bidx < 3; bidx++) {
            int by = (int)card.y + 19 + bidx * 13;
            DrawText(bars[bidx].n, (int)card.x + 40, by, 10, Color{ 130, 128, 160, 255 });
            DrawRectangle((int)card.x + 68, by + 2, 72, 5, Color{ 40, 38, 56, 255 });
            DrawRectangle((int)card.x + 68, by + 2, (int)(72 * std::max(0.08f, std::min(1.0f, bars[bidx].v))), 5,
                          Color{ 250, 114, 20, 255 });
        }
        if (g_ui.pressed && CheckCollisionPointRec(g_ui.mouse, card)) {
            g.selWeapon = (u8)i;
            g.as.playS(g.as.sClick);
        }
    }

    // sub weapons
    DrawText("SUB", 10, 216, 10, GRAY);
    for (int s = 0; s < SUB_COUNT; s++) {
        Rectangle card = { 36.0f + s * 96, 210, 90, 22 };
        bool sel = g.selSub == s;
        DrawRectangleRec(card, sel ? Color{ 46, 40, 72, 245 } : Color{ 24, 22, 38, 235 });
        DrawRectangleLinesEx(card, sel ? 2.0f : 1.0f, sel ? Color{ 255, 200, 60, 255 } : Color{ 90, 86, 130, 255 });
        DrawText(SUBS[s].name, (int)(card.x + card.width / 2 - MeasureText(SUBS[s].name, 10) / 2),
                 (int)card.y + 6, 10, WHITE);
        if (g_ui.pressed && CheckCollisionPointRec(g_ui.mouse, card)) {
            g.selSub = (u8)s;
            g.as.playS(g.as.sClick);
        }
    }

    // skins
    DrawText("STYLE", 246, 216, 10, GRAY);
    for (int s = 0; s < SKIN_COUNT; s++) {
        Rectangle sw = { 284.0f + s * 24, 210, 20, 20 };
        DrawRectangleRec(sw, skinColor((u8)s));
        DrawRectangleLinesEx(sw, g.selSkin == s ? 2.0f : 1.0f, g.selSkin == s ? WHITE : Color{ 90, 86, 130, 255 });
        if (g_ui.pressed && CheckCollisionPointRec(g_ui.mouse, sw)) {
            g.selSkin = (u8)s;
            g.as.playS(g.as.sClick);
        }
    }

    // hats: owned = click to equip; locked = click to buy
    DrawText("HAT", 10, 242, 10, GRAY);
    for (int h = 0; h < HAT_COUNT; h++) {
        Rectangle hw = { 36.0f + h * 30, 236, 26, 24 };
        bool owned = (g.hatsOwned & (1 << h)) != 0;
        bool sel = g.selHat == h;
        DrawRectangleRec(hw, owned ? Color{ 34, 32, 52, 245 } : Color{ 18, 17, 28, 245 });
        DrawRectangleLinesEx(hw, sel ? 2.0f : 1.0f,
            sel ? Color{ 255, 200, 60, 255 } : (owned ? Color{ 110, 106, 150, 255 } : Color{ 60, 58, 76, 255 }));
        if (h == 0) DrawText("-", (int)hw.x + 11, (int)hw.y + 7, 10, GRAY);
        else DrawTexturePro(g.as.hats[h], { 0, 0, 16, 16 }, { hw.x + 5, hw.y + 4, 16, 16 }, { 0, 0 }, 0,
                            owned ? WHITE : Fade(WHITE, 0.35f));
        if (!owned)
            DrawText(TextFormat("%u", HAT_PRICES[h]), (int)hw.x + 2, (int)hw.y + 25, 10,
                     g.coins >= HAT_PRICES[h] ? Color{ 252, 206, 48, 255 } : Color{ 110, 106, 130, 255 });
        if (g_ui.pressed && CheckCollisionPointRec(g_ui.mouse, hw)) {
            if (owned) {
                g.selHat = (u8)h;
                g.as.playS(g.as.sClick);
            } else if (g.coins >= HAT_PRICES[h]) {
                BufW w;
                w.u8_(C_BUY_HAT);
                w.u8_((u8)h);
                sendMsg(w, true);
                g.status = "Buying...";
            } else {
                g.status = "Not enough coins - play matches to earn more";
            }
        }
    }
    const char* coins = TextFormat("$ %u", g.coins);
    DrawText(coins, VW - 12 - MeasureText(coins, 10), 62, 10, Color{ 252, 206, 48, 255 });

    if (uiButton({ VW - 96.0f, 210, 42, 20 }, "SAVE")) {
        sendLoadout(g.selWeapon, g.selSkin, g.selSub, g.selHat);
        g.status = "Saving...";
    }
    if (uiButton({ VW - 50.0f, 210, 42, 20 }, "BACK"))
        g.screen = SC_MENU;
    if (!g.status.empty())
        DrawText(g.status.c_str(), 200, 246, 10, Color{ 150, 240, 150, 255 });
}

static void screenSettings() {
    drawMenuBackdrop("settings");
    Rectangle panel = { VW / 2.0f - 100, 84, 200, 176 };
    uiPanel(panel);

    DrawText("WINDOW SIZE", (int)panel.x + 12, (int)panel.y + 8, 10, GRAY);
    for (int s = 1; s <= 3; s++) {
        Rectangle b = { panel.x + 12 + (s - 1) * 60, panel.y + 20, 56, 18 };
        bool current = !g_disp.fullscreen && g_disp.scale == s;
        if (uiButton(b, TextFormat("%dx", s), !g_disp.fullscreen,
                     current ? Color{ 150, 70, 20, 255 } : Color{ 70, 74, 105, 255 })) {
            g_disp.scale = s;
            applyDisplay();
        }
    }
    if (uiButton({ panel.x + 12, panel.y + 42, 176, 18 },
                 g_disp.fullscreen ? "FULLSCREEN: ON" : "FULLSCREEN: OFF", true,
                 g_disp.fullscreen ? Color{ 150, 70, 20, 255 } : Color{ 70, 74, 105, 255 })) {
        g_disp.fullscreen = !g_disp.fullscreen;
        applyDisplay();
    }
    DrawText(TextFormat("output: %dx%d", GetScreenWidth(), GetScreenHeight()),
             (int)panel.x + 12, (int)panel.y + 64, 10, Color{ 120, 118, 150, 255 });

    DrawText(TextFormat("MUSIC %d", g_disp.music), (int)panel.x + 12, (int)panel.y + 80, 10, GRAY);
    float mv = g_disp.music / 100.0f;
    if (uiSlider({ panel.x + 78, panel.y + 78, 110, 12 }, mv, 101)) {
        g_disp.music = (int)(mv * 100 + 0.5f);
        applyVolumes();
        saveDisplayCfg();
    }
    DrawText(TextFormat("SFX   %d", g_disp.sfx), (int)panel.x + 12, (int)panel.y + 96, 10, GRAY);
    float sv = g_disp.sfx / 100.0f;
    if (uiSlider({ panel.x + 78, panel.y + 94, 110, 12 }, sv, 102)) {
        g_disp.sfx = (int)(sv * 100 + 0.5f);
        applyVolumes();
        saveDisplayCfg();
    }

    if (uiButton({ panel.x + 12, panel.y + 112, 176, 18 },
                 g_disp.showFps ? "SHOW FPS: ON" : "SHOW FPS: OFF", true,
                 g_disp.showFps ? Color{ 150, 70, 20, 255 } : Color{ 70, 74, 105, 255 })) {
        g_disp.showFps = !g_disp.showFps;
        saveDisplayCfg();
    }

    if (uiButton({ panel.x + 12, panel.y + 136, 176, 18 },
                 g.settingsFrom == SC_MATCH ? "BACK TO MATCH" : "BACK"))
        g.screen = g.settingsFrom;
}

static void screenLobby() {
    drawMenuBackdrop(nullptr);
    const char* mn = g.queueMode == MODE_TURF ? "TURF WAR" : "TEAM DEATHMATCH";
    DrawText(mn, VW / 2 - MeasureText(mn, 20) / 2, 95, 20, WHITE);

    DrawTexturePro(g.as.squidPreview, { 0, 0, 16, 16 },
        { VW / 2.0f, 150, 48, 48 }, { 24, 24 }, (float)(g.now * 130), WHITE);

    const char* q = TextFormat("players queued: %d  (bots fill the rest)", g.queueN);
    DrawText(q, VW / 2 - MeasureText(q, 10) / 2, 185, 10, GRAY);

    const char* st;
    if (g.queueCd >= 0) st = TextFormat("match starts in %d...", (int)g.queueCd + 1);
    else if (g.queueCd < -1.5f) st = "contacting server...";
    else st = "waiting for the current match to finish...";
    DrawText(st, VW / 2 - MeasureText(st, 10) / 2, 200, 10, Color{ 255, 200, 60, 255 });

    if (uiButton({ VW / 2.0f - 50, 220, 100, 20 }, "CANCEL")) {
        BufW w;
        w.u8_(C_LEAVE_QUEUE);
        sendMsg(w, true);
        g.screen = SC_MENU;
    }
}

static void screenResults() {
    ClearBackground(Color{ 18, 16, 30, 255 });
    u8 mine = TEAM_NONE;
    for (auto& row : g.resRows)
        if ((int)row.slot == g.mySlot) mine = row.team;

    const char* banner;
    Color bc;
    if (g.resWinner == TEAM_NONE) { banner = "DRAW!"; bc = WHITE; }
    else if (g.resWinner == mine) { banner = "VICTORY!"; bc = teamColor(g.colorPair, mine); }
    else { banner = "DEFEAT..."; bc = Color{ 150, 150, 170, 255 }; }
    DrawText(banner, VW / 2 - MeasureText(banner, 30) / 2, 16, 30, bc);

    Color ca = teamColor(g.colorPair, TEAM_A), cb = teamColor(g.colorPair, TEAM_B);
    const char* sc = g.mode == MODE_TURF
        ? TextFormat("%.1f%%  vs  %.1f%%", g.resScoreA / 10.0f, g.resScoreB / 10.0f)
        : TextFormat("%u  vs  %u", g.resScoreA, g.resScoreB);
    DrawText(sc, VW / 2 - MeasureText(sc, 20) / 2, 50, 20, WHITE);
    DrawRectangle(VW / 2 - 90, 74, 60, 4, ca);
    DrawRectangle(VW / 2 + 30, 74, 60, 4, cb);

    int y = 88;
    DrawText("PLAYER", 70, y, 10, GRAY);
    DrawText("SPLATS", 240, y, 10, GRAY);
    DrawText("DEATHS", 290, y, 10, GRAY);
    DrawText("INKED", 342, y, 10, GRAY);
    DrawText("COINS", 392, y, 10, GRAY);
    y += 13;
    for (auto& row : g.resRows) {
        Color c = teamColor(g.colorPair, row.team);
        if ((int)row.slot == g.mySlot) DrawRectangle(64, y - 1, 372, 12, Color{ 60, 56, 90, 160 });
        DrawRectangle(70, y + 2, 6, 6, c);
        DrawText(row.name.c_str(), 82, y, 10, (int)row.slot == g.mySlot ? WHITE : Color{ 210, 208, 226, 255 });
        DrawText(TextFormat("%u", row.kills), 244, y, 10, WHITE);
        DrawText(TextFormat("%u", row.deaths), 294, y, 10, WHITE);
        DrawText(TextFormat("%u", row.paint), 342, y, 10, WHITE);
        if (row.coins)
            DrawText(TextFormat("+%u", row.coins), 392, y, 10, Color{ 252, 206, 48, 255 });
        y += 13;
    }

    bool cont = uiButton({ VW / 2.0f - 50, VH - 26.0f, 100, 20 }, "CONTINUE") || IsKeyPressed(KEY_ENTER);
    if (g.ap.on && g.now - g.resT > 3.0) cont = true;
    if (cont) g.screen = SC_MENU;
}

// ---------------- autopilot glue ----------------

static void autopilotDrive() {
    AutoPilot& a = g.ap;
    if (!a.on) return;
    switch (g.screen) {
    case SC_CONNECT:
        if (!g.netUp && g.hostConnectT < 0) {
            if (!doConnect("127.0.0.1")) {
                printf("AUTOTEST: FAIL connect\n");
                g.quit = true;
            }
        }
        break;
    case SC_LOGIN:
        if (!a.sentRegister) {
            a.sentRegister = true;
            g.user = a.user;
            g.pass = a.pass;
            sendAuth(C_REGISTER);
        }
        break;
    case SC_MENU:
        if (!a.menuActed) {
            a.menuActed = true;
            if (a.mode == 8) {                   // mode 8: park on the settings screen (UI testing)
                g.settingsFrom = SC_MENU;
                g.screen = SC_SETTINGS;
                break;
            }
            if (a.mode >= MODE_COUNT) {          // mode 9: park on the loadout screen (UI testing)
                g.selWeapon = g.weapon;
                g.selSkin = g.skin;
                g.selSub = g.sub;
                g.selHat = g.hat;
                g.screen = SC_LOADOUT;
                break;
            }
            if (!a.loadoutSent) {
                a.loadoutSent = true;
                sendLoadout((u8)GetRandomValue(0, W_COUNT - 1), (u8)GetRandomValue(0, SKIN_COUNT - 1),
                            (u8)GetRandomValue(0, SUB_COUNT - 1), 0);
            }
            queueFor(a.mode);
        }
        break;
    case SC_LOBBY:
    case SC_MATCH:
    case SC_RESULTS:
        a.menuActed = false;
        break;
    default: break;
    }
}

static void autopilotPost(float dt) {
    AutoPilot& a = g.ap;
    if (!a.on) return;
    a.ssT -= dt;
    if (a.ssT <= 0) {
        a.ssT = 5.0f;
        TakeScreenshot(TextFormat("shot_%s_%02d.png", a.user.c_str(), a.ssN++));
    }
    if (g.now - a.t0 >= a.dur) {
        int myA = 0, myB = 0;
        g.paint.counts(myA, myB);
        printf("AUTOTEST: %s screen=%d reachedMatch=%d kills=%d paintA=%d paintB=%d\n",
            a.reachedMatch ? "OK" : "FAIL", (int)g.screen, (int)a.reachedMatch, a.myKills, myA, myB);
        g.quit = true;
    }
}

// ---------------- main ----------------

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--auto") == 0 && i + 4 < argc) {
            g.ap.on = true;
            g.ap.user = argv[i + 1];
            g.ap.pass = argv[i + 2];
            g.ap.mode = (u8)atoi(argv[i + 3]);
            g.ap.dur = (float)atof(argv[i + 4]);
        }
    }

    SetTraceLogLevel(LOG_WARNING);
    loadDisplayCfg();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);     // any size works; blit letterboxes
    InitWindow(VW * g_disp.scale, VH * g_disp.scale, "Splaton't");
    SetWindowMinSize(VW, VH);
    if (g_disp.fullscreen) applyDisplay();
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    RenderTexture2D rt = LoadRenderTexture(VW, VH);
    SetTextureFilter(rt.texture, TEXTURE_FILTER_POINT);

    if (!net::init()) return 1;
    g.as.init();
    g_ui.click = g.as.audio ? &g.as.sClick : nullptr;
    g.ap.t0 = GetTime();

    while (!WindowShouldClose() && !g.quit) {
        g.now = GetTime();
        float dt = GetFrameTime();
        netPump();
        autopilotDrive();

        Vector2 vm = virtualMouse();
        uiBegin(vm, IsMouseButtonDown(MOUSE_BUTTON_LEFT), IsMouseButtonPressed(MOUSE_BUTTON_LEFT), g.now);

        if (g.screen == SC_MATCH && !g.escMenu) HideCursor();
        else ShowCursor();
        g.as.updateMusic(g.screen == SC_MATCH);

        BeginTextureMode(rt);
        switch (g.screen) {
        case SC_CONNECT: screenConnect(); break;
        case SC_LOGIN: screenLogin(); break;
        case SC_MENU: screenMenu(); break;
        case SC_LOADOUT: screenLoadout(); break;
        case SC_LOBBY: screenLobby(); break;
        case SC_MATCH: updateMatch(dt); drawMatch(); break;
        case SC_RESULTS: screenResults(); break;
        case SC_SETTINGS: screenSettings(); break;
        }
        EndTextureMode();

        int bs, box, boy;
        blitParams(bs, box, boy);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(rt.texture, { 0, 0, (float)VW, (float)-VH },
            { (float)box, (float)boy, (float)(VW * bs), (float)(VH * bs) }, { 0, 0 }, 0, WHITE);
        EndDrawing();

        autopilotPost(dt);
    }

    g.host.close();
    net::shutdown();
    CloseWindow();
    return g.ap.on && !g.ap.reachedMatch ? 4 : 0;
}
