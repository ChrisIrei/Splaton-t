#include "server/game.h"
#include <algorithm>
#include <cmath>
#include <queue>

static float flerp(float a, float b, float t) { return a + (b - a) * t; }
static float angDiff(float a, float b) {
    float d = fmodf(b - a + 3.14159265f, 6.2831853f);
    if (d < 0) d += 6.2831853f;
    return d - 3.14159265f;
}

static const char* BOT_NAMES[] = {
    "Squidbert", "Inkabelle", "TakoTom", "KrakenJr", "Cephalofred",
    "Woomy", "Blooper", "SushiRoll", "Glublub", "MsTentacles",
    "InkyMcSplat", "Squidward2",
};

void Match::start(u8 mode_, int mapId, u32 seed) {
    rng.seed(seed);
    map.load(mapId);
    paint.clear();
    mode = mode_;
    colorPair = (u8)(rng() % COLOR_PAIR_COUNT);
    timeLeft = (mode == MODE_TURF) ? TURF_TIME : TDM_TIME;
    tick = 0;
    scoreA = scoreB = 0;
    for (auto& p : players) p = PlayerState{};
    projs.clear();
    grenades.clear();
    paintDeltas.clear();
    killEvents.clear();
    boomEvents.clear();
    paintableTotal = paint.paintableTotal(map);
    if (paintableTotal < 1) paintableTotal = 1;
    ended = false;
    winner = TEAM_NONE;
    scoreRefreshT = 0;
}

int Match::humanCount() const {
    int n = 0;
    for (const auto& p : players)
        if (p.active && !p.isBot) n++;
    return n;
}

int Match::addPlayer(const std::string& name, u8 weapon, u8 skin, u8 sub, bool isBot, void* peer, int64_t accountId) {
    int slot = -1;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (!players[i].active) { slot = i; break; }
    if (slot < 0) return -1;

    int nA = 0, nB = 0;
    for (const auto& p : players)
        if (p.active) (p.team == TEAM_A ? nA : nB)++;

    PlayerState& p = players[slot];
    p = PlayerState{};
    p.active = true;
    p.isBot = isBot;
    p.peer = peer;
    p.accountId = accountId;
    p.name = name;
    p.weapon = weapon < W_COUNT ? weapon : W_SPLATTERSHOT;
    p.skin = skin < SKIN_COUNT ? skin : 0;
    p.sub = sub < SUB_COUNT ? sub : SUB_SPLAT_BOMB;
    p.team = (nA <= nB) ? TEAM_A : TEAM_B;
    p.spawnIdx = (p.team == TEAM_A ? nA : nB) % TEAM_SIZE;
    respawn(p);
    return slot;
}

int Match::replaceBotWithHuman(const std::string& name, u8 weapon, u8 skin, u8 sub, void* peer, int64_t accountId) {
    int humA = 0, humB = 0;
    for (const auto& p : players)
        if (p.active && !p.isBot) (p.team == TEAM_A ? humA : humB)++;
    u8 prefer = (humA <= humB) ? TEAM_A : TEAM_B;

    int slot = -1;
    for (int pass = 0; pass < 2 && slot < 0; pass++) {
        u8 want = pass == 0 ? prefer : (prefer == TEAM_A ? TEAM_B : TEAM_A);
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (players[i].active && players[i].isBot && players[i].team == want) { slot = i; break; }
    }
    if (slot < 0) return -1;

    PlayerState& p = players[slot];
    u8 team = p.team;
    int spawnIdx = p.spawnIdx;
    p = PlayerState{};
    p.active = true;
    p.peer = peer;
    p.accountId = accountId;
    p.name = name;
    p.weapon = weapon < W_COUNT ? weapon : W_SPLATTERSHOT;
    p.skin = skin < SKIN_COUNT ? skin : 0;
    p.sub = sub < SUB_COUNT ? sub : SUB_SPLAT_BOMB;
    p.team = team;
    p.spawnIdx = spawnIdx;
    respawn(p);
    return slot;
}

void Match::humanLeft(int slot) {
    if (slot < 0 || slot >= MAX_PLAYERS || !players[slot].active) return;
    PlayerState& p = players[slot];
    p.isBot = true;
    p.peer = nullptr;
    p.accountId = 0;
    p.buttons = 0;
    p.bot = BotBrain{};
    p.bot.aimCur = p.aim;
}

void Match::fillWithBots() {
    int nameOff = (int)(rng() % 12);
    for (int i = 0; humanCount() + i < MAX_PLAYERS; i++) {
        std::string nm = BOT_NAMES[(nameOff + i) % 12];
        if (addPlayer(nm, (u8)(rng() % W_COUNT), (u8)(rng() % SKIN_COUNT),
                      (u8)(rng() % SUB_COUNT), true, nullptr, 0) < 0) break;
    }
}

void Match::respawn(PlayerState& p) {
    p.pos = map.spawns[p.team - 1][p.spawnIdx];
    p.hp = HP_MAX;
    p.ink = INK_MAX;
    p.dead = false;
    p.respawnT = 0;
    p.buttons = p.prevButtons = 0;
    p.charge = 0;
    p.barrage = 0;
    p.charging = p.swimming = p.rolling = false;
    p.fireCd = 0.35f;   // brief no-fire after spawning
}

void Match::handleInput(int slot, Vec2 pos, float aimRad, u8 buttons) {
    if (slot < 0 || slot >= MAX_PLAYERS) return;
    PlayerState& p = players[slot];
    if (!p.active || p.isBot || p.dead || ended) return;

    // trust the client's movement but clamp it to plausible speed + walls
    Vec2 d = pos - p.pos;
    float maxd = SPEED_SWIM * 2.5f / INPUT_RATE;
    float dl = vlen(d);
    if (dl > maxd) pos = p.pos + vnorm(d) * maxd;
    if (pos.x < PLAYER_R) pos.x = PLAYER_R;
    if (pos.y < PLAYER_R) pos.y = PLAYER_R;
    if (pos.x > WORLD_W - PLAYER_R) pos.x = WORLD_W - PLAYER_R;
    if (pos.y > WORLD_H - PLAYER_R) pos.y = WORLD_H - PLAYER_R;
    p.pos = depenetrate(map, pos);
    p.aim = aimRad;
    p.buttons = buttons;
}

void Match::spawnPellets(PlayerState& p, int slot, const WeaponDef& wd, int pellets, float spreadDeg,
                         float speed, float range, float dmg, float paintR, bool explodes) {
    for (int i = 0; i < pellets; i++) {
        float a = p.aim + frand(-spreadDeg, spreadDeg) * 3.14159265f / 180.0f;
        Projectile pr;
        Vec2 dir = { cosf(a), sinf(a) };
        pr.pos = p.pos + dir * (PLAYER_R + 3.0f);
        pr.vel = dir * (speed * frand(0.92f, 1.08f));
        pr.maxTravel = range * frand(0.85f, 1.1f);
        pr.team = p.team;
        pr.owner = (u8)slot;
        pr.dmg = dmg;
        pr.paintR = paintR;
        pr.explodes = explodes;
        projs.push_back(pr);
    }
}

void Match::explodeAt(Vec2 pos, u8 team, int owner, float rInner, float dmgInner,
                      float rOuter, float dmgOuter, float paintR, int skipSlot) {
    size_t before = paintDeltas.size();
    paint.paintCircle(map, pos.x, pos.y, paintR, team, paintDeltas);
    if (owner >= 0 && owner < MAX_PLAYERS)
        players[owner].paintCells += (u32)(paintDeltas.size() - before);
    for (int j = 0; j < MAX_PLAYERS; j++) {
        if (j == skipSlot) continue;
        PlayerState& q = players[j];
        if (!q.active || q.dead || q.team == team) continue;
        float d = vlen(q.pos - pos);
        if (d > rOuter) continue;
        if (!lineOfSight(map, pos, q.pos)) continue;
        damagePlayer(j, d <= rInner ? dmgInner : dmgOuter, owner);
    }
    boomEvents.push_back({ team, pos, (u8)(rOuter > 255 ? 255 : (int)rOuter) });
}

void Match::throwBomb(PlayerState& p, int slot) {
    const SubDef& sd = SUBS[p.sub];
    p.ink -= sd.inkCost;
    p.bombCd = BOMB_COOLDOWN;
    Grenade gr;
    Vec2 dir = { cosf(p.aim), sinf(p.aim) };
    gr.pos = p.pos + dir * (PLAYER_R + 3.0f);
    gr.vel = dir * sd.throwSpeed;
    gr.team = p.team;
    gr.owner = (u8)slot;
    gr.sub = p.sub;
    gr.fuse = sd.fuse;
    grenades.push_back(gr);
}

void Match::updateGrenades(float dt) {
    for (size_t i = 0; i < grenades.size(); ) {
        Grenade& gr = grenades[i];
        const SubDef& sd = SUBS[gr.sub];
        bool boom = false;

        if (gr.sub == SUB_SPLAT_BOMB) {
            if (!gr.landed) {
                Vec2 npos = gr.pos + gr.vel * dt;
                if (map.solidAtPx(npos.x, npos.y)) {
                    gr.vel = gr.vel * -0.35f;           // bounce off walls
                } else {
                    gr.pos = npos;
                }
                gr.vel = gr.vel * (1.0f - 2.6f * dt);   // friction slide
                if (vlen(gr.vel) < 30.0f) gr.landed = true;
            } else {
                gr.fuse -= dt;
                if (gr.fuse <= 0) boom = true;
            }
        } else { // burst bomb: pops on any contact
            Vec2 step = gr.vel * dt;
            Vec2 npos = gr.pos + step;
            gr.travelled += vlen(step);
            if (map.solidAtPx(npos.x, npos.y)) boom = true;
            else gr.pos = npos;
            if (gr.travelled >= BURST_BOMB_RANGE) boom = true;
            for (int j = 0; j < MAX_PLAYERS && !boom; j++) {
                PlayerState& q = players[j];
                if (!q.active || q.dead || q.team == gr.team) continue;
                if (vlen(q.pos - gr.pos) < PLAYER_R + 3.0f) boom = true;
            }
        }

        if (boom) {
            explodeAt(gr.pos, gr.team, gr.owner, sd.rInner, sd.dmgInner, sd.rOuter, sd.dmgOuter, sd.paintR);
            grenades[i] = grenades.back();
            grenades.pop_back();
        } else i++;
    }
}

void Match::fireChargerRay(PlayerState& p, int slot, float t) {
    const WeaponDef& wd = WEAPONS[W_CHARGER];
    float range = flerp(CHARGER_MIN_RANGE, wd.range, t);
    float dmg = flerp(CHARGER_MIN_DMG, wd.dmg, t);
    float pr = flerp(4.0f, wd.paintR, t);
    bool pierce = t >= 0.99f;
    Vec2 dir = { cosf(p.aim), sinf(p.aim) };
    Vec2 pos = p.pos + dir * (PLAYER_R + 2.0f);
    float step = 4.0f, painted = 0;
    for (float d = 0; d < range; d += step) {
        pos = pos + dir * step;
        if (map.solidAtPx(pos.x, pos.y)) break;
        painted += step;
        if (painted >= 8.0f) {
            painted = 0;
            size_t before = paintDeltas.size();
            paint.paintCircle(map, pos.x, pos.y, pr, p.team, paintDeltas);
            p.paintCells += (u32)(paintDeltas.size() - before);
        }
        bool hitSomeone = false;
        for (int j = 0; j < MAX_PLAYERS; j++) {
            PlayerState& q = players[j];
            if (!q.active || q.dead || q.team == p.team) continue;
            Vec2 dq = q.pos - pos;
            if (dq.x * dq.x + dq.y * dq.y < (PLAYER_R + 2.5f) * (PLAYER_R + 2.5f)) {
                damagePlayer(j, dmg, slot);
                hitSomeone = true;
            }
        }
        if (hitSomeone && !pierce) break;
    }
    p.firingVisT = 0.18f;
}

void Match::updateWeapon(int slot, PlayerState& p, float dt) {
    const WeaponDef& wd = WEAPONS[p.weapon];
    bool held = (p.buttons & BTN_FIRE) != 0;
    bool pressed = held && !(p.prevButtons & BTN_FIRE);
    bool released = !held && (p.prevButtons & BTN_FIRE);
    p.fireCd -= dt;
    if (p.firingVisT > 0) p.firingVisT -= dt;

    if (p.swimming) {           // squid form can't attack
        p.charging = p.rolling = false;
        p.charge = 0;
        p.barrage = 0;
        return;
    }

    switch (p.weapon) {
    case W_SPLATTERSHOT:
    case W_AEROSPRAY:
    case W_BLASTER:
        if (held && p.fireCd <= 0 && p.ink >= wd.inkCost) {
            p.fireCd = wd.fireInterval;
            p.ink -= wd.inkCost;
            spawnPellets(p, slot, wd, wd.pellets, wd.spreadDeg, wd.projSpeed, wd.range, wd.dmg, wd.paintR,
                         p.weapon == W_BLASTER);
            p.firingVisT = p.weapon == W_BLASTER ? 0.2f : 0.12f;
        }
        break;
    case W_SPLATLING:
        if (p.barrage > 0) {                            // unloading
            p.charging = false;
            p.barrage -= dt;
            if (p.fireCd <= 0 && p.ink >= wd.inkCost) {
                p.fireCd = wd.fireInterval;
                p.ink -= wd.inkCost;
                spawnPellets(p, slot, wd, 1, wd.spreadDeg, wd.projSpeed, wd.range, wd.dmg, wd.paintR);
                p.firingVisT = 0.1f;
            }
            if (p.ink < wd.inkCost) p.barrage = 0;
        } else if (held) {                              // revving up
            p.charging = true;
            p.charge += dt / SPLATLING_REV_TIME;
            if (p.charge > 1) p.charge = 1;
            p.ink -= SPLATLING_REV_DRAIN * dt;
            if (p.ink < 0) p.ink = 0;
        } else if (p.charging) {                        // released: unleash
            p.charging = false;
            if (p.charge > 0.2f) p.barrage = p.charge * SPLATLING_BARRAGE_MAX;
            p.charge = 0;
        }
        break;
    case W_ROLLER:
        p.rolling = held && p.ink > 1.0f;
        if (pressed && p.fireCd <= 0 && p.ink >= wd.inkCost) {
            p.fireCd = wd.fireInterval;
            p.ink -= wd.inkCost;
            spawnPellets(p, slot, wd, wd.pellets, wd.spreadDeg, wd.projSpeed, wd.range, wd.dmg, wd.paintR);
            p.firingVisT = 0.2f;
        }
        break;
    case W_CHARGER:
        if (held) {
            p.charging = true;
            p.charge += dt / CHARGER_CHARGE_TIME;
            if (p.charge > 1) p.charge = 1;
        }
        if (released && p.charging) {
            p.charging = false;
            float inkNeed = flerp(CHARGER_MIN_INK, wd.inkCost, p.charge);
            if (p.fireCd <= 0 && p.ink >= inkNeed) {
                p.ink -= inkNeed;
                p.fireCd = wd.fireInterval;
                fireChargerRay(p, slot, p.charge);
            }
            p.charge = 0;
        }
        break;
    }
}

void Match::damagePlayer(int victimSlot, float dmg, int killerSlot) {
    PlayerState& v = players[victimSlot];
    if (!v.active || v.dead) return;
    v.hp -= dmg;
    v.regenDelay = 5.0f;
    if (v.hp > 0) return;

    v.hp = 0;
    v.dead = true;
    v.respawnT = RESPAWN_TIME;
    v.deaths++;
    v.charging = v.rolling = v.swimming = false;
    v.charge = 0;
    v.buttons = 0;

    u8 killerTeam = v.team == TEAM_A ? TEAM_B : TEAM_A;
    if (killerSlot >= 0 && killerSlot < MAX_PLAYERS && players[killerSlot].active) {
        players[killerSlot].kills++;
        killerTeam = players[killerSlot].team;
    }
    if (mode == MODE_TDM) (killerTeam == TEAM_A ? scoreA : scoreB)++;

    // big splat where they popped
    size_t before = paintDeltas.size();
    paint.paintCircle(map, v.pos.x, v.pos.y, 16.0f, killerTeam, paintDeltas);
    if (killerSlot >= 0) players[killerSlot].paintCells += (u32)(paintDeltas.size() - before);

    killEvents.push_back({ (u8)(killerSlot < 0 ? victimSlot : killerSlot), (u8)victimSlot, v.pos });
}

void Match::updateProjectiles(float dt) {
    for (size_t i = 0; i < projs.size(); ) {
        Projectile& pr = projs[i];
        bool dead = false;
        for (int sub = 0; sub < 2 && !dead; sub++) {
            Vec2 step = pr.vel * (dt * 0.5f);
            float sl = vlen(step);
            Vec2 prev = pr.pos;
            pr.pos = pr.pos + step;
            pr.travelled += sl;
            pr.dripAcc += sl;

            if (map.solidAtPx(pr.pos.x, pr.pos.y)) {
                size_t before = paintDeltas.size();
                paint.paintCircle(map, prev.x, prev.y, pr.paintR * 0.8f, pr.team, paintDeltas);
                players[pr.owner].paintCells += (u32)(paintDeltas.size() - before);
                if (pr.explodes)
                    explodeAt(prev, pr.team, pr.owner, 12.0f, BLASTER_AOE_DMG, BLASTER_AOE_R, 35.0f, 16.0f);
                dead = true;
                break;
            }
            if (pr.dripAcc >= PROJ_DRIP_EVERY) {
                pr.dripAcc -= PROJ_DRIP_EVERY;
                size_t before = paintDeltas.size();
                paint.paintCircle(map, pr.pos.x, pr.pos.y, PROJ_DRIP_R, pr.team, paintDeltas);
                players[pr.owner].paintCells += (u32)(paintDeltas.size() - before);
            }
            for (int j = 0; j < MAX_PLAYERS; j++) {
                PlayerState& q = players[j];
                if (!q.active || q.dead || q.team == pr.team) continue;
                Vec2 dq = q.pos - pr.pos;
                float rr = PLAYER_R + 2.5f;
                if (dq.x * dq.x + dq.y * dq.y < rr * rr) {
                    damagePlayer(j, pr.dmg, pr.owner);
                    size_t before = paintDeltas.size();
                    paint.paintCircle(map, pr.pos.x, pr.pos.y, 5.0f, pr.team, paintDeltas);
                    players[pr.owner].paintCells += (u32)(paintDeltas.size() - before);
                    if (pr.explodes)   // blaster: splash others, not the direct-hit victim
                        explodeAt(pr.pos, pr.team, pr.owner, 12.0f, BLASTER_AOE_DMG, BLASTER_AOE_R, 35.0f, 16.0f, j);
                    dead = true;
                    break;
                }
            }
            if (!dead && pr.travelled >= pr.maxTravel) {
                size_t before = paintDeltas.size();
                paint.paintCircle(map, pr.pos.x, pr.pos.y, pr.paintR, pr.team, paintDeltas);
                players[pr.owner].paintCells += (u32)(paintDeltas.size() - before);
                if (pr.explodes)
                    explodeAt(pr.pos, pr.team, pr.owner, 12.0f, BLASTER_AOE_DMG, BLASTER_AOE_R, 35.0f, 16.0f);
                dead = true;
            }
        }
        if (dead) { projs[i] = projs.back(); projs.pop_back(); }
        else i++;
    }
}

void Match::update(float dt) {
    if (ended) return;
    tick++;
    timeLeft -= dt;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerState& p = players[i];
        if (!p.active) continue;

        if (p.dead) {
            p.respawnT -= dt;
            if (p.respawnT <= 0) respawn(p);
            continue;
        }

        if (p.isBot) botThink(i, p, dt);

        u8 under = paint.atPx(p.pos.x, p.pos.y);
        p.swimming = (p.buttons & BTN_SWIM) != 0;

        updateWeapon(i, p, dt);

        // sub weapon throw (edge-triggered)
        p.bombCd -= dt;
        bool bombPressed = (p.buttons & BTN_BOMB) && !(p.prevButtons & BTN_BOMB);
        if (bombPressed && !p.swimming && p.bombCd <= 0 && p.ink >= SUBS[p.sub].inkCost)
            throwBomb(p, i);

        // roller lays a trail under the player
        if (p.rolling) {
            size_t before = paintDeltas.size();
            paint.paintCircle(map, p.pos.x, p.pos.y, ROLLER_ROLL_PAINT_R, p.team, paintDeltas);
            p.paintCells += (u32)(paintDeltas.size() - before);
            p.ink -= ROLLER_ROLL_DRAIN * dt;
            if (p.ink <= 0) { p.ink = 0; p.rolling = false; }
        }

        // hp regen after 5s without taking damage
        p.regenDelay -= dt;
        if (p.regenDelay <= 0 && p.hp < HP_MAX) {
            p.hp += 30.0f * dt;
            if (p.hp > HP_MAX) p.hp = HP_MAX;
        }

        // ink regen
        u8 enemy = p.team == TEAM_A ? TEAM_B : TEAM_A;
        if (p.swimming && under == p.team) p.ink += INK_REGEN_SWIM * dt;
        else if (under != enemy) p.ink += INK_REGEN * dt;
        if (p.ink > INK_MAX) p.ink = INK_MAX;

        p.prevButtons = p.buttons;
    }

    updateProjectiles(dt);
    updateGrenades(dt);

    if (mode == MODE_TURF) {
        scoreRefreshT += dt;
        if (scoreRefreshT >= 1.0f) {
            scoreRefreshT = 0;
            int a, b;
            paint.counts(a, b);
            scoreA = (u16)(a * 1000 / paintableTotal);
            scoreB = (u16)(b * 1000 / paintableTotal);
        }
    }

    bool tdmDone = mode == MODE_TDM && (scoreA >= TDM_KILL_TARGET || scoreB >= TDM_KILL_TARGET);
    if (timeLeft <= 0 || tdmDone) {
        ended = true;
        timeLeft = 0;
        if (mode == MODE_TURF) {
            int a, b;
            paint.counts(a, b);
            scoreA = (u16)(a * 1000 / paintableTotal);
            scoreB = (u16)(b * 1000 / paintableTotal);
        }
        winner = scoreA > scoreB ? TEAM_A : (scoreB > scoreA ? TEAM_B : TEAM_NONE);
    }
}

// ---------------- bots ----------------

static int tileOf(float v) { return (int)(v / TILE); }

void Match::botPathTo(PlayerState& p, Vec2 goal) {
    BotBrain& b = p.bot;
    b.path.clear();
    b.pathI = 0;
    int sx = tileOf(p.pos.x), sy = tileOf(p.pos.y);
    int gx = tileOf(goal.x), gy = tileOf(goal.y);
    if (map.solidAt(gx, gy) || (sx == gx && sy == gy)) return;

    static int prev[MAPW * MAPH];
    for (int i = 0; i < MAPW * MAPH; i++) prev[i] = -2;
    std::queue<int> q;
    int start = sy * MAPW + sx;
    prev[start] = -1;
    q.push(start);
    int goalIdx = gy * MAPW + gx;
    static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        if (cur == goalIdx) break;
        int cx = cur % MAPW, cy = cur / MAPW;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            int ni = ny * MAPW + nx;
            if (prev[ni] != -2 || map.solidAt(nx, ny)) continue;
            prev[ni] = cur;
            q.push(ni);
        }
    }
    if (prev[goalIdx] == -2) return;
    std::vector<Vec2> rev;
    for (int cur = goalIdx; cur != -1; cur = prev[cur])
        rev.push_back({ (cur % MAPW) * (float)TILE + TILE / 2.0f, (cur / MAPW) * (float)TILE + TILE / 2.0f });
    b.path.assign(rev.rbegin(), rev.rend());
    if (!b.path.empty()) b.pathI = 1;    // skip our own tile
}

Vec2 Match::botMoveDir(PlayerState& p) {
    BotBrain& b = p.bot;
    // waypoint smoothing: skip ahead while we can see the next-next waypoint
    while (b.pathI + 1 < b.path.size() && lineOfSight(map, p.pos, b.path[b.pathI + 1]))
        b.pathI++;
    while (b.pathI < b.path.size()) {
        Vec2 to = b.path[b.pathI] - p.pos;
        if (vlen(to) < 7.0f) { b.pathI++; continue; }
        return vnorm(to);
    }
    return { 0, 0 };
}

void Match::botThink(int slot, PlayerState& p, float dt) {
    BotBrain& b = p.bot;
    u8 buttons = 0;

    b.retargetT -= dt;
    if (b.retargetT <= 0) {
        b.retargetT = 0.3f;
        b.targetSlot = -1;
        float sight = p.weapon == W_CHARGER ? 250.0f : 165.0f;
        float best = sight;
        for (int j = 0; j < MAX_PLAYERS; j++) {
            PlayerState& q = players[j];
            if (!q.active || q.dead || q.team == p.team) continue;
            float d = vlen(q.pos - p.pos);
            // swimmers in their own ink are hard to spot
            if (q.swimming && paint.atPx(q.pos.x, q.pos.y) == q.team && d > 45.0f) continue;
            if (d < best) { best = d; b.targetSlot = j; }
        }
    }
    if (b.targetSlot >= 0) {
        PlayerState& t = players[b.targetSlot];
        if (!t.active || t.dead || !lineOfSight(map, p.pos, t.pos)) b.targetSlot = -1;
    }

    Vec2 moveDir = { 0, 0 };
    b.burstT += dt;

    b.bombT -= dt;

    if (b.targetSlot >= 0) {
        PlayerState& t = players[b.targetSlot];
        Vec2 to = t.pos - p.pos;
        float d = vlen(to);
        float wobble = sinf(tick * 0.13f + slot * 1.7f) * 0.06f;
        float desired = atan2f(to.y, to.x) + wobble;
        float diff = angDiff(b.aimCur, desired);
        float turn = 7.0f * dt;
        if (diff > turn) diff = turn;
        if (diff < -turn) diff = -turn;
        b.aimCur += diff;
        p.aim = b.aimCur;

        static const float KEEP[W_COUNT] = { 75, 60, 85, 8, 150, 110 };
        float keep = KEEP[p.weapon];
        Vec2 fwd = vnorm(to);
        Vec2 perp = { -fwd.y, fwd.x };
        float side = ((tick / 45 + slot) & 1) ? 1.0f : -1.0f;
        moveDir = vnorm(perp * side * 0.8f + fwd * (d > keep ? 0.9f : -0.6f));

        if (p.weapon == W_CHARGER) {
            bool aimed = fabsf(angDiff(b.aimCur, atan2f(to.y, to.x))) < 0.07f;
            if (!(p.charge >= 1.0f && aimed)) buttons |= BTN_FIRE;   // release fires
        } else if (p.weapon == W_SPLATLING) {
            // rev ~0.8s, release, let the barrage run, repeat
            if (p.barrage <= 0 && fmodf(b.burstT, 3.0f) < 0.85f) buttons |= BTN_FIRE;
        } else {
            if (fmodf(b.burstT, 1.0f) < 0.65f) buttons |= BTN_FIRE;
        }

        // lob a bomb at mid range now and then
        if (b.bombT <= 0 && d > 55.0f && d < 165.0f && p.ink >= SUBS[p.sub].inkCost + 15.0f) {
            buttons |= BTN_BOMB;
            b.bombT = 5.0f + frand(0.0f, 3.0f);
        }
    } else {
        b.goalT -= dt;
        if (b.goalT <= 0 || b.pathI >= b.path.size()) {
            // pick a new goal
            Vec2 goal = p.pos;
            if (mode == MODE_TURF || (rng() & 1)) {
                // seek unpainted / enemy-painted ground
                for (int tries = 0; tries < 14; tries++) {
                    int cx = (int)(rng() % PW), cy = (int)(rng() % PH);
                    if (!map.paintableCell(cx, cy)) continue;
                    if (paint.at(cx, cy) == p.team) continue;
                    goal = { cx * (float)PAINT_CELL, cy * (float)PAINT_CELL };
                    break;
                }
            } else {
                // hunt: head toward a random living enemy
                for (int tries = 0; tries < 8; tries++) {
                    int j = (int)(rng() % MAX_PLAYERS);
                    PlayerState& q = players[j];
                    if (q.active && !q.dead && q.team != p.team) { goal = q.pos; break; }
                }
            }
            botPathTo(p, goal);
            b.goalT = frand(3.5f, 7.0f);
        }
        moveDir = botMoveDir(p);
        if (vlen(moveDir) > 0.1f) {
            float desired = atan2f(moveDir.y, moveDir.x);
            float diff = angDiff(b.aimCur, desired);
            float turn = 6.0f * dt;
            if (diff > turn) diff = turn;
            if (diff < -turn) diff = -turn;
            b.aimCur += diff;
            p.aim = b.aimCur;
        }

        // paint while travelling
        if (p.weapon == W_ROLLER) buttons |= BTN_FIRE;
        else if (p.weapon != W_CHARGER && p.weapon != W_SPLATLING) {
            float cycle = mode == MODE_TURF ? 1.5f : 2.4f;
            float on = mode == MODE_TURF ? 0.55f : 0.3f;
            if (fmodf(b.burstT, cycle) < on && p.ink > 25.0f) buttons |= BTN_FIRE;
        } else if (p.weapon == W_SPLATLING && mode == MODE_TURF) {
            // short revs to lay paint lanes
            if (p.barrage <= 0 && fmodf(b.burstT, 3.5f) < 0.5f && p.ink > 40.0f) buttons |= BTN_FIRE;
        }

        u8 under = paint.atPx(p.pos.x, p.pos.y);
        if (under == p.team && !(buttons & BTN_FIRE) && p.ink < 90.0f) buttons |= BTN_SWIM;
    }

    p.buttons = buttons;

    // bots move themselves with the same rules as players
    u8 under = paint.atPx(p.pos.x, p.pos.y);
    bool firing = p.firingVisT > 0 || p.rolling || p.charging || p.barrage > 0;
    bool swim = (buttons & BTN_SWIM) != 0;
    float sp = speedFor(p.team, under, swim, firing);
    if (!swim) sp *= WEAPONS[p.weapon].moveMult;   // weapon weight class
    if (vlen(moveDir) > 0.1f)
        p.pos = moveAndSlide(map, p.pos, moveDir * (sp * dt));
}

// ---------------- serialization ----------------

void Match::writeRosterEntry(BufW& w, int slot) {
    PlayerState& p = players[slot];
    w.u8_((u8)slot);
    w.str(p.name);
    w.u8_(p.team);
    w.u8_(p.weapon);
    w.u8_(p.skin);
    w.u8_(p.isBot ? PF_BOT : 0);
}

void Match::writeSnapshot(BufW& w) {
    w.u8_(S_SNAPSHOT);
    w.u32_(tick);
    w.f32_(timeLeft);
    w.u16_(scoreA);
    w.u16_(scoreB);
    int n = 0;
    for (auto& p : players) if (p.active) n++;
    w.u8_((u8)n);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerState& p = players[i];
        if (!p.active) continue;
        w.u8_((u8)i);
        w.f32_(p.pos.x);
        w.f32_(p.pos.y);
        w.f32_(p.aim);
        w.u8_((u8)(p.hp + 0.5f));
        w.u8_((u8)(p.ink + 0.5f));
        u8 fl = 0;
        if (p.swimming) fl |= PF_SWIM;
        if (p.dead) fl |= PF_DEAD;
        if (p.firingVisT > 0) fl |= PF_FIRING;
        if (p.charging) fl |= PF_CHARGING;
        if (p.rolling) fl |= PF_ROLLING;
        if (p.isBot) fl |= PF_BOT;
        w.u8_(fl);
        w.u8_((u8)(p.respawnT * 10.0f));
    }
    size_t np = projs.size(), ng = grenades.size();
    if (np + ng > 250) np = 250 - (ng > 250 ? (ng = 250, 0) : ng);
    w.u8_((u8)(np + ng));
    for (size_t i = 0; i < np; i++) {
        w.f32_(projs[i].pos.x);
        w.f32_(projs[i].pos.y);
        w.u8_(projs[i].team);
        w.u8_(PK_BLOB);
        w.u8_(0);
    }
    for (size_t i = 0; i < ng; i++) {
        const Grenade& gr = grenades[i];
        w.f32_(gr.pos.x);
        w.f32_(gr.pos.y);
        w.u8_(gr.team);
        w.u8_(gr.sub == SUB_SPLAT_BOMB ? PK_SPLAT_BOMB : PK_BURST_BOMB);
        // aux: deciseconds of fuse once landed (drives the blink), 255 in flight
        w.u8_(gr.landed ? (u8)(gr.fuse * 10.0f) : 255);
    }
}

void Match::writePaintDeltas(BufW& w) {
    std::sort(paintDeltas.begin(), paintDeltas.end());
    paintDeltas.erase(std::unique(paintDeltas.begin(), paintDeltas.end()), paintDeltas.end());
    w.u8_(S_PAINT);
    w.u16_((u16)paintDeltas.size());
    for (u16 idx : paintDeltas) {
        w.u16_(idx);
        w.u8_(paint.c[idx]);
    }
    paintDeltas.clear();
}

void Match::writeFullPaint(BufW& w) {
    std::vector<u16> cells;
    for (u32 i = 0; i < (u32)(PW * PH); i++)
        if (paint.c[i] != TEAM_NONE) cells.push_back((u16)i);
    w.u8_(S_PAINT);
    w.u16_((u16)cells.size());
    for (u16 idx : cells) {
        w.u16_(idx);
        w.u8_(paint.c[idx]);
    }
}
