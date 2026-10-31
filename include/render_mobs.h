#ifndef RENDER_MOBS_H
#define RENDER_MOBS_H

#include <nds.h>
#include "player.h"

enum { PART_HEAD, PART_BODY, PART_ARM, PART_LEG };

void mobgfx_load(void);     // sube las texturas de mobs y armaduras (tras gfx_unload)
void mobgfx_unload(void);
// dibuja los mobs y flechas visibles (coordenadas de mundo ya activas); light = brillo 12..31
void mobgfx_draw_all(const Player *p, int fx, int fy, int fz, int light, int quad_budget);
// armadura sobre una parte del modelo de Steve (la matriz actual ya esta colocada como la de esa parte)
void mobgfx_armor_part(const Player *p, int part, int light);

#endif
