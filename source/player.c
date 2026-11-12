#include <string.h>
#include <stdlib.h>
#include "player.h"
#include "game.h"
#include "blocks.h"
#include "items.h"
#include "drops.h"
#include "sfx.h"
#include "mobs.h"
#include "furnace.h"
#include "chest.h"
#include "daynight.h"

// Dimensiones del jugador (punto fijo 4096 = 1 bloque)
#define PW   1229    // medio ancho 0.3
#define PH   7373    // alto 1.8
#define EYEH 6636    // ojos 1.62
#define EYEH_SNEAK 6144

#define WALK_SPEED   340    // ~5 bloques/s a 60 fps (340 * 60 / 4096)
// Multiplicador de correr en 4.12: x1.3 = 1.3 * 4096 = 5325.
// (Antes valia 1331 = x0.325: al hacer doble toque en la cruceta Steve iba 3 veces MAS LENTO.)
#define SPRINT_MULT  5325
#define FLY_SPEED    620
#define JUMP_VEL     573
#define GRAVITY      36
#define WATER_CLIMB  360    // impulso vertical al trepar a un bloque desde el agua
#define TERMINAL     -4000
#define REACH        (5 * FP)
#define BED_TOP      2304   // altura de la cama: 9/16 de bloque (4.12)

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
static inline int fsin(int a) { return sinLerp((s16)a); }
static inline int fcos(int a) { return cosLerp((s16)a); }

void player_forward(const Player *p, int *fx, int *fy, int *fz)
{
    int cp = fcos(p->pitch);
    *fx = (fsin(p->yaw) * cp) >> 12;
    *fy = fsin(p->pitch);
    *fz = -((fcos(p->yaw) * cp) >> 12);
}

void player_eye(const Player *p, int *ex, int *ey, int *ez)
{
    *ex = p->x;
    *ey = p->y + (p->sleep_t > 0 ? 1100 : (p->sneaking ? EYEH_SNEAK : EYEH));   // acostado: la vista queda a ras de la cama
    *ez = p->z;
}

u8 player_held(const Player *p) { return p->inv[p->sel].id; }

int inv_add(Player *p, u8 id, int n)
{
    int max = item_max_stack(id);
    // 1) apilar en un slot existente
    if (max > 1)
        for (int i = 0; i < 36 && n > 0; i++) {
            if (p->inv[i].id == id && p->inv[i].count < max) {
                int add = max - p->inv[i].count;
                if (add > n) add = n;
                p->inv[i].count += add;
                n -= add;
            }
        }
    // 2) huecos vacios
    for (int i = 0; i < 36 && n > 0; i++) {
        if (p->inv[i].id == 0) {
            int add = n > max ? max : n;
            p->inv[i].id = id;
            p->inv[i].count = add;
            p->inv[i].dmg = 0;
            n -= add;
        }
    }
    return n == 0;
}

// Mete un slot en el inventario y devuelve cuantas unidades NO han cabido (0 = todo dentro)
int inv_give(Player *p, Slot s)
{
    if (!s.id || !s.count) return 0;
    if (item_max_stack(s.id) > 1) {
        int max = 64, n = s.count;
        for (int i = 0; i < 36 && n > 0; i++)
            if (p->inv[i].id == s.id && p->inv[i].count < max) {
                int add = max - p->inv[i].count; if (add > n) add = n;
                p->inv[i].count += add; n -= add;
            }
        for (int i = 0; i < 36 && n > 0; i++)
            if (!p->inv[i].id) {
                int add = n > max ? max : n;
                p->inv[i].id = s.id; p->inv[i].count = add; p->inv[i].dmg = 0; n -= add;
            }
        return n;
    }
    for (int i = 0; i < 36; i++)
        if (!p->inv[i].id) { p->inv[i] = s; return 0; }
    return s.count ? s.count : 1;
}

// Lanza un slot hacia donde mira el jugador (tirar item / inventario lleno)
void player_drop_slot(Player *p, Slot s)
{
    int ex, ey, ez, fx, fy, fz;
    player_eye(p, &ex, &ey, &ez);
    player_forward(p, &fx, &fy, &fz);
    drops_spawn_slot(ex, ey - FP / 4, ez, s, (fx * 420) >> 12, ((fy * 420) >> 12) + 100, (fz * 420) >> 12, 70);
}

int inv_add_slot(Player *p, Slot s)
{
    if (!s.id) return 1;
    if (item_max_stack(s.id) > 1) return inv_add(p, s.id, s.count);
    for (int i = 0; i < 36; i++)
        if (p->inv[i].id == 0) { p->inv[i] = s; return 1; }
    return 0;
}

void player_init(Player *p, int gamemode)
{
    memset(p, 0, sizeof(*p));
    p->x = (g_spawn_x << 12) + 2048;
    p->y = g_spawn_y << 12;
    p->z = (g_spawn_z << 12) + 2048;
    p->yaw = 0;
    p->pitch = 0;
    p->gamemode = gamemode;
    p->health = 20;
    p->food = 20;
    p->air = MAX_AIR;
    p->brk_x = p->brk_y = p->brk_z = -1;
    p->food_sat = 500;
    p->mob_target = -1;
    p->fall_y = p->y;
    p->body_yaw = p->yaw;
    if (gamemode == 1) {
        // Hotbar inicial del modo creativo
        static const u8 start[9] = {B_STONE, B_DIRT, B_GRASS, B_COBBLE, B_PLANKS, B_LOG, B_GLASS, B_SAND, B_BRICK};
        for (int i = 0; i < 9; i++) { p->inv[i].id = start[i]; p->inv[i].count = 64; }
    }
}

void player_look(Player *p, int dx, int dy)
{
    if (p->sleep_t > 0) return;
    p->yaw = (p->yaw + dx) & 0x7FFF;
    p->pitch += (g_opt.invert_y ? dy : -dy);
    if (p->pitch > 7900) p->pitch = 7900;
    if (p->pitch < -7900) p->pitch = -7900;
}

// ---------------------------------------------------------------------------
// Colisiones
// ---------------------------------------------------------------------------
static void start_swing(Player *p) { if (p->swing == 0) p->swing = SWING_LEN; }

static int box_hits(int x, int y, int z)
{
    int x0 = (x - PW) >> 12, x1 = (x + PW - 1) >> 12;
    int y0 = y >> 12, y1 = (y + PH - 1) >> 12;
    int z0 = (z - PW) >> 12, z1 = (z + PW - 1) >> 12;
    for (int cy = y0; cy <= y1; cy++)
        for (int cz = z0; cz <= z1; cz++)
            for (int cx = x0; cx <= x1; cx++)
            {
                u8 b = world_get(cx, cy, cz);
                if (BLK_IS_BED(b)) { if (y < (cy << 12) + BED_TOP) return 1; }     // la cama solo mide 9/16
                else if (blk_solid(b)) return 1;
            }
    return 0;
}

// Altura del suelo sobre el que aterriza una caja que acaba de chocar hacia abajo en (x, y, z)
static int floor_top(int x, int y, int z)
{
    int cy = y >> 12, any = 0, top = 0;
    int x0 = (x - PW) >> 12, x1 = (x + PW - 1) >> 12;
    int z0 = (z - PW) >> 12, z1 = (z + PW - 1) >> 12;
    for (int cz = z0; cz <= z1; cz++)
        for (int cx = x0; cx <= x1; cx++) {
            u8 b = world_get(cx, cy, cz);
            int t;
            if (BLK_IS_BED(b)) t = (cy << 12) + BED_TOP;
            else if (blk_solid(b)) t = (cy + 1) << 12;
            else continue;
            if (!any || t > top) { top = t; any = 1; }
        }
    return any ? top : ((cy + 1) << 12);
}

static int has_support(int x, int y, int z) { return box_hits(x, y - 410, z); }

// Mueve un eje; devuelve 1 si choca
static int move_axis(Player *p, int axis, int d)
{
    while (d != 0) {
        int step = d > 2048 ? 2048 : (d < -2048 ? -2048 : d);
        int nx = p->x, ny = p->y, nz = p->z;
        if (axis == 0) nx += step; else if (axis == 1) ny += step; else nz += step;

        if (axis != 1 && p->sneaking && p->on_ground && !p->flying) {
            if (!has_support(nx, ny, nz) && has_support(p->x, p->y, p->z)) return 1;
        }
        if (box_hits(nx, ny, nz)) {
            if (axis == 0) {
                if (step > 0) p->x = (((nx + PW - 1) >> 12) << 12) - PW;
                else p->x = ((((nx - PW) >> 12) + 1) << 12) + PW;
            } else if (axis == 2) {
                if (step > 0) p->z = (((nz + PW - 1) >> 12) << 12) - PW;
                else p->z = ((((nz - PW) >> 12) + 1) << 12) + PW;
            } else {
                if (step > 0) p->y = (((ny + PH - 1) >> 12) << 12) - PH;
                else { p->y = floor_top(nx, ny, nz); p->on_ground = 1; }
            }
            return 1;
        }
        p->x = nx; p->y = ny; p->z = nz;
        d -= step;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Rayo (DDA de Amanatides & Woo en punto fijo)
// ---------------------------------------------------------------------------
static void raycast(Player *p)
{
    int ex, ey, ez, fx, fy, fz;
    player_eye(p, &ex, &ey, &ez);
    player_forward(p, &fx, &fy, &fz);
    p->has_target = 0;
    p->target_dist = 0x7FFFFFFF;

    int cell[3] = {ex >> 12, ey >> 12, ez >> 12};
    int pos[3] = {ex, ey, ez};
    int dir[3] = {fx, fy, fz};
    int step[3], tmax[3], tdel[3];
    for (int a = 0; a < 3; a++) {
        if (dir[a] == 0) { step[a] = 0; tmax[a] = 0x7FFFFFFF; tdel[a] = 0x7FFFFFFF; continue; }
        step[a] = dir[a] > 0 ? 1 : -1;
        int next = (dir[a] > 0) ? ((cell[a] + 1) << 12) - pos[a] : pos[a] - (cell[a] << 12);
        int ad = abs(dir[a]);
        tmax[a] = (int)(((s64)next << 12) / ad);
        tdel[a] = (int)(((s64)FP << 12) / ad);
    }
    for (int i = 0; i < 40; i++) {
        int a = 0;
        if (tmax[1] < tmax[a]) a = 1;
        if (tmax[2] < tmax[a]) a = 2;
        if (tmax[a] > REACH) return;
        int hit_t = tmax[a];
        cell[a] += step[a];
        tmax[a] += tdel[a];
        u8 b = world_get(cell[0], cell[1], cell[2]);
        if (b != B_AIR && b != B_WATER) {
            if (cell[1] < 0 || cell[1] >= WY || cell[0] < 0 || cell[0] >= WX || cell[2] < 0 || cell[2] >= WZ) return;
            p->has_target = 1;
            p->target_dist = hit_t;
            p->tx = cell[0]; p->ty = cell[1]; p->tz = cell[2];
            p->place_x = cell[0]; p->place_y = cell[1]; p->place_z = cell[2];
            p->place_x -= (a == 0) ? step[0] : 0;
            p->place_y -= (a == 1) ? step[1] : 0;
            p->place_z -= (a == 2) ? step[2] : 0;
            if (a == 0) p->tface = step[0] > 0 ? D_NX : D_PX;
            else if (a == 1) p->tface = step[1] > 0 ? D_NY : D_PY;
            else p->tface = step[2] > 0 ? D_NZ : D_PZ;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Romper / poner bloques
// ---------------------------------------------------------------------------
static void break_block(Player *p, int x, int y, int z)
{
    u8 b = world_get(x, y, z);
    if (b == B_AIR || b == B_WATER || b == B_BEDROCK) return;
    if (BLK_IS_FURNACE(b)) furnace_remove(x, y, z, 1);   // suelta lo que contenia
    if (BLK_IS_CHEST(b)) chest_remove(x, y, z, 1);
    if (BLK_IS_BED(b)) {       // la cama son dos bloques: se quitan juntos y solo suelta un item
        int fd = bed_facing(x, y, z);
        int sgn = (b == B_BED_HEAD) ? -1 : 1;
        int ox = x + sgn * bed_dx(fd), oz = z + sgn * bed_dz(fd);
        if (BLK_IS_BED(world_get(ox, y, oz))) world_set(ox, y, oz, B_AIR);
    }
    world_set(x, y, z, B_AIR);
    sfx_dig(b, 100);
    if (p->gamemode == 0) {
        p->food_exh += 3;
        Slot *h = &p->inv[p->sel];
        int kind, tier;
        block_tool_req(b, &kind, &tier);
        int have = (h->id && tool_kind(h->id) == kind && kind >= 0);
        int can = (tier < 0) || (have && tool_tier(h->id) >= tier);
        if (can && g_blocks[b].drop != B_AIR) drops_spawn_block(x, y, z, g_blocks[b].drop, 1);
        // desgaste de la herramienta
        if (h->id && tool_kind(h->id) >= 0) {
            if (++h->dmg >= item_max_dmg(h->id)) { h->id = 0; h->count = 0; h->dmg = 0; sfx_tool_break(); }
        }
    }
    // las plantas y antorchas apoyadas en este bloque se caen
    u8 above = world_get(x, y + 1, z);
    if ((g_blocks[above].flags & F_PLANT) || above == B_TORCH) {
        if (p->gamemode == 0 && g_blocks[above].drop != B_AIR) drops_spawn_block(x, y + 1, z, g_blocks[above].drop, 1);
        world_set(x, y + 1, z, B_AIR);
    }
    static const s8 tdx[4] = {1, 0, -1, 0}, tdz[4] = {0, 1, 0, -1};
    static const u8 twall[4] = {B_TORCH_E, B_TORCH_S, B_TORCH_W, B_TORCH_N};
    for (int d = 0; d < 4; d++) {
        int tx = x + tdx[d], tz = z + tdz[d];
        if (world_get(tx, y, tz) == twall[d]) {
            if (p->gamemode == 0) drops_spawn_block(tx, y, tz, B_TORCH, 1);
            world_set(tx, y, tz, B_AIR);
        }
    }
    start_swing(p);
}

static int player_overlaps_cell(const Player *p, int cx, int cy, int cz)
{
    int x0 = cx << 12, x1 = x0 + 4096, y0 = cy << 12, y1 = y0 + 4096, z0 = cz << 12, z1 = z0 + 4096;
    return (p->x + PW > x0 && p->x - PW < x1 && p->y + PH > y0 && p->y < y1 && p->z + PW > z0 && p->z - PW < z1);
}

static int bed_cell_free(u8 c) { return c == B_AIR || (g_blocks[c].flags & F_PLANT); }

// Cama: el pie va en la celda apuntada y la cabeza se extiende hacia donde mira el jugador
static void place_bed(Player *p)
{
    Slot *s = &p->inv[p->sel];
    if (p->gamemode == 0 && s->count == 0) return;
    u8 tb = world_get(p->tx, p->ty, p->tz);
    if (BLK_IS_TORCH(tb) || (g_blocks[tb].flags & F_PLANT)) return;
    int x = p->place_x, y = p->place_y, z = p->place_z;
    int fx, fy, fz;
    player_forward(p, &fx, &fy, &fz);
    int d = abs(fx) > abs(fz) ? (fx > 0 ? 1 : 3) : (fz > 0 ? 2 : 0);
    int hx = x + bed_dx(d), hz = z + bed_dz(d);
    if ((unsigned)x >= WX || (unsigned)z >= WZ || (unsigned)hx >= WX || (unsigned)hz >= WZ || y < 1 || y >= WY) return;
    if (!bed_cell_free(world_get(x, y, z)) || !bed_cell_free(world_get(hx, y, hz))) return;
    if (!blk_opaque(world_get(x, y - 1, z)) || !blk_opaque(world_get(hx, y - 1, hz))) return;   // hace falta suelo firme bajo las dos mitades
    if (player_overlaps_cell(p, x, y, z) || player_overlaps_cell(p, hx, y, hz)) return;
    world_set(x, y, z, (u8)(B_BED_FOOT + d));
    world_set(hx, y, hz, B_BED_HEAD);
    sfx_dig(B_BED_FOOT, 100);
    start_swing(p);
    if (p->gamemode == 0) {
        if (--s->count == 0) { s->id = 0; s->dmg = 0; }
    }
}

static void place_block(Player *p, Slot *s)
{
    if (!p->has_target || !s) return;
    if (s->id == I_BED) { place_bed(p); return; }
    if (s->id == 0 || s->id >= B_COUNT || (p->gamemode == 0 && s->count == 0)) return;   // los items no se colocan
    u8 tb = world_get(p->tx, p->ty, p->tz);
    // No permitir colocar bloques encima ni sobre el tallgrass (ni apoyados en antorchas/plantas)
    if (tb == B_TALLGRASS || BLK_IS_TORCH(tb) || (g_blocks[tb].flags & F_PLANT)) return;
    int x = p->place_x, y = p->place_y, z = p->place_z;
    if ((unsigned)x >= WX || (unsigned)y >= WY || (unsigned)z >= WZ) return;
    u8 cur = world_get(x, y, z);
    if (cur == B_TALLGRASS) return;
    if (y > 0 && world_get(x, y - 1, z) == B_TALLGRASS) return;
    if (!(cur == B_AIR || cur == B_WATER)) return;
    const BlockInfo *bi = &g_blocks[s->id];
    if ((bi->flags & F_SOLID) && player_overlaps_cell(p, x, y, z)) return;
    // las plantas necesitan suelo
    if (bi->flags & F_PLANT) {
        u8 below = world_get(x, y - 1, z);
        if (below != B_GRASS && below != B_DIRT && below != B_SAND) return;
    }
    u8 put = s->id;
    int jx = -1, jz = -1;
    if (put == B_TORCH) {
        if (cur == B_WATER) return;
        if (p->tface == D_PY && blk_solid(world_get(x, y - 1, z)) && !BLK_IS_BED(world_get(x, y - 1, z))) put = B_TORCH;
        else if (p->tface == D_PX && blk_opaque(world_get(x - 1, y, z))) put = B_TORCH_E;
        else if (p->tface == D_PZ && blk_opaque(world_get(x, y, z - 1))) put = B_TORCH_S;
        else if (p->tface == D_NX && blk_opaque(world_get(x + 1, y, z))) put = B_TORCH_W;
        else if (p->tface == D_NZ && blk_opaque(world_get(x, y, z + 1))) put = B_TORCH_N;
        else if (blk_solid(world_get(x, y - 1, z)) && !BLK_IS_BED(world_get(x, y - 1, z))) put = B_TORCH;
        else return;
    } else if (put == B_FURNACE) {   // la cara frontal mira hacia el jugador
        int fx, fy, fz;
        player_forward(p, &fx, &fy, &fz);
        int face = abs(fx) > abs(fz) ? (fx > 0 ? 3 : 1) : (fz < 0 ? 2 : 0);
        put = (u8)(B_FURNACE + face);
    } else if (put == B_CHEST) {     // cofre: se orienta y si hay un vecino individual libre se une en cofre doble
        int fx, fy, fz;
        player_forward(p, &fx, &fy, &fz);
        if (!chest_plan_place(x, y, z, fx, fz, &put, &jx, &jz)) return;
        if (jx >= 0) world_set(jx, y, jz, put);
    }
    world_set(x, y, z, put);
    if (BLK_IS_FURNACE(put)) furnace_add(x, y, z);
    if (BLK_IS_CHEST(put)) {
        chest_add(x, y, z);
        if (jx >= 0) chest_link(x, y, z, jx, jz);
    }
    sfx_dig(s->id, 100);
    start_swing(p);
    if (p->gamemode == 0) {
        if (--s->count == 0) { s->id = 0; s->dmg = 0; }
    }
}

static void attack_mob(Player *p)
{
    Slot *h = &p->inv[p->sel];
    int dmg = attack_damage(h->id);
    if (!p->on_ground && p->vy < 0 && !p->in_water && !p->flying) dmg += dmg / 2 + 1;   // golpe critico al caer
    mob_hurt(p->mob_target, dmg, fsin(p->yaw), -fcos(p->yaw), p);
    if (p->gamemode == 0) {
        p->food_exh += 30;
        if (h->id && tool_kind(h->id) >= 0) {
            h->dmg += (u16)(tool_kind(h->id) == TK_SWORD ? 1 : 2);
            if (h->dmg >= item_max_dmg(h->id)) { h->id = 0; h->count = 0; h->dmg = 0; sfx_tool_break(); }
        }
    }
}

static void show_tip(Player *p, const char *msg) { p->tip_msg = msg; p->tip_t = 180; }

// Noche para dormir (World.isDaytime de 1.7.10: se puede dormir entre 12541 y 23459)
static int sleep_night(void) { return g_daytime >= 12541 && g_daytime < 23460; }

// R sobre una cama: dormir (solo de noche y sin monstruos cerca) y fijar el punto de reaparicion
static void try_sleep(Player *p, int x, int y, int z)
{
    int fd = bed_facing(x, y, z);
    if (fd < 0) return;
    int hx = x, hz = z;
    if (world_get(x, y, z) != B_BED_HEAD) { hx += bed_dx(fd); hz += bed_dz(fd); }
    if (!sleep_night()) { show_tip(p, "You can only sleep at night"); return; }
    for (int i = 0; i < MAX_MOBS; i++) {
        const Mob *m = &g_mobs[i];
        if (!m->active || m->dying || !mob_is_hostile(m->type)) continue;
        if (abs((m->x >> 12) - hx) <= 8 && abs((m->z >> 12) - hz) <= 8 && abs((m->y >> 12) - y) <= 5) {
            show_tip(p, "You may not rest now, there are monsters nearby");
            return;
        }
    }
    p->sleep_t = 1;
    p->sleep_ov = 0;
    p->sleep_x = hx; p->sleep_y = y; p->sleep_z = hz;
    p->x = (hx << 12) + 2048; p->z = (hz << 12) + 2048; p->y = (y << 12) + BED_TOP;
    p->vx = p->vy = p->vz = 0;
    p->on_ground = 1; p->sprinting = 0; p->sneaking = 0; p->flying = 0;
    p->eat_t = 0; p->bow_t = 0; p->brk_prog = 0; p->brk_x = p->brk_y = p->brk_z = -1;
    p->yaw = fd * 8192;               // mira hacia donde esta la cabeza de la cama
    p->pitch = 1500;
    p->has_bed = 1; p->bed_x = hx; p->bed_y = y; p->bed_z = hz;
    p->fall_y = p->y;
}

// Busca un hueco libre de pie (con suelo firme) junto a la celda (x, y, z)
static int stand_near(int x, int y, int z, int *ox, int *oz)
{
    static const s8 dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dzs[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    for (int i = 0; i < 8; i++) {
        int cx = x + dxs[i], cz = z + dzs[i];
        if (blk_solid(world_get(cx, y, cz)) || blk_solid(world_get(cx, y + 1, cz))) continue;
        if (BLK_IS_BED(world_get(cx, y, cz))) continue;
        u8 below = world_get(cx, y - 1, cz);
        if (!blk_solid(below) || BLK_IS_BED(below)) continue;
        *ox = cx; *oz = cz;
        return 1;
    }
    return 0;
}

void player_wake(Player *p)
{
    if (p->sleep_t <= 0) return;
    p->sleep_t = 0;
    p->sleep_ov = SLEEP_FULL;       // MC: wakeUpPlayer(false, ...) deja sleepTimer = 100 y la pantalla se aclara en 10 ticks
    int x = p->sleep_x, y = p->sleep_y, z = p->sleep_z, ox, oz;
    int fd = bed_facing(x, y, z);
    int found = 0;
    if (fd >= 0) {
        // primero junto a la cabeza, luego junto al pie
        found = stand_near(x, y, z, &ox, &oz) || stand_near(x - bed_dx(fd), y, z - bed_dz(fd), &ox, &oz);
    }
    if (found) { p->x = (ox << 12) + 2048; p->z = (oz << 12) + 2048; p->y = y << 12; }
    else p->y = (y << 12) + BED_TOP;                 // sin hueco: de pie sobre la cama
    p->vx = p->vy = p->vz = 0;
    p->pitch = 0;
    p->fall_y = p->y;
}

// Opacidad del velo de dormir (GuiIngame 1.7.10: alfa = 220/255 * sleepTimer / 100, color 0x101020).
// En la DS el alfa va de 0 a 31: 220/255 de 31 = 27.
int player_sleep_alpha(const Player *p)
{
    int t = p->sleep_ov;
    if (t <= 0) return 0;
    int a = (t <= SLEEP_FULL) ? 27 * t / SLEEP_FULL
                              : 27 * (SLEEP_FULL + SLEEP_FADEOUT - t) / SLEEP_FADEOUT;
    return a < 0 ? 0 : (a > 27 ? 27 : a);
}

// sleepTimer de EntityPlayer.onUpdate: sube mientras duerme (tope 100 ticks de MC) y, ya despierto,
// baja durante 10 ticks de MC hasta quitar el velo
static void sleep_overlay_tick(Player *p)
{
    if (p->sleep_t > 0) {
        if (p->sleep_ov < SLEEP_FULL) p->sleep_ov++;
    } else if (p->sleep_ov > 0) {
        if (++p->sleep_ov >= SLEEP_FULL + SLEEP_FADEOUT) p->sleep_ov = 0;
    }
}

// Acostado, igual que Minecraft 1.7.10:
//  - la pantalla se oscurece poco a poco durante 5 s (SLEEP_FULL) y la vista no se mueve;
//  - al cumplirse, "todos duermen" (WorldServer.tick): la hora pasa al inicio del dia siguiente y Steve se levanta;
//  - si amanece antes de tiempo se levanta solo, sin saltar la hora;
//  - solo "Leave Bed" (boton tactil o B) lo levanta antes: ya no lo despierta cualquier boton.
static void sleep_update(Player *p, u32 down)
{
    p->sleep_t++;
    p->has_target = 0; p->mob_target = -1;
    if (p->sleep_t >= SLEEP_FULL) {
        daynight_set(0);
        player_wake(p);
    } else if (!sleep_night()) {
        player_wake(p);
    } else if (down & KEY_B) {
        player_wake(p);
    }
    if (p->invuln > 0) p->invuln--;
    if (p->tip_t > 0) p->tip_t--;
}

static void do_actions(Player *p, u32 held, u32 down)
{
    if (p->act_cd > 0) p->act_cd--;
    if (p->swing > 0) p->swing--;

    // ---- golpear mobs / picar (L) ----
    if ((held & KEY_L) && p->mob_target >= 0) {
        p->brk_prog = 0;
        p->brk_x = p->brk_y = p->brk_z = -1;
        if ((down & KEY_L) || p->act_cd == 0) { attack_mob(p); p->act_cd = 12; }
        start_swing(p);
    } else if (held & KEY_L) {
        if (p->has_target) {
            u8 b = world_get(p->tx, p->ty, p->tz);
            int hard = g_blocks[b].hardness;
            if (hard != 0xFFFF) {
                if (p->gamemode == 1) {
                    if ((down & KEY_L) || p->act_cd == 0) {
                        break_block(p, p->tx, p->ty, p->tz);
                        p->act_cd = 10;
                    }
                } else {
                    if (p->brk_x != p->tx || p->brk_y != p->ty || p->brk_z != p->tz) {
                        p->brk_x = p->tx; p->brk_y = p->ty; p->brk_z = p->tz;
                        p->brk_prog = 0;
                        int kind, tier;
                        block_tool_req(b, &kind, &tier);
                        Slot *h = &p->inv[p->sel];
                        int mult = (h->id && kind >= 0 && tool_kind(h->id) == kind) ? tool_speed(h->id) : 1;
                        p->brk_total = hard * 9 / mult;
                        if (p->brk_total < 1) p->brk_total = 1;
                    }
                    p->brk_prog++;
                    start_swing(p);
                    if (p->brk_prog < p->brk_total && p->brk_prog % 12 == 1) sfx_hit(b);   // golpeo cada 12 ticks (0,2 s a 60 Hz, como los 4 ticks de Minecraft)
                    if (p->brk_prog >= p->brk_total) {
                        break_block(p, p->tx, p->ty, p->tz);
                        p->brk_prog = 0;
                        p->brk_x = p->brk_y = p->brk_z = -1;
                    }
                }
            }
        } else {
            p->brk_prog = 0;
            p->brk_x = -1;
            if (down & KEY_L) start_swing(p);
        }
    } else {
        p->brk_prog = 0;
        p->brk_x = p->brk_y = p->brk_z = -1;
    }

    // ---- usar / poner / tensar arco (R) ----
    Slot *h = (held & KEY_SELECT) ? &p->offhand : &p->inv[p->sel];
    int offhand_use = (held & KEY_SELECT) != 0;
    int has_arr = (p->gamemode == 1);
    if (!has_arr) {
        for (int i = 0; i < 36; i++) if (p->inv[i].id == I_ARROW && p->inv[i].count > 0) { has_arr = 1; break; }
    }
    if (h->id != I_BOW && p->bow_t > 0) p->bow_t = 0;

    if (held & KEY_R) {
        u8 tb = p->has_target ? world_get(p->tx, p->ty, p->tz) : B_AIR;
        int sat = 0;
        int fv = h->id ? food_value(h->id, &sat) : 0;
        if (!offhand_use && p->has_target && !p->sneaking && tb == B_CRAFTING) {
            if (down & KEY_R) p->want_craft = 1;      // abrir crafteo 3x3
        } else if (!offhand_use && p->has_target && !p->sneaking && BLK_IS_FURNACE(tb)) {
            if (down & KEY_R) { p->want_furnace = 1; p->fx = p->tx; p->fy = p->ty; p->fz = p->tz; }
        } else if (!offhand_use && p->has_target && !p->sneaking && BLK_IS_CHEST(tb)) {
            if (down & KEY_R) { p->want_chest = 1; p->fx = p->tx; p->fy = p->ty; p->fz = p->tz; }
        } else if (!offhand_use && p->has_target && !p->sneaking && BLK_IS_BED(tb)) {
            if (down & KEY_R) try_sleep(p, p->tx, p->ty, p->tz);
        } else if (!offhand_use && h->id == I_BOW) {
            if (has_arr) { if (p->bow_t < 7200) p->bow_t++; }
            else p->bow_t = 0;
        } else if (!offhand_use && h->id && armor_piece(h->id) >= 0) {
            if (down & KEY_R) player_equip_armor(p, p->sel);      // ponerse la armadura desde la mano
        } else if (fv > 0) {
            if (p->gamemode == 0 && p->food < 20) {
                // 1.7.10: cada 4 ticks de 20 Hz cuando quedan <= 25 (sonido de masticar); no hay balanceo del brazo
                if (p->eat_t % 3 == 0) { int rem = 32 - p->eat_t / 3; if (rem <= 25 && rem % 4 == 0) sfx_eat(); }
                if (++p->eat_t >= 96) {                   // 32 ticks de MC
                    p->eat_t = 0;
                    p->food += fv; if (p->food > 20) p->food = 20;
                    p->food_sat += sat; if (p->food_sat > p->food * 100) p->food_sat = p->food * 100;
                    sfx_burp();
                    if (--h->count == 0) { h->id = 0; h->dmg = 0; }
                }
            }
        } else if ((down & KEY_R) || p->act_cd == 0) {
            place_block(p, h);
            p->act_cd = 10;
        }
    } else {
        p->eat_t = 0;
        if (p->bow_t > 0) {
            if (h->id == I_BOW && has_arr && p->bow_t >= 9) {
                mobs_player_shoot_arrow(p, p->bow_t);
                if (p->gamemode == 0) {
                    for (int i = 0; i < 36; i++) {
                        if (p->inv[i].id == I_ARROW && p->inv[i].count > 0) {
                            if (--p->inv[i].count == 0) { p->inv[i].id = 0; p->inv[i].dmg = 0; }
                            break;
                        }
                    }
                    if (++h->dmg >= item_max_dmg(I_BOW)) { h->id = 0; h->count = 0; h->dmg = 0; sfx_tool_break(); }
                }
            }
            p->bow_t = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Salto automatico: devuelve 1 si hay un obstaculo de 1 bloque de alto justo delante
// (en la direccion w = (wx, wz), 4.12) y hay espacio libre encima para subirse.
// ---------------------------------------------------------------------------
static int step_ahead(const Player *p, int wx, int wz)
{
    if (wx == 0 && wz == 0) return 0;
    // sonda: un poco mas alla del borde de la caja, en cada eje por separado
    // (asi tambien se sube rozando una esquina) y en diagonal
    static const int PROBE = 700;                    // ~0.17 bloques
    int dx = (wx * PROBE) >> 12, dz = (wz * PROBE) >> 12;
    int ax = abs(wx), az = abs(wz);
    // se normaliza para que la sonda avance siempre ~PROBE en el eje dominante
    int m = ax > az ? ax : az;
    if (m > 0) { dx = (wx * PROBE) / m; dz = (wz * PROBE) / m; }

    int nx = p->x + dx, nz = p->z + dz;
    if (!box_hits(nx, p->y, nz)) return 0;           // delante hay paso libre: no hace falta saltar
    // el bloque no debe ser mas alto que 1: libre una celda mas arriba (con la altura del jugador)
    int up = ((p->y >> 12) + 1) << 12;               // parte alta del bloque que tenemos delante
    if (box_hits(nx, up, nz)) return 0;
    // y debe caber la cabeza al subir (el hueco encima de donde estamos)
    if (box_hits(p->x, up, p->z)) return 0;
    return 1;
}

// ---------------------------------------------------------------------------
// Actualizacion por frame (60 Hz)
// ---------------------------------------------------------------------------
static int s_tick;

int player_armor_points(const Player *p)
{
    int n = 0;
    for (int i = 0; i < 4; i++) if (p->armor[i].id) n += armor_defense(p->armor[i].id);
    return n;
}

void player_drop_all(Player *p)
{
    for (int i = 0; i < 36; i++) {
        if (p->inv[i].id && p->inv[i].count) {
            int vx = (rand() % 321) - 160, vz = (rand() % 321) - 160;
            int vy = 160 + (rand() % 140);
            drops_spawn_slot(p->x, p->y + FP / 2, p->z, p->inv[i], vx, vy, vz, 45);
            p->inv[i].id = 0; p->inv[i].count = 0; p->inv[i].dmg = 0;
        }
    }
    if (p->offhand.id && p->offhand.count) {
        int vx = (rand() % 321) - 160, vz = (rand() % 321) - 160;
        drops_spawn_slot(p->x, p->y + FP / 2, p->z, p->offhand, vx, 180, vz, 45);
        p->offhand.id = p->offhand.count = 0; p->offhand.dmg = 0;
    }
    for (int i = 0; i < 4; i++) {
        if (p->armor[i].id && p->armor[i].count) {
            int vx = (rand() % 321) - 160, vz = (rand() % 321) - 160;
            int vy = 180 + (rand() % 120);
            drops_spawn_slot(p->x, p->y + FP / 2, p->z, p->armor[i], vx, vy, vz, 45);
            p->armor[i].id = 0; p->armor[i].count = 0; p->armor[i].dmg = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Reaparicion
// ---------------------------------------------------------------------------
// Causa del bug "Steve cae del cielo": world_get() devuelve PIEDRA en las columnas que no estan
// cargadas, asi que world_height() de una columna descargada daba 127 y Steve aparecia a 128 de altura.
// Pasaba cuando morias lejos del punto de aparicion (su columna ya estaba descargada). Ahora la columna
// se carga ANTES de leer el terreno y la altura se busca con un hueco comprobado (suelo + 2 celdas libres).
static int spawn_spot_ok(int x, int y, int z)
{
    if (y < 1 || y + 1 >= WY) return 0;
    u8 feet = world_get(x, y, z), head = world_get(x, y + 1, z), floor = world_get(x, y - 1, z);
    if (blk_solid(feet) || blk_solid(head) || feet == B_WATER || head == B_WATER) return 0;
    return blk_solid(floor) && !BLK_IS_BED(floor);
}

// y donde aparecer en la columna (x, z), ya cargada. Como MC: parte de la altura del punto de aparicion
// y sube hasta encontrar hueco; si no hay, baja; como ultimo recurso, la superficie.
static int safe_spawn_y(int x, int z)
{
    int y0 = g_spawn_y;
    if (y0 < 1) y0 = 1;
    if (y0 > WY - 3) y0 = WY - 3;
    for (int y = y0; y < WY - 2; y++) if (spawn_spot_ok(x, y, z)) return y;
    for (int y = y0 - 1; y >= 1; y--) if (spawn_spot_ok(x, y, z)) return y;
    return world_height(x, z) + 1;
}

void player_respawn(Player *p)
{
    p->body_yaw = p->yaw;
    int placed = 0;

    // 1) cama: se cargan sus columnas (y las vecinas, por si la cama esta en un borde) antes de comprobarla
    if (p->has_bed) {
        int bx = p->bed_x, by = p->bed_y, bz = p->bed_z, ox, oz;
        world_stream(bx, bz, 0);                                  // libera lo lejano antes de cargar (memoria)
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) world_ensure_loaded(bx + dx, bz + dz);
        if (world_col_loaded(bx, bz) && BLK_IS_BED(world_get(bx, by, bz)) && stand_near(bx, by, bz, &ox, &oz)) {
            p->x = (ox << 12) + 2048; p->y = by << 12; p->z = (oz << 12) + 2048;
            placed = 1;
        } else {
            p->has_bed = 0;
            show_tip(p, "Your home bed was missing or obstructed");
        }
    }

    // 2) punto de aparicion del mundo
    if (!placed) {
        int sx = g_spawn_x, sz = g_spawn_z;
        world_stream(sx, sz, 0);
        int ok = world_ensure_loaded(sx, sz);
        int sy = ok ? safe_spawn_y(sx, sz) : g_spawn_y;
        p->x = (sx << 12) + 2048;
        p->y = sy << 12;
        p->z = (sz << 12) + 2048;
    }

    p->sleep_t = 0;
    p->sleep_ov = 0;
    p->vx = p->vy = p->vz = 0;
    p->on_ground = 0; p->in_water = 0; p->head_water = 0;
    p->flying = 0; p->sneaking = 0; p->sprinting = 0;
    p->health = 20; p->food = 20; p->air = MAX_AIR;
    p->hurt_t = 0; p->death_t = 0;
    p->food_sat = 500; p->food_exh = 0; p->regen_t = 0;
    p->invuln = 120;
    p->msg_t = 0;
    p->fall_y = p->y;
}

int player_equip_armor(Player *p, int inv_slot)
{
    Slot *s = &p->inv[inv_slot];
    int piece = s->id ? armor_piece(s->id) : -1;
    if (piece < 0) return 0;
    Slot t = p->armor[piece];
    p->armor[piece] = *s;
    *s = t;
    return 1;
}

void player_hurt(Player *p, int dmg, int sx, int sz, int bypass_armor)
{
    if (p->gamemode == 1 || p->health <= 0 || dmg <= 0 || p->invuln > 0) return;
    int orig = dmg;
    if (!bypass_armor) {
        int pts = player_armor_points(p);
        dmg = (dmg * (25 - pts) * 2 + 25) / 50;          // 4 % menos por punto, redondeado
        if (dmg < 1) dmg = 1;
        int wear = orig / 4; if (wear < 1) wear = 1;
        for (int i = 0; i < 4; i++) {
            Slot *a = &p->armor[i];
            if (!a->id) continue;
            a->dmg += (u16)wear;
            if (a->dmg >= item_max_dmg(a->id)) { a->id = 0; a->count = 0; a->dmg = 0; sfx_tool_break(); }
        }
    }
    if (p->sleep_t > 0) player_wake(p);       // el dano despierta (el velo se aclara igual que al levantarse)
    p->health -= dmg;
    p->hurt_t = HURT_LEN;
    p->invuln = 30;
    p->food_exh += 30;
    sfx_hurt();
    if (sx || sz) {
        int dx = p->x - sx, dz = p->z - sz;
        int ax = abs(dx), az = abs(dz);
        int m = ax > az ? ax : az;
        if (m > 0) { p->vx += dx * 600 / m; p->vz += dz * 600 / m; }
        p->vy = 250;
    }
    if (p->health <= 0) {
        p->health = 0;
        p->death_t = 1;
        p->eat_t = 0;
        p->bow_t = 0;
        p->brk_total = 0;
        p->brk_prog = 0;
        player_drop_all(p);
    }
}

// hambre y regeneracion natural (valores de 1.7.10, tick de 60 Hz)
static void hunger_tick(Player *p)
{
    if (p->food_exh >= 400) {
        p->food_exh -= 400;
        if (p->food_sat > 0) { p->food_sat -= 100; if (p->food_sat < 0) p->food_sat = 0; }
        else if (p->food > 0) p->food--;
    }
    if (p->food >= 18 && p->health < 20) {
        if (++p->regen_t >= 240) { p->regen_t = 0; p->health++; p->food_exh += 300; }
    } else if (p->food <= 0) {
        if (++p->regen_t >= 240) {
            p->regen_t = 0;
            if (p->health > 1) player_hurt(p, 1, 0, 0, 1);
        }
    } else p->regen_t = 0;
}

void player_update(Player *p, u32 held, u32 down)
{
    s_tick++;
    if (p->hurt_t > 0) p->hurt_t--;
    if (p->health <= 0) {
        p->death_t++;
        return;
    }
    sleep_overlay_tick(p);
    if (p->sleep_t > 0) { sleep_update(p, down); return; }      // acostado: sin fisica ni acciones
    // hotbar
    if (down & KEY_X) p->sel = (p->sel + 8) % 9;
    if (down & KEY_Y) p->sel = (p->sel + 1) % 9;

    p->sneaking = (held & KEY_B) && !p->flying;

    // doble toque de A: volar (solo creativo)
    if ((down & KEY_A) && p->gamemode == 1) {
        if (s_tick - p->last_a_frame < 20) {
            p->flying = !p->flying;
            p->vy = 0;
        }
        p->last_a_frame = s_tick;
    }

    // doble toque adelante: correr
    int fwd = (held & KEY_UP) ? 1 : 0;
    if (fwd && !p->fwd_was_down) {
        if (s_tick - p->last_fwd_frame < 20) p->sprinting = 1;
        p->last_fwd_frame = s_tick;
    }
    if (!fwd) { if (p->fwd_was_down) p->last_fwd_frame = s_tick; p->sprinting = 0; }
    p->fwd_was_down = fwd;
    if (p->sneaking || p->bow_t > 0) p->sprinting = 0;

    // entrada de movimiento
    int mf = ((held & KEY_UP) ? 1 : 0) - ((held & KEY_DOWN) ? 1 : 0);
    int ms = ((held & KEY_RIGHT) ? 1 : 0) - ((held & KEY_LEFT) ? 1 : 0);

    int sy = fsin(p->yaw), cy = fcos(p->yaw);
    // adelante = (sin, -cos), derecha = (cos, sin)
    int wx = sy * mf + cy * ms;
    int wz = -cy * mf + sy * ms;
    if (mf && ms) { wx = (wx * 2896) >> 12; wz = (wz * 2896) >> 12; }

    int speed = WALK_SPEED;
    if (p->flying) speed = FLY_SPEED;
    if (p->sprinting && mf > 0) speed = (speed * SPRINT_MULT) >> 12;
    if (p->sneaking || p->bow_t > 0) speed = (speed * 1229) >> 12;   // 0.3

    // agua
    u8 feet = world_get(p->x >> 12, (p->y + 1600) >> 12, p->z >> 12);
    int ey = p->y + (p->sneaking ? EYEH_SNEAK : EYEH);
    int was_in_water = p->in_water;
    p->in_water = (feet == B_WATER);
    // chapoteo: al nadar en la superficie in_water entra y sale cada pocos ticks;
    // sin enfriamiento el sonido (1,2 s) se dispara una y otra vez
    static int splash_cd;
    if (splash_cd > 0) splash_cd--;
    if (p->in_water && !was_in_water && !p->flying && splash_cd == 0) { sfx_splash(); splash_cd = 40; }
    p->head_water = (world_get(p->x >> 12, ey >> 12, p->z >> 12) == B_WATER);
    if (p->in_water && !p->flying) speed >>= 1;

    int tx = (wx * speed) >> 12;
    int tz = (wz * speed) >> 12;
    int k = p->on_ground ? 180 : 45;
    if (p->flying) k = 110;
    if (p->in_water) k = 90;
    {
        int dvx = tx - p->vx, dvz = tz - p->vz;
        int ax = (dvx * k) / 256, az = (dvz * k) / 256;
        if (ax == 0 && dvx != 0) ax = dvx > 0 ? 1 : -1;   // converge exacto, sin deriva
        if (az == 0 && dvz != 0) az = dvz > 0 ? 1 : -1;
        p->vx += ax;
        p->vz += az;
    }

    // vertical
    if (p->flying) {
        int target = 0;
        if (held & KEY_A) target = FLY_SPEED;
        if (held & KEY_B) target = -FLY_SPEED;
        // Acercarse a la velocidad objetivo un 25 % por tick. OJO: antes era ">> 2", que redondea hacia -infinito;
        // al soltar B (vy negativo, objetivo 0) la diferencia positiva pequena se truncaba a 0 y vy se quedaba
        // atascada en -1..-3: Steve seguia cayendo despacio. Aqui se redondea hacia la velocidad objetivo.
        int dv = target - p->vy;
        int st = dv / 4;
        if (st == 0) st = dv;
        p->vy += st;
    } else if (p->in_water) {
        p->vy -= 8;
        if (p->vy < -220) p->vy = -220;
        if (held & KEY_A) p->vy += 40;
        if (p->vy > 300) p->vy = 300;
        // Nadando con A pulsado junto a un bloque: trepa y sube encima de el.
        // Si no se esta pulsando la cruceta se empuja hacia donde mira el jugador.
        if ((held & KEY_A) && g_opt.autojump) {
            int cx = wx, cz = wz;
            if (cx == 0 && cz == 0) { cx = sy; cz = -cy; }
            if (step_ahead(p, cx, cz)) {
                if (p->vy < WATER_CLIMB) p->vy = WATER_CLIMB;
                if (wx == 0 && wz == 0) {          // empujon hacia el bloque
                    int sp = speed;
                    p->vx += ((cx * sp) >> 12) >> 2;
                    p->vz += ((cz * sp) >> 12) >> 2;
                }
            }
        }
    } else {
        p->vy -= GRAVITY;
        if (p->vy < TERMINAL) p->vy = TERMINAL;
        if ((held & KEY_A) && p->on_ground) { p->vy = JUMP_VEL; p->food_exh += p->sprinting ? 80 : 20; }
        // salto automatico: andando contra un bloque de 1 de alto
        else if (g_opt.autojump && p->on_ground && !p->sneaking && (wx || wz) && step_ahead(p, wx, wz))
            p->vy = JUMP_VEL;
    }

    // mover
    // on_ground se conserva durante el movimiento horizontal (lo usa el
    // control de bordes al agacharse) y se recalcula con el movimiento vertical
    if (move_axis(p, 0, p->vx)) p->vx = 0;
    if (move_axis(p, 2, p->vz)) p->vz = 0;
    int was_on_ground = p->on_ground;
    p->on_ground = 0;
    int oldvy = p->vy;
    if (move_axis(p, 1, p->vy)) {
        p->vy = 0;
        if (oldvy < 0) {
            p->on_ground = 1;
            // aterrizaje: suena el paso del bloque de abajo (mas fuerte si la caida fue larga)
            if (!was_on_ground && p->gamemode == 0 && !p->in_water && p->fall_y - p->y > 3 * FP)
                player_hurt(p, (p->fall_y - p->y - 3 * FP + FP - 1) / FP, 0, 0, 1);   // dano por caida (no lo frena la armadura)
            if (!was_on_ground && oldvy < -150 && !p->in_water)
                sfx_step(world_get(p->x >> 12, (p->y - 800) >> 12, p->z >> 12), oldvy < -400 ? 110 : 70);
        }
    }
    if (p->on_ground && p->flying) p->flying = 0;
    if (p->on_ground || p->in_water || p->flying) p->fall_y = p->y;
    else if (p->y > p->fall_y) p->fall_y = p->y;

    // limites del mundo
    if (p->y < -8 * FP) { p->y = (WY + 2) << 12; p->vy = 0; }

    // animacion
    int hs = (int)(((s64)p->vx * p->vx + (s64)p->vz * p->vz) >> 12);
    p->walk_speed = hs;
    int sp = abs(p->vx) + abs(p->vz);
    p->walk_dist += sp >> 3;

    // ModelBiped: limbSwing / limbSwingAmount (1.7.10, a 20 Hz) pasados a 60 Hz.
    // amount += (min(1, bloques_por_tick * 4) - amount) * 0.4 ; limbSwing += amount ; fase = limbSwing * 0.6662 rad
    {
        s64 v2 = (s64)p->vx * p->vx + (s64)p->vz * p->vz;
        int spd = (int)sqrt32(v2 > 0x7FFFFFFF ? 0x7FFFFFFF : (int)v2);
        int target = spd * 12;
        if (target > 4096) target = 4096;
        p->limb_amt += ((target - p->limb_amt) * 5) >> 5;
        if (p->limb_amt < 0) p->limb_amt = 0;
        p->limb_ph = (p->limb_ph + ((p->limb_amt * 1158) >> 12)) & 0x7FFF;
        p->age++;

        // renderYawOffset: el cuerpo sigue a la cabeza al andar/golpear/comer y nunca se queda a mas de 75 grados
        int d = (p->yaw - p->body_yaw) & 0x7FFF;
        if (d >= 0x4000) d -= 0x8000;
        if (spd > 20 || p->swing > 0 || p->eat_t > 0 || p->bow_t > 0) {
            p->body_yaw = (p->body_yaw + ((d * 7) >> 6)) & 0x7FFF;
            d = (p->yaw - p->body_yaw) & 0x7FFF;
            if (d >= 0x4000) d -= 0x8000;
        }
        if (d > 6827) p->body_yaw = (p->yaw - 6827) & 0x7FFF;
        else if (d < -6827) p->body_yaw = (p->yaw + 6827) & 0x7FFF;
    }

    // sonido de pasos (cada ~1,7 bloques) y de nado
    {
        static int step_acc, swim_cd;
        if (p->on_ground && !p->flying && !p->in_water) {
            step_acc += sp >> 3;
            if (step_acc >= 850) {   // 1 paso cada ~1,7 bloques, como Minecraft
                step_acc = 0;
                sfx_step(world_get(p->x >> 12, (p->y - 800) >> 12, p->z >> 12), p->sneaking ? 25 : 55);
            }
        } else step_acc = 0;
        if (swim_cd > 0) swim_cd--;
        if (p->in_water && !p->flying && sp > 60 && swim_cd == 0) { sfx_swim(); swim_cd = 28; }
    }

    raycast(p);
    {   // mob apuntado: el mas cercano que quede por delante del bloque apuntado
        int md = 0;
        int mi = mobs_target(p, &md);
        p->mob_target = (mi >= 0 && (!p->has_target || md < p->target_dist)) ? mi : -1;
    }
    do_actions(p, held, down);

    // supervivencia
    if (p->invuln > 0) p->invuln--;
    if (p->msg_t > 0) p->msg_t--;
    if (p->tip_t > 0) p->tip_t--;
    if (p->gamemode == 0) {
        if (p->sprinting && p->on_ground && sp > 60) p->food_exh += 1;
        hunger_tick(p);
        if (p->head_water) {
            p->air--;
            if (p->air <= -60) {
                p->air = 0;
                player_hurt(p, 2, 0, 0, 1);
            }
        } else {
            p->air = MAX_AIR;
        }
    } else {
        p->air = MAX_AIR;
    }
}
