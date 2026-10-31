#ifndef RENDER_H
#define RENDER_H

#include <nds.h>
#include "player.h"

extern u32 g_polyfog;   // POLY_FOG mientras se dibuja el mundo (niebla por hardware), 0 en el resto

void gfx_init(void);
void gfx_load_menu(void);
void gfx_load_game(void);
void gfx_unload(void);
void gfx_prepare_title(const char *splash);   // hornea frase + textos del menu (tras gfx_load_menu)

void render_blank(int r, int g, int b);                 // fotograma vacio (carga)
void render_loading_bg(void);                          // fondo de tierra
void render_title(int frame, const char *splash);      // menu principal (pantalla superior)
void render_game(const Player *p, int show_hud);       // partida
void render_prebuild(const Player *p, int max_sections);
void render_free_world(void);
int  render_quads_last(void);
void render_draw_skeleton_bow(int pull_stage, int light);
int  render_sample_light(int wx, int wy, int wz);

#endif
