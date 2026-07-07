// Splaton't — shared constants and definitions (client + server)
#pragma once
#include <cstdint>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i32 = int32_t;
using i64 = int64_t;

constexpr u32 PROTOCOL_VERSION = 3;
constexpr u16 DEFAULT_PORT = 27777;

// timing
constexpr int   TICK_RATE = 30;                 // server sim ticks/s
constexpr float TICK_DT = 1.0f / TICK_RATE;
constexpr int   SNAP_RATE = 20;                 // snapshots/s
constexpr int   INPUT_RATE = 30;                // client input sends/s

// world: tile map + higher-resolution paint grid
constexpr int TILE = 16;                        // px per tile
constexpr int MAPW = 48;                        // tiles
constexpr int MAPH = 32;
constexpr int WORLD_W = MAPW * TILE;            // 768 px
constexpr int WORLD_H = MAPH * TILE;            // 512 px
constexpr int PAINT_CELL = 4;                   // px per paint cell
constexpr int PW = WORLD_W / PAINT_CELL;        // 192
constexpr int PH = WORLD_H / PAINT_CELL;        // 128

// maps
constexpr int MAP_COUNT = 3;
constexpr const char* MAP_NAMES[MAP_COUNT] = { "Dockside", "Warehouse", "Plaza" };

// tiles
enum : u8 { T_FLOOR = 0, T_WALL = 1, T_PAD_A = 2, T_PAD_B = 3 };

// teams
enum : u8 { TEAM_NONE = 0, TEAM_A = 1, TEAM_B = 2 };

// player
constexpr float PLAYER_R = 5.5f;               // collision radius, px
constexpr float HP_MAX = 100.0f;
constexpr float INK_MAX = 100.0f;
constexpr float RESPAWN_TIME = 3.0f;
constexpr float SPEED_RUN = 95.0f;             // px/s on neutral or own ink
constexpr float SPEED_SWIM = 185.0f;           // squid form in own ink
constexpr float SPEED_ENEMY_INK = 36.0f;
constexpr float SPEED_SHOOT_MULT = 0.72f;      // slowdown while firing
constexpr float INK_REGEN = 9.0f;              // per s, normal
constexpr float INK_REGEN_SWIM = 48.0f;        // per s, swimming in own ink

// match
constexpr int   MAX_PLAYERS = 8;               // 4v4
constexpr int   TEAM_SIZE = 4;
constexpr float TURF_TIME = 180.0f;
constexpr float TDM_TIME = 240.0f;
constexpr int   TDM_KILL_TARGET = 25;
constexpr float LOBBY_COUNTDOWN = 6.0f;
constexpr float RESULTS_TIME = 9.0f;
constexpr int   MAX_ROOMS = 3;                 // concurrent matches on one server

enum : u8 { MODE_TURF = 0, MODE_TDM = 1, MODE_COUNT = 2 };

// ---------------- weapons ----------------
enum : u8 {
    W_SPLATTERSHOT = 0, W_AEROSPRAY, W_BLASTER, W_ROLLER, W_CHARGER, W_SPLATLING, W_COUNT
};

struct WeaponDef {
    const char* name;
    const char* desc;
    float fireInterval;   // s between shots / swings
    float dmg;            // per pellet (blaster: direct hit)
    float inkCost;        // per shot
    float projSpeed;      // px/s
    float range;          // px of projectile travel
    float paintR;         // px, splat radius at impact/landing
    float spreadDeg;      // aim cone
    int   pellets;        // projectiles per trigger
    float moveMult;       // run-speed multiplier (weapon weight)
};

// Balance intent (time-to-kill at effective range / role):
//  Splattershot  0.25s  all-rounder baseline
//  Aerospray     0.32s + high spread — worst duels, best paint output
//  Blaster       one-shot on direct hit but 0.63s between shots, short range
//  Roller        one swing kills point-blank only; paints a swath while held
//  Charger       1.0s charge one-shot at long range; weak tap shots
//  Splatling     0.40s sustained after a 0.9s rev; slowest movement
// Charger row holds full-charge numbers; splatling row is per-barrage-shot.
constexpr WeaponDef WEAPONS[W_COUNT] = {
    { "Splattershot", "Rapid fire. Reliable.",      0.125f, 34.0f,  2.0f, 260.0f, 135.0f,  9.0f,  5.0f, 1, 1.00f },
    { "Aerospray",    "Spray and pray. Paints.",    0.080f, 24.0f,  1.4f, 250.0f, 110.0f, 10.0f,  9.5f, 1, 1.05f },
    { "Blaster",      "Slow, explosive shots.",     0.62f, 120.0f, 10.0f, 230.0f, 105.0f, 12.0f,  1.0f, 1, 0.92f },
    { "Ink Roller",   "Paints as you go. Splashy.", 0.70f,  50.0f,  9.0f, 210.0f,  75.0f, 11.0f, 25.0f, 3, 0.97f },
    { "Splat Charger","Charge up. Snipe far.",      0.30f, 100.0f, 18.0f,   0.0f, 300.0f,  7.0f,  0.0f, 1, 0.90f },
    { "Splatling",    "Rev up, then unleash.",      0.10f,  28.0f,  1.1f, 265.0f, 150.0f,  8.0f,  6.0f, 1, 0.85f },
};

constexpr float CHARGER_CHARGE_TIME = 1.0f;
constexpr float CHARGER_MIN_DMG = 35.0f;
constexpr float CHARGER_MIN_RANGE = 110.0f;
constexpr float CHARGER_MIN_INK = 4.0f;
constexpr float ROLLER_ROLL_PAINT_R = 13.0f;   // px painted under player
constexpr float ROLLER_ROLL_DRAIN = 13.0f;     // ink/s while rolling
constexpr float BLASTER_AOE_R = 26.0f;         // px, splash radius
constexpr float BLASTER_AOE_DMG = 55.0f;
constexpr float SPLATLING_REV_TIME = 0.9f;     // s to full rev
constexpr float SPLATLING_REV_DRAIN = 6.0f;    // ink/s while revving
constexpr float SPLATLING_BARRAGE_MAX = 2.2f;  // s of fire at full rev
constexpr float PROJ_DRIP_EVERY = 22.0f;       // px travelled between drip splats
constexpr float PROJ_DRIP_R = 3.5f;            // px

// ---------------- sub weapons (grenades) ----------------
enum : u8 { SUB_SPLAT_BOMB = 0, SUB_BURST_BOMB, SUB_COUNT };

struct SubDef {
    const char* name;
    const char* desc;
    float inkCost;
    float throwSpeed;
    float fuse;           // s after landing (0 = detonate on impact)
    float rInner, dmgInner;
    float rOuter, dmgOuter;
    float paintR;
};

// Splat bomb: expensive, delayed, lethal zone control.
// Burst bomb: cheap chip damage, instant pop, weak paint.
constexpr SubDef SUBS[SUB_COUNT] = {
    { "Splat Bomb", "Lobbed. Big delayed boom.", 55.0f, 240.0f, 0.9f, 22.0f, 150.0f, 40.0f, 50.0f, 26.0f },
    { "Burst Bomb", "Pops on impact. Cheap.",    30.0f, 270.0f, 0.0f, 12.0f,  60.0f, 24.0f, 35.0f, 16.0f },
};
constexpr float BOMB_COOLDOWN = 1.1f;          // min s between throws
constexpr float BURST_BOMB_RANGE = 110.0f;     // px before airburst

// ---------------- special weapons (charged by painting turf) ----------------
enum : u8 { SP_INKZOOKA = 0, SP_INKSTORM, SP_BUBBLE, SP_COUNT };
constexpr const char* SPECIAL_NAMES[SP_COUNT] = { "Inkzooka", "Ink Storm", "Bubbler" };
// which special each main weapon charges
constexpr u8 WEAPON_SPECIAL[W_COUNT] = {
    SP_INKZOOKA,   // Splattershot
    SP_INKSTORM,   // Aerospray
    SP_BUBBLE,     // Blaster
    SP_BUBBLE,     // Roller
    SP_INKSTORM,   // Charger
    SP_INKZOOKA,   // Splatling
};
constexpr float SPECIAL_COST = 360.0f;         // paint cells to fill the meter
constexpr float SPECIAL_KILL_POINTS = 25.0f;   // meter points per splat
constexpr float ZOOKA_TIME = 6.0f;             // s of Inkzooka mode
constexpr float ZOOKA_INTERVAL = 0.55f;        // s between zooka blasts
constexpr float ZOOKA_RANGE = 320.0f;
constexpr float ZOOKA_DMG = 120.0f;
constexpr float ZOOKA_PAINT_R = 9.0f;
constexpr float STORM_TIME = 6.0f;             // s of rain
constexpr float STORM_RADIUS = 48.0f;
constexpr float STORM_DPS = 14.0f;
constexpr float STORM_THROW_RANGE = 140.0f;
constexpr float BUBBLE_TIME = 5.0f;            // s of invulnerability
constexpr float SPAWN_PROTECT_TIME = 2.5f;     // s of post-respawn shield (ends when you fire)
constexpr float MATCH_INTRO_TIME = 3.2f;       // 3-2-1-GO freeze at match start

// ---------------- progression: coins + hats ----------------
constexpr int HAT_COUNT = 5;
constexpr const char* HAT_NAMES[HAT_COUNT] = { "None", "Cap", "Cone", "Phones", "Crown" };
constexpr u32 HAT_PRICES[HAT_COUNT] = { 0, 250, 500, 900, 2000 };

// ---------------- quick chat ----------------
constexpr int CHAT_COUNT = 4;
constexpr const char* CHAT_MSGS[CHAT_COUNT] = { "Booyah!", "This way!", "Help!", "Nice!" };

constexpr int SKIN_COUNT = 4;

// input buttons bitmask (client -> server)
enum : u8 {
    BTN_FIRE = 1 << 0,
    BTN_SWIM = 1 << 1,
    BTN_BOMB = 1 << 2,
    BTN_SPECIAL = 1 << 3,
};

// snapshot player flags (server -> client)
enum : u8 {
    PF_SWIM = 1 << 0,
    PF_DEAD = 1 << 1,
    PF_FIRING = 1 << 2,
    PF_CHARGING = 1 << 3,   // also splatling rev
    PF_ROLLING = 1 << 4,
    PF_BOT = 1 << 5,
    PF_SHIELD = 1 << 6,     // bubbler or spawn protection
    PF_ZOOKA = 1 << 7,      // inkzooka active
};

// snapshot entity kinds (projectile list)
enum : u8 { PK_BLOB = 0, PK_SPLAT_BOMB = 1, PK_BURST_BOMB = 2, PK_STORM = 3 };

// color pairs per match (client maps these to RGB)
constexpr int COLOR_PAIR_COUNT = 3; // 0: orange/blue, 1: pink/green, 2: yellow/purple
