#include "client/assets.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

// ---------------- palette ----------------

Color teamColor(u8 colorPair, u8 team) {
    static const Color pairs[COLOR_PAIR_COUNT][2] = {
        { { 250, 114, 20, 255 }, { 46, 80, 246, 255 } },    // orange / blue
        { { 240, 60, 160, 255 }, { 90, 220, 40, 255 } },    // pink / green
        { { 246, 210, 30, 255 }, { 150, 60, 230, 255 } },   // yellow / purple
    };
    if (team != TEAM_A && team != TEAM_B) return GRAY;
    return pairs[colorPair % COLOR_PAIR_COUNT][team - 1];
}

Color teamColorDark(u8 colorPair, u8 team) {
    Color c = teamColor(colorPair, team);
    return { (unsigned char)(c.r * 2 / 3), (unsigned char)(c.g * 2 / 3), (unsigned char)(c.b * 2 / 3), 255 };
}

Color skinColor(u8 skin) {
    static const Color tones[SKIN_COUNT] = {
        { 255, 224, 196, 255 }, { 231, 183, 146, 255 }, { 172, 120, 84, 255 }, { 116, 78, 54, 255 },
    };
    return tones[skin % SKIN_COUNT];
}

// ---------------- pixel sprites ----------------
// 16x16 character sprites drawn facing RIGHT (rotation 0 == aim along +x)

static const char* KID_GUN[16] = {
    "................",
    "................",
    "....OOOOO.......",
    "...OBBBBBO......",
    "..OBBBBBBBO.....",
    ".OBBBBBBSSSO....",
    ".OBBBBBSSeSO....",
    ".ObBBBBSSSSOWWW.",
    ".ObBBBBSSSSOWWW.",
    ".OBBBBBSSeSO....",
    ".OBBBBBBSSSO....",
    "..OBBBBBBBO.....",
    "...OBBBBBO......",
    "....OOOOO.......",
    "................",
    "................",
};

static const char* KID_ROLLER[16] = {
    "................",
    "................",
    "....OOOOO.......",
    "...OBBBBBO......",
    "..OBBBBBBBO..Ww.",
    ".OBBBBBBSSSO.Ww.",
    ".OBBBBBSSeSO.Ww.",
    ".ObBBBBSSSSOwWw.",
    ".ObBBBBSSSSOwWw.",
    ".OBBBBBSSeSO.Ww.",
    ".OBBBBBBSSSO.Ww.",
    "..OBBBBBBBO..Ww.",
    "...OBBBBBO......",
    "....OOOOO.......",
    "................",
    "................",
};

static const char* KID_AERO[16] = {     // wide spray nozzle
    "................",
    "................",
    "....OOOOO.......",
    "...OBBBBBO......",
    "..OBBBBBBBO.....",
    ".OBBBBBBSSSO....",
    ".OBBBBBSSeSO.WW.",
    ".ObBBBBSSSSOWWWW",
    ".ObBBBBSSSSOWWWW",
    ".OBBBBBSSeSO.WW.",
    ".OBBBBBBSSSO....",
    "..OBBBBBBBO.....",
    "...OBBBBBO......",
    "....OOOOO.......",
    "................",
    "................",
};

static const char* KID_BLASTER[16] = {  // thick stubby barrel
    "................",
    "................",
    "....OOOOO.......",
    "...OBBBBBO......",
    "..OBBBBBBBO.....",
    ".OBBBBBBSSSO....",
    ".OBBBBBSSeSO.ww.",
    ".ObBBBBSSSSOWWw.",
    ".ObBBBBSSSSOWWw.",
    ".OBBBBBSSeSO.ww.",
    ".OBBBBBBSSSO....",
    "..OBBBBBBBO.....",
    "...OBBBBBO......",
    "....OOOOO.......",
    "................",
    "................",
};

static const char* KID_SPLATLING[16] = { // triple gatling barrels
    "................",
    "................",
    "....OOOOO.......",
    "...OBBBBBO......",
    "..OBBBBBBBO.....",
    ".OBBBBBBSSSO.WWw",
    ".OBBBBBSSeSO....",
    ".ObBBBBSSSSOWWWw",
    ".ObBBBBSSSSO....",
    ".OBBBBBSSeSO.WWw",
    ".OBBBBBBSSSO....",
    "..OBBBBBBBO.....",
    "...OBBBBBO......",
    "....OOOOO.......",
    "................",
    "................",
};

static const char* KID_CHARGER[16] = {
    "................",
    "................",
    "....OOOOO.......",
    "...OBBBBBO......",
    "..OBBBBBBBO.....",
    ".OBBBBBBSSSO....",
    ".OBBBBBSSeSO.w..",
    ".ObBBBBSSSSOWWWw",
    ".ObBBBBSSSSOWWWw",
    ".OBBBBBSSeSO.w..",
    ".OBBBBBBSSSO....",
    "..OBBBBBBBO.....",
    "...OBBBBBO......",
    "....OOOOO.......",
    "................",
    "................",
};

static const char* SQUID[16] = {
    "................",
    "................",
    "................",
    "....O..O...O....",
    "...OBOOBO.OBO...",
    "..OBBBBBBOBBO...",
    ".OBBBBBBBBBBBO..",
    ".OBBBBBBBBeEBO..",
    ".OBBBBBBBBeEBO..",
    ".OBBBBBBBBBBBO..",
    "..OBBBBBBOBBO...",
    "...OBOOBO.OBO...",
    "....O..O...O....",
    "................",
    "................",
    "................",
};

static const char* BLOB[8] = {
    "..BBB...",
    ".BBBBB..",
    "BBBBBBB.",
    "BBBBBBB.",
    "BBBBBBB.",
    ".BBBBB..",
    "..BBB...",
    "........",
};

static const char* BOMB[8] = {          // splat bomb: little pyramid drop
    "...O....",
    "..OBO...",
    "..OBO...",
    ".OBBBO..",
    ".OBbBO..",
    "OBBBBBO.",
    ".OOOOO..",
    "........",
};

// hats: 16x16 overlays aligned with the kid sprite (facing right)
static const char* HAT_CAP[16] = {
    "................", "................", "................", "................",
    "................",
    ".....RRRR.......",
    "....RRRRRRr.....",
    "....RRRRRRrr....",
    "....RRRRRRrr....",
    "....RRRRRRr.....",
    ".....RRRR.......",
    "................", "................", "................", "................", "................",
};
static const char* HAT_CONE[16] = {
    "................", "................", "................", "................",
    "................",
    "......CCC.......",
    ".....CCWCC......",
    ".....CWWWC......",
    ".....CCWCC......",
    "......CCC.......",
    "................",
    "................", "................", "................", "................", "................",
};
static const char* HAT_PHONES[16] = {
    "................", "................", "................",
    "....DDD.........",
    "....DDD.........",
    ".....DD.........",
    ".....DD.........",
    ".....DD.........",
    ".....DD.........",
    ".....DD.........",
    ".....DD.........",
    "....DDD.........",
    "....DDD.........",
    "................", "................", "................",
};
static const char* HAT_CROWN[16] = {
    "................", "................", "................", "................",
    "................",
    ".....G.G.G......",
    ".....GGGGG......",
    ".....GgGgG......",
    ".....GgGgG......",
    ".....GGGGG......",
    ".....G.G.G......",
    "................", "................", "................", "................", "................",
};
static const char** hatArt(int hat) {
    switch (hat) {
    case 1: return HAT_CAP;
    case 2: return HAT_CONE;
    case 3: return HAT_PHONES;
    case 4: return HAT_CROWN;
    default: return nullptr;
    }
}

static const char** kidArt(int weapon) {
    switch (weapon) {
    case W_AEROSPRAY: return KID_AERO;
    case W_BLASTER: return KID_BLASTER;
    case W_ROLLER: return KID_ROLLER;
    case W_CHARGER: return KID_CHARGER;
    case W_SPLATLING: return KID_SPLATLING;
    default: return KID_GUN;
    }
}

static Texture2D buildSprite(const char** rows, int w, int h, Color body, Color bodyDark, Color skin) {
    Image img = GenImageColor(w, h, BLANK);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            Color c;
            switch (rows[y][x]) {
            case 'O': c = { 22, 18, 36, 255 }; break;
            case 'B': c = body; break;
            case 'b': c = bodyDark; break;
            case 'S': c = skin; break;
            case 'e': c = { 30, 26, 44, 255 }; break;
            case 'E': c = { 245, 245, 250, 255 }; break;
            case 'W': c = { 198, 202, 216, 255 }; break;
            case 'w': c = { 106, 110, 134, 255 }; break;
            case 'R': c = { 190, 44, 60, 255 }; break;     // hat: cap red
            case 'r': c = { 130, 28, 40, 255 }; break;
            case 'C': c = { 255, 140, 20, 255 }; break;    // hat: cone orange
            case 'G': c = { 252, 206, 48, 255 }; break;    // hat: crown gold
            case 'g': c = { 190, 148, 24, 255 }; break;
            case 'D': c = { 58, 58, 72, 255 }; break;      // hat: headphones
            default: continue;
            }
            ImageDrawPixel(&img, x, y, c);
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    return t;
}

// ---------------- map pre-render ----------------

static u32 thash(u32 x, u32 y) {
    u32 h = x * 73856093u ^ y * 19349663u;
    h ^= h >> 13;
    h *= 2654435761u;
    return h ^ (h >> 16);
}

static Texture2D buildMapTexture(const GameMap& map, u8 colorPair) {
    Image img = GenImageColor(WORLD_W, WORLD_H, Color{ 20, 18, 32, 255 });
    for (int ty = 0; ty < MAPH; ty++) {
        for (int tx = 0; tx < MAPW; tx++) {
            u8 t = map.tileAt(tx, ty);
            int px = tx * TILE, py = ty * TILE;
            u32 h = thash(tx, ty);
            if (t == T_FLOOR) {
                unsigned char v = (unsigned char)(182 + (h % 4) * 4);
                ImageDrawRectangle(&img, px, py, TILE, TILE, Color{ v, v, (unsigned char)(v + 8), 255 });
                ImageDrawRectangle(&img, px, py, TILE, 1, Color{ 164, 162, 178, 255 });
                ImageDrawRectangle(&img, px, py, 1, TILE, Color{ 164, 162, 178, 255 });
                for (int s = 0; s < 3; s++) {
                    u32 hs = thash(tx * 16 + s, ty * 7 + s);
                    ImageDrawPixel(&img, px + (int)(hs % TILE), py + (int)((hs >> 8) % TILE),
                                   Color{ 158, 156, 174, 255 });
                }
                if (h % 17 == 0)  // occasional drain grate
                    for (int gy = 0; gy < 4; gy++)
                        for (int gx = 0; gx < 6; gx++)
                            if ((gx + gy) % 2 == 0)
                                ImageDrawPixel(&img, px + 5 + gx, py + 6 + gy, Color{ 130, 128, 146, 255 });
            } else if (t == T_WALL) {
                ImageDrawRectangle(&img, px, py, TILE, TILE, Color{ 66, 60, 88, 255 });
                for (int r = 0; r < 4; r++) {
                    int by = py + r * 4;
                    ImageDrawRectangle(&img, px, by + 3, TILE, 1, Color{ 44, 40, 62, 255 });
                    int joint = ((r + tx + ty) % 2) * 8 + 4;
                    ImageDrawRectangle(&img, px + joint, by, 1, 3, Color{ 44, 40, 62, 255 });
                }
                if (!map.solidAt(tx, ty - 1))
                    ImageDrawRectangle(&img, px, py, TILE, 2, Color{ 96, 90, 124, 255 });
            } else { // spawn pads
                u8 team = (t == T_PAD_A) ? TEAM_A : TEAM_B;
                Color base = teamColorDark(colorPair, team);
                Color lite = teamColor(colorPair, team);
                ImageDrawRectangle(&img, px, py, TILE, TILE, base);
                for (int yy = 0; yy < TILE; yy++)
                    for (int xx = 0; xx < TILE; xx++)
                        if (((xx + yy) / 4) % 2 == 0)
                            ImageDrawPixel(&img, px + xx, py + yy,
                                Color{ (unsigned char)(base.r + 24), (unsigned char)(base.g + 24),
                                       (unsigned char)(base.b + 24), 255 });
                ImageDrawRectangleLines(&img, Rectangle{ (float)px, (float)py, TILE, TILE }, 1, lite);
            }
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    return t;
}

// ---------------- sound synthesis ----------------

float g_sfxVol = 0.8f;
float g_musVol = 0.55f;

static constexpr int SR = 22050;

static Wave makeWave(float seconds, const std::function<float(float)>& f) {
    int frames = (int)(seconds * SR);
    short* data = (short*)RL_MALLOC(frames * sizeof(short));
    for (int i = 0; i < frames; i++) {
        float v = f((float)i / SR);
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        data[i] = (short)(v * 32000);
    }
    Wave w{};
    w.frameCount = (unsigned)frames;
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data;
    return w;
}

static float frnd() { // deterministic-ish noise
    static u32 s = 0x12345;
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) / 8388608.0f - 1.0f;
}

void SoundPool::load(Wave w) {
    base = LoadSoundFromWave(w);
    for (auto& a : alias) a = LoadSoundAlias(base);
    UnloadWave(w);
    ok = true;
}

void SoundPool::play(float pitch, float vol) {
    if (!ok) return;
    Sound& s = alias[i++ % 4];
    SetSoundPitch(s, pitch);
    SetSoundVolume(s, vol * g_sfxVol);
    PlaySound(s);
}

void SoundPool::unload() {
    if (!ok) return;
    for (auto& a : alias) UnloadSoundAlias(a);
    UnloadSound(base);
    ok = false;
}

void Assets::playS(Sound s, float pitch) {
    if (!audio) return;
    SetSoundPitch(s, pitch);
    SetSoundVolume(s, g_sfxVol);
    PlaySound(s);
}

// ---------------- procedural chiptune loops ----------------

static float noteFreq(int semi) { return 220.0f * powf(2.0f, semi / 12.0f); }

// square-wave pluck with decay, t local to the note
static float pluck(float t, float freq, float amp, float decay) {
    if (t < 0) return 0;
    float sq = sinf(6.2832f * freq * t) > 0 ? 1.0f : -1.0f;
    return sq * amp * expf(-t * decay);
}

static Wave makeMenuLoop() {
    // 8 bars, 100 BPM, Am F C G — mellow plucks over a soft bass
    const float beat = 0.6f;
    const float dur = 16 * beat;
    static const int roots[4] = { 0, -4, 3, -2 };            // A F C G
    return makeWave(dur, [=](float t) {
        int b = (int)(t / beat);
        float bt = t - b * beat;
        int root = roots[(b / 4) % 4];
        float out = 0;
        out += pluck(bt, noteFreq(root) * 0.5f, 0.10f, 4.0f);           // bass on the beat
        int arpStep = (int)(bt / (beat / 2));                           // 8th-note arp
        static const int arp[2] = { 7, 12 };
        float at = bt - arpStep * (beat / 2);
        out += sinf(6.2832f * noteFreq(root + arp[arpStep & 1]) * t) * 0.06f * expf(-at * 6.0f);
        if (bt > beat * 0.5f)                                           // offbeat hat tick
            out += frnd() * 0.015f * expf(-(bt - beat * 0.5f) * 60.0f);
        return out;
    });
}

static Wave makeMatchLoop() {
    // 8 bars, 138 BPM — driving bass eighths, punchy lead, snare on 2 & 4
    const float beat = 0.435f;
    const float dur = 32 * beat;
    static const int roots[4] = { 0, 0, -4, -2 };
    static const int lead[16] = { 12, 12, 15, 12, 17, 15, 12, 10, 12, 12, 15, 17, 19, 17, 15, 12 };
    return makeWave(dur, [=](float t) {
        int b = (int)(t / beat);
        float bt = t - b * beat;
        int root = roots[(b / 8) % 4];
        float out = 0;
        float et = fmodf(bt, beat / 2);                                 // bass 8ths
        out += pluck(et, noteFreq(root) * 0.5f, 0.11f, 10.0f);
        int li = (b * 2 + (bt >= beat / 2 ? 1 : 0)) % 16;               // lead 8ths
        out += pluck(fmodf(bt, beat / 2), noteFreq(root + lead[li]), 0.055f, 7.0f);
        if ((b & 1) == 1)                                               // snare on 2 & 4
            out += frnd() * 0.06f * expf(-bt * 25.0f);
        return out;
    });
}

void Assets::updateMusic(bool inMatch) {
    if (!audio) return;
    if (!musStarted || inMatch != musIsMatch) {
        if (musStarted) StopSound(musCur);
        musCur = inMatch ? musMatch : musMenu;
        musIsMatch = inMatch;
        musStarted = true;
        PlaySound(musCur);
    }
    SetSoundVolume(musCur, g_musVol);
    if (!IsSoundPlaying(musCur)) PlaySound(musCur);   // loop
}

void Assets::init() {
    InitAudioDevice();
    audio = IsAudioDeviceReady();
    if (audio) {
        SetMasterVolume(0.65f);
        sShoot.load(makeWave(0.09f, [](float t) {
            float env = expf(-t * 34.0f);
            return (frnd() * 0.45f + sinf(6.2832f * (820.0f - 3800.0f * t) * t) * 0.5f) * env;
        }));
        sSplat.load(makeWave(0.17f, [lp = 0.0f](float t) mutable {
            lp += (frnd() - lp) * 0.22f;
            return (lp * 1.6f + sinf(6.2832f * 130.0f * t) * 0.35f) * expf(-t * 17.0f);
        }));
        sKill = LoadSoundFromWave(makeWave(0.30f, [](float t) {
            float fr = t < 0.1f ? 523.0f : (t < 0.2f ? 659.0f : 784.0f);
            float ph = fmodf(t, 0.1f);
            return (sinf(6.2832f * fr * t) > 0 ? 0.22f : -0.22f) * expf(-ph * 9.0f);
        }));
        sDeath = LoadSoundFromWave(makeWave(0.38f, [](float t) {
            float fr = 420.0f - 320.0f * (t / 0.38f);
            return (fmodf(fr * t, 1.0f) * 2 - 1) * 0.3f * (1.0f - t / 0.38f);
        }));
        sClick = LoadSoundFromWave(makeWave(0.05f, [](float t) {
            return sinf(6.2832f * 1300.0f * t) * expf(-t * 80.0f) * 0.5f;
        }));
        sQueue = LoadSoundFromWave(makeWave(0.14f, [](float t) {
            return sinf(6.2832f * (520.0f + 2600.0f * t) * t) * expf(-t * 12.0f) * 0.4f;
        }));
        sStart = LoadSoundFromWave(makeWave(0.6f, [](float t) {
            float fr = t < 0.18f ? 392.0f : (t < 0.36f ? 523.0f : 659.0f);
            float ph = fmodf(t, 0.18f);
            return sinf(6.2832f * fr * t) * expf(-ph * 6.0f) * 0.45f;
        }));
        sEnd = LoadSoundFromWave(makeWave(0.8f, [](float t) {
            float vib = 1.0f + 0.01f * sinf(6.2832f * 6.0f * t);
            float env = t < 0.06f ? t / 0.06f : (1.0f - t / 0.8f);
            return (sinf(6.2832f * 233.0f * vib * t) + 0.5f * sinf(6.2832f * 466.0f * t)) * env * 0.4f;
        }));
        sBoom = LoadSoundFromWave(makeWave(0.45f, [lp = 0.0f](float t) mutable {
            lp += (frnd() - lp) * 0.12f;
            float thump = sinf(6.2832f * (95.0f - 60.0f * t) * t) * 0.8f;
            return (lp * 1.8f + thump) * expf(-t * 7.0f);
        }));
        sThrow = LoadSoundFromWave(makeWave(0.12f, [](float t) {
            return sinf(6.2832f * (300.0f + 900.0f * t) * t) * expf(-t * 16.0f) * 0.4f;
        }));
        sReady = LoadSoundFromWave(makeWave(0.35f, [](float t) {
            float fr = t < 0.12f ? 660.0f : (t < 0.24f ? 880.0f : 1100.0f);
            return sinf(6.2832f * fr * t) * expf(-fmodf(t, 0.12f) * 10.0f) * 0.4f;
        }));
        sChat = LoadSoundFromWave(makeWave(0.09f, [](float t) {
            return sinf(6.2832f * (900.0f - 300.0f * t) * t) * expf(-t * 30.0f) * 0.4f;
        }));
        musMenu = LoadSoundFromWave(makeMenuLoop());
        musMatch = LoadSoundFromWave(makeMatchLoop());
    }

    // loadout previews use color pair 0, team A
    for (int w = 0; w < W_COUNT; w++)
        for (int s = 0; s < SKIN_COUNT; s++)
            kidPreview[w][s] = buildSprite(kidArt(w), 16, 16, teamColor(0, TEAM_A), teamColorDark(0, TEAM_A), skinColor((u8)s));
    squidPreview = buildSprite(SQUID, 16, 16, teamColor(0, TEAM_A), teamColorDark(0, TEAM_A), skinColor(0));
    for (int h = 1; h < HAT_COUNT; h++)
        hats[h] = buildSprite(hatArt(h), 16, 16, WHITE, GRAY, WHITE);
}

void Assets::buildMatchAssets(u8 colorPair, const GameMap& map) {
    if (matchSpritesReady) {
        for (auto& tw : kid)
            for (auto& tv : tw)
                for (auto& t : tv) UnloadTexture(t);
        UnloadTexture(squid[0]); UnloadTexture(squid[1]);
        UnloadTexture(blob[0]); UnloadTexture(blob[1]);
        UnloadTexture(bomb[0]); UnloadTexture(bomb[1]);
        UnloadTexture(mapTex);
        UnloadTexture(paintTex);
        UnloadImage(paintImg);
    }
    for (int team = 0; team < 2; team++) {
        Color body = teamColor(colorPair, (u8)(team + 1));
        Color dark = teamColorDark(colorPair, (u8)(team + 1));
        for (int w = 0; w < W_COUNT; w++)
            for (int s = 0; s < SKIN_COUNT; s++)
                kid[team][w][s] = buildSprite(kidArt(w), 16, 16, body, dark, skinColor((u8)s));
        squid[team] = buildSprite(SQUID, 16, 16, body, dark, skinColor(0));
        blob[team] = buildSprite(BLOB, 8, 8, body, dark, skinColor(0));
        bomb[team] = buildSprite(BOMB, 8, 8, body, dark, skinColor(0));
    }
    mapTex = buildMapTexture(map, colorPair);
    paintImg = GenImageColor(PW, PH, BLANK);
    paintTex = LoadTextureFromImage(paintImg);
    matchSpritesReady = true;
}

void Assets::updatePaintTex(const PaintGrid& paint, u8 colorPair) {
    Color* px = (Color*)paintImg.data;
    Color a = teamColor(colorPair, TEAM_A), b = teamColor(colorPair, TEAM_B);
    for (int i = 0; i < PW * PH; i++) {
        u8 t = paint.c[i];
        if (t == TEAM_NONE) { px[i] = BLANK; continue; }
        Color c = t == TEAM_A ? a : b;
        u32 h = (u32)i * 2654435761u;
        int j = (int)(h >> 28) - 8;                        // -8..7 brightness jitter
        px[i] = { (unsigned char)std::max(0, std::min(255, c.r + j * 3)),
                  (unsigned char)std::max(0, std::min(255, c.g + j * 3)),
                  (unsigned char)std::max(0, std::min(255, c.b + j * 3)), 255 };
    }
    UpdateTexture(paintTex, paintImg.data);
}
