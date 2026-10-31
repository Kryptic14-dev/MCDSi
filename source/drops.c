// Items sueltos: fisica sencilla, recogida y fusion de pilas
#include <stdlib.h>
#include "drops.h"
#include "sfx.h"
#include "world.h"
#include "blocks.h"
#include "items.h"

Drop g_drops[MAX_DROPS];

#define D_GRAVITY   20          // 4.12 por tick^2 (items de MC caen mas lento que el jugador)
#define D_TERMINAL  (-1500)
#define D_LIFE      18000       // 5 minutos a 60 Hz
#define PICK_H      (FP + FP / 5)   // 1.2 bloques en horizontal
#define MERGE_R     (FP / 2)

void drops_clear(void)
{
    for (int i = 0; i < MAX_DROPS; i++) g_drops[i].active = 0;
}

static int alloc_drop(void)
{
    for (int i = 0; i < MAX_DROPS; i++) if (!g_drops[i].active) return i;
    // lleno: se reemplaza el mas viejo
    int old = 0;
    for (int i = 1; i < MAX_DROPS; i++) if (g_drops[i].age > g_drops[old].age) old = i;
    return old;
}

void drops_spawn_slot(int x, int y, int z, Slot s, int vx, int vy, int vz, int delay)
{
    if (!s.id || !s.count) return;
    int i = alloc_drop();
    Drop *d = &g_drops[i];
    d->active = 1; d->id = s.id; d->count = s.count; d->dmg = s.dmg;
    d->x = x; d->y = y; d->z = z;
    d->vx = vx; d->vy = vy; d->vz = vz;
    d->age = 0; d->delay = (u16)delay; d->on_ground = 0;
}

void drops_spawn_block(int bx, int by, int bz, u8 id, int n)
{
    Slot s = { id, (u8)n, 0 };
    int vx = (rand() % 161) - 80, vz = (rand() % 161) - 80;
    drops_spawn_slot((bx << 12) + FP / 2, (by << 12) + FP / 4, (bz << 12) + FP / 2, s, vx, 200, vz, 15);
}

static inline int solid_at(int x, int y, int z)
{
    return blk_solid(world_get(x >> 12, y >> 12, z >> 12));
}

static void drop_physics(Drop *d)
{
    d->vy -= D_GRAVITY;
    if (d->vy < D_TERMINAL) d->vy = D_TERMINAL;

    // si quedo dentro de un bloque (p. ej. al poner uno encima) se sube
    for (int k = 0; k < 4 && solid_at(d->x, d->y + FP / 8, d->z); k++) {
        d->y = ((d->y >> 12) + 1) << 12;
        d->vy = 0;
    }

    int nx = d->x + d->vx;
    if (solid_at(nx, d->y + FP / 8, d->z)) d->vx = -d->vx / 3; else d->x = nx;
    int nz = d->z + d->vz;
    if (solid_at(d->x, d->y + FP / 8, nz)) d->vz = -d->vz / 3; else d->z = nz;

    int ny = d->y + d->vy;
    d->on_ground = 0;
    if (d->vy < 0 && solid_at(d->x, ny, d->z)) {
        d->y = ((ny >> 12) + 1) << 12;
        d->vy = 0;
        d->on_ground = 1;
    } else if (d->vy > 0 && solid_at(d->x, ny + FP / 4, d->z)) {
        d->vy = 0;
    } else {
        d->y = ny;
    }
    if (d->y < 0) d->active = 0;   // cayo al vacio

    // rozamiento (mas fuerte en el suelo)
    if (d->on_ground) { d->vx = d->vx * 3 / 5; d->vz = d->vz * 3 / 5; }
    else { d->vx = d->vx * 31 / 32; d->vz = d->vz * 31 / 32; }
}

void drops_update(Player *p)
{
    for (int i = 0; i < MAX_DROPS; i++) {
        Drop *d = &g_drops[i];
        if (!d->active) continue;
        if (!world_col_loaded(d->x >> 12, d->z >> 12)) continue;   // columna descargada: congelado

        d->age++;
        if (d->age >= D_LIFE) { d->active = 0; continue; }
        if (d->delay) d->delay--;
        drop_physics(d);
        if (!d->active) continue;

        // fusionar pilas iguales cercanas (cada 8 ticks)
        if (((d->age + i) & 7) == 0 && item_max_stack(d->id) > 1) {
            for (int j = 0; j < MAX_DROPS; j++) {
                Drop *e = &g_drops[j];
                if (j == i || !e->active || e->id != d->id) continue;
                if (abs(e->x - d->x) > MERGE_R || abs(e->z - d->z) > MERGE_R || abs(e->y - d->y) > MERGE_R) continue;
                int room = 64 - d->count;
                if (room <= 0) break;
                int mv = e->count < room ? e->count : room;
                d->count += (u8)mv; e->count -= (u8)mv;
                if (!e->count) e->active = 0;
            }
        }

        // recogida por el jugador
        if (p->health > 0 && !d->delay &&
            abs(d->x - p->x) < PICK_H && abs(d->z - p->z) < PICK_H &&
            d->y > p->y - (FP * 7 / 10) && d->y < p->y + FP * 2 + FP / 3) {
            Slot s = { d->id, d->count, d->dmg };
            int left = inv_give(p, s);
            if (left < s.count) sfx_pop();
            if (left <= 0) d->active = 0;
            else d->count = (u8)left;    // inventario lleno: queda en el suelo lo que no cabe
        }
    }
}
