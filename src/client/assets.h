// Splaton't client — procedurally generated pixel art + synthesized sounds
#pragma once
#include "raylib.h"
#include "shared/defs.h"
#include "shared/sim.h"

// global mixer levels (0..1), applied at play time; music volume applied per frame
extern float g_sfxVol;
extern float g_musVol;

struct SoundPool {
    Sound base{};
    Sound alias[4]{};
    int i = 0;
    bool ok = false;
    void load(Wave w);
    void play(float pitch = 1.0f, float vol = 1.0f);
    void unload();
};

struct Assets {
    bool audio = false;

    // sprites: [team 0/1] indexed by weapon and skin
    Texture2D kid[2][W_COUNT][SKIN_COUNT]{};
    Texture2D squid[2]{};
    Texture2D blob[2]{};
    Texture2D bomb[2]{};
    Texture2D kidPreview[W_COUNT][SKIN_COUNT]{};   // menu preview (team A colors)
    Texture2D squidPreview{};
    Texture2D hats[HAT_COUNT]{};                   // [0] unused ("None")
    Texture2D mapTex{};                            // pre-rendered world
    Texture2D paintTex{};                          // PW x PH, scaled up when drawn
    Image paintImg{};
    bool matchSpritesReady = false;

    SoundPool sShoot, sSplat;
    Sound sKill{}, sDeath{}, sClick{}, sStart{}, sEnd{}, sQueue{}, sBoom{}, sThrow{};
    Sound sReady{}, sChat{};                       // special charged / quick chat blip
    Sound musMenu{}, musMatch{};                   // looping chiptune tracks
    Sound musCur{};
    bool musIsMatch = false, musStarted = false;

    void init();                                   // audio + menu previews
    void buildMatchAssets(u8 colorPair, const GameMap& map);
    void updatePaintTex(const PaintGrid& paint, u8 colorPair);
    void playS(Sound s, float pitch = 1.0f);
    void updateMusic(bool inMatch);                // call once per frame
};

Color teamColor(u8 colorPair, u8 team);
Color teamColorDark(u8 colorPair, u8 team);
Color skinColor(u8 skin);
