#include <nds.h>
#include <stdbool.h>
#include "sfx.h"
#include "sfx_ids.h"
#include "blocks.h"

// data/sfx.bin: u32 n; n x {u32 offset, u32 size, u32 rate}; datos s8 mono.
// El tono de Minecraft 1.7.10 ya esta aplicado al convertir (romper/poner 0.8x, golpeo 0.5x, pop 2x):
// la DS no interpola al reproducir, asi que cambiar la tasa en tiempo real suena rasposo.
// IMPORTANTE: la DS tampoco interpola al reproducir las muestras guardadas (muestra y mantener), asi que una
// muestra a 8-16 kHz suena mas aguda en la consola que en un emulador que interpola. tools/convert_sounds.py
// ya elige tasas (hasta 32768 Hz, la tasa de mezcla de la DS) con las que el tono coincide con el original.
// Por eso aqui se reproduce SIEMPRE a la tasa guardada, sin tocarla.
extern const u8 sfx_bin[];

// Minecraft 1.7.10 NO varia el tono de pasos / romper / poner (la variedad viene de las variantes .ogg).
// Esto es un +/- % aleatorio de tasa; 0 = fiel al original. (Antes: 6.)
#define SFX_JITTER_PCT 0

static int s_ready;
static u8 s_last[SFXG_COUNT];
static u32 s_rng = 12345;

void sfx_init(void)
{
    const u32 *h = (const u32 *)sfx_bin;
    u32 total = h[0] ? h[1 + 3 * (h[0] - 1)] + h[2 + 3 * (h[0] - 1)] : 0;
    DC_FlushRange(sfx_bin, total);   // el ARM7 lee los datos directamente de la RAM
    soundEnable();
    s_ready = 1;
}

// max_ms > 0: reproduce solo el principio del sonido (golpes al picar). pan: 0 izq .. 64 centro .. 127 der
static void play_id_pan(int id, int vol, int max_ms, int pan)
{
    const u32 *h = (const u32 *)sfx_bin;
    if (!s_ready || id >= (int)h[0]) return;
    u32 off = h[1 + 3 * id], size = h[2 + 3 * id], rate = h[3 + 3 * id];
    if (max_ms > 0) {
        u32 lim = (rate * (u32)max_ms / 1000) & ~3u;
        if (lim < size) size = lim;
    }
    if (vol > 127) vol = 127;
    if (vol < 1) return;
#if SFX_JITTER_PCT > 0
    s_rng = s_rng * 1664525u + 1013904223u;
    rate = rate * (100u - SFX_JITTER_PCT + ((s_rng >> 16) % (2u * SFX_JITTER_PCT + 1u))) / 100u;
#endif
    soundPlaySample(sfx_bin + off, SoundFormat_8Bit, size, rate, vol, pan, false, 0);
}

static void play_id(int id, int vol, int max_ms) { play_id_pan(id, vol, max_ms, 64); }

static int pick_variant(int g)
{
    int n = sfx_group_count[g], k = 0;
    s_rng = s_rng * 1664525u + 1013904223u;
    if (n > 1) {
        k = (s_rng >> 16) % n;
        if (k == s_last[g]) k = (k + 1) % n;
    }
    s_last[g] = (u8)k;
    return k;
}

static void play_group(int g, int vol, int max_ms)
{
    int n = sfx_group_count[g], k = 0;
    s_rng = s_rng * 1664525u + 1013904223u;
    if (n > 1) {
        k = (s_rng >> 16) % n;
        if (k == s_last[g]) k = (k + 1) % n;
    }
    s_last[g] = (u8)k;
    play_id(sfx_group_first[g] + k, vol, max_ms);
}

void sfx_play_group(int g, int vol) { play_group(g, vol, 0); }

enum { M_STONE, M_GRASS, M_GRAVEL, M_SAND, M_WOOD, M_CLOTH, M_SNOW, M_GLASS, M_NONE };

static int material(u8 b)
{
    switch (b) {
        case B_AIR: case B_WATER: return M_NONE;
        case B_GRASS: case B_TALLGRASS: case B_DANDELION: case B_ROSE: case B_LEAVES: return M_GRASS;
        case B_DIRT: case B_GRAVEL: return M_GRAVEL;
        case B_SAND: return M_SAND;
        case B_PLANKS: case B_LOG: case B_CRAFTING: case B_BOOKSHELF:
        case B_CHEST: case B_CHEST_E: case B_CHEST_S: case B_CHEST_W:
        case B_TORCH: case B_TORCH_E: case B_TORCH_S: case B_TORCH_W: case B_TORCH_N:
        case B_BED_HEAD: case B_BED_FOOT: case B_BED_FOOT_E: case B_BED_FOOT_S: case B_BED_FOOT_W: return M_WOOD;
        case B_WOOL_WHITE: case B_WOOL_RED: case B_WOOL_BLUE: case B_WOOL_GREEN:
        case B_WOOL_YELLOW: case B_WOOL_BLACK: return M_CLOTH;
        case B_SNOW: return M_SNOW;
        case B_GLASS: return M_GLASS;
        default: return M_STONE;
    }
}

void sfx_dig(u8 b, int vol)
{
    switch (material(b)) {
        case M_STONE:  sfx_play_group(SFXG_DIG_STONE, vol);  break;
        case M_GRASS:  sfx_play_group(SFXG_DIG_GRASS, vol);  break;
        case M_GRAVEL: sfx_play_group(SFXG_DIG_GRAVEL, vol); break;
        case M_SAND:   sfx_play_group(SFXG_DIG_SAND, vol);   break;
        case M_WOOD:   sfx_play_group(SFXG_DIG_WOOD, vol);   break;
        case M_CLOTH:  sfx_play_group(SFXG_DIG_CLOTH, vol);  break;
        case M_SNOW:   sfx_play_group(SFXG_DIG_SNOW, vol);   break;
        case M_GLASS:  sfx_play_group(SFXG_GLASS, vol);      break;
    }
}

void sfx_hit(u8 b)
{
    // golpeo mientras se pica: como en 1.7.10 es el sonido de PASO del material a media velocidad
    // (ya guardado asi en sfx.bin) y a volumen bajo
    switch (material(b)) {
        case M_STONE: case M_GLASS: play_group(SFXG_HIT_STONE, 45, 0); break;
        case M_GRASS:  play_group(SFXG_HIT_GRASS, 45, 0);  break;
        case M_GRAVEL: play_group(SFXG_HIT_GRAVEL, 45, 0); break;
        case M_SAND:   play_group(SFXG_HIT_SAND, 45, 0);   break;
        case M_WOOD:   play_group(SFXG_HIT_WOOD, 45, 0);   break;
        case M_CLOTH:  play_group(SFXG_HIT_CLOTH, 45, 0);  break;
        case M_SNOW:   play_group(SFXG_HIT_SNOW, 45, 0);   break;
    }
}

void sfx_step(u8 b, int vol)
{
    switch (material(b)) {
        case M_STONE: case M_GLASS: sfx_play_group(SFXG_STEP_STONE, vol); break;
        case M_GRASS:  sfx_play_group(SFXG_STEP_GRASS, vol);  break;
        case M_GRAVEL: sfx_play_group(SFXG_STEP_GRAVEL, vol); break;
        case M_SAND:   sfx_play_group(SFXG_STEP_SAND, vol);   break;
        case M_WOOD:   sfx_play_group(SFXG_STEP_WOOD, vol);   break;
        case M_CLOTH:  sfx_play_group(SFXG_STEP_CLOTH, vol);  break;
        case M_SNOW:   sfx_play_group(SFXG_STEP_SNOW, vol);   break;
    }
}

void sfx_click(void)      { sfx_play_group(SFXG_CLICK, 100); }
void sfx_pop(void)
{
    // random.pop de 1.7.10: tono ((r1-r2)*0.7+1)*2 recortado a [0.5, 2.0] -> la mitad de las veces 2.0 y el resto
    // mas grave. sfx.bin trae 5 variantes (POP_PITCHES en convert_sounds.py): 0..3 mas graves, 4 = 2.0.
    static const u8 pick[8] = {4, 4, 4, 4, 0, 1, 2, 3};
    s_rng = s_rng * 1664525u + 1013904223u;
    int k = pick[(s_rng >> 16) & 7];
    if (k >= sfx_group_count[SFXG_POP]) k = sfx_group_count[SFXG_POP] - 1;
    play_id(sfx_group_first[SFXG_POP] + k, 70, 0);
}
void sfx_splash(void)     { sfx_play_group(SFXG_SPLASH, 90); }
void sfx_swim(void)       { sfx_play_group(SFXG_SWIM, 60); }
void sfx_tool_break(void) { sfx_play_group(SFXG_TOOLBREAK, 100); }

// ---------------------------------------------------------------------------
// Sonido posicional (mobs, explosiones, flechas): el volumen baja linealmente hasta 0 a 16 bloques
// y se reparte entre el altavoz izquierdo y derecho segun hacia donde mira el jugador.
// dx, dz = posicion de la fuente relativa al jugador (4.12); yaw = giro del jugador (15 bits).
// ---------------------------------------------------------------------------
static u32 isqrt_u32(u32 v)
{
    u32 r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

void sfx_play_group_3d(int group, int vol, int dx, int dz, int yaw)
{
    s64 d2 = (s64)dx * dx + (s64)dz * dz;
    int d16 = (int)isqrt_u32((u32)(d2 >> 16));           // distancia en 1/16 de bloque
    if (d16 >= 256) return;
    int v = vol * (256 - d16) / 256;
    if (v < 4) return;
    int pan = 64;
    if (d16 > 8) {
        int right = (int)(((s64)dx * cosLerp((s16)yaw) + (s64)dz * sinLerp((s16)yaw)) >> 12);   // 4.12
        int dist = d16 * 256;                                                                    // 4.12
        pan = 64 + right * 45 / dist;
        if (pan < 8) pan = 8;
        if (pan > 120) pan = 120;
    }
    play_id_pan(sfx_group_first[group] + pick_variant(group), v, 0, pan);
}

// paso de un mob sin sonido propio (creeper): el del bloque que pisa, con posicion
void sfx_step_3d(u8 b, int vol, int dx, int dz, int yaw)
{
    int g;
    switch (material(b)) {
        case M_STONE: case M_GLASS: g = SFXG_STEP_STONE; break;
        case M_GRASS:  g = SFXG_STEP_GRASS;  break;
        case M_GRAVEL: g = SFXG_STEP_GRAVEL; break;
        case M_SAND:   g = SFXG_STEP_SAND;   break;
        case M_WOOD:   g = SFXG_STEP_WOOD;   break;
        case M_CLOTH:  g = SFXG_STEP_CLOTH;  break;
        case M_SNOW:   g = SFXG_STEP_SNOW;   break;
        default: return;
    }
    sfx_play_group_3d(g, vol, dx, dz, yaw);
}

void sfx_hurt(void)
{
    sfx_play_group(SFXG_HURT, 110);                      // el "uh"
#ifdef HAVE_SFXG_DAMAGE_HIT
    sfx_play_group(SFXG_DAMAGE_HIT, 110);                // y el golpe de daño de 1.7.10 (damage/hit1-3)
#endif
}
void sfx_eat(void)   { sfx_play_group(SFXG_EAT, 90); }
void sfx_burp(void)  { sfx_play_group(SFXG_BURP, 90); }
void sfx_chest_open(void)
{
#ifdef HAVE_SFXG_CHESTOPEN
    sfx_play_group(SFXG_CHESTOPEN, 95);
#endif
}
void sfx_chest_close(void)
{
#ifdef HAVE_SFXG_CHESTCLOSE
    sfx_play_group(SFXG_CHESTCLOSE, 95);
#endif
}
