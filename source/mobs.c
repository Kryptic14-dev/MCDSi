// Mobs: IA, fisica, aparicion, combate, flechas y explosiones (valores de Minecraft 1.7.10, nivel Normal)
#include <stdlib.h>
#include <string.h>
#include "mobs.h"
#include "world.h"
#include "blocks.h"
#include "items.h"
#include "drops.h"
#include "furnace.h"
#include "chest.h"
#include "daynight.h"
#include "sfx.h"
#include "sfx_ids.h"

Mob g_mobs[MAX_MOBS];
Arrow g_arrows[MAX_ARROWS];

#define GRAV        36
#define JUMP        573
#define PW          1229        // medio ancho del jugador
#define PH          7373
#define KB_H        420         // retroceso horizontal inicial
#define KB_V        330
#define INVULN      30          // 0,5 s
#define HOSTILE_CAP 10
#define PASSIVE_CAP 10
#define ARROW_SPEED 2185        // ~32 bloques/s
#define ARROW_GRAV  23

typedef struct {
    s16 hw, h, hp, speed;       // medio ancho, alto (4.12), vida, velocidad (4.12 / tick)
    u8 hostile, panic_mul;      // panic_mul x16
    s8 say, hurt, death, step; // grupos de sonido (-1 = ninguno; step -1 = el del bloque)
} MobInfo;

static const MobInfo g_info[MT_COUNT] = {
    [MT_PIG]      = {1843, 3686, 10, 217, 0, 20, SFXG_PIG_SAY, SFXG_PIG_SAY, SFXG_PIG_DEATH, SFXG_PIG_STEP},
    [MT_COW]      = {1843, 5325, 10, 139, 0, 32, SFXG_COW_SAY, SFXG_COW_HURT, SFXG_COW_HURT, SFXG_COW_STEP},
    [MT_SHEEP]    = {1843, 5325, 8, 181, 0, 20, SFXG_SHEEP_SAY, SFXG_SHEEP_SAY, SFXG_SHEEP_SAY, SFXG_SHEEP_STEP},
    [MT_CHICKEN]  = {614, 2867, 4, 217, 0, 22, SFXG_CHICKEN_SAY, SFXG_CHICKEN_HURT, SFXG_CHICKEN_HURT, SFXG_CHICKEN_STEP},
    [MT_ZOMBIE]   = {1229, 7373, 20, 181, 1, 16, SFXG_ZOMBIE_SAY, SFXG_ZOMBIE_HURT, SFXG_ZOMBIE_DEATH, SFXG_ZOMBIE_STEP},
    [MT_SKELETON] = {1229, 7373, 20, 217, 1, 16, SFXG_SKEL_SAY, SFXG_SKEL_HURT, SFXG_SKEL_DEATH, SFXG_SKEL_STEP},
    [MT_CREEPER]  = {1229, 6963, 20, 217, 1, 16, -1, SFXG_CREEPER_HURT, SFXG_CREEPER_DEATH, -1},
    [MT_SPIDER]   = {2867, 3686, 16, 312, 1, 16, SFXG_SPIDER_SAY, SFXG_SPIDER_SAY, SFXG_SPIDER_DEATH, SFXG_SPIDER_STEP},
};

int mob_is_hostile(int t) { return g_info[t].hostile; }
int mob_half_width(int t) { return g_info[t].hw; }
int mob_height(int t) { return g_info[t].h; }

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
static u32 s_rng = 777;
static int rnd(int n)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (int)((s_rng >> 8) % (u32)n);
}

static u32 isqrt(u32 v)
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

static int atan_r(int num, int den)      // atan(num/den), 0 <= num <= den, en unidades de 15 bits (0..4096)
{
    int r = (int)(((s64)num << 12) / den);                              // 4.12
    return (int)(((s64)r * (4096 * 4096 + 1424 * (4096 - r))) >> 24);
}

// angulo 15 bits del vector (x, y) respecto al eje +x
int atan2_a(int y, int x)
{
    if (!x && !y) return 0;
    int ax = abs(x), ay = abs(y), a;
    if (ax >= ay) a = atan_r(ay, ax);
    else a = 8192 - atan_r(ax, ay);
    if (x < 0) a = 16384 - a;
    if (y < 0) a = -a;
    return a & 0x7FFF;
}

// yaw (0 = -Z, 90 grados = +X) de un vector horizontal (dx, dz)
static int yaw_of(int dx, int dz) { return atan2_a(dx, -dz); }

static int angle_diff(int to, int from) { return (((to - from) + 16384) & 0x7FFF) - 16384; }

static void turn_to(Mob *m, int yaw, int step)
{
    int d = angle_diff(yaw, m->yaw);
    if (d > step) d = step;
    if (d < -step) d = -step;
    m->yaw = (m->yaw + d) & 0x7FFF;
}

static void dir_vel(int yaw, int spd, int *vx, int *vz)
{
    *vx = (sinLerp((s16)yaw) * spd) >> 12;
    *vz = -((cosLerp((s16)yaw) * spd) >> 12);
}

static void snd3d(const Mob *m, const Player *p, int group, int vol)
{
    if (group < 0) return;
    sfx_play_group_3d(group, vol, m->x - p->x, m->z - p->z, p->yaw);
}

// ---------------------------------------------------------------------------
// Colisiones
// ---------------------------------------------------------------------------
static int mbox_hits(int type, int x, int y, int z)
{
    int hw = g_info[type].hw, h = g_info[type].h;
    int x0 = (x - hw) >> 12, x1 = (x + hw - 1) >> 12;
    int y0 = y >> 12, y1 = (y + h - 1) >> 12;
    int z0 = (z - hw) >> 12, z1 = (z + hw - 1) >> 12;
    for (int cy = y0; cy <= y1; cy++)
        for (int cz = z0; cz <= z1; cz++)
            for (int cx = x0; cx <= x1; cx++)
                if (blk_solid(world_get(cx, cy, cz))) return 1;
    return 0;
}

static int mob_axis(Mob *m, int axis, int d)      // 1 = choca
{
    int hw = g_info[m->type].hw, h = g_info[m->type].h;
    while (d != 0) {
        int step = d > 2048 ? 2048 : (d < -2048 ? -2048 : d);
        int nx = m->x, ny = m->y, nz = m->z;
        if (axis == 0) nx += step; else if (axis == 1) ny += step; else nz += step;
        if (mbox_hits(m->type, nx, ny, nz)) {
            if (axis == 0) m->x = step > 0 ? (((nx + hw - 1) >> 12) << 12) - hw : ((((nx - hw) >> 12) + 1) << 12) + hw;
            else if (axis == 2) m->z = step > 0 ? (((nz + hw - 1) >> 12) << 12) - hw : ((((nz - hw) >> 12) + 1) << 12) + hw;
            else if (step > 0) m->y = (((ny + h - 1) >> 12) << 12) - h;
            else m->y = ((ny >> 12) + 1) << 12;
            return 1;
        }
        m->x = nx; m->y = ny; m->z = nz;
        d -= step;
    }
    return 0;
}

static int sky_exposed(int x, int y, int z)
{
    for (int yy = y + 1; yy < WY; yy++) {
        u8 b = world_get(x, yy, z);
        if (blk_solid(b) || (g_blocks[b].flags & F_LIQUID)) return 0;
    }
    return 1;
}

// vision directa entre dos puntos (4.12): sin bloques opacos en medio
static int line_clear(int x0, int y0, int z0, int x1, int y1, int z1)
{
    int dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    int ad = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (abs(dz) > ad) ad = abs(dz);
    int n = ad / 2048 + 1;
    if (n > 40) n = 40;
    for (int i = 1; i < n; i++) {
        int x = x0 + dx * i / n, y = y0 + dy * i / n, z = z0 + dz * i / n;
        if (blk_opaque(world_get(x >> 12, y >> 12, z >> 12))) return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Movimiento de un mob (fisica comun)
// ---------------------------------------------------------------------------
static void mob_step(Mob *m, int twx, int twz, int want_move)
{
    const MobInfo *mi = &g_info[m->type];
    int wet = world_get(m->x >> 12, (m->y + 600) >> 12, m->z >> 12) == B_WATER;
    m->in_water = (u8)wet;
    if (wet) { twx >>= 1; twz >>= 1; }
    int k = m->on_ground ? 180 : 45;
    if (wet) k = 90;
    {
        int dvx = twx - m->vx, dvz = twz - m->vz;
        int ax = dvx * k / 256, az = dvz * k / 256;
        if (ax == 0 && dvx) ax = dvx > 0 ? 1 : -1;
        if (az == 0 && dvz) az = dvz > 0 ? 1 : -1;
        m->vx += ax; m->vz += az;
    }
    if (wet) {                                  // flota como en MC
        m->vy -= 8;
        if (world_get(m->x >> 12, (m->y + mi->h * 7 / 10) >> 12, m->z >> 12) == B_WATER) m->vy += 44;
        if (m->vy < -220) m->vy = -220;
        if (m->vy > 260) m->vy = 260;
    } else {
        m->vy -= GRAV;
        if (m->vy < -4000) m->vy = -4000;
        if (m->type == MT_CHICKEN && m->vy < -150) m->vy = -150;   // planea
    }
    int ax = m->vx + m->kx, az = m->vz + m->kz;
    int bx = mob_axis(m, 0, ax);
    int bz = mob_axis(m, 2, az);
    if (bx) { m->vx = 0; m->kx = 0; }
    if (bz) { m->vz = 0; m->kz = 0; }
    // salto automatico: bloque de 1 de alto delante y hueco encima
    if ((bx || bz) && want_move && m->on_ground && !mbox_hits(m->type, m->x, m->y + 4096, m->z)) {
        int px = m->x + (bx ? (ax > 0 ? 700 : -700) : 0), pz = m->z + (bz ? (az > 0 ? 700 : -700) : 0);
        if (!mbox_hits(m->type, px, m->y + 4096 + 64, pz)) m->vy = JUMP;
    }
    if (wet && (bx || bz) && want_move) m->vy = 300;   // sale del agua trepando
    int oldvy = m->vy;
    m->on_ground = 0;
    if (mob_axis(m, 1, m->vy)) { if (oldvy < 0) m->on_ground = 1; m->vy = 0; }
    if (m->on_ground) m->kx = m->kx * 3 / 4, m->kz = m->kz * 3 / 4;
    else m->kx = m->kx * 15 / 16, m->kz = m->kz * 15 / 16;
    m->walk_speed = (int)(((s64)m->vx * m->vx + (s64)m->vz * m->vz) >> 12);
    m->walk_dist += (abs(m->vx) + abs(m->vz)) >> 3;
    if (m->y < -4 * FP) m->active = 0;       // cayo al vacio
}

// ---------------------------------------------------------------------------
// Dano, muerte y botin
// ---------------------------------------------------------------------------
static void drop_item(const Mob *m, int id, int n)
{
    if (n <= 0) return;
    Slot s = { (u8)id, (u8)n, 0 };
    drops_spawn_slot(m->x, m->y + g_info[m->type].h / 2, m->z, s, rnd(121) - 60, 220, rnd(121) - 60, 15);
}

static void drop_loot(const Mob *m)
{
    switch (m->type) {
        case MT_PIG:      drop_item(m, I_PORK_RAW, 1 + rnd(3)); break;
        case MT_COW:      drop_item(m, I_LEATHER, rnd(3)); drop_item(m, I_BEEF_RAW, 1 + rnd(3)); break;
        case MT_SHEEP:    drop_item(m, B_WOOL_WHITE, 1); break;
        case MT_CHICKEN:  drop_item(m, I_FEATHER, rnd(3)); drop_item(m, I_CHICKEN_RAW, 1); break;
        case MT_ZOMBIE:   drop_item(m, I_ROTTEN_FLESH, rnd(3)); break;
        case MT_SKELETON: drop_item(m, I_BONE, rnd(3)); drop_item(m, I_ARROW, rnd(3)); break;
        case MT_CREEPER:  drop_item(m, I_GUNPOWDER, rnd(3)); break;
        case MT_SPIDER:   drop_item(m, I_STRING, rnd(3)); if (rnd(3) == 0) drop_item(m, I_SPIDER_EYE, 1); break;
    }
}

static void mob_die(Mob *m, const Player *p)
{
    m->dying = 1; m->die_t = 0; m->target = 0; m->fuse = 0; m->burn = 0;
    snd3d(m, p, g_info[m->type].death, 110);
    drop_loot(m);
}

void mob_hurt(int idx, int dmg, int kbx, int kbz, Player *p)
{
    if (idx < 0 || idx >= MAX_MOBS) return;
    Mob *m = &g_mobs[idx];
    if (!m->active || m->dying || m->invuln) return;
    m->hp -= (s16)dmg;
    m->invuln = INVULN;
    m->hurt_t = 20;
    m->kx = kbx * KB_H >> 12; m->kz = kbz * KB_H >> 12;
    m->vy = KB_V; m->on_ground = 0;
    if (!g_info[m->type].hostile) {
        m->panic = 180;
        m->ai_yaw = (kbx || kbz) ? yaw_of(kbx, kbz) : rnd(32768);
    } else {
        m->provoked = 1; m->target = 1;
    }
    if (m->hp <= 0) mob_die(m, p);
    else snd3d(m, p, g_info[m->type].hurt, 100);
}

// ---------------------------------------------------------------------------
// Explosion
// ---------------------------------------------------------------------------
static int exposure(int cx, int cy, int cz, int px, int py, int pz)   // 0..256
{
    int hit = 0;
    int ys[3] = { py + 500, py + PH / 2, py + PH - 500 };
    for (int i = 0; i < 3; i++) if (line_clear(cx, cy, cz, px, ys[i], pz)) hit++;
    return hit * 256 / 3;
}

// dano de explosion (formula de MC) segun distancia en 1/16 de bloque y exposicion
static int blast_damage(int d16, int expo, int power)
{
    int reach = 2 * power * 16;
    if (d16 >= reach) return 0;
    int imp = (reach - d16) * 256 / reach;
    imp = imp * expo / 256;
    return (((imp * imp) >> 8) + imp) * (8 * power) / 512 + (expo ? 1 : 0);
}

void mobs_explode(Player *p, int x, int y, int z, int power)
{
    sfx_play_group_3d(SFXG_EXPLODE, 127, x - p->x, z - p->z, p->yaw);

    // dano y retroceso al jugador (antes de destruir bloques, como en MC)
    {
        int dx = p->x - x, dy = (p->y + PH / 2) - y, dz = p->z - z;
        int d16 = (int)isqrt((u32)(((s64)dx * dx + (s64)dy * dy + (s64)dz * dz) >> 16));
        int ex = exposure(x, y, z, p->x, p->y, p->z);
        int dmg = blast_damage(d16, ex, power);
        if (dmg > 0 && p->gamemode == 0) {
            player_hurt(p, dmg, x, z, 0);
            int imp = (2 * power * 16 - d16) * 256 / (2 * power * 16);
            if (imp < 0) imp = 0;
            int h = isqrt((u32)(((s64)dx * dx + (s64)dz * dz) >> 16)) * 16;
            if (h > 0) { p->vx += (int)((s64)dx * 700 / h) * imp / 256; p->vz += (int)((s64)dz * 700 / h) * imp / 256; }
            p->vy += 300 * imp / 256;
        }
    }
    // dano a otros mobs
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &g_mobs[i];
        if (!m->active || m->dying) continue;
        int dx = m->x - x, dy = (m->y + g_info[m->type].h / 2) - y, dz = m->z - z;
        int d16 = (int)isqrt((u32)(((s64)dx * dx + (s64)dy * dy + (s64)dz * dz) >> 16));
        int dmg = blast_damage(d16, 256, power);
        if (dmg <= 0 || d16 == 0) continue;
        m->invuln = 0;
        mob_hurt(i, dmg, dx > 0 ? 3000 : -3000, dz > 0 ? 3000 : -3000, p);
    }
    // bloques: esfera de radio ~power con borde irregular
    int bx = x >> 12, by = y >> 12, bz = z >> 12;
    int r = power;
    for (int yy = -r; yy <= r; yy++)
        for (int zz = -r; zz <= r; zz++)
            for (int xx = -r; xx <= r; xx++) {
                int d2 = xx * xx + yy * yy + zz * zz;
                if (d2 > r * r + 1 + rnd(power * 2)) continue;
                int wx = bx + xx, wy = by + yy, wz = bz + zz;
                if (wy < 1 || wy >= WY) continue;
                u8 b = world_get(wx, wy, wz);
                if (b == B_AIR || b == B_BEDROCK || b == B_OBSIDIAN || b == B_WATER) continue;
                if (BLK_IS_FURNACE(b)) furnace_remove(wx, wy, wz, 1);
                if (BLK_IS_CHEST(b)) chest_remove(wx, wy, wz, 1);
                world_set(wx, wy, wz, B_AIR);
            }
    // plantas que se quedaron sin suelo
    for (int zz = -r; zz <= r; zz++)
        for (int xx = -r; xx <= r; xx++)
            for (int yy = -r; yy <= r + 1; yy++) {
                int wx = bx + xx, wy = by + yy, wz = bz + zz;
                if ((g_blocks[world_get(wx, wy, wz)].flags & F_PLANT) && world_get(wx, wy - 1, wz) == B_AIR)
                    world_set(wx, wy, wz, B_AIR);
            }
}

// ---------------------------------------------------------------------------
// Flechas
// ---------------------------------------------------------------------------
static void shoot_arrow(const Mob *m, const Player *p)
{
    int ai = -1;
    for (int i = 0; i < MAX_ARROWS; i++) if (!g_arrows[i].active) { ai = i; break; }
    if (ai < 0) return;
    int sx = m->x, sy = m->y + 6000, sz = m->z;
    int tx = p->x, ty = p->y + 5500, tz = p->z;
    int dx = tx - sx, dy = ty - sy, dz = tz - sz;
    s64 hd2 = (s64)dx * dx + (s64)dz * dz;
    dy += (int)(hd2 / 415000);                          // compensa la caida
    dx += (rnd(1001) - 500) * 2; dz += (rnd(1001) - 500) * 2; dy += (rnd(1001) - 500);
    u32 len2 = (u32)(((s64)dx * dx + (s64)dy * dy + (s64)dz * dz) >> 4);
    int len = (int)isqrt(len2) * 4;
    if (len < 1) len = 1;
    Arrow *a = &g_arrows[ai];
    memset(a, 0, sizeof(*a));
    a->active = 1;
    a->x = sx; a->y = sy; a->z = sz;
    a->vx = (int)((s64)dx * ARROW_SPEED / len);
    a->vy = (int)((s64)dy * ARROW_SPEED / len);
    a->vz = (int)((s64)dz * ARROW_SPEED / len);
    sfx_play_group_3d(SFXG_BOW, 100, sx - p->x, sz - p->z, p->yaw);
}

void mobs_player_shoot_arrow(const Player *p, int bow_t)
{
    int ai = -1, oldest = 0;
    for (int i = 0; i < MAX_ARROWS; i++) {
        if (!g_arrows[i].active) { ai = i; break; }
        if (g_arrows[i].age > g_arrows[oldest].age) oldest = i;
    }
    if (ai < 0) ai = oldest;

    int t = bow_t * 4096 / 60;
    if (t > 4096) t = 4096;
    int chg = (int)(((s64)t * t / 4096 + 2 * t) / 3);
    if (chg > 4096) chg = 4096;
    if (chg < 600) chg = 600;

    int ex, ey, ez, fx, fy, fz;
    player_eye(p, &ex, &ey, &ez);
    player_forward(p, &fx, &fy, &fz);
    int spd = ARROW_SPEED * chg / 4096;

    Arrow *a = &g_arrows[ai];
    memset(a, 0, sizeof(*a));
    a->active = 1;
    a->from_player = 1;
    a->dmg = (u8)(2 + (7 * chg + 2048) / 4096 + (chg >= 4000 ? 1 : 0));
    a->x = ex + (fx >> 3);
    a->y = ey - 400 + (fy >> 3);
    a->z = ez + (fz >> 3);
    a->vx = (fx * spd) >> 12;
    a->vy = (fy * spd) >> 12;
    a->vz = (fz * spd) >> 12;
    int hd = (int)isqrt((u32)(((s64)a->vx * a->vx + (s64)a->vz * a->vz) >> 4)) * 4;
    a->yaw = yaw_of(a->vx, a->vz);
    a->pitch = atan2_a(a->vy, hd > 0 ? hd : 1);
    sfx_play_group(SFXG_BOW, 100);
}

static void arrows_update(Player *p)
{
    for (int i = 0; i < MAX_ARROWS; i++) {
        Arrow *a = &g_arrows[i];
        if (!a->active) continue;
        a->age++;
        if (a->age > (a->stuck ? 1200 : 400) || !world_col_loaded(a->x >> 12, a->z >> 12)) { a->active = 0; continue; }
        if (a->stuck) {
            if (a->from_player && a->age > 15 &&
                abs(a->x - p->x) < PW + 2048 && abs(a->z - p->z) < PW + 2048 &&
                a->y >= p->y - 1024 && a->y <= p->y + PH + 1024) {
                if (p->gamemode == 1 || inv_add(p, I_ARROW, 1)) {
                    sfx_pop();
                    a->active = 0;
                }
            }
            continue;
        }
        int n = 3;
        for (int s = 0; s < n && !a->stuck && a->active; s++) {
            a->x += a->vx / n; a->y += a->vy / n; a->z += a->vz / n;
            if (blk_solid(world_get(a->x >> 12, a->y >> 12, a->z >> 12))) {
                a->stuck = 1;
                sfx_play_group_3d(SFXG_BOWHIT, 80, a->x - p->x, a->z - p->z, p->yaw);
                break;
            }
            if (a->from_player) {
                for (int mi = 0; mi < MAX_MOBS; mi++) {
                    Mob *m = &g_mobs[mi];
                    if (!m->active || m->dying) continue;
                    int hw = g_info[m->type].hw + 600, mh = g_info[m->type].h + 400;
                    if (abs(a->x - m->x) < hw && abs(a->z - m->z) < hw &&
                        a->y >= m->y - 400 && a->y <= m->y + mh) {
                        int kbx = a->vx, kbz = a->vz;
                        int m_len = abs(kbx) > abs(kbz) ? abs(kbx) : abs(kbz);
                        if (m_len > 0) { kbx = kbx * 4096 / m_len; kbz = kbz * 4096 / m_len; }
                        mob_hurt(mi, a->dmg, kbx, kbz, p);
                        sfx_play_group_3d(SFXG_BOWHIT, 95, a->x - p->x, a->z - p->z, p->yaw);
                        sfx_play_group(SFXG_SUCCESSHIT, 90);      // el "ding" que oye quien disparo
                        a->active = 0;
                        break;
                    }
                }
            } else if (p->gamemode == 0 &&
                       abs(a->x - p->x) < PW + 250 && abs(a->z - p->z) < PW + 250 &&
                       a->y > p->y && a->y < p->y + PH) {
                player_hurt(p, 3 + rnd(3), a->x - a->vx, a->z - a->vz, 0);
                a->active = 0;
                break;
            }
        }
        if (!a->stuck && a->active) {
            a->vy -= ARROW_GRAV;
            int hd = (int)isqrt((u32)(((s64)a->vx * a->vx + (s64)a->vz * a->vz) >> 4)) * 4;
            a->yaw = yaw_of(a->vx, a->vz);
            a->pitch = atan2_a(a->vy, hd > 0 ? hd : 1);
        }
    }
}

// ---------------------------------------------------------------------------
// IA
// ---------------------------------------------------------------------------
static int edge_ahead(const Mob *m, int yaw)          // 1 = agua o precipicio a un paso
{
    int vx, vz;
    dir_vel(yaw, 4096, &vx, &vz);
    int px = (m->x + vx * 3 / 4) >> 12, pz = (m->z + vz * 3 / 4) >> 12, py = m->y >> 12;
    if (world_get(px, py, pz) == B_WATER || world_get(px, py - 1, pz) == B_WATER) return 1;
    for (int d = 1; d <= 3; d++) if (blk_solid(world_get(px, py - d, pz))) return 0;
    return 1;
}

static void ai_wander(Mob *m, int *twx, int *twz, int *moving)
{
    const MobInfo *mi = &g_info[m->type];
    if (m->ai_t == 0) {
        if (rnd(100) < 40) { m->ai_mode = 1; m->ai_yaw = rnd(32768); m->ai_t = (u16)(90 + rnd(180)); }
        else { m->ai_mode = 0; m->ai_t = (u16)(60 + rnd(240)); }
    } else m->ai_t--;
    if (m->ai_mode) {
        if (m->on_ground && edge_ahead(m, m->ai_yaw)) { m->ai_mode = 0; m->ai_t = (u16)(30 + rnd(60)); return; }
        turn_to(m, m->ai_yaw, 700);
        dir_vel(m->yaw, mi->speed * 3 / 4, twx, twz);
        *moving = 1;
    }
}

static void ai_passive(Mob *m, int *twx, int *twz, int *moving)
{
    const MobInfo *mi = &g_info[m->type];
    if (m->panic) {
        m->panic--;
        turn_to(m, m->ai_yaw, 1400);
        dir_vel(m->yaw, mi->speed * mi->panic_mul / 16, twx, twz);
        *moving = 1;
        if (m->on_ground && (m->age & 63) == 0) m->ai_yaw = (m->ai_yaw + rnd(6000) - 3000) & 0x7FFF;
    } else ai_wander(m, twx, twz, moving);
}

static void melee(Mob *m, Player *p, int dmg, int reach)
{
    if (m->atk_cd) return;
    s64 dx = p->x - m->x, dz = p->z - m->z;
    if (dx * dx + dz * dz > (s64)reach * reach) return;
    if (!(p->y < m->y + g_info[m->type].h && p->y + PH > m->y)) return;
    m->atk_cd = 60;
    m->swing = 18;
    player_hurt(p, dmg, m->x, m->z, 0);
}

static void ai_hostile(Mob *m, Player *p, int light, int *twx, int *twz, int *moving)
{
    const MobInfo *mi = &g_info[m->type];
    int dx = p->x - m->x, dz = p->z - m->z, dy = p->y - m->y;
    s64 d2 = (s64)dx * dx + (s64)dz * dz;

    if (m->ai_check == 0) {
        m->ai_check = 10;
        int ok_player = p->gamemode == 0 && p->health > 0;
        if (!ok_player) m->target = 0;
        else if (!m->target) {
            int r = 16 * FP;
            if (d2 < (s64)r * r && abs(dy) < 10 * FP &&
                line_clear(m->x, m->y + 6000, m->z, p->x, p->y + 6000, p->z)) m->target = 1;
        } else if (d2 > (s64)(28 * FP) * (28 * FP)) m->target = 0;
        if (m->type == MT_SPIDER && light >= 20 && !m->provoked) m->target = 0;   // de dia no atacan si no las provocas
    } else m->ai_check--;

    if (p->health <= 0) m->target = 0;
    if (!m->target) {
        if (m->type == MT_CREEPER && m->fuse > 0) m->fuse--;
        ai_wander(m, twx, twz, moving);
        return;
    }

    int yaw = yaw_of(dx, dz);
    turn_to(m, yaw, 1200);

    switch (m->type) {
    case MT_ZOMBIE:
        dir_vel(m->yaw, mi->speed, twx, twz); *moving = 1;
        melee(m, p, 3, 6000);
        break;
    case MT_SPIDER: {
        dir_vel(m->yaw, mi->speed, twx, twz); *moving = 1;
        melee(m, p, 2, 6600);
        if (m->on_ground && d2 > (s64)(2 * FP) * (2 * FP) && d2 < (s64)(6 * FP) * (6 * FP) && rnd(40) == 0) {
            m->vx = (sinLerp((s16)m->yaw) * 600) >> 12;      // salto de ataque
            m->vz = -((cosLerp((s16)m->yaw) * 600) >> 12);
            m->vy = 450;
        }
        break;
    }
    case MT_SKELETON: {
        int see = line_clear(m->x, m->y + 6000, m->z, p->x, p->y + 5500, p->z);
        s64 near2 = (s64)(10 * FP) * (10 * FP);
        if (!see || d2 > near2) { dir_vel(m->yaw, mi->speed, twx, twz); *moving = 1; }
        if (m->shoot_cd) m->shoot_cd--;
        if (see && d2 < (s64)(15 * FP) * (15 * FP)) {
            if (m->shoot_cd == 0) { shoot_arrow(m, p); m->shoot_cd = (u8)(100 + rnd(60)); m->swing = 18; }
        }
        break;
    }
    case MT_CREEPER: {
        s64 d3 = d2 + (s64)dy * dy;
        if (d3 < (s64)(3 * FP) * (3 * FP)) {
            if (m->fuse == 0) sfx_play_group_3d(SFXG_FUSE, 100, m->x - p->x, m->z - p->z, p->yaw);
            if (++m->fuse >= 90) {
                int ex = m->x, ey = m->y + 3000, ez = m->z;
                m->active = 0;
                mobs_explode(p, ex, ey, ez, 3);
                return;
            }
        } else {
            if (m->fuse > 0) m->fuse--;
            dir_vel(m->yaw, mi->speed, twx, twz); *moving = 1;
        }
        break;
    }
    default: break;
    }
}

static void update_one(Mob *m, Player *p, int light)
{
    const MobInfo *mi = &g_info[m->type];
    m->age++;
    if (m->invuln) m->invuln--;
    if (m->hurt_t) m->hurt_t--;
    if (m->swing) m->swing--;
    if (m->atk_cd) m->atk_cd--;

    if (m->dying) {
        mob_step(m, 0, 0, 0);
        if (++m->die_t >= 40) m->active = 0;
        return;
    }

    // zombis y esqueletos arden al sol
    if (m->type == MT_ZOMBIE || m->type == MT_SKELETON) {
        if ((m->age & 15) == 0) {
            if (light >= 28 && !m->in_water && sky_exposed(m->x >> 12, (m->y + 4000) >> 12, m->z >> 12)) { if (!m->burn) m->burn = 480; }
            else if (m->in_water) m->burn = 0;
        }
        if (m->burn) {
            m->burn--;
            if (m->burn % 60 == 0) { m->hp -= 1; m->hurt_t = 10; if (m->hp <= 0) { mob_die(m, p); return; } else snd3d(m, p, mi->hurt, 90); }
        }
    }

    // sonido ambiente
    if (m->snd_t == 0) {
        m->snd_t = (u16)(240 + rnd(360));
        if (m->type != MT_CREEPER) snd3d(m, p, mi->say, 70);
    } else m->snd_t--;

    int twx = 0, twz = 0, moving = 0;
    if (mi->hostile) ai_hostile(m, p, light, &twx, &twz, &moving);
    else ai_passive(m, &twx, &twz, &moving);
    if (!m->active) return;
    int wd0 = m->walk_dist;
    mob_step(m, twx, twz, moving);
    // paso cada ~0,75 bloque recorrido (en el suelo y fuera del agua)
    if (m->on_ground && !m->in_water && (m->walk_dist / 384) != (wd0 / 384)) {
        if (mi->step >= 0) snd3d(m, p, mi->step, 45);
        else sfx_step_3d(world_get(m->x >> 12, (m->y - 600) >> 12, m->z >> 12), 45, m->x - p->x, m->z - p->z, p->yaw);
    }
}

// ---------------------------------------------------------------------------
// Aparicion
// ---------------------------------------------------------------------------
static void spawn_at(int type, int x, int y, int z)
{
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &g_mobs[i];
        if (m->active) continue;
        memset(m, 0, sizeof(*m));
        m->active = 1; m->type = (u8)type;
        m->hp = g_info[type].hp;
        m->x = (x << 12) + 2048; m->y = y << 12; m->z = (z << 12) + 2048;
        m->yaw = rnd(32768);
        m->snd_t = (u16)(120 + rnd(400));
        m->ai_t = (u16)rnd(120);
        m->ai_check = (u16)rnd(10);
        m->shoot_cd = (u8)(60 + rnd(60));
        return;
    }
}

static int free_cell(int x, int y, int z)
{
    u8 b = world_get(x, y, z);
    return !blk_solid(b) && !(g_blocks[b].flags & F_LIQUID);
}

static int spot_ok(int x, int y, int z)
{
    return y > 1 && y < WY - 3 && blk_solid(world_get(x, y - 1, z)) && !(g_blocks[world_get(x, y - 1, z)].flags & F_LIQUID) &&
           free_cell(x, y, z) && free_cell(x, y + 1, z) && free_cell(x, y + 2, z);
}

static void ring_offset(int rmin, int rmax, int *ox, int *oz)
{
    int a = rnd(32768), r = rmin + rnd(rmax - rmin);
    *ox = (sinLerp((s16)a) * r) >> 12;
    *oz = -((cosLerp((s16)a) * r) >> 12);
}

static void try_spawn_passive(const Player *p, int rmin, int rmax)
{
    int ox, oz;
    ring_offset(rmin, rmax, &ox, &oz);
    int x = (p->x >> 12) + ox, z = (p->z >> 12) + oz;
    if (!world_col_loaded(x, z)) return;
    int y = world_height(x, z) + 1;
    if (world_get(x, y - 1, z) != B_GRASS || !spot_ok(x, y, z)) return;
    static const u8 kinds[10] = {MT_PIG, MT_PIG, MT_COW, MT_COW, MT_SHEEP, MT_SHEEP, MT_SHEEP, MT_CHICKEN, MT_CHICKEN, MT_COW};
    spawn_at(kinds[rnd(10)], x, y, z);
}

static void try_spawn_hostile(const Player *p, int light)
{
    int ox, oz;
    ring_offset(20, 36, &ox, &oz);
    int x = (p->x >> 12) + ox, z = (p->z >> 12) + oz;
    if (!world_col_loaded(x, z)) return;
    int y0 = (p->y >> 12) + rnd(25) - 12;
    if (y0 < 2) y0 = 2;
    for (int y = y0; y < y0 + 8 && y < WY - 3; y++) {
        if (!spot_ok(x, y, z)) continue;
        int exposed = sky_exposed(x, y, z);
        if (exposed && light > 15) return;               // en la superficie solo de noche
        for (int ty = -3; ty <= 3; ty++)
            for (int tz = -6; tz <= 6; tz++)
                for (int tx = -6; tx <= 6; tx++)
                    if (BLK_IS_TORCH(world_get(x + tx, y + ty, z + tz))) return;
        static const u8 kinds[10] = {MT_ZOMBIE, MT_ZOMBIE, MT_ZOMBIE, MT_ZOMBIE, MT_SKELETON, MT_SKELETON, MT_CREEPER, MT_CREEPER, MT_SPIDER, MT_SPIDER};
        spawn_at(kinds[rnd(10)], x, y, z);
        return;
    }
}

void mobs_clear(void)
{
    memset(g_mobs, 0, sizeof(g_mobs));
    memset(g_arrows, 0, sizeof(g_arrows));
}

void mobs_populate(const Player *p)
{
    for (int i = 0; i < 40; i++) {
        int n = 0;
        for (int k = 0; k < MAX_MOBS; k++) if (g_mobs[k].active) n++;
        if (n >= 8) break;
        try_spawn_passive(p, 6, 30);
    }
}

void mobs_update(Player *p)
{
    static int tick;
    tick++;
    int light = daynight_light();

    if (tick % 120 == 0) {
        int hn = 0, pn = 0;
        for (int i = 0; i < MAX_MOBS; i++)
            if (g_mobs[i].active && !g_mobs[i].dying) { if (g_info[g_mobs[i].type].hostile) hn++; else pn++; }
        if (pn < PASSIVE_CAP && rnd(3) == 0) try_spawn_passive(p, 14, 40);
        if (hn < HOSTILE_CAP) try_spawn_hostile(p, light);
    }

    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &g_mobs[i];
        if (!m->active) continue;
        int dx = abs(m->x - p->x) >> 12, dz = abs(m->z - p->z) >> 12;
        if (dx > 70 || dz > 70) { m->active = 0; continue; }
        // los monstruos lejanos se retiran poco a poco (como el despawn de MC)
        if (g_info[m->type].hostile && (dx > 44 || dz > 44) && rnd(300) == 0) { m->active = 0; continue; }
        if (dx > 48 || dz > 48) continue;                              // fuera de la zona simulada
        if (!world_col_loaded(m->x >> 12, m->z >> 12)) continue;
        update_one(m, p, light);
    }
    arrows_update(p);
}

// ---------------------------------------------------------------------------
// Puntería del jugador: rayo contra las cajas de los mobs
// ---------------------------------------------------------------------------
#define MOB_REACH (7 * FP / 2)

int mobs_target(const Player *p, int *dist)
{
    int ex, ey, ez, fx, fy, fz;
    player_eye(p, &ex, &ey, &ez);
    player_forward(p, &fx, &fy, &fz);
    int best = -1, bt = 0x7FFFFFFF;
    const int pad = 400;                   // 0,1 bloque de margen, como collisionBorderSize
    for (int i = 0; i < MAX_MOBS; i++) {
        const Mob *m = &g_mobs[i];
        if (!m->active || m->dying) continue;
        if (abs(m->x - ex) > 6 * FP || abs(m->z - ez) > 6 * FP) continue;
        int hw = g_info[m->type].hw + pad;
        int lo[3] = { m->x - hw, m->y - pad, m->z - hw };
        int hi[3] = { m->x + hw, m->y + g_info[m->type].h + pad, m->z + hw };
        int o[3] = { ex, ey, ez }, d[3] = { fx, fy, fz };
        s64 tmin = 0, tmax = (s64)MOB_REACH;
        int ok = 1;
        for (int a = 0; a < 3 && ok; a++) {
            if (d[a] == 0) { if (o[a] < lo[a] || o[a] > hi[a]) ok = 0; continue; }
            s64 t0 = ((s64)(lo[a] - o[a]) << 12) / d[a];
            s64 t1 = ((s64)(hi[a] - o[a]) << 12) / d[a];
            if (t0 > t1) { s64 t = t0; t0 = t1; t1 = t; }
            if (t0 > tmin) tmin = t0;
            if (t1 < tmax) tmax = t1;
            if (tmin > tmax) ok = 0;
        }
        if (ok && (int)tmin < bt) { bt = (int)tmin; best = i; }
    }
    if (dist) *dist = bt;
    return best;
}
