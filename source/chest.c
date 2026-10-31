#include <string.h>
#include <stdlib.h>
#include "chest.h"
#include "world.h"
#include "blocks.h"
#include "drops.h"
#include "sfx.h"

Chest g_chest[MAX_CHEST];

static const s8 p_dx[5] = {0, 1, -1, 0, 0};
static const s8 p_dz[5] = {0, 0, 0, 1, -1};
static const u8 p_opp[5] = {0, 2, 1, 4, 3};

void chest_clear(void) { memset(g_chest, 0, sizeof(g_chest)); }

int chest_find(int x, int y, int z)
{
    for (int i = 0; i < MAX_CHEST; i++)
        if (g_chest[i].used && g_chest[i].x == x && g_chest[i].y == y && g_chest[i].z == z) return i;
    return -1;
}

int chest_add(int x, int y, int z)
{
    int i = chest_find(x, y, z);
    if (i >= 0) return i;
    for (i = 0; i < MAX_CHEST; i++)
        if (!g_chest[i].used) {
            memset(&g_chest[i], 0, sizeof(Chest));
            g_chest[i].used = 1;
            g_chest[i].x = (u16)x;
            g_chest[i].y = (u8)y;
            g_chest[i].z = (u16)z;
            return i;
        }
    return -1;
}

int chest_get(int x, int y, int z)
{
    int i = chest_find(x, y, z);
    return i >= 0 ? i : chest_add(x, y, z);
}

void chest_link(int x1, int y, int z1, int x2, int z2)
{
    int i1 = chest_get(x1, y, z1);
    int i2 = chest_get(x2, y, z2);
    if (i1 < 0 || i2 < 0) return;
    int dir = 0;
    if (x2 == x1 + 1 && z2 == z1) dir = 1;
    else if (x2 == x1 - 1 && z2 == z1) dir = 2;
    else if (x2 == x1 && z2 == z1 + 1) dir = 3;
    else if (x2 == x1 && z2 == z1 - 1) dir = 4;
    if (dir > 0) {
        g_chest[i1].pair_dir = (u8)dir;
        g_chest[i2].pair_dir = p_opp[dir];
    }
}

static void drop_slot(Chest *c, Slot s)
{
    if (!s.id || !s.count) return;
    drops_spawn_slot((c->x << 12) + 2048, (c->y << 12) + 2048, (c->z << 12) + 2048, s, 0, 150, 0, 15);
}

void chest_remove(int x, int y, int z, int drop_contents)
{
    int i = chest_find(x, y, z);
    if (i < 0) return;
    Chest *c = &g_chest[i];
    if (c->pair_dir >= 1 && c->pair_dir <= 4) {
        int px = x + p_dx[c->pair_dir], pz = z + p_dz[c->pair_dir];
        int pi = chest_find(px, y, pz);
        if (pi >= 0 && g_chest[pi].pair_dir == p_opp[c->pair_dir]) g_chest[pi].pair_dir = 0;
    }
    if (drop_contents) {
        for (int k = 0; k < CHEST_SLOTS; k++) drop_slot(c, c->slots[k]);
    }
    memset(c, 0, sizeof(*c));
}

int chest_partner(int x, int y, int z, int *px, int *pz)
{
    u8 b = world_get(x, y, z);
    if (!BLK_IS_CHEST(b)) return 0;
    int i = chest_find(x, y, z);
    if (i >= 0) {
        u8 d = g_chest[i].pair_dir;
        if (d >= 1 && d <= 4) {
            int tx = x + p_dx[d], tz = z + p_dz[d];
            if (BLK_IS_CHEST(world_get(tx, y, tz))) {
                int pi = chest_find(tx, y, tz);
                if (pi < 0 || g_chest[pi].pair_dir == 0 || g_chest[pi].pair_dir == p_opp[d]) {
                    if (pi >= 0) g_chest[pi].pair_dir = p_opp[d];
                    if (px) *px = tx;
                    if (pz) *pz = tz;
                    return 1;
                }
            }
            g_chest[i].pair_dir = 0;
        }
        return 0;
    }
    return 0;
}

int chest_plan_place(int x, int y, int z, int fx, int fz, u8 *out_block, int *jx, int *jz)
{
    static const int d4[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int nx = -1, nz = -1;
    // Busca un cofre vecino que sea INDIVIDUAL (no unido ya a otro cofre).
    // Si al lado hay un cofre doble, lo ignora y permite colocar este como un cofre individual.
    int pref_x = abs(fz) >= abs(fx);   // si mira a N/S, prefiere unir en X; si mira a E/O, prefiere unir en Z
    for (int pass = 0; pass < 2 && nx < 0; pass++) {
        int want_x = (pass == 0) ? pref_x : !pref_x;
        for (int d = 0; d < 4; d++) {
            int is_x = (d4[d][0] != 0);
            if (is_x != want_x) continue;
            int tx = x + d4[d][0], tz = z + d4[d][1];
            if (!BLK_IS_CHEST(world_get(tx, y, tz))) continue;
            if (chest_partner(tx, y, tz, NULL, NULL)) continue;   // ya es un cofre doble: no se une
            nx = tx; nz = tz;
            break;
        }
    }
    if (jx) *jx = nx;
    if (jz) *jz = nz;
    if (nx >= 0) {
        int face = (nx != x) ? (fz < 0 ? 2 : 0) : (fx > 0 ? 3 : 1);
        *out_block = (u8)(B_CHEST + face);
    } else {
        int face = abs(fx) > abs(fz) ? (fx > 0 ? 3 : 1) : (fz < 0 ? 2 : 0);
        *out_block = (u8)(B_CHEST + face);
    }
    return 1;
}

int chest_can_open(int x, int y, int z)
{
    if (blk_opaque(world_get(x, y + 1, z))) return 0;
    int px, pz;
    if (chest_partner(x, y, z, &px, &pz) && blk_opaque(world_get(px, y + 1, pz))) return 0;
    return 1;
}

int chest_open_pair(int x, int y, int z, int *c1, int *c2)
{
    int px, pz;
    if (chest_partner(x, y, z, &px, &pz)) {
        // Orden canonico como en 1.7.10: la coordenada menor (x o z) es la mitad superior (slots 0..26)
        int x1 = x, z1 = z, x2 = px, z2 = pz;
        if (px < x || pz < z) { x1 = px; z1 = pz; x2 = x; z2 = z; }
        *c1 = chest_get(x1, y, z1);
        *c2 = chest_get(x2, y, z2);
        return *c1 >= 0;
    }
    *c1 = chest_get(x, y, z);
    *c2 = -1;
    return *c1 >= 0;
}

void chest_update(int open_c1, int open_c2)
{
    for (int i = 0; i < MAX_CHEST; i++) {
        Chest *c = &g_chest[i];
        if (!c->used) continue;
        if (!world_col_loaded(c->x, c->z)) continue;
        if (!BLK_IS_CHEST(world_get(c->x, c->y, c->z))) {
            chest_remove(c->x, c->y, c->z, 1);
            continue;
        }
        int is_open = (i == open_c1 || i == open_c2);
        if (!is_open) {
            int px, pz;
            if (chest_partner(c->x, c->y, c->z, &px, &pz)) {
                int pi = chest_find(px, c->y, pz);
                if (pi >= 0 && (pi == open_c1 || pi == open_c2)) is_open = 1;
            }
        }
        if (is_open) {
            if (c->open_t < CHEST_ANIM_TICKS) c->open_t++;
        } else if (c->open_t > 0) {
            u8 prev = c->open_t--;
            // Como en TileEntityChest de 1.7.10: suena al cruzar la mitad del recorrido de cierre
            // (para un cofre doble solo suena la mitad principal para no duplicar el sonido).
            if (prev == CHEST_ANIM_TICKS / 2) {
                int px, pz;
                int primary = 1;
                if (chest_partner(c->x, c->y, c->z, &px, &pz) && (px < c->x || pz < c->z)) primary = 0;
                if (primary) sfx_chest_close();
            }
        }
    }
}
