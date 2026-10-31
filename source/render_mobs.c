// Renderizado de mobs, flechas y armaduras (modelos de cajas de Minecraft 1.7.10)
#include <stdlib.h>
#include <string.h>
#include "render_mobs.h"
#include "render.h"
#include "mobs.h"
#include "items.h"
#include "assets.h"
#include "world.h"

#define PXM 64          // unidades v16 por pixel del modelo (1 bloque = 1024 = 16 px)
#define PXS 60          // pixeles de Steve (modelo escalado 0.9375)

enum { TX_PIG, TX_COW, TX_SHEEP, TX_FUR, TX_CHICKEN, TX_ZOMBIE, TX_SKELETON, TX_CREEPER, TX_SPIDER, TX_COUNT };
static int tex_mob[TX_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
static int tex_armor[5] = {-1, -1, -1, -1, -1};

static int size_en(int n)
{
    switch (n) {
        case 32: return TEXTURE_SIZE_32;
        case 64: return TEXTURE_SIZE_64;
        default: return TEXTURE_SIZE_128;
    }
}

static int upload64(const u8 *data, int w, int h)
{
    int id;
    size_t bytes = (size_t)w * h * 2;
    u16 *buf = (u16 *)malloc(bytes);
    if (!buf) return -1;
    memcpy(buf, data, bytes);
    glGenTextures(1, &id);
    glBindTexture(0, id);
    glTexImage2D(0, 0, GL_RGBA, size_en(w), size_en(h), 0, TEXGEN_TEXCOORD, buf);
    free(buf);
    return id;
}

void mobgfx_load(void)
{
    for (int i = 0; i < TX_COUNT; i++) tex_mob[i] = upload64(mobs_bin + i * 64 * 32 * 2, 64, 32);
    for (int i = 0; i < 5; i++) tex_armor[i] = upload64(armor_bin + i * 64 * 64 * 2, 64, 64);
}

void mobgfx_unload(void)
{
    for (int i = 0; i < TX_COUNT; i++) tex_mob[i] = -1;
    for (int i = 0; i < 5; i++) tex_armor[i] = -1;
}

// ---------------------------------------------------------------------------
// Cajas
// ---------------------------------------------------------------------------
static const s8 fv[6][12] = {
    {1,1,1, 1,1,0, 1,0,0, 1,0,1},   // +X
    {0,1,0, 0,1,1, 0,0,1, 0,0,0},   // -X
    {0,1,0, 1,1,0, 1,1,1, 0,1,1},   // +Y
    {0,0,1, 1,0,1, 1,0,0, 0,0,0},   // -Y
    {0,1,1, 1,1,1, 1,0,1, 0,0,1},   // +Z
    {1,1,0, 0,1,0, 0,0,0, 1,0,0},   // -Z
};
static const s8 vtop[12] = {1,1,1, 0,1,1, 0,1,0, 1,1,0};
static const s8 vbot[12] = {1,0,0, 0,0,0, 0,0,1, 1,0,1};
static const u8 uvc[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
static const u8 shd[6] = {19, 19, 31, 15, 25, 25};

// colores del fotograma actual
static int c_light = 31;       // brillo del mundo
static int c_r = 31, c_g = 31, c_b = 31;   // tinte (31 = sin tinte)
static int s_quads;            // poligonos emitidos

static inline u16 shade_col(int s)
{
    s = s * c_light / 31;
    return RGB15(s * c_r / 31, s * c_g / 31, s * c_b / 31);
}

// Caja con la distribucion de textura de Minecraft. Coordenadas y tamano en pixeles de modelo (y arriba);
// e = expansion en unidades v16 por cada lado (armaduras, lana). unit = unidades v16 por pixel.
static void mbox(int unit, int x0, int y0, int z0, int w, int h, int d, int u, int v, int e)
{
    int ru[6], rv[6], rw[6], rh[6];
    ru[0] = u;             rv[0] = v + d; rw[0] = d; rh[0] = h;
    ru[1] = u + d + w;     rv[1] = v + d; rw[1] = d; rh[1] = h;
    ru[2] = u + d;         rv[2] = v;     rw[2] = w; rh[2] = d;
    ru[3] = u + d + w;     rv[3] = v;     rw[3] = w; rh[3] = d;
    ru[4] = u + 2 * d + w; rv[4] = v + d; rw[4] = w; rh[4] = h;
    ru[5] = u + d;         rv[5] = v + d; rw[5] = w; rh[5] = h;
    int bx = x0 * unit - e, by = y0 * unit - e, bz = z0 * unit - e;
    int sx = w * unit + 2 * e, sy = h * unit + 2 * e, sz = d * unit + 2 * e;
    glBegin(GL_QUADS);
    for (int dir = 0; dir < 6; dir++) {
        const s8 *vt = dir == 2 ? vtop : (dir == 3 ? vbot : fv[dir]);
        glColor(shade_col(shd[dir]));
        for (int i = 0; i < 4; i++) {
            int tu = (ru[dir] << 4) + (uvc[i][0] ? (rw[dir] << 4) - 2 : 2);
            int tv = (rv[dir] << 4) + (uvc[i][1] ? (rh[dir] << 4) - 2 : 2);
            glTexCoord2t16(tu, tv);
            glVertex3v16(bx + vt[i * 3] * sx, by + vt[i * 3 + 1] * sy, bz + vt[i * 3 + 2] * sz);
        }
    }
    glEnd();
    s_quads += 6;
}

// Caja en coordenadas del modelo de Minecraft (y hacia abajo, derecha en -x, suelo en y = 24):
// pivote (px, py, pz), giros (radianes de MC en unidades de 15 bits), caja (ox, oy, oz, w, h, d) y textura (u, v).
static void mcbox(int px, int py, int pz, int rx, int ry, int rz,
                  int ox, int oy, int oz, int w, int h, int d, int u, int v, int e)
{
    glPushMatrix();
    glTranslatef32(-px * PXM, (24 - py) * PXM, pz * PXM);
    if (rz) glRotateZi(rz & 0x7FFF);
    if (ry) glRotateYi((-ry) & 0x7FFF);
    if (rx) glRotateXi((-rx) & 0x7FFF);
    mbox(PXM, -(ox + w), -(oy + h), oz, w, h, d, u, v, e);
    glPopMatrix(1);
}

#define A(deg) ((deg) * 32768 / 360)

// ---------------------------------------------------------------------------
// Modelos
// ---------------------------------------------------------------------------
static void quad_legs(int px1, int px2, int pz1, int pz2, int py, int h, int u, int v, int sw)
{
    mcbox(-px1, py, pz1, sw, 0, 0, -2, 0, -2, 4, h, 4, u, v, 0);
    mcbox(px2, py, pz1, -sw, 0, 0, -2, 0, -2, 4, h, 4, u, v, 0);
    mcbox(-px1, py, pz2, -sw, 0, 0, -2, 0, -2, 4, h, 4, u, v, 0);
    mcbox(px2, py, pz2, sw, 0, 0, -2, 0, -2, 4, h, 4, u, v, 0);
}

static void model_pig(const Mob *m, int sw)
{
    glBindTexture(0, tex_mob[TX_PIG]);
    mcbox(0, 12, -6, 0, 0, 0, -4, -4, -8, 8, 8, 8, 0, 0, 0);
    mcbox(0, 12, -6, 0, 0, 0, -2, 0, -9, 4, 3, 1, 16, 16, 0);
    mcbox(0, 11, 2, A(90), 0, 0, -5, -10, -7, 10, 16, 8, 28, 8, 0);
    quad_legs(3, 3, 7, -5, 18, 6, 0, 16, sw);
}

static void model_cow(const Mob *m, int sw)
{
    glBindTexture(0, tex_mob[TX_COW]);
    mcbox(0, 4, -8, 0, 0, 0, -4, -4, -6, 8, 8, 6, 0, 0, 0);
    mcbox(0, 4, -8, 0, 0, 0, -5, -5, -4, 1, 3, 1, 22, 0, 0);
    mcbox(0, 4, -8, 0, 0, 0, 4, -5, -4, 1, 3, 1, 22, 0, 0);
    mcbox(0, 5, 2, A(90), 0, 0, -6, -10, -7, 12, 18, 10, 18, 4, 0);
    mcbox(0, 5, 2, A(90), 0, 0, -2, 2, -8, 4, 6, 1, 52, 0, 0);
    quad_legs(4, 4, 7, -6, 12, 12, 0, 16, sw);
}

static void model_sheep(const Mob *m, int sw)
{
    glBindTexture(0, tex_mob[TX_SHEEP]);
    mcbox(0, 6, -8, 0, 0, 0, -3, -4, -6, 6, 6, 8, 0, 0, 0);
    quad_legs(3, 3, 7, -5, 12, 12, 0, 16, sw);
    glBindTexture(0, tex_mob[TX_FUR]);
    mcbox(0, 6, -8, 0, 0, 0, -3, -4, -4, 6, 6, 6, 0, 0, 36);
    mcbox(0, 5, 2, A(90), 0, 0, -4, -10, -7, 8, 16, 6, 28, 8, 105);
}

static void model_chicken(const Mob *m, int sw)
{
    glBindTexture(0, tex_mob[TX_CHICKEN]);
    mcbox(0, 15, -4, 0, 0, 0, -2, -6, -2, 4, 6, 3, 0, 0, 0);
    mcbox(0, 15, -4, 0, 0, 0, -2, -4, -4, 4, 2, 2, 14, 0, 0);
    mcbox(0, 15, -4, 0, 0, 0, -1, -2, -3, 2, 2, 2, 14, 4, 0);
    mcbox(0, 16, 0, A(90), 0, 0, -3, -4, -3, 6, 8, 6, 0, 9, 0);
    mcbox(-2, 19, 1, sw, 0, 0, -1, 0, -3, 3, 5, 3, 26, 0, 0);
    mcbox(1, 19, 1, -sw, 0, 0, -1, 0, -3, 3, 5, 3, 26, 0, 0);
    int fl = m->on_ground ? 0 : (sinLerp((s16)(m->age * 2600)) * 6000) >> 12;   // aletea en el aire
    mcbox(-4, 13, 0, 0, 0, -fl - (m->on_ground ? 0 : 3000), 0, 0, -3, 1, 4, 6, 24, 13, 0);
    mcbox(4, 13, 0, 0, 0, fl + (m->on_ground ? 0 : 3000), -1, 0, -3, 1, 4, 6, 24, 13, 0);
}

static void model_biped(const Mob *m, int sw, int skeleton)
{
    glBindTexture(0, tex_mob[skeleton ? TX_SKELETON : TX_ZOMBIE]);
    int aw = skeleton ? 2 : 4;          // ancho de brazos y piernas
    int ao = skeleton ? 1 : 2;
    mcbox(0, 0, 0, 0, 0, 0, -4, -8, -4, 8, 8, 8, 0, 0, 0);
    mcbox(0, 0, 0, 0, 0, 0, -4, 0, -2, 8, 12, 4, 16, 16, 0);
    // En Minecraft 1.7.10 (ModelSkeleton), el esqueleto lleva siempre el arco en la mano derecha con ambos brazos alzados
    int bob = (sinLerp((s16)(m->age * 300)) * 300) >> 12;
    int arm = skeleton ? (-A(85) + bob) : (-A(90) + bob);
    if (m->swing) arm -= (sinLerp((s16)((18 - m->swing) * 910)) * A(30)) >> 12;
    int lsw = skeleton ? (-A(85) - bob) : (arm - 2 * bob);
    int ry_r = skeleton ? -A(6) : 0;
    int ry_l = skeleton ? A(26) : 0;
    int oz = skeleton ? -1 : -2;
    mcbox(-5, 2, 0, arm, ry_r, 0, skeleton ? -1 : -3, -2, oz, aw, 12, aw, 40, 16, 0);
    mcbox(5, 2, 0, lsw, ry_l, 0, -1, -2, oz, aw, 12, aw, 40, 16, 0);
    if (skeleton) {
        int pull = 0;
        if (m->target && m->shoot_cd < 45) pull = m->shoot_cd < 15 ? 3 : (m->shoot_cd < 30 ? 2 : 1);
        glPushMatrix();
        glTranslatef32(5 * PXM, (24 - 2) * PXM, 0);
        if (ry_r) glRotateYi((-ry_r) & 0x7FFF);
        glRotateXi((-arm) & 0x7FFF);
        render_draw_skeleton_bow(pull, c_light);
        glPopMatrix(1);
        glBindTexture(0, tex_mob[TX_SKELETON]);
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(7) | g_polyfog);
        s_quads += 6;
    }
    mcbox(-2, 12, 0, sw, 0, 0, -ao, 0, -ao, aw, 12, aw, 0, 16, 0);
    mcbox(2, 12, 0, -sw, 0, 0, -ao, 0, -ao, aw, 12, aw, 0, 16, 0);
}

static void model_creeper(const Mob *m, int sw)
{
    glBindTexture(0, tex_mob[TX_CREEPER]);
    mcbox(0, 6, 0, 0, 0, 0, -4, -8, -4, 8, 8, 8, 0, 0, 0);
    mcbox(0, 6, 0, 0, 0, 0, -4, 0, -2, 8, 12, 4, 16, 16, 0);
    mcbox(-2, 18, 4, sw, 0, 0, -2, 0, -2, 4, 6, 4, 0, 16, 0);
    mcbox(2, 18, 4, -sw, 0, 0, -2, 0, -2, 4, 6, 4, 0, 16, 0);
    mcbox(-2, 18, -4, -sw, 0, 0, -2, 0, -2, 4, 6, 4, 0, 16, 0);
    mcbox(2, 18, -4, sw, 0, 0, -2, 0, -2, 4, 6, 4, 0, 16, 0);
}

// capa blanca translucida que parpadea cuando la mecha esta encendida
static void model_creeper_flash(const Mob *m)
{
    glBindTexture(0, 0);
    glPolyFmt(POLY_ALPHA(14) | POLY_CULL_NONE | POLY_ID(12));
    c_light = 31; c_r = c_g = c_b = 31;
    mcbox(0, 6, 0, 0, 0, 0, -4, -8, -4, 8, 8, 8, 0, 0, 40);
    mcbox(0, 6, 0, 0, 0, 0, -4, 0, -2, 8, 12, 4, 16, 16, 40);
    mcbox(-2, 18, 4, 0, 0, 0, -2, 0, -2, 4, 6, 4, 0, 16, 40);
    mcbox(2, 18, 4, 0, 0, 0, -2, 0, -2, 4, 6, 4, 0, 16, 40);
    mcbox(-2, 18, -4, 0, 0, 0, -2, 0, -2, 4, 6, 4, 0, 16, 40);
    mcbox(2, 18, -4, 0, 0, 0, -2, 0, -2, 4, 6, 4, 0, 16, 40);
}

static void model_spider(const Mob *m, int sw)
{
    glBindTexture(0, tex_mob[TX_SPIDER]);
    mcbox(0, 15, -3, 0, 0, 0, -4, -4, -8, 8, 8, 8, 32, 4, 0);
    mcbox(0, 15, 0, 0, 0, 0, -3, -3, -3, 6, 6, 6, 0, 0, 0);
    mcbox(0, 15, 9, 0, 0, 0, -5, -4, -6, 10, 8, 12, 0, 12, 0);
    // 8 patas: (z del pivote, giro Y, giro Z); las derechas (x < 0) y las izquierdas (x > 0)
    static const s8 pz[4] = {2, 1, 0, -1};
    static const s16 ry[4] = {A(45), A(22), -A(22), -A(45)};
    static const s16 rz[4] = {-A(45), -A(33), -A(33), -A(45)};
    int w = sw / 3;
    for (int i = 0; i < 4; i++) {
        int wig = (i & 1) ? w : -w;
        mcbox(-4, 15, pz[i], 0, ry[i] + wig, rz[i], -15, -1, -1, 16, 2, 2, 18, 0, 0);
        mcbox(4, 15, pz[i], 0, -ry[i] - wig, -rz[i], -1, -1, -1, 16, 2, 2, 18, 0, 0);
    }
}

static int model_cost(int type)
{
    static const u8 c[MT_COUNT] = {7 * 6, 9 * 6, 7 * 6, 8 * 6, 6 * 6, 6 * 6, 6 * 6, 11 * 6};
    return c[type];
}

static void draw_mob(const Mob *m, int light)
{
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(7) | g_polyfog);
    c_light = light;
    c_r = c_g = c_b = 31;
    if (m->hurt_t > 0 || m->dying) { c_g = c_b = 11; }                       // rojo al recibir dano
    else if (m->burn > 0 && ((m->age >> 3) & 1)) { c_g = 20; c_b = 8; }      // ardiendo
    glPushMatrix();
    glTranslatef32(m->x >> 2, m->y >> 2, m->z >> 2);
    glRotateYi((-m->yaw) & 0x7FFF);
    if (m->dying) glRotateZi(-(m->die_t > 40 ? 40 : m->die_t) * A(90) / 40);   // cae de costado
    if (m->type == MT_CREEPER && m->fuse > 0) {                                    // se hincha
        int sc = 4096 + m->fuse * 14 + ((sinLerp((s16)(m->fuse * 1500)) * m->fuse) >> 9);
        glScalef32(sc, sc * 9 / 10 + 410, sc);
    }
    int ws = m->walk_speed * 2;
    if (ws > 21) ws = 21;
    int sw = (sinLerp((s16)(m->walk_dist * 16)) * 5200 >> 12) * ws / 21;
    switch (m->type) {
        case MT_PIG: model_pig(m, sw); break;
        case MT_COW: model_cow(m, sw); break;
        case MT_SHEEP: model_sheep(m, sw); break;
        case MT_CHICKEN: model_chicken(m, sw); break;
        case MT_ZOMBIE: model_biped(m, sw, 0); break;
        case MT_SKELETON: model_biped(m, sw, 1); break;
        case MT_CREEPER:
            model_creeper(m, sw);
            if (m->fuse > 0 && ((m->fuse / 4) & 1)) model_creeper_flash(m);
            break;
        case MT_SPIDER: model_spider(m, sw); break;
    }
    glPopMatrix(1);
}

// ---------------------------------------------------------------------------
// Flechas
// ---------------------------------------------------------------------------
static void draw_arrow(const Arrow *a, int light)
{
    glBindTexture(0, 0);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(9) | g_polyfog);
    glPushMatrix();
    glTranslatef32(a->x >> 2, a->y >> 2, a->z >> 2);
    glRotateYi((-a->yaw) & 0x7FFF);
    glRotateXi(a->pitch & 0x7FFF);
    c_light = light; c_r = c_g = c_b = 31;
    // asta (madera) y punta (gris): 1 px de lado, 14 px de largo, apuntando a -Z
    static const struct { int z0, d; } part[2] = { {-5, 12}, {-8, 3} };
    for (int i = 0; i < 2; i++) {
        int shade_r = i ? 22 : 17, shade_g = i ? 22 : 12, shade_b = i ? 22 : 5;
        int l = light;
        glBegin(GL_QUADS);
        for (int dir = 0; dir < 6; dir++) {
            const s8 *vt = dir == 2 ? vtop : (dir == 3 ? vbot : fv[dir]);
            int sh = shd[dir] * l / 31;
            glColor(RGB15(shade_r * sh / 31, shade_g * sh / 31, shade_b * sh / 31));
            for (int k = 0; k < 4; k++)
                glVertex3v16(-32 + vt[k * 3] * 64, -32 + vt[k * 3 + 1] * 64, part[i].z0 * 64 + vt[k * 3 + 2] * part[i].d * 64);
        }
        glEnd();
        s_quads += 6;
    }
    glPopMatrix(1);
}

// ---------------------------------------------------------------------------
// Todos los mobs visibles
// ---------------------------------------------------------------------------
void mobgfx_draw_all(const Player *p, int fx, int fy, int fz, int light, int quad_budget)
{
    int order[MAX_MOBS], dist[MAX_MOBS], n = 0;
    for (int i = 0; i < MAX_MOBS; i++) {
        const Mob *m = &g_mobs[i];
        if (!m->active || !world_col_loaded(m->x >> 12, m->z >> 12)) continue;
        int dx = m->x - p->x, dy = m->y - p->y, dz = m->z - p->z;
        if (abs(dx >> 12) > 26 || abs(dz >> 12) > 26 || abs(dy >> 12) > 20) continue;
        if (((dx * fx + dy * fy + dz * fz) >> 12) < -2 * FP) continue;          // detras de la camara
        int d = (dx >> 6) * (dx >> 6) + (dz >> 6) * (dz >> 6);
        int k = n++;
        while (k > 0 && dist[k - 1] > d) { dist[k] = dist[k - 1]; order[k] = order[k - 1]; k--; }
        dist[k] = d; order[k] = i;
    }
    s_quads = 0;
    for (int k = 0; k < n; k++) {
        const Mob *m = &g_mobs[order[k]];
        if (s_quads + model_cost(m->type) > quad_budget) continue;
        (void)light;
        draw_mob(m, render_sample_light(m->x >> 12, (m->y >> 12) + 1, m->z >> 12));
    }
    for (int i = 0; i < MAX_ARROWS && s_quads + 12 <= quad_budget + 24; i++) {
        const Arrow *a = &g_arrows[i];
        if (!a->active) continue;
        if (abs((a->x - p->x) >> 12) > 24 || abs((a->z - p->z) >> 12) > 24) continue;
        draw_arrow(a, render_sample_light(a->x >> 12, a->y >> 12, a->z >> 12));
    }
}

// ---------------------------------------------------------------------------
// Armadura sobre el modelo de Steve (capa 1: casco, peto, botas; capa 2: pantalones)
// ---------------------------------------------------------------------------
static void armor_box(const Slot *s, int layer, int x0, int y0, int z0, int w, int h, int d, int u, int v, int e)
{
    int mat = armor_mat(s->id);
    glBindTexture(0, tex_armor[mat]);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(6) | g_polyfog);
    mbox(PXS, x0, y0, z0, w, h, d, u, v + (layer == 2 ? 32 : 0), e);
}

void mobgfx_armor_part(const Player *p, int part, int light)
{
    c_light = light; c_r = c_g = c_b = 31;
    const Slot *hel = &p->armor[0], *che = &p->armor[1], *leg = &p->armor[2], *boo = &p->armor[3];
    switch (part) {
        case PART_HEAD:
            if (hel->id) armor_box(hel, 1, -4, 0, -4, 8, 8, 8, 0, 0, 30);
            break;
        case PART_BODY:
            if (che->id) armor_box(che, 1, -4, 12, -2, 8, 12, 4, 16, 16, 30);
            if (leg->id) armor_box(leg, 2, -4, 12, -2, 8, 12, 4, 16, 16, 15);
            break;
        case PART_ARM:
            if (che->id) armor_box(che, 1, -2, -10, -2, 4, 12, 4, 40, 16, 30);
            break;
        case PART_LEG:
            if (leg->id) armor_box(leg, 2, -2, -12, -2, 4, 12, 4, 0, 16, 15);
            if (boo->id) armor_box(boo, 1, -2, -12, -2, 4, 12, 4, 0, 16, 40);
            break;
    }
}
