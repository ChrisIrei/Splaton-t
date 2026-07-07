// Splaton't — shared simulation: map, paint grid, movement
#pragma once
#include "shared/defs.h"
#include <vector>

struct Vec2 {
    float x = 0, y = 0;
    Vec2 operator+(Vec2 o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(Vec2 o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(float s) const { return { x * s, y * s }; }
};
float vlen(Vec2 v);
Vec2 vnorm(Vec2 v);

struct GameMap {
    u8 tiles[MAPW * MAPH] = {};
    Vec2 spawns[2][TEAM_SIZE];      // [team-1][slot]
    int mapId = 0;

    void load(int mapId = 0);       // builds one of the built-in symmetric arenas
    u8 tileAt(int tx, int ty) const {
        if (tx < 0 || ty < 0 || tx >= MAPW || ty >= MAPH) return T_WALL;
        return tiles[ty * MAPW + tx];
    }
    bool solidAt(int tx, int ty) const { return tileAt(tx, ty) == T_WALL; }
    bool solidAtPx(float x, float y) const { return solidAt((int)(x / TILE), (int)(y / TILE)); }
    // true if this paint cell sits on paintable floor (not wall / spawn pad)
    bool paintableCell(int cx, int cy) const {
        u8 t = tileAt(cx * PAINT_CELL / TILE, cy * PAINT_CELL / TILE);
        return t == T_FLOOR;
    }
};

struct PaintGrid {
    std::vector<u8> c;              // TEAM_NONE / TEAM_A / TEAM_B per cell
    PaintGrid() : c(PW * PH, TEAM_NONE) {}
    void clear() { c.assign(PW * PH, TEAM_NONE); }
    u8 at(int cx, int cy) const {
        if (cx < 0 || cy < 0 || cx >= PW || cy >= PH) return TEAM_NONE;
        return c[cy * PW + cx];
    }
    u8 atPx(float x, float y) const { return at((int)(x / PAINT_CELL), (int)(y / PAINT_CELL)); }
    void set(u32 idx, u8 team) { if (idx < c.size()) c[idx] = team; }
    // paints a filled circle (px coords/radius); appends changed cell indices
    void paintCircle(const GameMap& map, float px, float py, float r, u8 team, std::vector<u16>& changed);
    void counts(int& a, int& b) const;
    int paintableTotal(const GameMap& map) const;
};

// axis-separated circle-vs-tile movement; returns resolved position
Vec2 moveAndSlide(const GameMap& map, Vec2 pos, Vec2 delta);
// ground speed given paint under feet and player state
float speedFor(u8 team, u8 paintUnder, bool swimming, bool firing);
// pushes a position out of walls if embedded (safety for client-sent coords)
Vec2 depenetrate(const GameMap& map, Vec2 pos);
// line of sight between two points (for bots / charger ray stepping)
bool lineOfSight(const GameMap& map, Vec2 a, Vec2 b);
