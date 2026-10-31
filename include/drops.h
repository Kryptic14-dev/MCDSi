// Items sueltos en el mundo (item drop)
#ifndef DROPS_H
#define DROPS_H

#include <nds.h>
#include "player.h"

#define MAX_DROPS 64

typedef struct {
    u8  active;
    u8  id, count;
    u16 dmg;
    int x, y, z;          // 4.12, centro-abajo del item
    int vx, vy, vz;
    u16 age;              // ticks a 60 Hz (animacion y despawn)
    u16 delay;            // ticks hasta poder recogerse
    u8  on_ground;
} Drop;

extern Drop g_drops[MAX_DROPS];

void drops_clear(void);
// Suelta un item con una pequena velocidad aleatoria (al romper un bloque)
void drops_spawn_block(int bx, int by, int bz, u8 id, int n);
// Suelta un slot completo con velocidad y retardo de recogida
void drops_spawn_slot(int x, int y, int z, Slot s, int vx, int vy, int vz, int delay);
void drops_update(Player *p);   // 1 tick (1/60 s)

#endif
