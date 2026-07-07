#include "shared/sim.h"
#include <cmath>

float vlen(Vec2 v) { return sqrtf(v.x * v.x + v.y * v.y); }
Vec2 vnorm(Vec2 v) { float l = vlen(v); return l > 0.0001f ? Vec2{ v.x / l, v.y / l } : Vec2{ 0, 0 }; }

struct IRect { int x, y, w, h; };

// Each arena is defined as wall rects on the top-left half; every rect is also
// stamped 180-degree rotated so the map is guaranteed symmetric for both teams.

static const IRect MAP0_WALLS[] = {   // Dockside — scattered crates, open flanks
    // spawn room walls (leave two exits, carved below)
    { 7, 1, 1, 5 }, { 1, 7, 4, 1 },
    // upper lane cover
    { 12, 4, 4, 2 }, { 21, 2, 2, 4 },
    { 30, 5, 5, 2 }, { 40, 2, 2, 5 },
    // left lane
    { 3, 12, 2, 5 }, { 9, 10, 2, 2 },
    { 5, 21, 5, 2 }, { 12, 25, 2, 4 },
    // mid structures
    { 15, 9, 2, 6 }, { 20, 12, 8, 2 },             // center bar (self-mirrors into an S)
    { 22, 15, 4, 2 },                               // center block
    { 14, 18, 3, 2 },
    { 31, 10, 2, 3 },
    // right-of-mid pillars
    { 36, 9, 2, 2 }, { 42, 12, 3, 2 },
    { 33, 17, 2, 2 },
};

static const IRect MAP1_WALLS[] = {   // Warehouse — long shelves, three lanes
    { 8, 5, 14, 2 },  { 28, 3, 2, 6 },  { 34, 7, 10, 2 },
    { 4, 11, 2, 6 },  { 12, 10, 4, 2 },
    { 20, 12, 8, 2 }, { 32, 13, 2, 5 },
    { 14, 16, 2, 6 }, { 24, 15, 2, 2 },
    { 40, 11, 2, 2 },
};

static const IRect MAP2_WALLS[] = {   // Plaza — big open middle, cross structure
    { 14, 8, 4, 2 },  { 30, 8, 4, 2 },
    { 8, 12, 2, 8 },
    { 22, 10, 4, 2 },
    { 23, 13, 2, 6 },                               // vertical cross bar (self-mirrors)
    { 18, 15, 3, 2 },
    { 12, 20, 4, 2 },
    { 36, 4, 2, 4 },  { 40, 14, 4, 2 },
    { 27, 22, 2, 2 },
};

void GameMap::load(int id) {
    mapId = id < 0 || id >= MAP_COUNT ? 0 : id;
    for (int i = 0; i < MAPW * MAPH; i++) tiles[i] = T_FLOOR;

    const IRect* walls;
    size_t nWalls;
    switch (mapId) {
    case 1: walls = MAP1_WALLS; nWalls = sizeof MAP1_WALLS / sizeof *MAP1_WALLS; break;
    case 2: walls = MAP2_WALLS; nWalls = sizeof MAP2_WALLS / sizeof *MAP2_WALLS; break;
    default: walls = MAP0_WALLS; nWalls = sizeof MAP0_WALLS / sizeof *MAP0_WALLS; break;
    }

    static const IRect borders[] = { { 0, 0, MAPW, 1 }, { 0, 0, 1, MAPH } };
    auto stamp = [&](const IRect& r) {
        for (int y = r.y; y < r.y + r.h; y++)
            for (int x = r.x; x < r.x + r.w; x++) {
                if (x >= 0 && y >= 0 && x < MAPW && y < MAPH) tiles[y * MAPW + x] = T_WALL;
                int mx = MAPW - 1 - x, my = MAPH - 1 - y;
                if (mx >= 0 && my >= 0 && mx < MAPW && my < MAPH) tiles[my * MAPW + mx] = T_WALL;
            }
    };
    for (const IRect& r : borders) stamp(r);
    for (size_t i = 0; i < nWalls; i++) stamp(walls[i]);

    // spawn pads: 4x4 unpaintable pads in opposite corners
    static const IRect padA = { 2, 2, 4, 4 };
    for (int y = padA.y; y < padA.y + padA.h; y++)
        for (int x = padA.x; x < padA.x + padA.w; x++) {
            tiles[y * MAPW + x] = T_PAD_A;
            tiles[(MAPH - 1 - y) * MAPW + (MAPW - 1 - x)] = T_PAD_B;
        }
    if (mapId == 0) {
        // Dockside has spawn-room walls: carve exits so pads can't be sealed
        tiles[4 * MAPW + 7] = T_FLOOR; tiles[3 * MAPW + 7] = T_FLOOR;
        tiles[7 * MAPW + 3] = T_FLOOR; tiles[7 * MAPW + 4] = T_FLOOR;
        tiles[(MAPH - 5) * MAPW + (MAPW - 8)] = T_FLOOR; tiles[(MAPH - 4) * MAPW + (MAPW - 8)] = T_FLOOR;
        tiles[(MAPH - 8) * MAPW + (MAPW - 4)] = T_FLOOR; tiles[(MAPH - 8) * MAPW + (MAPW - 5)] = T_FLOOR;
    }

    // spawn points: pad corners
    float ax = (padA.x + 1) * TILE, ay = (padA.y + 1) * TILE;
    Vec2 offs[TEAM_SIZE] = { {0,0}, {TILE * 2, 0}, {0, TILE * 2}, {TILE * 2, TILE * 2} };
    for (int i = 0; i < TEAM_SIZE; i++) {
        spawns[0][i] = { ax + offs[i].x, ay + offs[i].y };
        spawns[1][i] = { WORLD_W - (ax + offs[i].x), WORLD_H - (ay + offs[i].y) };
    }
}

void PaintGrid::paintCircle(const GameMap& map, float px, float py, float r, u8 team, std::vector<u16>& changed) {
    int cx0 = (int)((px - r) / PAINT_CELL), cx1 = (int)((px + r) / PAINT_CELL);
    int cy0 = (int)((py - r) / PAINT_CELL), cy1 = (int)((py + r) / PAINT_CELL);
    float r2 = r * r;
    for (int cy = cy0; cy <= cy1; cy++) {
        if (cy < 0 || cy >= PH) continue;
        for (int cx = cx0; cx <= cx1; cx++) {
            if (cx < 0 || cx >= PW) continue;
            float dx = cx * PAINT_CELL + PAINT_CELL * 0.5f - px;
            float dy = cy * PAINT_CELL + PAINT_CELL * 0.5f - py;
            if (dx * dx + dy * dy > r2) continue;
            if (!map.paintableCell(cx, cy)) continue;
            u32 idx = cy * PW + cx;
            if (c[idx] != team) {
                c[idx] = team;
                changed.push_back((u16)idx);
            }
        }
    }
}

void PaintGrid::counts(int& a, int& b) const {
    a = b = 0;
    for (u8 v : c) {
        if (v == TEAM_A) a++;
        else if (v == TEAM_B) b++;
    }
}

int PaintGrid::paintableTotal(const GameMap& map) const {
    int t = 0;
    for (int cy = 0; cy < PH; cy++)
        for (int cx = 0; cx < PW; cx++)
            if (map.paintableCell(cx, cy)) t++;
    return t;
}

static bool circleHitsWalls(const GameMap& map, float x, float y) {
    float r = PLAYER_R;
    int tx0 = (int)((x - r) / TILE), tx1 = (int)((x + r) / TILE);
    int ty0 = (int)((y - r) / TILE), ty1 = (int)((y + r) / TILE);
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            if (!map.solidAt(tx, ty)) continue;
            float nx = x < tx * TILE ? tx * TILE : (x > (tx + 1) * TILE ? (tx + 1) * TILE : x);
            float ny = y < ty * TILE ? ty * TILE : (y > (ty + 1) * TILE ? (ty + 1) * TILE : y);
            float dx = x - nx, dy = y - ny;
            if (dx * dx + dy * dy < r * r) return true;
        }
    return false;
}

Vec2 moveAndSlide(const GameMap& map, Vec2 pos, Vec2 delta) {
    // step in small increments per axis so we can't tunnel through 16px tiles
    Vec2 p = pos;
    int steps = (int)(vlen(delta) / 4.0f) + 1;
    Vec2 st = delta * (1.0f / steps);
    for (int i = 0; i < steps; i++) {
        float nx = p.x + st.x;
        if (!circleHitsWalls(map, nx, p.y)) p.x = nx;
        float ny = p.y + st.y;
        if (!circleHitsWalls(map, p.x, ny)) p.y = ny;
    }
    if (p.x < PLAYER_R) p.x = PLAYER_R;
    if (p.y < PLAYER_R) p.y = PLAYER_R;
    if (p.x > WORLD_W - PLAYER_R) p.x = WORLD_W - PLAYER_R;
    if (p.y > WORLD_H - PLAYER_R) p.y = WORLD_H - PLAYER_R;
    return p;
}

Vec2 depenetrate(const GameMap& map, Vec2 pos) {
    if (!circleHitsWalls(map, pos.x, pos.y)) return pos;
    for (float r = 2; r < 64; r += 2) {
        for (int i = 0; i < 16; i++) {
            float a = i * 6.2831853f / 16;
            Vec2 q = { pos.x + cosf(a) * r, pos.y + sinf(a) * r };
            if (q.x < PLAYER_R || q.y < PLAYER_R || q.x > WORLD_W - PLAYER_R || q.y > WORLD_H - PLAYER_R) continue;
            if (!circleHitsWalls(map, q.x, q.y)) return q;
        }
    }
    return pos;
}

float speedFor(u8 team, u8 paintUnder, bool swimming, bool firing) {
    float s;
    if (paintUnder != TEAM_NONE && paintUnder != team) s = SPEED_ENEMY_INK;
    else if (swimming && paintUnder == team) s = SPEED_SWIM;
    else s = SPEED_RUN;
    if (firing && !swimming) s *= SPEED_SHOOT_MULT;
    return s;
}

bool lineOfSight(const GameMap& map, Vec2 a, Vec2 b) {
    Vec2 d = b - a;
    float len = vlen(d);
    if (len < 0.001f) return true;
    int steps = (int)(len / 4.0f) + 1;
    Vec2 st = d * (1.0f / steps);
    Vec2 p = a;
    for (int i = 0; i <= steps; i++) {
        if (map.solidAtPx(p.x, p.y)) return false;
        p = p + st;
    }
    return true;
}
