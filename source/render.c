// Renderizado 3D (pantalla superior): mundo, Steve, HUD, menu principal
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "render.h"
#include "game.h"
#include "blocks.h"
#include "tiles.h"
#include "assets.h"
#include "ui_layout.h"
#include "logo.h"
#include "items.h"
#include "drops.h"
#include "splash.h"
#include "mobs.h"
#include "render_mobs.h"
#include "daynight.h"
#include "chest.h"

// 1 bloque = 1024 unidades v16 (0.25 unidades GL)
#define BS 1024
// margen dentro de cada tile de 16 px (en 1/16 de texel) para evitar sangrado
#define TEX_LO 5
#define TEX_HI 251
#define MAX_WORLD_QUADS 820
#define MAX_WATER_QUADS 200
#define MAX_TOTAL_QUADS 1040
#define SCRATCH_CAP 8192

static int tex_terrain = -1, tex_hud = -1, tex_font = -1, tex_steve = -1, tex_chest = -1, tex_chest_dbl = -1, tex_logo = -1, tex_veil = -1;
static int tex_pano[6] = {-1, -1, -1, -1, -1, -1};
// Textos del menu principal pre-renderizados en CPU (ver splash.c)
static int tex_splash[SPL_VARIANTS] = {-1, -1, -1}, tex_labels = -1;
static const char *s_splash_src;            // frase para la que esta horneada tex_splash
static int s_splash_hw = 60;                // semianchura visible de la frase girada
static int s_lbl_w1, s_lbl_w2, s_lbl_w3;    // anchos de los 3 textos inferiores (version, "Developed by ..." y "and ...")
static int s_quads_last;
u32 g_polyfog;
static int s_light = 31;                    // brillo actual (mundo o entidad local)
static int s_sky_light = 31;                // brillo del cielo (dia / noche, 12..31)
static int s_cave_sky = 256;                // 256 = cielo exterior visible, 0 = dentro de cueva / espacio cerrado
static int s_steve_hurt = 0;                // 1 = tinte rojizo al recibir dano
#define LIT(v) ((v) * s_light / 31)         // aplica la luz a sprites y modelos que no son del terreno

// Tabla de color por vertice: indice = (v_blk << 4) | v_sky (0..255) -> RGB15
static u16 s_vcol_lut[256];

static void update_vcol_lut(int sky_light)
{
    static int s_last_sky = -1;
    if (sky_light == s_last_sky) return;
    s_last_sky = sky_light;
    for (int blk = 0; blk < 16; blk++) {
        int br = blk ? (4 + (blk * 27 + 7) / 15) : 0;
        int bg = blk ? (3 + (blk * 24 + 7) / 15) : 0;
        int bb = blk ? (2 + (blk * 18 + 7) / 15) : 0;
        for (int sky = 0; sky < 16; sky++) {
            int sl = sky ? (3 + (sky * (sky_light - 3) + 7) / 15) : 2;
            int r = sl > br ? sl : br;
            int g = sl > bg ? sl : bg;
            int b = sl > bb ? sl : bb;
            s_vcol_lut[(blk << 4) | sky] = RGB15(r, g, b);
        }
    }
}

// ---------------------------------------------------------------------------
// Texturas
// ---------------------------------------------------------------------------
static int size_enum(int n)
{
    switch (n) {
        case 8: return TEXTURE_SIZE_8;
        case 16: return TEXTURE_SIZE_16;
        case 32: return TEXTURE_SIZE_32;
        case 64: return TEXTURE_SIZE_64;
        case 128: return TEXTURE_SIZE_128;
        default: return TEXTURE_SIZE_256;
    }
}

static int upload_ex(const u8 *data, int w, int h, u32 flags)
{
    int id;
    size_t bytes = (size_t)w * h * 2;
    u16 *buf = (u16 *)malloc(bytes);      // copia alineada
    memcpy(buf, data, bytes);
    glGenTextures(1, &id);
    glBindTexture(0, id);
    glTexImage2D(0, 0, GL_RGBA, size_enum(w), size_enum(h), 0, TEXGEN_TEXCOORD | flags, buf);
    free(buf);
    return id;
}

static int upload(const u8 *data, int w, int h) { return upload_ex(data, w, h, 0); }

void gfx_init(void)
{
    videoSetMode(MODE_0_3D);
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankB(VRAM_B_TEXTURE);
    vramSetBankC(VRAM_C_TEXTURE);
    vramSetBankD(VRAM_D_TEXTURE);
    glInit();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(7);
    glEnable(GL_FOG);                           // solo afecta a los poligonos con POLY_FOG
    glClearColor(15, 21, 31, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
}

static void veil_init_tex(void)
{
    static const u8 vdc16[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    u16 *buf = (u16 *)malloc(16 * 256 * 2);
    if (!buf) return;
    for (int y = 0; y < 256; y++) {
        int cy = y < 192 ? y : 191;
        int vf = (31 * 16 * (191 - cy) * (796 - cy)) / (191 * 796);
        for (int x = 0; x < 16; x++) {
            int c = (vf + vdc16[(x + cy * 5) & 15]) >> 4;
            if (c > 31) c = 31;
            buf[y * 16 + x] = ARGB16(1, c, c, c);
        }
    }
    tex_veil = upload_ex((const u8 *)buf, 16, 256, GL_TEXTURE_WRAP_S);
    free(buf);
}

void gfx_unload(void)
{
    glResetTextures();
    mobgfx_unload();
    tex_terrain = tex_hud = tex_font = tex_steve = tex_chest = tex_chest_dbl = tex_logo = tex_veil = -1;
    for (int i = 0; i < SPL_VARIANTS; i++) tex_splash[i] = -1;
    tex_labels = -1;
    s_splash_src = NULL;
    for (int i = 0; i < 6; i++) tex_pano[i] = -1;
}

void gfx_load_menu(void)
{
    gfx_unload();
    const u8 *pn[6] = {pano0_bin, pano1_bin, pano2_bin, pano3_bin, pano4_bin, pano5_bin};
    for (int i = 0; i < 6; i++) tex_pano[i] = upload(pn[i], 128, 128);
    tex_logo = upload(logo_bin, 256, 128);
    tex_font = upload(font_bin, 128, 128);
    veil_init_tex();
}

static void upload_to(int *id, const u16 *data, int w, int h)
{
    if (*id < 0) glGenTextures(1, id);
    glBindTexture(0, *id);
    glTexImage2D(0, 0, GL_RGBA, size_enum(w), size_enum(h), 0, TEXGEN_TEXCOORD, data);
}

void gfx_prepare_title(const char *splash)
{
    u16 *buf = (u16 *)malloc(SPL_W * SPL_H * 2 * SPL_VARIANTS);
    if (!buf) return;
    s_splash_src = splash;
    if (splash) {
        int hw = 60, hh = 30;
        splash_bake(splash, font_bin, fontw_bin, buf, &hw, &hh);
        s_splash_hw = hw;
        for (int i = 0; i < SPL_VARIANTS; i++) upload_to(&tex_splash[i], buf + i * SPL_W * SPL_H, SPL_W, SPL_H);
    }
    if (tex_labels < 0) {
        memset(buf, 0, LBL_W * LBL_H * 2);
        s_lbl_w1 = text_bake_outlined("Minecraft 1.0 (based on 1.7.10)", font_bin, fontw_bin, buf, LBL_W, 1, 1);
        s_lbl_w2 = text_bake_outlined("Developed by MartusTheMartian", font_bin, fontw_bin, buf, LBL_W, 1, 1 + LBL_LINE_H);
        s_lbl_w3 = text_bake_outlined("and TholyTorch", font_bin, fontw_bin, buf, LBL_W, 1, 1 + 2 * LBL_LINE_H);
        upload_to(&tex_labels, buf, LBL_W, LBL_H);
    }
    free(buf);
}

void gfx_load_game(void)
{
    gfx_unload();
    tex_terrain = upload(terrain_bin, 128, 128);
    tex_hud = upload(hud_bin, 256, HUD_ATLAS_H);
    tex_font = upload(font_bin, 128, 128);
    tex_steve = upload(steve_bin, 64, 32);
    tex_chest = upload(chest_bin, 64, 64);
    tex_chest_dbl = upload(chestdbl_bin, 128, 64);
    mobgfx_load();
    s_cave_sky = 256;
}

// ---------------------------------------------------------------------------
// Reparto del z-buffer (0 = plano cercano, 1 = lejano; unidades 4.12):
//   HUD   [0.00, 0.03)  siempre por delante de todo (corazones, muslos, burbujas, mira)
//   mano  [0.03, 0.05)  por delante del mundo
//   mundo [0.05, 1.00]  la proyeccion del mundo se comprime a este tramo
// ZNEAR_GL = 0.035f (~0.14 bloques, menor que la mitad del radio de colision PW = 0.30 bloques),
// asi que nunca se recorta la cara frontal de un bloque aunque el jugador se pegue en diagonal.
// ---------------------------------------------------------------------------
#define ZB_HAND_LO    123    // 0.03
#define ZB_HAND_SPAN   82    // 0.02
#define ZB_WORLD_LO   205    // 0.05
#define ZNEAR_GL      0.035f // ~0.14 bloques: impide ver el interior transparente al pegarse a un bloque
#define ZFAR_GL       8.0f   // 32 bloques

// ---------------------------------------------------------------------------
// HUD ortografico (1 unidad GL = 64 px; v16 = px * 64)
// ---------------------------------------------------------------------------
static int hud_z;
static int hud_z_max = 1020;

static void hud_begin(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof32(0, inttof32(4), inttof32(3), 0, -inttof32(1) / 4, inttof32(1));
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    hud_z = -1020;
    hud_z_max = 1020;
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(10));
}

#define HUD_FRONT_QUADS 48      // capacidad: mira + corazones(10) + hambre(10) + armadura(10) + burbujas(10) + velo muerte
static void hud_begin_front(void)
{
    hud_begin();
    hud_z = 1020 - 4 * HUD_FRONT_QUADS;
    hud_z_max = 1020;
}

static void hq(int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1)
{
    int z = hud_z;
    if (hud_z < hud_z_max) hud_z += 4;
    glBegin(GL_QUADS);
    glTexCoord2t16(u0 << 4, v0 << 4);               glVertex3v16(x0 * 64, y0 * 64, z);
    glTexCoord2t16((u1 << 4) - 1, v0 << 4);         glVertex3v16(x1 * 64, y0 * 64, z);
    glTexCoord2t16((u1 << 4) - 1, (v1 << 4) - 1);   glVertex3v16(x1 * 64, y1 * 64, z);
    glTexCoord2t16(u0 << 4, (v1 << 4) - 1);         glVertex3v16(x0 * 64, y1 * 64, z);
    glEnd();
}

static int hud_text_width_s(const char *s, int scale)
{
    int w = 0;
    for (const char *p = s; *p; p++) w += fontw_bin[(u8)*p] * scale;
    return w;
}

static void hud_text_pass_s(int x, int y, const char *s, u16 color, int scale)
{
    glColor(color);
    for (const char *p = s; *p; p++) {
        u8 c = (u8)*p;
        if (c != ' ') hq(x, y, x + 8 * scale, y + 8 * scale, (c & 15) * 8, (c >> 4) * 8, (c & 15) * 8 + 8, (c >> 4) * 8 + 8);
        x += fontw_bin[c] * scale;
    }
}

static void hud_text_s(int x, int y, const char *s, u16 color, int shadow, int scale)
{
    glBindTexture(0, tex_font);
    if (shadow) hud_text_pass_s(x + scale, y + scale, s, RGB15(6, 2, 2), scale);
    hud_text_pass_s(x, y, s, color, scale);
}

static void hud_icon(int block, int x, int y)
{
    if (block <= 0 || (block >= B_COUNT && (block < I_FIRST || block >= I_END))) return;
    int ic = icon_index((u8)block);
    int u = (ic & 15) * 16, v = HUD_ICON_Y0 + (ic >> 4) * 16;
    hq(x, y, x + 16, y + 16, u, v, u + 16, v + 16);
}

#define STAT_STEP       8
#define STAT_Y          2
#define STAT_HEART_X    2
#define STAT_FOOD_X     (256 - 2 - (9 * STAT_STEP + 9))

static void hud_stat_row(int x, int y, int tex_x, int value, int right_to_left)
{
    if (value < 0) value = 0;
    if (value > 20) value = 20;
    for (int i = 0; i < 10; i++) {
        int bx = x + (right_to_left ? 9 - i : i) * STAT_STEP - 1;
        int by = y - 1;
        int state = 0;
        if (value > i * 2 + 1) state = 1;
        else if (value == i * 2 + 1) state = 2;
        int tx = tex_x + state * HUD_STAT_S;
        hq(bx, by, bx + HUD_STAT_S, by + HUD_STAT_S, tx, HUD_STAT_Y, tx + HUD_STAT_S, HUD_STAT_Y + HUD_STAT_S);
    }
}

// Fila de burbujas de oxigeno bajo el agua (GuiIngame 1.7.10: 10 burbujas de derecha a izquierda bajo el hambre)
static void hud_bubbles_row(int x, int y, int air)
{
    int air20 = air > 0 ? (air + 2) / 3 : 0;   // convertir de 60 Hz (0..900) a 20 Hz de MC (0..300)
    if (air20 > 300) air20 = 300;
    int full = ((air20 - 2) * 10 + 299) / 300;
    if (full < 0) full = 0;
    int pop = (air20 * 10 + 299) / 300 - full;
    if (pop < 0) pop = 0;
    int total = full + pop;
    if (total > 10) total = 10;
    for (int i = 0; i < total; i++) {
        int bx = x + (9 - i) * STAT_STEP - 1;
        int by = y - 1;
        int tx = HUD_BUBBLE_X + (i < full ? 0 : HUD_STAT_S);
        hq(bx, by, bx + HUD_STAT_S, by + HUD_STAT_S, tx, HUD_STAT_Y, tx + HUD_STAT_S, HUD_STAT_Y + HUD_STAT_S);
    }
}

static void hud_game(const Player *p, int show_crosshair)
{
    hud_begin_front();
    if (p->health <= 0) {
        // Velo rojo translucido de muerte (GuiGameOver de Minecraft 1.7.10) + "You died!" a doble escala
        int z = hud_z;
        hud_z += 4;
        glBindTexture(0, 0);
        glPolyFmt(POLY_ALPHA(14) | POLY_CULL_NONE | POLY_ID(11));
        glColor(RGB15(20, 2, 2));
        glBegin(GL_QUADS);
        glVertex3v16(0, 0, z);
        glVertex3v16(256 * 64, 0, z);
        glVertex3v16(256 * 64, 192 * 64 + 3, z);
        glVertex3v16(0, 192 * 64 + 3, z);
        glEnd();
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(10));
        const char *msg = "You died!";
        int w2 = hud_text_width_s(msg, 2);
        hud_text_s(128 - w2 / 2, 54, msg, RGB15(31, 31, 31), 1, 2);
        return;
    }

    glBindTexture(0, tex_hud);
    glColor(RGB15(31, 31, 31));
    if (show_crosshair) hq(120, 88, 136, 104, 184, 0, 200, 16);

    if (p->gamemode == 0) {
        hud_stat_row(STAT_FOOD_X,  STAT_Y, HUD_FOOD_X,  p->food,   1);
        hud_stat_row(STAT_HEART_X, STAT_Y, HUD_STAT_X,  p->health, 0);
        int ap = player_armor_points(p);
        if (ap > 0) hud_stat_row(STAT_HEART_X, STAT_Y + 10, HUD_ARMOR_X, ap, 0);
        if (p->head_water) hud_bubbles_row(STAT_FOOD_X, STAT_Y + 10, p->air);
    }

    // Dormir (GuiIngame 1.7.10): velo azul oscuro 0x101020 que sube hasta 220/255 en 5 s y se aclara en 0,5 s al
    // despertar. Va ENCIMA del HUD (los corazones se ven oscurecidos, no desaparecen) y no llega a negro total.
    int sa = player_sleep_alpha(p);
    if (sa >= 1) {
        int z = hud_z;
        hud_z += 4;
        glBindTexture(0, 0);
        glPolyFmt(POLY_ALPHA(sa) | POLY_CULL_NONE | POLY_ID(11));
        glColor(RGB15(2, 2, 4));
        glBegin(GL_QUADS);
        glVertex3v16(0, 0, z);
        glVertex3v16(256 * 64, 0, z);
        glVertex3v16(256 * 64, 192 * 64 + 3, z);
        glVertex3v16(0, 192 * 64 + 3, z);
        glEnd();
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(10));
    }
}

// ---------------------------------------------------------------------------
// Tablas de caras y emision con Vertex Color optimizado
// ---------------------------------------------------------------------------
static const s8 face_vt[6][12] = {
    {1,1,1, 1,1,0, 1,0,0, 1,0,1},   // +X
    {0,1,0, 0,1,1, 0,0,1, 0,0,0},   // -X
    {0,1,0, 1,1,0, 1,1,1, 0,1,1},   // +Y
    {0,0,1, 1,0,1, 1,0,0, 0,0,0},   // -Y
    {0,1,1, 1,1,1, 1,0,1, 0,0,1},   // +Z
    {1,1,0, 0,1,0, 0,0,0, 1,0,0},   // -Z
};
static const u8 uv_c[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
static const u8 shade_v[6] = {19, 19, 31, 15, 25, 25};

#define FEPS 3

static const s16 face_off[6][4][3] = {
    {{1024,1027,1027}, {1024,1027,-3}, {1024,-3,-3}, {1024,-3,1027}},
    {{0,1027,-3}, {0,1027,1027}, {0,-3,1027}, {0,-3,-3}},
    {{-3,1024,-3}, {1027,1024,-3}, {1027,1024,1027}, {-3,1024,1027}},
    {{-3,0,1027}, {1027,0,1027}, {1027,0,-3}, {-3,0,-3}},
    {{-3,1027,1024}, {1027,1027,1024}, {1027,-3,1024}, {-3,-3,1024}},
    {{1027,1027,0}, {-3,1027,0}, {-3,-3,0}, {1027,-3,0}},
};

// Caras: desplazamientos de vertice precalculados (sin ramas por vertice) y bucle desenrollado.
static inline void emit_face_raw(int lx, int ly, int lz, int dir, int tile, int low)
{
    const s16 (*o)[3] = face_off[dir];
    int bx = lx * BS, by = ly * BS, bz = lz * BS;
    int u0 = ((tile & 7) << 8) + TEX_LO, v0 = ((tile >> 3) << 8) + TEX_LO;
    int u1 = u0 + (TEX_HI - TEX_LO), v1 = v0 + (TEX_HI - TEX_LO);
    int dy0 = 0, dy1 = 0, dy2 = 0, dy3 = 0;
    if (low) {      // la cara superior baja 1/8 de bloque (agua)
        const s8 *vt = face_vt[dir];
        if (vt[1])  dy0 = BS / 8;
        if (vt[4])  dy1 = BS / 8;
        if (vt[7])  dy2 = BS / 8;
        if (vt[10]) dy3 = BS / 8;
    }
    glTexCoord2t16(u0, v0); glVertex3v16(bx + o[0][0], by + o[0][1] - dy0, bz + o[0][2]);
    glTexCoord2t16(u1, v0); glVertex3v16(bx + o[1][0], by + o[1][1] - dy1, bz + o[1][2]);
    glTexCoord2t16(u1, v1); glVertex3v16(bx + o[2][0], by + o[2][1] - dy2, bz + o[2][2]);
    glTexCoord2t16(u0, v1); glVertex3v16(bx + o[3][0], by + o[3][1] - dy3, bz + o[3][2]);
}

static inline void emit_face_vc(int lx, int ly, int lz, int dir, int tile, int low, u32 vc4)
{
    u32 v0_b = vc4 & 255;
    if (vc4 == v0_b * 0x01010101u) {
        glColor(s_vcol_lut[v0_b]);
        emit_face_raw(lx, ly, lz, dir, tile, low);
        return;
    }
    const s16 (*o)[3] = face_off[dir];
    int bx = lx * BS, by = ly * BS, bz = lz * BS;
    int u0 = ((tile & 7) << 8) + TEX_LO, v0 = ((tile >> 3) << 8) + TEX_LO;
    int u1 = u0 + (TEX_HI - TEX_LO), v1 = v0 + (TEX_HI - TEX_LO);
    glColor(s_vcol_lut[vc4 & 255]);         glTexCoord2t16(u0, v0); glVertex3v16(bx + o[0][0], by + o[0][1], bz + o[0][2]);
    glColor(s_vcol_lut[(vc4 >> 8) & 255]);  glTexCoord2t16(u1, v0); glVertex3v16(bx + o[1][0], by + o[1][1], bz + o[1][2]);
    glColor(s_vcol_lut[(vc4 >> 16) & 255]); glTexCoord2t16(u1, v1); glVertex3v16(bx + o[2][0], by + o[2][1], bz + o[2][2]);
    glColor(s_vcol_lut[vc4 >> 24]);         glTexCoord2t16(u0, v1); glVertex3v16(bx + o[3][0], by + o[3][1], bz + o[3][2]);
}

// Cara de cama (9/16 de alto). code: cara superior = giro de la textura (0..3); laterales = bit 0 -> invertir u.
// Las filas 0..6 de las texturas laterales son transparentes (el bloque mide 9/16): se recortan.
#define BED_H 576
static void emit_bed_face(int lx, int ly, int lz, int d, int tile, int code, u32 vc4)
{
    const s16 (*o)[3] = face_off[d];
    int bx = lx * BS, by = ly * BS, bz = lz * BS;
    int u0 = ((tile & 7) << 8) + TEX_LO, v0 = ((tile >> 3) << 8) + TEX_LO;
    int u1 = u0 + (TEX_HI - TEX_LO), v1 = v0 + (TEX_HI - TEX_LO);
    int uu[4], vv[4];
    if (d == D_PY) {
        static const u8 cu[4] = {0, 1, 1, 0}, cv[4] = {0, 0, 1, 1};
        for (int i = 0; i < 4; i++) {
            int k = (i + code) & 3;
            uu[i] = cu[k] ? u1 : u0;
            vv[i] = cv[k] ? v1 : v0;
        }
    } else {
        int vt = v0 + 7 * 16;
        int ua = (code & 1) ? u1 : u0, ub = (code & 1) ? u0 : u1;
        uu[0] = ua; vv[0] = vt;
        uu[1] = ub; vv[1] = vt;
        uu[2] = ub; vv[2] = v1;
        uu[3] = ua; vv[3] = v1;
    }
    for (int i = 0; i < 4; i++) {
        int y = by + o[i][1];
        if (o[i][1] > 512) y -= BS - BED_H;
        glColor(s_vcol_lut[(vc4 >> (i * 8)) & 255]);
        glTexCoord2t16(uu[i], vv[i]);
        glVertex3v16(bx + o[i][0], y, bz + o[i][2]);
    }
}

static inline void emit_face(int lx, int ly, int lz, int dir, int tile, int low)
{
    int s = shade_v[dir];
    glColor(RGB15(LIT(s), LIT(s), LIT(s)));
    emit_face_raw(lx, ly, lz, dir, tile, low);
}

static inline void emit_cross_col(int lx, int ly, int lz, int tile, u16 col_top, u16 col_bot)
{
    static const s8 q[2][12] = {
        {0,1,0, 1,1,1, 1,0,1, 0,0,0},
        {1,1,0, 0,1,1, 0,0,1, 1,0,0},
    };
    int u0 = (tile & 7) * 16, v0 = (tile >> 3) * 16;
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 4; i++) {
            int vy = q[k][i * 3 + 1];
            glColor(vy ? col_top : col_bot);
            int tu = (u0 << 4) + (uv_c[i][0] ? TEX_HI : TEX_LO);
            int tv = (v0 << 4) + (uv_c[i][1] ? TEX_HI : TEX_LO);
            glTexCoord2t16(tu, tv);
            glVertex3v16((lx + q[k][i * 3]) * BS, (ly + vy) * BS, (lz + q[k][i * 3 + 2]) * BS);
        }
}

static inline void emit_cross(int lx, int ly, int lz, int tile)
{
    u16 ct = RGB15(LIT(31), LIT(31), LIT(31));
    u16 cb = RGB15(LIT(23), LIT(23), LIT(23));
    emit_cross_col(lx, ly, lz, tile, ct, cb);
}

// Antorcha 3D (2x10x2 px, de suelo o inclinada en pared como RenderBlocks.renderBlockTorchAtAngle de 1.7.10)
static void emit_torch(int lx, int ly, int lz, int dir, int tile)
{
    static const s8 b_xz[5][2] = {{8, 8}, {1, 8}, {8, 1}, {15, 8}, {8, 15}};
    static const s8 t_xz[5][2] = {{8, 8}, {5, 8}, {8, 5}, {11, 8}, {8, 11}};
    static const s8 b_y[5]     = {0, 3, 3, 3, 3};
    static const s8 t_y[5]     = {10, 13, 13, 13, 13};
    if ((unsigned)dir > 4) dir = 0;
    int bx = lx * BS + b_xz[dir][0] * 64, by = ly * BS + b_y[dir] * 64, bz = lz * BS + b_xz[dir][1] * 64;
    int tx = lx * BS + t_xz[dir][0] * 64, ty = ly * BS + t_y[dir] * 64, tz = lz * BS + t_xz[dir][1] * 64;
    const int R = 64;
    int u0 = (tile & 7) * 16, v0 = (tile >> 3) * 16;
    int ua = ((u0 + 7) << 4) + 2, ub = ((u0 + 9) << 4) - 2;
    int vt = ((v0 + 6) << 4) + 2, vb = ((v0 + 16) << 4) - 2;
    int vcap = ((v0 + 8) << 4) - 2;

    glColor(RGB15(31, 31, 31));
    // Cara +X
    glTexCoord2t16(ua, vt); glVertex3v16(tx + R, ty, tz + R);
    glTexCoord2t16(ub, vt); glVertex3v16(tx + R, ty, tz - R);
    glTexCoord2t16(ub, vb); glVertex3v16(bx + R, by, bz - R);
    glTexCoord2t16(ua, vb); glVertex3v16(bx + R, by, bz + R);
    // Cara -X
    glTexCoord2t16(ua, vt); glVertex3v16(tx - R, ty, tz - R);
    glTexCoord2t16(ub, vt); glVertex3v16(tx - R, ty, tz + R);
    glTexCoord2t16(ub, vb); glVertex3v16(bx - R, by, bz + R);
    glTexCoord2t16(ua, vb); glVertex3v16(bx - R, by, bz - R);
    // Cara +Z
    glTexCoord2t16(ua, vt); glVertex3v16(tx - R, ty, tz + R);
    glTexCoord2t16(ub, vt); glVertex3v16(tx + R, ty, tz + R);
    glTexCoord2t16(ub, vb); glVertex3v16(bx + R, by, bz + R);
    glTexCoord2t16(ua, vb); glVertex3v16(bx - R, by, bz + R);
    // Cara -Z
    glTexCoord2t16(ua, vt); glVertex3v16(tx + R, ty, tz - R);
    glTexCoord2t16(ub, vt); glVertex3v16(tx - R, ty, tz - R);
    glTexCoord2t16(ub, vb); glVertex3v16(bx - R, by, bz - R);
    glTexCoord2t16(ua, vb); glVertex3v16(bx + R, by, bz - R);
    // Tapa superior de la llama (2x2)
    glTexCoord2t16(ua, vt);   glVertex3v16(tx - R, ty, tz - R);
    glTexCoord2t16(ub, vt);   glVertex3v16(tx + R, ty, tz - R);
    glTexCoord2t16(ub, vcap); glVertex3v16(tx + R, ty, tz + R);
    glTexCoord2t16(ua, vcap); glVertex3v16(tx - R, ty, tz + R);
}

static void draw_chest_model(int is_double, int open_t);

static void draw_cube_blocktex(u8 id)
{
    if (BLK_IS_CHEST(id)) {
        glPushMatrix();
        glTranslatef32(BS / 2, 0, BS / 2);
        glRotateYi(16384);
        draw_chest_model(0, 0);
        glPopMatrix(1);
        glBindTexture(0, tex_terrain);
        return;
    }
    const BlockInfo *bi = &g_blocks[id];
    glBegin(GL_QUADS);
    if (bi->flags & F_PLANT) {
        emit_cross(0, 0, 0, bi->tex[D_PY]);
    } else {
        for (int d = 0; d < 6; d++) emit_face(0, 0, 0, d, bi->tex[d], 0);
    }
    glEnd();
}

// ---------------------------------------------------------------------------
// Iluminacion local de entidades y mallas por seccion (Vertex Color + AO)
// ---------------------------------------------------------------------------
typedef struct {
    u32 *f[3];       // 0 opacas, 1 plantas, 2 agua
    u32 *vc;         // 4 bytes de luz por vertice (i=0..3) para cada cara de f[0]
    u16 n[3];
    u16 ds[7];       // malla opaca ordenada por direccion: grupo d = [ds[d], ds[d+1])
    u8 built;
} Section;

static Section g_sec[NSEC];
static u32 *scratch[3];
static u32 *scratch_vc;

#define PACK(x, y, z, d, t, l) ((u32)(x) | ((u32)(y) << 4) | ((u32)(z) << 8) | ((u32)(d) << 12) | ((u32)(t) << 15) | ((u32)(l) << 22))
#define PACKL(x, y, z, d, t, l, lg) (PACK(x, y, z, d, t, l) | ((u32)(u8)(lg) << 24))

static const s8 ndx[6] = {1, -1, 0, 0, 0, 0};
static const s8 ndy[6] = {0, 0, 1, -1, 0, 0};
static const s8 ndz[6] = {0, 0, 0, 0, 1, -1};

static void sec_free(Section *s)
{
    for (int k = 0; k < 3; k++) { if (s->f[k]) free(s->f[k]); s->f[k] = NULL; s->n[k] = 0; }
    if (s->vc) { free(s->vc); s->vc = NULL; }
}

void render_free_world(void)
{
    for (int i = 0; i < NSEC; i++) { sec_free(&g_sec[i]); g_sec[i].built = 0; }
}

void render_column_unloaded(int cx, int cz)
{
    for (int sy = 0; sy < SYN; sy++) {
        Section *s = &g_sec[SECIDX(cx, sy, cz)];
        sec_free(s);
        s->built = 0;
    }
}

// Rejilla 3D de luz compacta con margen LG_PAD=2 (20x20x20) alrededor de la seccion de 16x16x16
#define LG_PAD 2
#define LG_S   (SEC + 2 * LG_PAD)
static u8 s_lg[LG_S][LG_S][LG_S];   // (blk << 4) | sky
static u8 s_op[LG_S][LG_S][LG_S];   // 1 = bloque opaco
static int s_lg_has_light;          // 0 si toda la seccion esta completamente a oscuras (atajo O(1))

static void build_light_grid(int sx, int sy, int sz)
{
    int wy_top = sy * SEC + (LG_S - 1 - LG_PAD);
    int max_sky = 0, has_blk = 0;

    for (int gz = 0; gz < LG_S; gz++) {
        int wz = sz * SEC + gz - LG_PAD;
        for (int gx = 0; gx < LG_S; gx++) {
            int wx = sx * SEC + gx - LG_PAD;
            const u8 *col = ((unsigned)wx < WX && (unsigned)wz < WZ) ? g_cols[(wz >> 4) * CXN + (wx >> 4)] : NULL;
            if (!col) {
                for (int gy = 0; gy < LG_S; gy++) {
                    int wy = sy * SEC + gy - LG_PAD;
                    if (wy <= SEA_LEVEL) {
                        s_op[gy][gz][gx] = 1;
                        s_lg[gy][gz][gx] = 0;
                    } else {
                        s_op[gy][gz][gx] = 0;
                        s_lg[gy][gz][gx] = 15;
                        max_sky = 15;
                    }
                }
                continue;
            }
            const u8 *col_xz = col + (((wz & 15) << 4) | (wx & 15));
            int sky = 15;
            for (int y = WY - 1; y > wy_top; y--) {
                if (y < 0) break;
                u8 b = col_xz[y << 8];
                if (!b) continue;
                if (blk_opaque(b)) { sky = 0; break; }
                if (b == B_WATER) { sky = sky > 2 ? sky - 2 : 0; if (!sky) break; }
                else if (b == B_LEAVES) { sky = sky > 1 ? sky - 1 : 0; }
            }
            for (int gy = LG_S - 1; gy >= 0; gy--) {
                int wy = sy * SEC + gy - LG_PAD;
                if (wy >= WY) {
                    s_op[gy][gz][gx] = 0;
                    s_lg[gy][gz][gx] = 15;
                    max_sky = 15;
                } else if (wy < 0) {
                    s_op[gy][gz][gx] = 1;
                    s_lg[gy][gz][gx] = 0;
                } else {
                    u8 b = col_xz[wy << 8];
                    if (!b) {
                        s_op[gy][gz][gx] = 0;
                        s_lg[gy][gz][gx] = (u8)sky;
                        if (sky > max_sky) max_sky = sky;
                        continue;
                    }
                    u8 op = blk_opaque(b) ? 1 : 0;
                    s_op[gy][gz][gx] = op;
                    if (op) sky = 0;
                    else if (b == B_WATER) sky = sky > 2 ? sky - 2 : 0;
                    else if (b == B_LEAVES) sky = sky > 1 ? sky - 1 : 0;
                    if (sky > max_sky) max_sky = sky;
                    int blk = 0;
                    if (BLK_IS_TORCH(b)) { blk = 14; has_blk = 1; }
                    else if (BLK_FURNACE_LIT(b)) { blk = 13; has_blk = 1; }
                    s_lg[gy][gz][gx] = (u8)((blk << 4) | sky);
                }
            }
        }
    }

    s_lg_has_light = (max_sky > 0 || has_blk);
    if (!s_lg_has_light) return;   // cueva completamente oscura sin antorchas: 0 barridos

    // Propagacion 3D Gauss-Seidel (2 barridos adelante + atras, saltando bloques opacos y cielo abierto)
    for (int pass = 0; pass < 2; pass++) {
        for (int gy = 1; gy < LG_S - 1; gy++)
            for (int gz = 1; gz < LG_S - 1; gz++)
                for (int gx = 1; gx < LG_S - 1; gx++) {
                    if (s_op[gy][gz][gx]) continue;
                    u8 cur = s_lg[gy][gz][gx];
                    if (!has_blk && cur >= 12) continue;
                    int sky = cur & 15, blk = cur >> 4;
                    u8 n0 = s_lg[gy - 1][gz][gx], n1 = s_lg[gy][gz - 1][gx], n2 = s_lg[gy][gz][gx - 1];
                    int s = n0 & 15; if ((n1 & 15) > s) s = n1 & 15; if ((n2 & 15) > s) s = n2 & 15;
                    if (s - 3 > sky) sky = s - 3;
                    if (has_blk) {
                        int b = n0 >> 4; if ((n1 >> 4) > b) b = n1 >> 4; if ((n2 >> 4) > b) b = n2 >> 4;
                        if (b - 2 > blk) blk = b - 2;
                    }
                    s_lg[gy][gz][gx] = (u8)((blk << 4) | sky);
                }
        for (int gy = LG_S - 2; gy >= 1; gy--)
            for (int gz = LG_S - 2; gz >= 1; gz--)
                for (int gx = LG_S - 2; gx >= 1; gx--) {
                    if (s_op[gy][gz][gx]) continue;
                    u8 cur = s_lg[gy][gz][gx];
                    if (!has_blk && cur >= 12) continue;
                    int sky = cur & 15, blk = cur >> 4;
                    u8 n0 = s_lg[gy + 1][gz][gx], n1 = s_lg[gy][gz + 1][gx], n2 = s_lg[gy][gz][gx + 1];
                    int s = n0 & 15; if ((n1 & 15) > s) s = n1 & 15; if ((n2 & 15) > s) s = n2 & 15;
                    if (s - 3 > sky) sky = s - 3;
                    if (has_blk) {
                        int b = n0 >> 4; if ((n1 >> 4) > b) b = n1 >> 4; if ((n2 >> 4) > b) b = n2 >> 4;
                        if (b - 2 > blk) blk = b - 2;
                    }
                    s_lg[gy][gz][gx] = (u8)((blk << 4) | sky);
                }
    }
}

static u32 compute_face_vc(int gx, int gy, int gz, int dir)
{
    if (!s_lg_has_light) return 0;
    static const u8 ao_mul[4] = {16, 13, 10, 8};
    const s8 *vt = face_vt[dir];
    int sh = shade_v[dir];
    u8 c0 = s_lg[gy][gz][gx];
    int s0 = c0 & 15, b0 = c0 >> 4;
    u32 vc4 = 0;
    for (int i = 0; i < 4; i++) {
        int vx = vt[i * 3] ? 1 : -1;
        int vy = vt[i * 3 + 1] ? 1 : -1;
        int vz = vt[i * 3 + 2] ? 1 : -1;
        int x1 = gx, y1 = gy, z1 = gz;
        int x2 = gx, y2 = gy, z2 = gz;
        int x3 = gx, y3 = gy, z3 = gz;
        if (dir < 2) {
            y1 += vy; z2 += vz; y3 += vy; z3 += vz;
        } else if (dir < 4) {
            x1 += vx; z2 += vz; x3 += vx; z3 += vz;
        } else {
            x1 += vx; y2 += vy; x3 += vx; y3 += vy;
        }
        int o1 = s_op[y1][z1][x1];
        int o2 = s_op[y2][z2][x2];
        int o3 = (o1 && o2) ? 1 : s_op[y3][z3][x3];
        int occ = o1 + o2 + o3;
        u8 c1 = o1 ? c0 : s_lg[y1][z1][x1];
        u8 c2 = o2 ? c0 : s_lg[y2][z2][x2];
        u8 c3 = (o1 && o2) ? c0 : (o3 ? (o1 ? c2 : (o2 ? c1 : c0)) : s_lg[y3][z3][x3]);
        int sum_sky = s0 + (c1 & 15) + (c2 & 15) + (c3 & 15);
        int sum_blk = b0 + (c1 >> 4) + (c2 >> 4) + (c3 >> 4);
        int fac = sh * ao_mul[occ];
        int v_sky = 0, v_blk = 0;
        if (sum_sky > 0) {
            v_sky = (sum_sky * fac + 992) / 1984;
            if (v_sky < 1) v_sky = 1;
            if (v_sky > 15) v_sky = 15;
        }
        if (sum_blk > 0) {
            v_blk = (sum_blk * fac + 992) / 1984;
            if (v_blk < 1) v_blk = 1;
            if (v_blk > 15) v_blk = 15;
        }
        vc4 |= (u32)((v_blk << 4) | v_sky) << (i * 8);
    }
    return vc4;
}

static int col_sky_up(int wx, int wy, int wz, int max_y)
{
    if ((unsigned)wx >= WX || (unsigned)wz >= WZ) return 0;
    const u8 *col = g_cols[(wz >> 4) * CXN + (wx >> 4)];
    if (!col) return wy > SEA_LEVEL ? 15 : 0;
    const u8 *col_xz = col + (((wz & 15) << 4) | (wx & 15));
    if (wy < 0) wy = 0;
    if (max_y >= WY) max_y = WY - 1;
    int sky = 15;
    for (int y = wy; y <= max_y; y++) {
        u8 b = col_xz[y << 8];
        if (!b) continue;
        if (blk_opaque(b)) return 0;
        if (b == B_WATER) { sky -= 2; if (sky <= 0) return 0; }
        else if (b == B_LEAVES) { sky -= 1; if (sky <= 0) return 0; }
    }
    return sky;
}

int render_sample_light(int wx, int wy, int wz)
{
    if (wy < 0) wy = 0;
    if (wy >= WY) return s_sky_light;
    int sky = col_sky_up(wx, wy, wz, 96);
    if (sky == 15 && s_sky_light >= 29) return s_sky_light;
    if (sky < 15) {
        static const s8 dir4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int d = 0; d < 4; d++) {
            int nx = wx, nz = wz;
            for (int step = 1; step <= 3; step++) {
                nx += dir4[d][0];
                nz += dir4[d][1];
                if (blk_opaque(world_get(nx, wy, nz))) break;
                int cs = col_sky_up(nx, wy, nz, 92);
                if (cs > 0) {
                    int s = cs - step * 4;
                    if (s > sky) sky = s;
                    break;
                }
            }
        }
    }
    int sl = sky > 0 ? (3 + (sky * (s_sky_light - 3) + 7) / 15) : 3;
    if (sl >= 29) return sl;
    int blk = 0;
    for (int dy = -2; dy <= 2; dy++) {
        int ny = wy + dy;
        if ((unsigned)ny >= WY) continue;
        for (int dz = -3; dz <= 3; dz++)
            for (int dx = -3; dx <= 3; dx++) {
                int dist = abs(dx) + abs(dy) + abs(dz);
                if (dist > 4) continue;
                u8 b = world_get(wx + dx, ny, wz + dz);
                if (BLK_IS_TORCH(b) || BLK_FURNACE_LIT(b)) {
                    int v = (BLK_IS_TORCH(b) ? 14 : 13) - dist * 2;
                    if (v > blk) blk = v;
                }
            }
    }
    int bl = blk > 0 ? (4 + (blk * 27 + 7) / 15) : 0;
    return sl > bl ? sl : bl;
}

static int sample_cam_sky(int wx, int wy, int wz)
{
    if (wy < 0) wy = 0;
    if (wy >= WY) return 15;
    int sky = col_sky_up(wx, wy, wz, 96);
    if (sky >= 12) return 15;
    static const s8 dir4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int d = 0; d < 4; d++) {
        int nx = wx, nz = wz;
        for (int step = 1; step <= 4; step++) {
            nx += dir4[d][0];
            nz += dir4[d][1];
            if (blk_opaque(world_get(nx, wy, nz))) break;
            int cs = col_sky_up(nx, wy, nz, 92);
            if (cs > 0) {
                int s = cs - step * 3;
                if (s > sky) sky = s;
                break;
            }
        }
    }
    return sky > 0 ? sky : 0;
}

static int s_build_heavy;   // 1 si la ultima build_section llego a calcular la luz (el trabajo caro)

static void build_section(int sx, int sy, int sz)
{
    s_build_heavy = 0;
    if (!scratch[0]) {
        for (int k = 0; k < 3; k++) scratch[k] = (u32 *)malloc(SCRATCH_CAP * 4);
        scratch_vc = (u32 *)malloc(SCRATCH_CAP * 4);
    }
    int cnt[3] = {0, 0, 0};
    Section *sec = &g_sec[SECIDX(sx, sy, sz)];
    sec_free(sec);
    const u8 *colp = g_cols[sz * CXN + sx];
    if (!colp) { sec->built = 0; g_sec_dirty[SECIDX(sx, sy, sz)] = 0; return; }

    int has_blocks = 0, has_non_opaque = 0;
    for (int ly = 0; ly < SEC; ly++) {
        int wy = sy * SEC + ly;
        for (int lz = 0; lz < SEC; lz++)
            for (int lx = 0; lx < SEC; lx++) {
                u8 b = colp[CIDX(lx, wy, lz)];
                if (b != B_AIR) has_blocks = 1;
                if (!blk_opaque(b)) has_non_opaque = 1;
            }
    }
    if (!has_blocks) {
        sec->built = 1;
        g_sec_dirty[SECIDX(sx, sy, sz)] = 0;
        return;
    }
    if (!has_non_opaque) {
        int border_open = 0;
        int x0 = sx * SEC, y0 = sy * SEC, z0 = sz * SEC;
        for (int a = 0; a < SEC && !border_open; a++)
            for (int b = 0; b < SEC; b++) {
                if (!blk_opaque(world_get(x0 - 1, y0 + a, z0 + b)) ||
                    !blk_opaque(world_get(x0 + SEC, y0 + a, z0 + b)) ||
                    !blk_opaque(world_get(x0 + b, y0 - 1, z0 + a)) ||
                    !blk_opaque(world_get(x0 + b, y0 + SEC, z0 + a)) ||
                    !blk_opaque(world_get(x0 + b, y0 + a, z0 - 1)) ||
                    !blk_opaque(world_get(x0 + b, y0 + a, z0 + SEC))) {
                    border_open = 1;
                    break;
                }
            }
        if (!border_open) {
            sec->built = 1;
            g_sec_dirty[SECIDX(sx, sy, sz)] = 0;
            return;
        }
    }

    s_build_heavy = 1;
    build_light_grid(sx, sy, sz);

    int unl_nx = (sx > 0 && !g_cols[sz * CXN + sx - 1]);
    int unl_px = (sx + 1 < SXN && !g_cols[sz * CXN + sx + 1]);
    int unl_nz = (sz > 0 && !g_cols[(sz - 1) * CXN + sx]);
    int unl_pz = (sz + 1 < SZN && !g_cols[(sz + 1) * CXN + sx]);

    for (int ly = 0; ly < SEC; ly++) {
        int wy = sy * SEC + ly;
        int gy = ly + LG_PAD;
        for (int lz = 0; lz < SEC; lz++) {
            int wz = sz * SEC + lz;
            int gz = lz + LG_PAD;
            for (int lx = 0; lx < SEC; lx++) {
                int wx = sx * SEC + lx;
                int gx = lx + LG_PAD;
                u8 id = colp[CIDX(lx, wy, lz)];
                if (id == B_AIR) {
                    if (wy <= SEA_LEVEL) {
                        if (lx == 0 && unl_nx && cnt[0] < SCRATCH_CAP) {
                            int idx = cnt[0]++;
                            scratch[0][idx] = PACK(0, ly, lz, D_PX, T_STONE, 0);
                            scratch_vc[idx] = 0;
                        }
                        if (lx == SEC - 1 && unl_px && cnt[0] < SCRATCH_CAP) {
                            int idx = cnt[0]++;
                            scratch[0][idx] = PACK(SEC - 1, ly, lz, D_NX, T_STONE, 0);
                            scratch_vc[idx] = 0;
                        }
                        if (lz == 0 && unl_nz && cnt[0] < SCRATCH_CAP) {
                            int idx = cnt[0]++;
                            scratch[0][idx] = PACK(lx, ly, 0, D_PZ, T_STONE, 0);
                            scratch_vc[idx] = 0;
                        }
                        if (lz == SEC - 1 && unl_pz && cnt[0] < SCRATCH_CAP) {
                            int idx = cnt[0]++;
                            scratch[0][idx] = PACK(lx, ly, SEC - 1, D_NZ, T_STONE, 0);
                            scratch_vc[idx] = 0;
                        }
                    }
                    continue;
                }
                if (BLK_IS_CHEST(id)) { chest_get(wx, wy, wz); continue; }
                if (BLK_IS_TORCH(id)) {
                    if (cnt[1] < SCRATCH_CAP) scratch[1][cnt[1]++] = PACK(lx, ly, lz, BLK_TORCH_DIR(id), T_TORCH, 1);
                    continue;
                }
                const BlockInfo *bi = &g_blocks[id];
                if (bi->flags & F_PLANT) {
                    u8 lg = s_lg[gy][gz][gx];
                    if (cnt[1] < SCRATCH_CAP) scratch[1][cnt[1]++] = PACKL(lx, ly, lz, 0, bi->tex[0], 0, lg);
                } else if (id == B_WATER) {
                    u8 above = world_get(wx, wy + 1, wz);
                    int low = (above != B_WATER);
                    if (above != B_WATER && !blk_opaque(above) && cnt[2] < SCRATCH_CAP) {
                        u8 lg = s_lg[gy + 1][gz][gx];
                        scratch[2][cnt[2]++] = PACKL(lx, ly, lz, D_PY, T_WATER, 1, lg);
                    }
                    for (int d = 0; d < 6; d++) {
                        if (d == D_PY || d == D_NY) continue;
                        u8 n = world_get(wx + ndx[d], wy + ndy[d], wz + ndz[d]);
                        if (n != B_WATER && !blk_opaque(n) && cnt[2] < SCRATCH_CAP) {
                            u8 lg = s_lg[gy + ndy[d]][gz + ndz[d]][gx + ndx[d]];
                            scratch[2][cnt[2]++] = PACKL(lx, ly, lz, d, T_WATER, low, lg);
                        }
                    }
                } else if (BLK_IS_BED(id)) {
                    // cama: pie / cabeza, caras con la textura que toca y giro de la superior segun la orientacion
                    static const u8 bed_d[4] = {D_NZ, D_PX, D_PZ, D_NX};      // direccion de cama -> cara
                    static const u8 top_rot[4] = {1, 0, 3, 2};
                    static const s8 udx[6] = {0, 0, 0, 0, 1, -1}, udz[6] = {-1, 1, 0, 0, 0, 0};   // hacia donde crece u en cada cara lateral
                    int is_head = (id == B_BED_HEAD);
                    int fd = bed_facing(wx, wy, wz);
                    int toward_head = bed_d[fd], toward_foot = bed_d[(fd + 2) & 3];
                    for (int d = 0; d < 6; d++) {
                        int nx = wx + ndx[d], ny = wy + ndy[d], nz = wz + ndz[d];
                        u8 n = world_get(nx, ny, nz);
                        if (blk_opaque(n)) continue;
                        if (d == (is_head ? toward_foot : toward_head)) continue;      // cara interior entre las dos mitades
                        if (cnt[0] >= SCRATCH_CAP) continue;
                        int tile, l = 1, code = 0;
                        if (d == D_NY) { tile = T_PLANKS; l = 0; }
                        else if (d == D_PY) { tile = is_head ? T_BED_HEAD_TOP : T_BED_FEET_TOP; code = top_rot[fd]; }
                        else if (d == toward_head) tile = T_BED_HEAD_END;
                        else if (d == toward_foot) tile = T_BED_FEET_END;
                        else {
                            tile = is_head ? T_BED_HEAD_SIDE : T_BED_FEET_SIDE;
                            code = (bed_dx(fd) * udx[d] + bed_dz(fd) * udz[d]) < 0;
                        }
                        int idx = cnt[0]++;
                        scratch[0][idx] = PACK(lx, ly, lz, d, tile, l ? (1 | (code << 2)) : 0);
                        scratch_vc[idx] = compute_face_vc(gx + ndx[d], gy + ndy[d], gz + ndz[d], d);
                    }
                } else {
                    for (int d = 0; d < 6; d++) {
                        int nx = wx + ndx[d], ny = wy + ndy[d], nz = wz + ndz[d];
                        u8 n = world_get(nx, ny, nz);
                        if (blk_opaque(n)) continue;
                        if (n == id && (bi->flags & F_CUTOUT)) continue;
                        if (cnt[0] < SCRATCH_CAP) {
                            int idx = cnt[0]++;
                            scratch[0][idx] = PACK(lx, ly, lz, d, bi->tex[d], 0);
                            scratch_vc[idx] = compute_face_vc(gx + ndx[d], gy + ndy[d], gz + ndz[d], d);
                        }
                    }
                }
            }
        }
    }
    for (int k = 0; k < 3; k++) {
        sec->n[k] = (u16)cnt[k];
        if (cnt[k]) {
            sec->f[k] = (u32 *)malloc(cnt[k] * 4);
            if (k == 0) {
                sec->vc = (u32 *)malloc(cnt[0] * 4);
                u16 cn[7] = {0, 0, 0, 0, 0, 0, 0};
                for (int i = 0; i < cnt[0]; i++) cn[((scratch[0][i] >> 12) & 7) + 1]++;
                for (int d = 0; d < 6; d++) cn[d + 1] += cn[d];
                int pos[6];
                for (int d = 0; d < 6; d++) pos[d] = cn[d];
                for (int d = 0; d < 7; d++) sec->ds[d] = cn[d];
                for (int i = 0; i < cnt[0]; i++) {
                    u32 q = scratch[0][i];
                    int p = pos[(q >> 12) & 7]++;
                    sec->f[0][p] = q;
                    sec->vc[p] = scratch_vc[i];
                }
            } else {
                memcpy(sec->f[k], scratch[k], cnt[k] * 4);
            }
        }
    }
    sec->built = 1;
    g_sec_dirty[SECIDX(sx, sy, sz)] = 0;
}

// ---------------------------------------------------------------------------
// Camara y vista
// ---------------------------------------------------------------------------
typedef struct {
    int cx, cy, cz;      // camara en 1/16 de bloque
    int fx, fy, fz;      // direccion 4.12
    int maxd2;           // distancia maxima al cuadrado (1/16 de bloque)
    int cone;            // cono segun FOV
} View;

static int g_cam32[3];

static void camera_pose(const Player *p, int *ex, int *ey, int *ez, int *yaw, int *pitch)
{
    player_eye(p, ex, ey, ez);
    *yaw = p->yaw;
    *pitch = p->pitch;
    if (p->cam_mode == 0) return;
    int fx, fy, fz;
    player_forward(p, &fx, &fy, &fz);
    int sgn = (p->cam_mode == 1) ? -1 : 1;
    int maxd = 4 * FP;
    int t = 0;
    while (t < maxd) {
        int nt = t + 512;
        int x = *ex + sgn * ((fx * nt) >> 12);
        int y = *ey + sgn * ((fy * nt) >> 12);
        int z = *ez + sgn * ((fz * nt) >> 12);
        if (blk_solid(world_get(x >> 12, y >> 12, z >> 12))) break;
        t = nt;
    }
    t -= 650;
    if (t < 0) t = 0;
    *ex += sgn * ((fx * t) >> 12);
    *ey += sgn * ((fy * t) >> 12);
    *ez += sgn * ((fz * t) >> 12);
    if (p->cam_mode == 2) {
        *yaw = (p->yaw + 16384) & 0x7FFF;
        *pitch = -p->pitch;
    }
}

static inline int face_ok(const View *v, int wx, int wy, int wz, int dir, int is_water)
{
    switch (dir) {
        case D_PX: if (!is_water && v->cx <= (wx + 1) * 16) return 0; break;
        case D_NX: if (!is_water && v->cx >= wx * 16) return 0; break;
        case D_PY: if (!is_water && v->cy <= (wy + 1) * 16) return 0; break;
        case D_NY: if (!is_water && v->cy >= wy * 16) return 0; break;
        case D_PZ: if (!is_water && v->cz <= (wz + 1) * 16) return 0; break;
        default:   if (!is_water && v->cz >= wz * 16) return 0; break;
    }
    int dx = wx * 16 + 8 - v->cx, dy = wy * 16 + 8 - v->cy, dz = wz * 16 + 8 - v->cz;
    int d2 = dx * dx + dy * dy + dz * dz;
    if (d2 <= 40 * 40) return 1;    // bloques pegados al jugador: nunca descartar caras por cono
    if (d2 > v->maxd2) return 0;
    int dot = ((dx * v->fx + dy * v->fy + dz * v->fz) >> 12) + 32;
    if (dot <= 0) return 0;
    if ((v->cone + 1) * dot * dot < d2) return 0;
    return 1;
}

typedef struct { int idx, d2; } Vis;

static inline int dir_visible(const View *v, int sx, int sy, int sz, int dir)
{
    int x0 = sx * SEC, y0 = sy * SEC, z0 = sz * SEC;
    switch (dir) {
        case D_PX: return v->cx > x0 * 16;
        case D_NX: return v->cx < (x0 + SEC) * 16;
        case D_PY: return v->cy > y0 * 16;
        case D_NY: return v->cy < (y0 + SEC) * 16;
        case D_PZ: return v->cz > z0 * 16;
        default:   return v->cz < (z0 + SEC) * 16;
    }
}

static void draw_section_list(const View *v, const Vis *vis, int nvis, int kind, int *quads_left)
{
    for (int i = 0; i < nvis && *quads_left > 0; i++) {
        int si = vis[i].idx;
        Section *s = &g_sec[si];
        if (!s->built || s->n[kind] == 0) continue;
        int sx = si % SXN, sz = (si / SXN) % SZN, sy = si / (SXN * SZN);
        int far_sec = (vis[i].d2 > (12 * 16) * (12 * 16));
        glPushMatrix();
        glTranslatef32(sx * SEC * BS - g_cam32[0], sy * SEC * BS - g_cam32[1], sz * SEC * BS - g_cam32[2]);
        glBegin(GL_QUADS);
        const u32 *f = s->f[kind];
        if (kind == 0) {
            const u32 *vc = s->vc;
            for (int d = 0; d < 6 && *quads_left > -48; d++) {
                if (s->ds[d] == s->ds[d + 1]) continue;
                if (!dir_visible(v, sx, sy, sz, d)) continue;
                for (int k = s->ds[d]; k < s->ds[d + 1] && *quads_left > -48; k++) {
                    u32 q = f[k];
                    int lx = q & 15, ly = (q >> 4) & 15, lz = (q >> 8) & 15;
                    int tile = (q >> 15) & 127;
                    if (q >> 22) {      // cama: malla propia; sin descarte por plano (su cara superior queda por debajo de 1 bloque)
                        if (!face_ok(v, sx * SEC + lx, sy * SEC + ly, sz * SEC + lz, d, 1)) continue;
                        emit_bed_face(lx, ly, lz, d, tile, (q >> 24) & 3, vc[k]);
                        (*quads_left)--;
                        continue;
                    }
                    if (!face_ok(v, sx * SEC + lx, sy * SEC + ly, sz * SEC + lz, d, 0)) continue;
                    if (far_sec) {
                        glColor(s_vcol_lut[vc[k] & 255]);
                        emit_face_raw(lx, ly, lz, d, tile, 0);
                    } else {
                        emit_face_vc(lx, ly, lz, d, tile, 0, vc[k]);
                    }
                    (*quads_left)--;
                }
            }
        } else {
            for (int k = 0; k < s->n[kind] && *quads_left > 0; k++) {
                u32 q = f[k];
                int lx = q & 15, ly = (q >> 4) & 15, lz = (q >> 8) & 15;
                int dir = (q >> 12) & 7, tile = (q >> 15) & 127, low = (q >> 22) & 1;
                u8 lg = (u8)(q >> 24);
                int wx = sx * SEC + lx, wy = sy * SEC + ly, wz = sz * SEC + lz;
                if (kind == 1) {
                    int dx = wx * 16 + 8 - v->cx, dy = wy * 16 + 8 - v->cy, dz = wz * 16 + 8 - v->cz;
                    int d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 > v->maxd2) continue;
                    if (d2 > 40 * 40) {
                        int dot = ((dx * v->fx + dy * v->fy + dz * v->fz) >> 12) + 32;
                        if (dot <= 0 || (v->cone + 1) * dot * dot < d2) continue;
                    }
                    if (low) {
                        emit_torch(lx, ly, lz, dir, tile);
                    } else {
                        u16 ct = s_vcol_lut[lg];
                        int sky_b = (lg & 15) * 3 / 4, blk_b = (lg >> 4) * 3 / 4;
                        if ((lg & 15) && sky_b < 1) sky_b = 1;
                        if ((lg >> 4) && blk_b < 1) blk_b = 1;
                        u16 cb = s_vcol_lut[(blk_b << 4) | sky_b];
                        emit_cross_col(lx, ly, lz, tile, ct, cb);
                    }
                } else {
                    if (!face_ok(v, wx, wy, wz, dir, 1)) continue;
                    int sh = shade_v[dir];
                    int sky_s = ((lg & 15) * sh + 15) / 31;
                    int blk_s = ((lg >> 4) * sh + 15) / 31;
                    if ((lg & 15) && sky_s < 1) sky_s = 1;
                    if ((lg >> 4) && blk_s < 1) blk_s = 1;
                    u32 vc1 = (u32)((blk_s << 4) | sky_s);
                    glColor(s_vcol_lut[vc1]);
                    emit_face_raw(lx, ly, lz, dir, tile, low);
                }
                (*quads_left)--;
            }
        }
        glEnd();
        glPopMatrix(1);
    }
}

// ---------------------------------------------------------------------------
// Steve
// ---------------------------------------------------------------------------
#define PXU 60
#define DEG(a) ((int)((a) * 32768.0f / 360.0f))

static const s8 box_top[12] = {1,1,1, 0,1,1, 0,1,0, 1,1,0};
static const s8 box_bot[12] = {1,0,0, 0,0,0, 0,0,1, 1,0,1};

static void box(int x0, int y0, int z0, int w, int h, int d, int u, int v)
{
    int ru[6], rv[6], rw[6], rh[6];
    ru[0] = u;             rv[0] = v + d; rw[0] = d; rh[0] = h;
    ru[1] = u + d + w;     rv[1] = v + d; rw[1] = d; rh[1] = h;
    ru[2] = u + d;         rv[2] = v;     rw[2] = w; rh[2] = d;
    ru[3] = u + d + w;     rv[3] = v;     rw[3] = w; rh[3] = d;
    ru[4] = u + 2 * d + w; rv[4] = v + d; rw[4] = w; rh[4] = h;
    ru[5] = u + d;         rv[5] = v + d; rw[5] = w; rh[5] = h;
    glBegin(GL_QUADS);
    for (int dir = 0; dir < 6; dir++) {
        const s8 *vt = dir == 2 ? box_top : (dir == 3 ? box_bot : face_vt[dir]);
        int s = LIT(shade_v[dir]);
        if (s_steve_hurt) glColor(RGB15(s, s * 10 / 31, s * 10 / 31));
        else glColor(RGB15(s, s, s));
        for (int i = 0; i < 4; i++) {
            int tu = (ru[dir] << 4) + (uv_c[i][0] ? (rw[dir] << 4) - 2 : 2);
            int tv = (rv[dir] << 4) + (uv_c[i][1] ? (rh[dir] << 4) - 2 : 2);
            glTexCoord2t16(tu, tv);
            glVertex3v16((x0 + vt[i * 3] * w) * PXU, (y0 + vt[i * 3 + 1] * h) * PXU, (z0 + vt[i * 3 + 2] * d) * PXU);
        }
    }
    glEnd();
}

static inline int swing_angle(const Player *p)
{
    return (SWING_LEN - p->swing) * 16384 / SWING_LEN;
}

static void draw_held_third(const Player *p, u8 id);

#define STEVE_ARMOR(part) do { mobgfx_armor_part(p, part, s_light); glBindTexture(0, tex_steve); \
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(6)); } while (0)

#define RAD(x) ((int)((x) * 5215.19f))     // radianes -> unidades de angulo (32768 = 360 grados)

// Steve en 3a persona. Reproduce ModelBiped.setRotationAngles de 1.7.10:
//  - brazos cos(fase)*amp, piernas cos(fase)*1.4*amp (fase = limbSwing*0.6662), con amp suavizado como limbSwingAmount
//  - objeto en la mano derecha: brazo*0.5 - PI/10
//  - golpe: giro del torso, brazo derecho adelante (f7*1.2 + f8) y hacia dentro (-0.4 sin)
//  - balanceo en reposo: Z cos(edad*0.09)*0.05+0.05, X sin(edad*0.067)*0.05
//  - agachado: cuerpo inclinado 0.5 rad, brazos +0.4, piernas atras/arriba, cabeza 1 px mas baja, modelo 2 px mas bajo
//  - cuerpo y cabeza independientes (renderYawOffset), arco tensado y pose de comer
static void draw_steve(const Player *p)
{
    int prev_light = s_light;
    int pl = render_sample_light(p->x >> 12, (p->y >> 12) + 1, p->z >> 12);
    if (BLK_IS_TORCH(player_held(p)) && pl < 29) pl = 29;
    s_light = pl;
    s_steve_hurt = (p->hurt_t > 0 || p->health <= 0);

    const u8 held_id = player_held(p);
    const int sneak = p->sneaking;
    const int bow_aim = (held_id == I_BOW && p->bow_t > 0);

    // ---- angulos (unidades de 32768 por vuelta; positivo = hacia delante, como en GL) ----
    const int ca = (cosLerp((s16)p->limb_ph) * p->limb_amt) >> 12;      // cos(fase) * amplitud, 4.12
    int arm_r = (ca * RAD(1.0f)) >> 12, arm_l = -arm_r;
    const int leg_r = -((ca * RAD(1.4f)) >> 12), leg_l = -leg_r;
    int arm_rz = 0, arm_lz = 0, arm_ry = 0, arm_ly = 0, torso_y = 0;

    if (held_id) arm_r = arm_r / 2 + RAD(0.314159f);                    // heldItemRight = 1
    if (sneak) { arm_r -= RAD(0.4f); arm_l -= RAD(0.4f); }

    if (p->swing > 0) {
        int g = (SWING_LEN - p->swing) * 4096 / SWING_LEN;               // onGround (0..1) en 4.12
        int sg = sinLerp((s16)(g * 4));                                  // sin(g * PI)
        int sq = (int)sqrt32(g << 12);                                   // sqrt(g) en 4.12
        int body_y = (sinLerp((s16)(sq * 8)) * RAD(0.2f)) >> 12;         // sin(sqrt(g) * 2PI) * 0.2
        int q = 4096 - g, q2 = (q * q) >> 12, q4 = (q2 * q2) >> 12;
        int f7 = sinLerp((s16)((4096 - q4) * 4));                        // sin((1 - (1-g)^4) * PI)
        int f8 = (sg * (((2867 + ((p->pitch * 3217) >> 12)) * 3) >> 2)) >> 12;   // sin(g PI) * (0.7 + pitch) * 0.75
        arm_r += ((f7 * RAD(1.2f)) >> 12) + ((f8 * 5215) >> 12);
        arm_rz -= (sg * RAD(0.4f)) >> 12;
        arm_l -= body_y;
        torso_y = -body_y;
        arm_ry = -2 * body_y;
    }

    // balanceo en reposo
    {
        int zs = ((cosLerp((s16)((p->age * 157) & 0x7FFF)) * RAD(0.05f)) >> 12) + RAD(0.05f);
        int xs = (sinLerp((s16)((p->age * 116) & 0x7FFF)) * RAD(0.05f)) >> 12;
        arm_rz += zs; arm_lz -= zs;
        arm_r -= xs;  arm_l += xs;
    }

    const int head_y = -(((p->yaw - p->body_yaw) & 0x7FFF) > 0x4000 ? ((p->yaw - p->body_yaw) & 0x7FFF) - 0x8000 : ((p->yaw - p->body_yaw) & 0x7FFF));

    if (bow_aim) {
        arm_r = DEG(90) + p->pitch;  arm_l = DEG(90) + p->pitch;
        arm_ry = DEG(6) + head_y;    arm_ly = -DEG(28) + head_y;
    }
    // Comer en 3a persona (1.7.10): ModelBiped no tiene pose de comer; el brazo sigue en la pose normal de sostener
    // el objeto (sonido de masticar + migas; no hay migas aqui). Antes el brazo subia hasta la boca.

    // ---- dibujado ----
    glBindTexture(0, tex_steve);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(6));
    glPushMatrix();
    glTranslatef32(p->x >> 2, p->y >> 2, p->z >> 2);
    glRotateYi((-p->body_yaw) & 0x7FFF);
    if (p->health <= 0) {
        int dt = p->death_t > 20 ? 20 : p->death_t;
        glRotateZi((dt * DEG(90) / 20) & 0x7FFF);
    }
    if (sneak) glTranslatef32(0, -2 * PXU, 0);

    // cabeza
    glPushMatrix();
    glTranslatef32(0, (sneak ? 23 : 24) * PXU, 0);
    glRotateYi(head_y & 0x7FFF);
    glRotateXi(p->pitch);
    box(-4, 0, -4, 8, 8, 8, 0, 0);
    STEVE_ARMOR(PART_HEAD);
    glPopMatrix(1);

    // torso + brazos (giran juntos al golpear)
    glPushMatrix();
    glRotateYi(torso_y & 0x7FFF);

    glPushMatrix();
    if (sneak) {
        glTranslatef32(0, 24 * PXU, 0);
        glRotateXi(-RAD(0.5f));
        glTranslatef32(0, -24 * PXU, 0);
    }
    box(-4, 12, -2, 8, 12, 4, 16, 16);
    STEVE_ARMOR(PART_BODY);
    glPopMatrix(1);

    glPushMatrix();                                   // brazo derecho (el que sostiene el objeto)
    glTranslatef32(6 * PXU, 22 * PXU, 0);
    glRotateZi(arm_rz & 0x7FFF);
    glRotateYi(arm_ry & 0x7FFF);
    glRotateXi(arm_r);
    box(-2, -10, -2, 4, 12, 4, 40, 16);
    STEVE_ARMOR(PART_ARM);
    if (held_id) {
        draw_held_third(p, held_id);
        glBindTexture(0, tex_steve);
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(6));
    }
    glPopMatrix(1);

    glPushMatrix();                                   // brazo izquierdo
    glTranslatef32(-6 * PXU, 22 * PXU, 0);
    glRotateZi(arm_lz & 0x7FFF);
    glRotateYi(arm_ly & 0x7FFF);
    glRotateXi(arm_l);
    box(-2, -10, -2, 4, 12, 4, 40, 16);
    STEVE_ARMOR(PART_ARM);
    glPopMatrix(1);
    glPopMatrix(1);

    // piernas
    glPushMatrix();
    glTranslatef32(2 * PXU, (sneak ? 15 : 12) * PXU, sneak ? 4 * PXU : 0);
    glRotateXi(leg_r);
    box(-2, -12, -2, 4, 12, 4, 0, 16);
    STEVE_ARMOR(PART_LEG);
    glPopMatrix(1);
    glPushMatrix();
    glTranslatef32(-2 * PXU, (sneak ? 15 : 12) * PXU, sneak ? 4 * PXU : 0);
    glRotateXi(leg_l);
    box(-2, -12, -2, 4, 12, 4, 0, 16);
    STEVE_ARMOR(PART_LEG);
    glPopMatrix(1);

    glPopMatrix(1);
    s_steve_hurt = 0;
    s_light = prev_light;
}

// ---------------------------------------------------------------------------
// Cofres 3D (ModelChest / ModelLargeChest de Minecraft 1.7.10)
// ---------------------------------------------------------------------------
#define CHPX 64

static void chest_box(int x0, int y0, int z0, int w, int h, int d, int u, int v, int e, int skip_mask)
{
    int ru[6], rv[6], rw[6], rh[6];
    ru[0] = u;             rv[0] = v + d; rw[0] = d; rh[0] = h;
    ru[1] = u + d + w;     rv[1] = v + d; rw[1] = d; rh[1] = h;
    ru[2] = u + d;         rv[2] = v;     rw[2] = w; rh[2] = d;
    ru[3] = u + d + w;     rv[3] = v;     rw[3] = w; rh[3] = d;
    ru[4] = u + 2 * d + w; rv[4] = v + d; rw[4] = w; rh[4] = h;
    ru[5] = u + d;         rv[5] = v + d; rw[5] = w; rh[5] = h;
    int bx = x0 * CHPX - e, by = y0 * CHPX - e, bz = z0 * CHPX - e;
    int sx = w * CHPX + 2 * e, sy = h * CHPX + 2 * e, sz = d * CHPX + 2 * e;
    glBegin(GL_QUADS);
    for (int dir = 0; dir < 6; dir++) {
        if ((skip_mask >> dir) & 1) continue;
        const s8 *vt = dir == 2 ? box_top : (dir == 3 ? box_bot : face_vt[dir]);
        int s = shade_v[dir];
        glColor(RGB15(LIT(s), LIT(s), LIT(s)));
        for (int i = 0; i < 4; i++) {
            int tu = (ru[dir] << 4) + (uv_c[i][0] ? (rw[dir] << 4) - 2 : 2);
            int tv = (rv[dir] << 4) + (uv_c[i][1] ? (rh[dir] << 4) - 2 : 2);
            glTexCoord2t16(tu, tv);
            glVertex3v16(bx + vt[i * 3] * sx, by + vt[i * 3 + 1] * sy, bz + vt[i * 3 + 2] * sz);
        }
    }
    glEnd();
}

static void draw_chest_model(int is_double, int open_t)
{
    int W = is_double ? 30 : 14;
    glBindTexture(0, is_double ? tex_chest_dbl : tex_chest);
    chest_box(-W / 2, 0, -7, W, 10, 14, 0, 19, 0, open_t == 0 ? (1 << 2) : 0);
    int lid_rx = 0;
    if (open_t > 0) {
        int u = ((CHEST_ANIM_TICKS - open_t) << 12) / CHEST_ANIM_TICKS;
        int u2 = (u * u) >> 12;
        int u3 = (u2 * u) >> 12;
        int ease = 4096 - u3;
        lid_rx = (ease * 8192) >> 12;
    }
    glPushMatrix();
    glTranslatef32(0, 9 * CHPX, 7 * CHPX);
    if (lid_rx) glRotateXi(lid_rx & 0x7FFF);
    chest_box(-W / 2, 0, -14, W, 5, 14, 0, 0, 2, open_t == 0 ? (1 << 3) : 0);
    chest_box(-1, -2, -15, 2, 4, 1, 0, 0, 0, 0);
    glPopMatrix(1);
}

static void draw_chests(const Player *p, const View *v)
{
    (void)p;
    static const s16 chest_rot_y[4] = {0, -8192, 16384, 8192};
    int prev_light = s_light;
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(1) | g_polyfog);
    for (int i = 0; i < MAX_CHEST; i++) {
        const Chest *c = &g_chest[i];
        if (!c->used || !world_col_loaded(c->x, c->z)) continue;
        u8 b = world_get(c->x, c->y, c->z);
        if (!BLK_IS_CHEST(b)) continue;
        int px = c->x, pz = c->z;
        int is_dbl = chest_partner(c->x, c->y, c->z, &px, &pz);
        if (is_dbl && (px < c->x || pz < c->z)) continue;

        int dx = c->x * 16 + 8 - v->cx, dy = c->y * 16 + 8 - v->cy, dz = c->z * 16 + 8 - v->cz;
        if (dx * dx + dy * dy + dz * dz > v->maxd2 + 64 * 16) continue;
        if (((dx * v->fx + dy * v->fy + dz * v->fz) >> 12) < -32) continue;

        int open_t = c->open_t;
        if (is_dbl) {
            int pi = chest_find(px, c->y, pz);
            if (pi >= 0 && g_chest[pi].open_t > open_t) open_t = g_chest[pi].open_t;
        }
        s_light = render_sample_light(c->x, c->y + 1, c->z);
        int f = BLK_CHEST_FACING(b);
        glPushMatrix();
        glTranslatef32((c->x + px) * (BS / 2) + BS / 2, c->y * BS, (c->z + pz) * (BS / 2) + BS / 2);
        glRotateYi(chest_rot_y[f] & 0x7FFF);
        draw_chest_model(is_dbl, open_t);
        glPopMatrix(1);
    }
    s_light = prev_light;
}

static void draw_target(const Player *p)
{
    if (!p->has_target || p->health <= 0) return;
    int x = p->tx, y = p->ty, z = p->tz;
    u8 tb = world_get(x, y, z);
    int x0 = -6, x1 = BS + 6, y0 = -6, y1 = BS + 6, z0 = -6, z1 = BS + 6;
    if (BLK_IS_CHEST(tb)) {
        x0 = CHPX - 6; x1 = BS - CHPX + 6;
        y0 = -6;       y1 = 14 * CHPX + 6;
        z0 = CHPX - 6; z1 = BS - CHPX + 6;
        int px, pz;
        if (chest_partner(x, y, z, &px, &pz)) {
            if (px < x) x0 -= BS; else if (px > x) x1 += BS;
            if (pz < z) z0 -= BS; else if (pz > z) z1 += BS;
        }
    } else if (BLK_IS_BED(tb)) {
        y1 = BED_H + 6;
    } else if (BLK_IS_TORCH(tb)) {
        int dir = BLK_TORCH_DIR(tb);
        x0 = 6 * CHPX; x1 = 10 * CHPX;
        y0 = (dir ? 3 : 0) * CHPX; y1 = (dir ? 13 : 10) * CHPX;
        z0 = 6 * CHPX; z1 = 10 * CHPX;
        if (dir == 1)      { x0 = 0;         x1 = 6 * CHPX; }
        else if (dir == 2) { z0 = 0;         z1 = 6 * CHPX; }
        else if (dir == 3) { x0 = 10 * CHPX; x1 = BS; }
        else if (dir == 4) { z0 = 10 * CHPX; z1 = BS; }
    }
    glPushMatrix();
    glTranslatef32(x * BS, y * BS, z * BS);

    glBindTexture(0, 0);
    glPolyFmt(POLY_ALPHA(0) | POLY_CULL_NONE | POLY_ID(4));
    glColor(RGB15(0, 0, 0));
    glBegin(GL_QUADS);
    for (int d = 0; d < 6; d++) {
        const s8 *vt = face_vt[d];
        for (int i = 0; i < 4; i++) {
            int vx = vt[i * 3] ? x1 : x0, vy = vt[i * 3 + 1] ? y1 : y0, vz = vt[i * 3 + 2] ? z1 : z0;
            glVertex3v16(vx, vy, vz);
        }
    }
    glEnd();

    if (p->gamemode == 0 && p->brk_total > 0 && p->brk_x == x && p->brk_y == y && p->brk_z == z && p->brk_prog > 0) {
        int stage = p->brk_prog * 10 / p->brk_total;
        if (stage > 9) stage = 9;
        glBindTexture(0, tex_terrain);
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(5));
        glBegin(GL_QUADS);
        int tile = T_DESTROY_0 + stage;
        int u0 = (tile & 7) * 16, v0 = (tile >> 3) * 16;
        glColor(RGB15(31, 31, 31));
        for (int d = 0; d < 6; d++) {
            const s8 *vt = face_vt[d];
            for (int i = 0; i < 4; i++) {
                int vx = vt[i * 3] ? BS + 24 : -24, vy = vt[i * 3 + 1] ? BS + 24 : -24, vz = vt[i * 3 + 2] ? BS + 24 : -24;
                glTexCoord2t16((u0 << 4) + (uv_c[i][0] ? TEX_HI : TEX_LO), (v0 << 4) + (uv_c[i][1] ? TEX_HI : TEX_LO));
                glVertex3v16(vx, vy, vz);
            }
        }
        glEnd();
    }
    glPopMatrix(1);
}

// ---------------------------------------------------------------------------
// Mano en primera persona: brazo de Steve (mano vacia) o bloque (con item)
// ---------------------------------------------------------------------------
#define HAND_DEPTH ZB_HAND_SPAN
#define MCB(v) ((int)((v) * 1024))

#define HELD_X 0.36f
#define HELD_MIRROR 1

static void hand_begin(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glTranslatef32(0, 0, 2 * ZB_HAND_LO + HAND_DEPTH - 4096);
    glScalef32(4096, 4096, HAND_DEPTH);
    gluPerspective(70.0f, 256.0f / 192.0f, 0.05f, 2.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static const s16 swing_f6[9] = {0, 2751, 4017, 3857, 2896, 1751, 799, 201, 0};
static const s16 swing_f7[9] = {0,  825, 1674, 2505, 3259, 3843, 4096, 3670, 0};
static const s16 swing_f8[9] = {0,-1617,-3055,-3964,-3948,-2658,    0, 3259, 0};
static const s16 swing_f9[9] = {0, 1567, 2896, 3784, 4096, 3784, 2896, 1567, 0};

static int swing_lookup(const s16 *t, int swing)
{
    if (swing <= 0 || swing >= SWING_LEN) return 0;
    int pos = swing * 8 * 256 / SWING_LEN;
    int i = pos >> 8, fr = pos & 255;
    return t[i] + (((t[i + 1] - t[i]) * fr) >> 8);
}

static void arm_box(void)
{
    enum { X0 = -3, X1 = 1, Y0 = -2, Y1 = 10, Z0 = -2, Z1 = 2, W = 4, H = 12, D = 4, U = 40, V = 16 };
    static const s8 c[6][12] = {
        {X1,Y0,Z1, X1,Y0,Z0, X1,Y1,Z0, X1,Y1,Z1},
        {X0,Y0,Z0, X0,Y0,Z1, X0,Y1,Z1, X0,Y1,Z0},
        {X0,Y0,Z0, X1,Y0,Z0, X1,Y0,Z1, X0,Y0,Z1},
        {X0,Y1,Z1, X1,Y1,Z1, X1,Y1,Z0, X0,Y1,Z0},
        {X0,Y0,Z1, X1,Y0,Z1, X1,Y1,Z1, X0,Y1,Z1},
        {X1,Y0,Z0, X0,Y0,Z0, X0,Y1,Z0, X1,Y1,Z0},
    };
    static const u8 ru[6] = {U, U + D + W, U + D, U + D + W, U + 2 * D + W, U + D};
    static const u8 rv[6] = {V + D, V + D, V, V, V + D, V + D};
    static const u8 rw[6] = {D, D, W, W, W, W};
    static const u8 rh[6] = {H, H, D, D, H, H};
    static const u8 sh[6] = {22, 20, 31, 15, 26, 29};
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; f++) {
        glColor(RGB15(LIT(sh[f]), LIT(sh[f]), LIT(sh[f])));
        for (int i = 0; i < 4; i++) {
            int tu = (ru[f] << 4) + (uv_c[i][0] ? (rw[f] << 4) - 2 : 2);
            int tv = (rv[f] << 4) + (uv_c[i][1] ? (rh[f] << 4) - 2 : 2);
            glTexCoord2t16(tu, tv);
            glVertex3v16(c[f][i * 3] * 64, c[f][i * 3 + 1] * 64, c[f][i * 3 + 2] * 64);
        }
    }
    glEnd();
}

static void draw_arm(const Player *p)
{
    int f6 = swing_lookup(swing_f6, p->swing);
    int f7 = swing_lookup(swing_f7, p->swing);
    int f8 = swing_lookup(swing_f8, p->swing);

    glBindTexture(0, tex_steve);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(7));
    glPushMatrix();
    glTranslatef32(MCB(0.34) - ((f7 * MCB(0.3)) >> 12), MCB(-0.86) + ((f8 * MCB(0.4)) >> 12), MCB(-0.72) - ((f6 * MCB(0.4)) >> 12));
    glRotateYi(DEG(45));
    glRotateYi((f7 * DEG(70)) >> 12);
    glRotateZi(-((f6 * DEG(20)) >> 12));
    glTranslatef32(-64, 230, 224);
    glRotateZi(DEG(120));
    glRotateXi(DEG(200));
    glRotateYi(-DEG(135));
    glTranslatef32(358, 0, 0);
    glTranslatef32(-320, 128, 0);
    glRotateZi(DEG(5.73f));
    arm_box();
    glPopMatrix(1);
}

#define SP_P (BS / 16)
#define SP_MAXQ 96
enum { SPE_TOP, SPE_BOT, SPE_LEFT, SPE_RIGHT };
static struct { u8 t, f, s, e; } sp_q[SP_MAXQ];
static int sp_n, sp_id = -1;

static int sp_opaque(int u0, int v0, int c, int r)
{
    if (c < 0 || c > 15 || r < 0 || r > 15) return 0;
    const u8 *t = hud_bin + (((v0 + r) * 256 + u0 + c) << 1);
    return (t[1] & 0x80) != 0;
}

static int bow_pull_stage(int bow_t)
{
    if (bow_t <= 0) return 0;
    if (bow_t < 39) return 1;
    if (bow_t < 54) return 2;
    return 3;
}

static void sp_build(int ic, int u0, int v0)
{
    sp_n = 0; sp_id = ic;
    for (int t = 0; t < 4; t++) {
        int dc = t == SPE_LEFT ? -1 : t == SPE_RIGHT ? 1 : 0;
        int dr = t == SPE_TOP ? -1 : t == SPE_BOT ? 1 : 0;
        for (int f = 0; f < 16; f++) {
            int run = -1;
            for (int k = 0; k <= 16; k++) {
                int c = (t <= SPE_BOT) ? k : f, r = (t <= SPE_BOT) ? f : k;
                int edge = k < 16 && sp_opaque(u0, v0, c, r) && !sp_opaque(u0, v0, c + dc, r + dr);
                if (edge && run < 0) run = k;
                if (!edge && run >= 0) {
                    if (sp_n < SP_MAXQ) { sp_q[sp_n].t = t; sp_q[sp_n].f = f; sp_q[sp_n].s = run; sp_q[sp_n].e = k - 1; sp_n++; }
                    run = -1;
                }
            }
        }
    }
}

static void draw_sprite3d_ic(int ic)
{
    int u0 = (ic & 15) * 16, v0 = HUD_ICON_Y0 + (ic >> 4) * 16;
    int ua = u0 << 4, ub = ((u0 + 16) << 4) - 1;
    int va = v0 << 4, vb = ((v0 + 16) << 4) - 1;
    int uL = HELD_MIRROR ? ub : ua, uR = HELD_MIRROR ? ua : ub;
    const int T = SP_P;
    static const u8 shade[4] = {28, 15, 21, 19};

    if (sp_id != ic) sp_build(ic, u0, v0);

    glBindTexture(0, tex_hud);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(7));
    glBegin(GL_QUADS);
    glColor(RGB15(LIT(31), LIT(31), LIT(31)));
    glTexCoord2t16(uL, va); glVertex3v16(0, BS, 0);
    glTexCoord2t16(uR, va); glVertex3v16(BS, BS, 0);
    glTexCoord2t16(uR, vb); glVertex3v16(BS, 0, 0);
    glTexCoord2t16(uL, vb); glVertex3v16(0, 0, 0);
    glColor(RGB15(LIT(24), LIT(24), LIT(24)));
    glTexCoord2t16(uL, va); glVertex3v16(0, BS, -T);
    glTexCoord2t16(uR, va); glVertex3v16(BS, BS, -T);
    glTexCoord2t16(uR, vb); glVertex3v16(BS, 0, -T);
    glTexCoord2t16(uL, vb); glVertex3v16(0, 0, -T);

    for (int i = 0; i < sp_n; i++) {
        int t = sp_q[i].t, f = sp_q[i].f, s = sp_q[i].s, e = sp_q[i].e;
        glColor(RGB15(LIT(shade[t]), LIT(shade[t]), LIT(shade[t])));
        if (t <= SPE_BOT) {
            int y  = BS - (t == SPE_TOP ? f : f + 1) * SP_P;
            int xa = (HELD_MIRROR ? 15 - e : s) * SP_P, xb = (HELD_MIRROR ? 16 - s : e + 1) * SP_P;
            int lo = (u0 + s) * 16, hi = (u0 + e + 1) * 16 - 1;
            int ul = HELD_MIRROR ? hi : lo, ur = HELD_MIRROR ? lo : hi;
            int v = (v0 + f) * 16 + 8;
            glTexCoord2t16(ul, v); glVertex3v16(xa, y, 2);
            glTexCoord2t16(ur, v); glVertex3v16(xb, y, 2);
            glTexCoord2t16(ur, v); glVertex3v16(xb, y, -T - 2);
            glTexCoord2t16(ul, v); glVertex3v16(xa, y, -T - 2);
        } else {
            int c = f;
            int x  = (HELD_MIRROR ? (t == SPE_LEFT ? 16 - c : 15 - c) : (t == SPE_LEFT ? c : c + 1)) * SP_P;
            int ya = BS - s * SP_P, yb = BS - (e + 1) * SP_P;
            int u = (u0 + c) * 16 + 8;
            int vt = (v0 + s) * 16, vbm = (v0 + e + 1) * 16 - 1;
            glTexCoord2t16(u, vt);  glVertex3v16(x, ya, 2);
            glTexCoord2t16(u, vt);  glVertex3v16(x, ya, -T - 2);
            glTexCoord2t16(u, vbm); glVertex3v16(x, yb, -T - 2);
            glTexCoord2t16(u, vbm); glVertex3v16(x, yb, 2);
        }
    }
    glEnd();
}

static void draw_sprite3d(u8 id) { draw_sprite3d_ic(icon_index(id)); }

static void draw_held_item(const Player *p, u8 id)
{
    int f6 = swing_lookup(swing_f6, p->swing);
    int f7 = swing_lookup(swing_f7, p->swing);
    int f8 = swing_lookup(swing_f8, p->swing);
    int f9 = swing_lookup(swing_f9, p->swing);

    glPushMatrix();
    glTranslatef32(-((f7 * MCB(0.4)) >> 12), (f8 * MCB(0.2)) >> 12, -((f9 * MCB(0.2)) >> 12));
    if (p->eat_t > 0 && id != I_BOW) {
        // ItemRenderer.renderItemInFirstPerson (1.7.10), EnumAction.eat:
        //   f6 = ticks restantes - parcial + 1 ; r = f6 / 32 (duracion maxima)
        //   subida: |cos(f6/4 * PI)| * 0.1 mientras r < 0.8
        //   f9 = 1 - r^27 ; translate(f9*0.6, -f9*0.5, 0) ; rotY f9*90 ; rotX f9*10 ; rotZ f9*30
        int f6 = 32 * 4096 + 2048 - p->eat_t * 4096 / 3;          // 4.12, ticks de 20 Hz (+0.5 de media del parcial)
        if (f6 > 32 * 4096) f6 = 32 * 4096;
        if (f6 < 0) f6 = 0;
        int r = f6 / 32;                                          // 4.12
        int r3 = (((r * r) >> 12) * r) >> 12;
        int r9 = (((r3 * r3) >> 12) * r3) >> 12;
        int r27 = (((r9 * r9) >> 12) * r9) >> 12;
        int e9 = 4096 - r27;
        if (r < 3277) glTranslatef32(0, (abs(cosLerp((s16)(f6 & 0x7FFF))) * MCB(0.1)) >> 12, 0);
        glTranslatef32((e9 * MCB(0.6)) >> 12, -((e9 * MCB(0.5)) >> 12), 0);
        glRotateYi((e9 * DEG(90)) >> 12);
        glRotateXi((e9 * DEG(10)) >> 12);
        glRotateZi((e9 * DEG(30)) >> 12);
    }
    glTranslatef32(MCB(HELD_X), MCB(-0.52), MCB(-0.72));
    glRotateYi(DEG(45));
    glRotateYi(-((f6 * DEG(20)) >> 12));
    glRotateZi(-((f7 * DEG(20)) >> 12));
    glRotateXi(-((f7 * DEG(80)) >> 12));
    glScalef32(floattof32(0.4), floattof32(0.4), floattof32(0.4));
    if (id == I_BOW && p->bow_t > 0) {
        glRotateZi(-DEG(18));
        glRotateXi(-DEG(12));
        glRotateYi(-DEG(8));
        glTranslatef32(MCB(-0.25), MCB(0.22), MCB(0.18));
        int pull = p->bow_t > 60 ? 60 : p->bow_t;
        int stretch = 4096 + pull * 600 / 60;
        glScalef32(4096, 4096, stretch);
    }
    glTranslatef32(0, MCB(-0.3), 0);
    glScalef32(floattof32(1.5), floattof32(1.5), floattof32(1.5));
    glRotateYi(DEG(50));
    glRotateZi(DEG(335));
    glTranslatef32(MCB(-0.9375), MCB(-0.0625), 0);
    if (id == I_BOW) draw_sprite3d_ic(icon_index(I_BOW) + bow_pull_stage(p->bow_t));
    else draw_sprite3d(id);
    glPopMatrix(1);
}

#define PXM 0.9375f
#define S_TOOL  (0.625f * PXM)
#define S_FLAT  (0.375f * PXM)
#define S_BLOCK (0.375f * PXM)
static int item_is_flat_tool(u8 id) { return id == I_STICK || id == I_BOW || BLK_IS_TORCH(id) || (id >= I_WOOD_PICK && id < I_TOOL_END); }

static void draw_held_third(const Player *p, u8 id)
{
    (void)p;
    const int U = PXU;
    glPushMatrix();
    glTranslatef32(1 * U, -7 * U, 1 * U);
    if (id < B_COUNT && !BLK_IS_TORCH(id) && !(g_blocks[id].flags & F_PLANT)) {
        glTranslatef32(0, -3 * U, -5 * U);
        glRotateXi(-DEG(20));
        glRotateYi(-DEG(45));
        glScalef32(floattof32(S_BLOCK), floattof32(S_BLOCK), floattof32(S_BLOCK));
        glTranslatef32(-BS / 2, -BS / 2, -BS / 2);
        glBindTexture(0, tex_terrain);
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(7));
        draw_cube_blocktex(id);
    } else {
        if (id == I_BOW) {
            glTranslatef32(-3 * U, -2 * U, 3 * U);
            glRotateYi(-DEG(20));
            glScalef32(floattof32(-0.44f * PXM), floattof32(0.44f * PXM), floattof32(0.44f * PXM));
            glRotateXi(-DEG(100));
            glRotateYi(DEG(45));
        } else if (item_is_flat_tool(id)) {
            glTranslatef32(0, -3 * U, 0);
            glScalef32(floattof32(-S_TOOL), floattof32(S_TOOL), floattof32(S_TOOL));
            glRotateXi(-DEG(100));
            glRotateYi(DEG(45));
        } else {
            glTranslatef32(-4 * U, -3 * U, -3 * U);
            glScalef32(floattof32(-S_FLAT), floattof32(-S_FLAT), floattof32(S_FLAT));
            glRotateZi(DEG(60));
            glRotateXi(-DEG(90));
            glRotateZi(DEG(20));
        }
        glTranslatef32(0, MCB(-0.3), 0);
        glScalef32(floattof32(1.5), floattof32(1.5), floattof32(1.5));
        glRotateYi(DEG(50));
        glRotateZi(DEG(335));
        glTranslatef32(MCB(-0.9375), MCB(-0.0625), 0);
        if (id == I_BOW) draw_sprite3d_ic(icon_index(I_BOW) + bow_pull_stage(p->bow_t));
        else draw_sprite3d(id);
    }
    glPopMatrix(1);
}

// Arco 3D en la mano derecha del esqueleto: escala 0.44 y centrado exactamente en la empunadura de madera
void render_draw_skeleton_bow(int pull_stage, int light)
{
    int prev_light = s_light;
    s_light = light;
    const int U = 64;
    glPushMatrix();
    glTranslatef32(-2 * U, -8 * U - U / 2, 4 * U);
    glRotateYi(-DEG(20));
    glScalef32(floattof32(-0.44f), floattof32(0.44f), floattof32(0.44f));
    glRotateXi(-DEG(100));
    glRotateYi(DEG(45));
    glTranslatef32(0, MCB(-0.3), 0);
    glScalef32(floattof32(1.5), floattof32(1.5), floattof32(1.5));
    glRotateYi(DEG(50));
    glRotateZi(DEG(335));
    glTranslatef32(MCB(-0.9375), MCB(-0.0625), 0);
    draw_sprite3d_ic(icon_index(I_BOW) + (pull_stage & 3));
    glPopMatrix(1);
    s_light = prev_light;
}

#define HAND_BOB 0.07f
static int s_bob_amp;

static void hand_bob(const Player *p)
{
    int target = 0;
    if (p->on_ground && !p->flying) {
        target = p->walk_speed * 256;
        if (target > 4096) target = 4096;
    }
    s_bob_amp += ((target - s_bob_amp) * 5) >> 4;
    if (s_bob_amp < 8 && target == 0) s_bob_amp = 0;
    if (s_bob_amp == 0) return;

    int th = (p->walk_dist * 17) & 0x7FFF;
    int sn = sinLerp((s16)th);
    int cs = abs(cosLerp((s16)th));
    int cs2 = abs(cosLerp((s16)(th - 1043)));
    int a = (s_bob_amp * MCB(HAND_BOB)) >> 12;

    glTranslatef32(((sn * a) >> 12) >> 1, -((cs * a) >> 12), 0);
    glRotateZi((((sn * s_bob_amp) >> 12) * 27) >> 12);
    glRotateXi((((cs2 * s_bob_amp) >> 12) * 45) >> 12);
}

static void draw_held(const Player *p)
{
    if (p->health <= 0) return;
    u8 id = player_held(p);
    int prev_light = s_light;
    int pl = render_sample_light(p->x >> 12, (p->y >> 12) + 1, p->z >> 12);
    if (BLK_IS_TORCH(id)) pl = 31;
    s_light = pl;

    hand_begin();
    if (p->hurt_t > 0) {
        int sn = sinLerp((s16)(p->hurt_t * 16384 / HURT_LEN));
        glRotateZi(-((sn * DEG(10)) >> 12));
    }
    hand_bob(p);
    if (id == 0) {
        draw_arm(p);
        s_light = prev_light;
        return;
    }
    if (id >= I_FIRST || BLK_IS_TORCH(id) || (g_blocks[id].flags & F_PLANT)) {
        draw_held_item(p, id);
        s_light = prev_light;
        return;
    }
    int sw = 0;
    if (p->swing > 0) sw = sinLerp((s16)swing_angle(p));
    glPushMatrix();
    glTranslatef32(floattof32(0.115) - (sw >> 5), floattof32(-0.105) - (sw >> 5), floattof32(-0.20) - (sw >> 4));
    glRotateYi(-3000);
    glRotateXi(-sw >> 1);
    glScalef32(floattof32(0.42), floattof32(0.42), floattof32(0.42));
    glTranslatef32(-BS / 2, -BS / 2, -BS / 2);
    glBindTexture(0, tex_terrain);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(7));
    draw_cube_blocktex(id);
    glPopMatrix(1);
    s_light = prev_light;
}

// ---------------------------------------------------------------------------
// Items sueltos en el mundo
// ---------------------------------------------------------------------------
#define DROP_DRAW_MAX 36

static void draw_drops(const Player *p, int fx, int fy, int fz)
{
    int prev_light = s_light;
    int drawn = 0;
    for (int i = 0; i < MAX_DROPS && drawn < DROP_DRAW_MAX; i++) {
        const Drop *d = &g_drops[i];
        if (!d->active || !world_col_loaded(d->x >> 12, d->z >> 12)) continue;
        int dx = d->x - p->x, dy = d->y - p->y, dz = d->z - p->z;
        if (abs(dx >> 12) > 20 || abs(dz >> 12) > 20 || abs(dy >> 12) > 20) continue;
        if (((dx * fx + dy * fy + dz * fz) >> 12) < -FP) continue;
        drawn++;

        s_light = BLK_IS_TORCH(d->id) ? 31 : render_sample_light(d->x >> 12, d->y >> 12, d->z >> 12);
        int bob = (sinLerp((s16)(d->age * 600)) * 90) >> 12;
        glPushMatrix();
        glTranslatef32(d->x >> 2, (d->y >> 2) + 150 + bob, d->z >> 2);
        glRotateYi((d->age * 105) & 0x7FFF);
        int n = d->count;
        if (d->id >= I_FIRST || BLK_IS_TORCH(d->id) || (g_blocks[d->id].flags & F_PLANT)) {
            int ic = icon_index(d->id);
            int u0 = (ic & 15) * 16, v0 = HUD_ICON_Y0 + (ic >> 4) * 16;
            glScalef32(2048, 2048, 2048);
            glTranslatef32(-BS / 2, -BS / 2, 0);
            glBindTexture(0, tex_hud);
            glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(8));
            glColor(RGB15(LIT(31), LIT(31), LIT(31)));
            for (int k = 0; k < (n > 1 ? 2 : 1); k++) {
                glBegin(GL_QUADS);
                int z = k * 120;
                glTexCoord2t16(u0 << 4, v0 << 4);               glVertex3v16(0, BS, z);
                glTexCoord2t16(((u0 + 16) << 4) - 1, v0 << 4);  glVertex3v16(BS, BS, z);
                glTexCoord2t16(((u0 + 16) << 4) - 1, ((v0 + 16) << 4) - 1); glVertex3v16(BS, 0, z);
                glTexCoord2t16(u0 << 4, ((v0 + 16) << 4) - 1);  glVertex3v16(0, 0, z);
                glEnd();
            }
        } else {
            glScalef32(1100, 1100, 1100);
            glTranslatef32(-BS / 2, -BS / 2, -BS / 2);
            glBindTexture(0, tex_terrain);
            glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(8));
            draw_cube_blocktex(d->id);
        }
        glPopMatrix(1);
    }
    s_light = prev_light;
}

// ---------------------------------------------------------------------------
// Fotograma del juego
// ---------------------------------------------------------------------------
static void perspective(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glTranslatef32(0, 0, ZB_WORLD_LO);
    glScalef32(4096, 4096, 4096 - ZB_WORLD_LO);
    gluPerspective((float)opt_fov_degrees(), 256.0f / 192.0f, ZNEAR_GL, ZFAR_GL);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// ---------------------------------------------------------------------------
// Niebla (hardware, gratis) y nubes
// ---------------------------------------------------------------------------
static float zb_of_dist(float z)
{
    float n = ZNEAR_GL, f = ZFAR_GL, lo = ZB_WORLD_LO / 4096.0f;
    float ndc = (f + n) / (f - n) - 2 * f * n / ((f - n) * z);
    return ((1 - lo) * ndc + lo + 1) * 0.5f;
}

static float dist_of_zb(float d)
{
    float n = ZNEAR_GL, f = ZFAR_GL, lo = ZB_WORLD_LO / 4096.0f;
    float ndc = ((2 * d - 1) - lo) / (1 - lo);
    float den = (f + n) / (f - n) - ndc;
    if (den < 0.0005f) return 1000.0f;
    return 2 * f * n / ((f - n) * den);
}

static void fog_setup(int start, int end)
{
    static int s_start = -1, s_end = -1;
    if (start == s_start && end == s_end) return;
    s_start = start; s_end = end;
    float d0 = zb_of_dist(start * 0.25f), d1 = zb_of_dist(end * 0.25f);
    int span = (int)((d1 - d0) * 32767.0f) + 1;
    int sh = 10;
    while (sh > 0 && 32 * (0x400 >> sh) < span) sh--;
    int step = 0x400 >> sh;
    int off = (int)(d0 * 32767.0f) - step;
    if (off < 0) off = 0;
    glFogShift(sh);
    glFogOffset(off);
    for (int i = 0; i < 32; i++) {
        float d = (off + step * (i + 1)) / 32767.0f;
        float blocks = dist_of_zb(d) * 4.0f;
        float k = (blocks - start) / (float)(end - start);
        if (k < 0) k = 0;
        if (k > 1) k = 1;
        glFogDensity(i, (int)(k * 127.0f));
    }
}

#define CLOUD_Y     84
#define CLOUD_T     3
#define CLOUD_C     (10 * BS)
#define CLOUD_N     16
#define CLOUD_R     4
#define CLOUD_RUN   3
#define CLOUD_MAXQ  48
static u16 s_cloud[CLOUD_N];

static void clouds_init(void)
{
    u8 a[CLOUD_N][CLOUD_N], b[CLOUD_N][CLOUD_N];
    u32 seed = 0x9E3779B1u;
    for (int z = 0; z < CLOUD_N; z++)
        for (int x = 0; x < CLOUD_N; x++) {
            seed = seed * 1664525u + 1013904223u;
            a[z][x] = ((seed >> 24) & 255) < 120;
        }
    for (int pass = 0; pass < 2; pass++) {
        for (int z = 0; z < CLOUD_N; z++)
            for (int x = 0; x < CLOUD_N; x++) {
                int c = 0;
                for (int dz = -1; dz <= 1; dz++)
                    for (int dx = -1; dx <= 1; dx++) c += a[(z + dz) & 15][(x + dx) & 15];
                b[z][x] = c >= 5;
            }
        memcpy(a, b, sizeof(a));
    }
    for (int z = 0; z < CLOUD_N; z++) {
        s_cloud[z] = 0;
        for (int x = 0; x < CLOUD_N; x++) if (a[z][x]) s_cloud[z] |= 1u << x;
    }
}

static inline int mix_c(int a, int b, int k) { return a + (((b - a) * k) >> 8); }

static void draw_clouds(const View *v, int sr, int sg, int sb)
{
    static int ready;
    if (!ready) { clouds_init(); ready = 1; }

    int off = g_daytime * 1024 / 15;
    const int P = CLOUD_N * CLOUD_C;
    int ux = ((g_cam32[0] - off) % P + P) % P;
    int ccx = ux / CLOUD_C, fx = ux - ccx * CLOUD_C;
    int uz = g_cam32[2] % P;
    int ccz = uz / CLOUD_C, fz = uz - ccz * CLOUD_C;
    int y0 = CLOUD_Y * BS - g_cam32[1];

    int c = 6 + (s_sky_light - 12) * 25 / 19;
    int quads = 0;

    glBindTexture(0, 0);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(13));
    for (int dz = -CLOUD_R; dz <= CLOUD_R; dz++) {
        u16 row = s_cloud[(ccz + dz) & 15];
        for (int dx = -CLOUD_R; dx <= CLOUD_R && quads + 2 <= CLOUD_MAXQ; ) {
            if (!(row >> ((ccx + dx) & 15) & 1)) { dx++; continue; }
            int n = 1;
            while (n < CLOUD_RUN && dx + n <= CLOUD_R && (row >> ((ccx + dx + n) & 15) & 1)) n++;
            int rx = dx * CLOUD_C - fx, rz = dz * CLOUD_C - fz;
            int cx = rx + n * CLOUD_C / 2, cz = rz + CLOUD_C / 2;
            int dot = (cx * v->fx + cz * v->fz) >> 12;
            if (dot > -CLOUD_C * 5 / 2) {
                int ax = abs(cx), az = abs(cz);
                int dist = (ax > az ? ax + az / 2 : az + ax / 2) / BS;
                int k = (dist - 14) * 256 / 22;
                if (k < 0) k = 0;
                if (k > 256) k = 256;
                int top = c, bot = c * 25 / 32;
                int tr = mix_c(top, sr, k), tg = mix_c(top, sg, k), tb = mix_c(top, sb, k);
                int br = mix_c(bot, sr, k), bg = mix_c(bot, sg, k), bb = mix_c(bot, sb, k);
                int w = n * CLOUD_C;
                glPushMatrix();
                glTranslatef32(rx, y0, rz);
                glBegin(GL_QUADS);
                glColor(RGB15(br, bg, bb));
                glVertex3v16(0, 0, 0); glVertex3v16(w, 0, 0); glVertex3v16(w, 0, CLOUD_C); glVertex3v16(0, 0, CLOUD_C);
                glColor(RGB15(tr, tg, tb));
                glVertex3v16(0, CLOUD_T * BS, 0); glVertex3v16(w, CLOUD_T * BS, 0);
                glVertex3v16(w, CLOUD_T * BS, CLOUD_C); glVertex3v16(0, CLOUD_T * BS, CLOUD_C);
                glEnd();
                glPopMatrix(1);
                quads += 2;
            }
            dx += n;
        }
    }
}

#define MAX_VIS 400

static int build_visible(const View *v, Vis *vis, int range16)
{
    int n = 0;
    int reach = range16 / 16 + 14;
    int bx = v->cx >> 4, bz = v->cz >> 4;
    int sx0 = (bx - reach) / SEC, sx1 = (bx + reach) / SEC;
    int sz0 = (bz - reach) / SEC, sz1 = (bz + reach) / SEC;
    if (bx - reach < 0) sx0 = 0;
    if (bz - reach < 0) sz0 = 0;
    if (sx1 >= SXN) sx1 = SXN - 1;
    if (sz1 >= SZN) sz1 = SZN - 1;
    int lim = range16 + 13 * 16;
    int lim2 = lim * lim;
    int cone_sec = v->cone + 2;
    int on_surface = (v->cy >= SEA_LEVEL * 16 && s_cave_sky >= 160);
    for (int sy = 0; sy < SYN; sy++)
        for (int sz = sz0; sz <= sz1; sz++)
            for (int sx = sx0; sx <= sx1; sx++) {
                if (!g_cols[sz * CXN + sx]) continue;
                int idx = SECIDX(sx, sy, sz);
                const Section *s = &g_sec[idx];
                // Saltar secciones vacias ya construidas sin meterlas en la lista ni ordenarlas
                if (s->built && !g_sec_dirty[idx] && (s->n[0] | s->n[1] | s->n[2]) == 0) continue;
                int dx = (sx * SEC + 8) * 16 - v->cx, dy = (sy * SEC + 8) * 16 - v->cy, dz = (sz * SEC + 8) * 16 - v->cz;
                if (on_surface && sy <= 1 && s->built && !g_sec_dirty[idx]) {
                    int h2 = dx * dx + dz * dz;
                    int max_cave_r = (sy == 0 ? 10 * 16 : 12 * 16);
                    if (h2 > max_cave_r * max_cave_r) continue;
                }
                int d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > lim2) continue;
                int dot = ((dx * v->fx + dy * v->fy + dz * v->fz) >> 12) + 340;
                if (dot <= 0) continue;
                if (cone_sec * dot * dot < d2) continue;
                if (n >= MAX_VIS) continue;
                int i = n++;
                while (i > 0 && vis[i - 1].d2 > d2) { vis[i] = vis[i - 1]; i--; }
                vis[i].idx = idx;
                vis[i].d2 = d2;
            }
    return n;
}

static void build_pending(const View *v, const Vis *vis, int n)
{
    int budget = 1;
    // 1) Prioridad maxima: secciones cercanas modificadas por el jugador o secciones sin construir
    for (int i = 0; i < n && budget > 0; i++) {
        int si = vis[i].idx;
        if (!g_sec[si].built || (g_sec_dirty[si] && vis[i].d2 <= (20 * 16) * (20 * 16))) {
            build_section(si % SXN, si / (SXN * SZN), (si / SXN) % SZN);
            if (s_build_heavy) budget--;
        }
    }
    // 2) Secciones sucias mas lejanas
    for (int i = 0; i < n && budget > 0; i++) {
        int si = vis[i].idx;
        if (g_sec_dirty[si]) {
            build_section(si % SXN, si / (SXN * SZN), (si / SXN) % SZN);
            if (s_build_heavy) budget--;
        }
    }
    // 3) Si todo lo visible esta listo y no estamos en Far, preconstruir secciones inmediatas alrededor del jugador
    if (budget > 0 && (g_opt.render_dist & 3) < 3) {
        int csx = (v->cx >> 4) / SEC, csy = (v->cy >> 4) / SEC, csz = (v->cz >> 4) / SEC;
        for (int dy = -1; dy <= 1 && budget > 0; dy++) {
            int sy = csy + dy;
            if ((unsigned)sy >= SYN) continue;
            for (int dz = -1; dz <= 1 && budget > 0; dz++)
                for (int dx = -1; dx <= 1 && budget > 0; dx++) {
                    int sx = csx + dx, sz = csz + dz;
                    if ((unsigned)sx >= SXN || (unsigned)sz >= SZN || !g_cols[sz * CXN + sx]) continue;
                    int si = SECIDX(sx, sy, sz);
                    if (!g_sec[si].built) {
                        build_section(sx, sy, sz);
                        if (s_build_heavy) budget--;
                    }
                }
        }
    }
}

void render_prebuild(const Player *p, int max_sections)
{
    int px = p->x >> 12, pz = p->z >> 12;
    int cx0 = px / SEC, cz0 = pz / SEC;
    int n = 0;
    for (int r = 0; r <= COL_LOAD_R && n < max_sections; r++)
        for (int dz = -r; dz <= r && n < max_sections; dz++)
            for (int dx = -r; dx <= r && n < max_sections; dx++) {
                if (abs(dx) != r && abs(dz) != r) continue;
                int sx = cx0 + dx, sz = cz0 + dz;
                if ((unsigned)sx >= SXN || (unsigned)sz >= SZN || !g_cols[sz * CXN + sx]) continue;
                for (int sy = 0; sy < SYN && n < max_sections; sy++) {
                    build_section(sx, sy, sz);
                    if (g_sec[SECIDX(sx, sy, sz)].n[0] || g_sec[SECIDX(sx, sy, sz)].n[1] || g_sec[SECIDX(sx, sy, sz)].n[2])
                        n++;
                }
            }
}

void render_game(const Player *p, int show_hud)
{
    int ex, ey, ez, yaw, pitch;
    camera_pose(p, &ex, &ey, &ez, &yaw, &pitch);

    View v;
    v.cx = ex >> 8; v.cy = ey >> 8; v.cz = ez >> 8;
    int cp = cosLerp((s16)pitch);
    v.fx = (sinLerp((s16)yaw) * cp) >> 12;
    v.fy = sinLerp((s16)pitch);
    v.fz = -((cosLerp((s16)yaw) * cp) >> 12);

    int uw = 0;
    if (p->cam_mode == 0) uw = p->head_water;
    s_sky_light = daynight_light();
    s_light = s_sky_light;
    update_vcol_lut(s_sky_light);

    int cbx = ex >> 12, cby = ey >> 12, cbz = ez >> 12;
    int cam_sky = sample_cam_sky(cbx, cby, cbz);
    if (cam_sky > 0 && cby <= SEA_LEVEL + 6) {
        int fbx = (ex + v.fx * 3) >> 12, fby = (ey + v.fy * 3) >> 12, fbz = (ez + v.fz * 3) >> 12;
        int f_sky = sample_cam_sky(fbx, fby, fbz);
        if (f_sky < cam_sky) cam_sky = (cam_sky + f_sky) >> 1;
    }
    int target_cave = cam_sky * 256 / 15;
    if (target_cave < s_cave_sky) {
        s_cave_sky -= 48;
        if (s_cave_sky < target_cave) s_cave_sky = target_cave;
    } else if (target_cave > s_cave_sky) {
        s_cave_sky += 24;
        if (s_cave_sky > target_cave) s_cave_sky = target_cave;
    }

    int sr, sg, sb;
    if (uw) {
        sr = 2 * s_sky_light / 31; sg = 5 * s_sky_light / 31; sb = 18 * s_sky_light / 31;
    } else {
        daynight_sky(&sr, &sg, &sb);
        sr = (2 * (256 - s_cave_sky) + sr * s_cave_sky) >> 8;
        sg = (2 * (256 - s_cave_sky) + sg * s_cave_sky) >> 8;
        sb = (2 * (256 - s_cave_sky) + sb * s_cave_sky) >> 8;
    }
    glClearColor(sr, sg, sb, 31);
    glFogColor(sr, sg, sb, 31);
    g_polyfog = POLY_FOG;

    perspective();
    // Sacudida de camara al recibir dano (hurtCameraEffect de Minecraft 1.7.10) o inclinacion al morir
    if (p->health <= 0) {
        int dt = p->death_t > 20 ? 20 : p->death_t;
        glRotateZi((dt * DEG(40) / 20) & 0x7FFF);
    } else if (p->hurt_t > 0) {
        int sn = sinLerp((s16)(p->hurt_t * 16384 / HURT_LEN));
        glRotateZi((-((sn * DEG(14)) >> 12)) & 0x7FFF);
        glRotateXi(((sn * DEG(4)) >> 12) & 0x7FFF);
    }
    glRotateXi((-pitch) & 0x7FFF);
    glRotateYi(yaw & 0x7FFF);
    g_cam32[0] = ex >> 2; g_cam32[1] = ey >> 2; g_cam32[2] = ez >> 2;

    int rd = uw ? 7 : opt_render_blocks();
    if (uw) fog_setup(1, 6); else fog_setup(rd * 7 / 20, rd * 3 / 4);
    int r_cull = (rd + 1) * 16;
    v.maxd2 = r_cull * r_cull;
    v.cone = g_opt.fov == 0 ? 2 : (g_opt.fov == 1 ? 3 : 4);

    static Vis vis[MAX_VIS];
    int nvis = build_visible(&v, vis, r_cull);
    build_pending(&v, vis, nvis);

    int quads = MAX_WORLD_QUADS;
    glBindTexture(0, tex_terrain);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(1) | g_polyfog);
    draw_section_list(&v, vis, nvis, 0, &quads);
    int drawn_world = MAX_WORLD_QUADS - quads;
    int spare = quads > 0 ? quads : 0;
    int total_extra = (MAX_TOTAL_QUADS - MAX_WORLD_QUADS) + spare;
    int plant_budget = total_extra - MAX_WATER_QUADS;
    if (plant_budget < 64) plant_budget = 64;
    int plant_left = plant_budget;
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(2) | g_polyfog);
    draw_section_list(&v, vis, nvis, 1, &plant_left);
    int drawn_plants = plant_budget - plant_left;
    int water_budget = total_extra - drawn_plants;
    if (water_budget < MAX_WATER_QUADS) water_budget = MAX_WATER_QUADS;
    int water_left = water_budget;
    glPolyFmt(POLY_ALPHA(20) | POLY_CULL_NONE | POLY_ID(3) | g_polyfog);
    draw_section_list(&v, vis, nvis, 2, &water_left);
    int drawn_water = water_budget - water_left;
    if (!uw && s_cave_sky > 48) draw_clouds(&v, sr, sg, sb);
    s_quads_last = drawn_world + drawn_plants + drawn_water;

    glTranslatef32(-g_cam32[0], -g_cam32[1], -g_cam32[2]);

    draw_chests(p, &v);
    draw_target(p);
    draw_drops(p, v.fx, v.fy, v.fz);
    mobgfx_draw_all(p, v.fx, v.fy, v.fz, s_sky_light, p->cam_mode != 0 ? 170 : 216);
    g_polyfog = 0;
    if (p->cam_mode != 0) draw_steve(p);
    if (p->cam_mode == 0 && show_hud && p->sleep_t == 0) draw_held(p);

    if (show_hud) hud_game(p, p->cam_mode == 0);
}

int render_quads_last(void) { return s_quads_last; }

void render_blank(int r, int g, int b)
{
    glClearColor(r, g, b, 31);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// ---------------------------------------------------------------------------
// Menu principal: panorama + logo
// ---------------------------------------------------------------------------
static const s8 pano_vt[6][12] = {
    {-1,1,-1,  1,1,-1,  1,-1,-1, -1,-1,-1},
    { 1,1,-1,  1,1, 1,  1,-1, 1,  1,-1,-1},
    { 1,1, 1, -1,1, 1, -1,-1, 1,  1,-1, 1},
    {-1,1, 1, -1,1,-1, -1,-1,-1, -1,-1, 1},
    {-1,1, 1,  1,1, 1,  1,1,-1, -1,1,-1},
    {-1,-1,-1, 1,-1,-1, 1,-1,1, -1,-1,1},
};

static void veil_draw(void)
{
    if (tex_veil < 0) veil_init_tex();
    if (tex_veil < 0) return;
    int z = hud_z;
    hud_z += 4;
    glBindTexture(0, tex_veil);
    glPolyFmt(POLY_ALPHA(15) | POLY_CULL_NONE | POLY_ID(11));
    glColor(RGB15(31, 31, 31));
    glBegin(GL_QUADS);
    glTexCoord2t16(0, 0);                        glVertex3v16(0, 0, z);
    glTexCoord2t16(256 << 4, 0);                 glVertex3v16(256 * 64, 0, z);
    glTexCoord2t16(256 << 4, (192 << 4) - 1);    glVertex3v16(256 * 64, 192 * 64 + 3, z);
    glTexCoord2t16(0, (192 << 4) - 1);           glVertex3v16(0, 192 * 64 + 3, z);
    glEnd();
}

void render_title(int frame, const char *splash)
{
    glClearColor(0, 0, 0, 31);
    perspective();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(105.0f, 256.0f / 192.0f, 0.1f, 10.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    int pitch = ((sinLerp((s16)(frame * 9)) * 25 * 91) >> 12) + 1820;
    glRotateXi(pitch);
    glRotateYi((-frame * 6) & 0x7FFF);

    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(1));
    glColor(RGB15(31, 31, 31));
    for (int f = 0; f < 6; f++) {
        glBindTexture(0, tex_pano[f]);
        glBegin(GL_QUADS);
        for (int i = 0; i < 4; i++) {
            glTexCoord2t16(uv_c[i][0] ? (128 << 4) - 6 : 6, uv_c[i][1] ? (128 << 4) - 6 : 6);
            glVertex3v16(pano_vt[f][i * 3] * 4095, pano_vt[f][i * 3 + 1] * 4095, pano_vt[f][i * 3 + 2] * 4095);
        }
        glEnd();
    }

    hud_begin();
    veil_draw();
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(10));
    int splash_x = 200, splash_y = 90;
    glBindTexture(0, tex_logo);
    glColor(RGB15(31, 31, 31));
    {
        int lx = 128 - (LOGO_BX0 + LOGO_BX1) / 2, ly = 96 - (LOGO_BY0 + LOGO_BY1) / 2;
        hq(lx, ly, lx + LOGO_W, ly + LOGO_H, 0, 0, LOGO_W, LOGO_H);
        splash_x = lx + LOGO_BX1 - 26;
        splash_y = ly + LOGO_BY1 - 6;
    }

    if (splash && (tex_splash[0] < 0 || s_splash_src != splash)) gfx_prepare_title(splash);
    if (splash && tex_splash[0] >= 0) {
        int a = sinLerp((s16)(frame * 1092));
        if (a < 0) a = -a;
        int v = (a * (SPL_VARIANTS - 1) + 2048) >> 12;
        if (v > SPL_VARIANTS - 1) v = SPL_VARIANTS - 1;
        int cx = splash_x;
        if (cx + s_splash_hw > 253) cx = 253 - s_splash_hw;
        if (cx - s_splash_hw < 3) cx = 3 + s_splash_hw;
        int x0 = cx - SPL_W / 2, y0 = splash_y - SPL_H / 2;
        glBindTexture(0, tex_splash[v]);
        glColor(RGB15(31, 31, 31));
        hq(x0, y0, x0 + SPL_W, y0 + SPL_H, 0, 0, SPL_W, SPL_H);
    }

    if (tex_labels >= 0) {
        glBindTexture(0, tex_labels);
        glColor(RGB15(31, 31, 31));
        // Abajo a la izquierda: version. Abajo a la derecha: creadores en dos lineas (alineados a la derecha).
        int y0 = 192 - 12;
        hq(3, y0, 3 + s_lbl_w1 + 2, y0 + LBL_LINE_H, 0, 0, s_lbl_w1 + 2, LBL_LINE_H);
        int x2 = 256 - 4 - s_lbl_w2 - 1;
        hq(x2, y0 - LBL_LINE_H, x2 + s_lbl_w2 + 2, y0, 0, LBL_LINE_H, s_lbl_w2 + 2, 2 * LBL_LINE_H);
        int x3 = 256 - 4 - s_lbl_w3 - 1;
        hq(x3, y0, x3 + s_lbl_w3 + 2, y0 + LBL_LINE_H, 0, 2 * LBL_LINE_H, s_lbl_w3 + 2, 3 * LBL_LINE_H);
    }
}

void render_loading_bg(void)
{
    gfx_unload();
    int id = upload_ex(dirtbg_bin, 16, 16, GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T);
    render_blank(0, 0, 0);
    hud_begin();
    glBindTexture(0, id);
    glColor(RGB15(31, 31, 31));
    hq(0, 0, 256, 192, 0, 0, 256, 192);
}
