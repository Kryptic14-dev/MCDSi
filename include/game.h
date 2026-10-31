// Estado global del juego
#ifndef GAME_H
#define GAME_H

#include <nds.h>
#include "world.h"

typedef enum {
    ST_TITLE, ST_SELECT, ST_CREATE, ST_OPTIONS, ST_LOADING,
    ST_PLAY, ST_PAUSE, ST_INVENTORY, ST_DEATH
} GameState;

typedef struct {
    int render_dist;   // 0..3  (Tiny, Short, Normal, Far)
    int sens;          // 0..2
    int invert_y;      // 0/1
    int fov;           // 0..2  (60, 70, 90)
    int autojump;      // 0/1  salto automatico de bloques
} Options;

typedef struct {
    int gamemode;      // 0 = Survival, 1 = Creative
    int worldtype;     // WT_DEFAULT / WT_FLAT
    u32 seed;
    char name[24];     // vacio = nombre automatico (World1, World2...)
} NewWorld;

extern GameState g_state;
extern GameState g_options_return;
extern Options g_opt;
extern NewWorld g_new;
extern int g_frame;
extern int g_fps;

static inline int opt_render_blocks(void)
{
    static const int d[4] = {8, 12, 16, 20};
    return d[g_opt.render_dist & 3];
}

static inline int opt_fov_degrees(void)
{
    static const int f[3] = {60, 70, 90};
    return f[g_opt.fov % 3];
}

#endif
