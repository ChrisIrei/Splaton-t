// Splaton't server — authoritative match simulation (turf war + TDM) with bots
#pragma once
#include "shared/defs.h"
#include "shared/protocol.h"
#include "shared/sim.h"
#include <random>
#include <string>
#include <vector>

struct BotBrain {
    float repathT = 0;
    std::vector<Vec2> path;     // tile-center waypoints
    size_t pathI = 0;
    Vec2 goal;
    float goalT = 0;            // give up on a goal after a while
    int targetSlot = -1;
    float retargetT = 0;
    float burstT = 0;           // fire pattern timer
    float aimCur = 0;
    float bombT = 4;            // cooldown before next bomb throw
};

struct PlayerState {
    bool active = false, isBot = false;
    void* peer = nullptr;
    int64_t accountId = 0;
    std::string name;
    u8 team = TEAM_NONE, weapon = W_SPLATTERSHOT, skin = 0, hat = 0;
    int spawnIdx = 0;

    Vec2 pos;
    float aim = 0;
    float hp = HP_MAX, ink = INK_MAX;
    bool dead = false;
    float respawnT = 0;
    u8 buttons = 0, prevButtons = 0;
    float fireCd = 0;
    float charge = 0;                   // charger charge / splatling rev
    float barrage = 0;                  // splatling: seconds of auto-fire left
    bool charging = false, swimming = false, rolling = false;
    float firingVisT = 0;
    float regenDelay = 0;               // hp regen kicks in when this reaches 0
    u8 sub = SUB_SPLAT_BOMB;
    float bombCd = 0;
    float special = 0;                  // meter points; full at SPECIAL_COST
    float zookaT = 0;                   // inkzooka seconds left
    float shieldT = 0;                  // bubbler seconds left
    float spawnShieldT = 0;             // post-respawn protection (breaks on firing)
    float lastInputAge = 0;             // humans: buttons cleared if inputs stop

    u16 kills = 0, deaths = 0;
    u32 paintCells = 0;
    BotBrain bot;
};

struct Projectile {
    Vec2 pos, vel;
    float travelled = 0, maxTravel = 100;
    u8 team = 0, owner = 0;
    float dmg = 0, paintR = 6;
    float dripAcc = 0;
    bool explodes = false;              // blaster shells splash on any death
};

struct Grenade {
    Vec2 pos, vel;
    u8 team = 0, owner = 0, sub = SUB_SPLAT_BOMB;
    float fuse = 0;                     // counts down once landed
    bool landed = false;
    float travelled = 0;
};

struct Storm { Vec2 pos; u8 team, owner; float t; };

struct KillEvent { u8 killer, victim; Vec2 pos; };
struct BoomEvent { u8 team; Vec2 pos; u8 radius; };
struct ChatEvent { u8 slot, msg; };
struct SpecialEvent { u8 slot, kind; };

struct Match {
    GameMap map;
    PaintGrid paint;
    u8 mode = MODE_TURF;
    u8 colorPair = 0;
    float timeLeft = 0;
    u32 tick = 0;
    u16 scoreA = 0, scoreB = 0;     // TDM: kills; turf: live permille coverage
    PlayerState players[MAX_PLAYERS];
    std::vector<Projectile> projs;
    std::vector<Grenade> grenades;
    std::vector<Storm> storms;
    std::vector<u16> paintDeltas;   // cell indices changed since last drain
    std::vector<KillEvent> killEvents;
    std::vector<BoomEvent> boomEvents;
    std::vector<ChatEvent> chatEvents;
    std::vector<SpecialEvent> specialEvents;
    float introT = 0;               // 3-2-1-GO freeze at match start
    int paintableTotal = 1;
    bool ended = false;
    u8 winner = TEAM_NONE;
    float scoreRefreshT = 0;
    std::mt19937 rng{ 12345 };

    int tdmKillTarget = TDM_KILL_TARGET;

    void start(u8 mode_, int mapId, u32 seed, float matchTime, int killTarget);
    // returns slot or -1; picks the team with fewer players
    int addPlayer(const std::string& name, u8 weapon, u8 skin, u8 sub, u8 hat, bool isBot, void* peer, int64_t accountId);
    // human replaces a bot on the emptier-of-humans team; returns slot or -1
    int replaceBotWithHuman(const std::string& name, u8 weapon, u8 skin, u8 sub, u8 hat, void* peer, int64_t accountId);
    void humanLeft(int slot);       // converts the slot to a bot
    void fillWithBots();
    void handleInput(int slot, Vec2 pos, float aimRad, u8 buttons);
    void update(float dt);

    void writeSnapshot(BufW& w);
    void writePaintDeltas(BufW& w);             // drains accumulated deltas
    void writeFullPaint(BufW& w);               // all painted cells (for late joiners)
    void writeRosterEntry(BufW& w, int slot);

    int humanCount() const;

private:
    void respawn(PlayerState& p);
    void updateWeapon(int slot, PlayerState& p, float dt);
    void spawnPellets(PlayerState& p, int slot, const WeaponDef& wd, int pellets, float spreadDeg,
                      float speed, float range, float dmg, float paintR, bool explodes = false);
    void fireChargerRay(PlayerState& p, int slot, float t);
    void fireZookaRay(PlayerState& p, int slot);
    void activateSpecial(int slot, PlayerState& p);
    void updateStorms(float dt);
    // paint-cell attribution + special-meter charge for cells added since `before`
    void creditPaint(int owner, size_t before);
    void throwBomb(PlayerState& p, int slot);
    void explodeAt(Vec2 pos, u8 team, int owner, float rInner, float dmgInner,
                   float rOuter, float dmgOuter, float paintR, int skipSlot = -1);
    void damagePlayer(int victimSlot, float dmg, int killerSlot);
    void updateProjectiles(float dt);
    void updateGrenades(float dt);
    void botThink(int slot, PlayerState& p, float dt);
    void botPathTo(PlayerState& p, Vec2 goal);
    Vec2 botMoveDir(PlayerState& p);
    float frand(float a, float b) { return a + (b - a) * (float)(rng() & 0xffff) / 65535.0f; }
};
