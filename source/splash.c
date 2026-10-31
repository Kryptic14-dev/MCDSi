// Pre-render en CPU de los textos del menu principal (ver splash.h)
#include <string.h>
#include "splash.h"

#define C_YELLOW ARGB16(1, 31, 31, 0)
#define C_SHADOW ARGB16(1, 7, 7, 0)       // 0x3F3F00: el amarillo de Minecraft con 1/4 de brillo
#define C_BLACK  ARGB16(1, 0, 0, 0)
#define C_WHITE  ARGB16(1, 31, 31, 31)

#define MASK_W 260
#define MASK_H 10
static u8 s_mask[MASK_H][MASK_W];         // bit0 = letra, bit1 = sombra (desplazada 1 px abajo-derecha)

static int font_px(const u8 *font, int x, int y)
{
    return (font[(y * 128 + x) * 2 + 1] & 0x80) != 0;   // bit 15 de ARGB1555 little-endian
}

int text_width(const char *s, const u8 *fontw)
{
    int w = 0;
    for (; *s; s++) w += fontw[(u8)*s];
    return w;
}

// Rellena s_mask con el texto (esquina en 0,0). Devuelve el ancho del texto en px.
static int build_mask(const char *s, const u8 *font, const u8 *fontw, int with_shadow)
{
    memset(s_mask, 0, sizeof(s_mask));
    int x = 0;
    for (; *s; s++) {
        u8 c = (u8)*s;
        int adv = fontw[c];
        if (x + adv + 2 > MASK_W) break;
        int cx = (c & 15) * 8, cy = (c >> 4) * 8;
        if (c != ' ') {
            for (int gy = 0; gy < 8; gy++)
                for (int gx = 0; gx < 8; gx++)
                    if (x + gx < MASK_W && font_px(font, cx + gx, cy + gy)) s_mask[gy][x + gx] |= 1;
        }
        x += adv;
    }
    if (with_shadow) {
        for (int y = 0; y < MASK_H - 1; y++)
            for (int xx = 0; xx < MASK_W - 1; xx++)
                if (s_mask[y][xx] & 1) s_mask[y + 1][xx + 1] |= 2;
    }
    return x;
}

// pixel vacio con algun vecino (8 direcciones) de color 'a' o 'b' -> negro
static void outline_pass(u16 *out, int stride, int rows, int x0, int y0, int x1, int y1, u16 a, u16 b)
{
    if (x0 < 1) x0 = 1;
    if (y0 < 1) y0 = 1;
    if (x1 > stride - 2) x1 = stride - 2;
    if (y1 > rows - 2) y1 = rows - 2;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            u16 *p = &out[y * stride + x];
            if (*p) continue;
            int hit = 0;
            for (int dy = -1; dy <= 1 && !hit; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    u16 n = out[(y + dy) * stride + x + dx];
                    if (n == a || n == b) { hit = 1; break; }
                }
            if (hit) *p = C_BLACK;
        }
    }
}

static const int COS20 = 3849, SIN20 = 1401;   // cos / sin de 20 grados en 4.12

// semiextension del rectangulo w x h (px de texto) girado 20 grados y escalado scq/256
static void rot_extent(int w, int h, int scq, int *ex, int *ey)
{
    *ex = (w * scq * COS20 + h * scq * SIN20) / (2 * 4096 * 256) + 2;
    *ey = (w * scq * SIN20 + h * scq * COS20) / (2 * 4096 * 256) + 2;
}

// Una variante: s_mask ya construida (w x h), escala scq (8.8), centrada en el medio de out.
static void bake_one(u16 *out, int w, int h, int scq, int *half_w, int *half_h)
{
    memset(out, 0, SPL_W * SPL_H * 2);
    int ex, ey;
    rot_extent(w, h, scq, &ex, &ey);
    if (ex > SPL_W / 2 - 2) ex = SPL_W / 2 - 2;
    if (ey > SPL_H / 2 - 2) ey = SPL_H / 2 - 2;
    const int cxo = SPL_W / 2, cyo = SPL_H / 2;
    // salida -> texto (rotacion inversa y division por la escala, en 16 bits de fraccion, por cuarto de pixel)
    const int Cp = COS20 * 1024 / scq, Sp = SIN20 * 1024 / scq;
    const int offx = w << 15, offy = h << 15;

    static const int sub[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};   // +-1/4 de pixel
    for (int oy = cyo - ey; oy < cyo + ey; oy++) {
        for (int ox = cxo - ex; ox < cxo + ex; ox++) {
            int yc = 0, any = 0;
            for (int k = 0; k < 4; k++) {
                int dx4 = (ox * 4 + 2 + sub[k][0]) - cxo * 4;
                int dy4 = (oy * 4 + 2 + sub[k][1]) - cyo * 4;
                int px = (dx4 * Cp - dy4 * Sp + offx) >> 16;   // el texto sube hacia la derecha
                int py = (dx4 * Sp + dy4 * Cp + offy) >> 16;
                if (px < 0 || px >= w || py < 0 || py >= h) continue;
                u8 m = s_mask[py][px];
                if (m & 1) yc++;
                if (m & 3) any++;
            }
            if (yc >= 2) out[oy * SPL_W + ox] = C_YELLOW;
            else if (any >= 2) out[oy * SPL_W + ox] = C_SHADOW;
        }
    }
    outline_pass(out, SPL_W, SPL_H, cxo - ex - 1, cyo - ey - 1, cxo + ex, cyo + ey, C_YELLOW, C_SHADOW);
    if (half_w) *half_w = ex + 1;
    if (half_h) *half_h = ey + 1;
}

void splash_bake(const char *s, const u8 *font, const u8 *fontw, u16 *out, int *half_w, int *half_h)
{
    int w = build_mask(s, font, fontw, 1) + 1;
    int h = 9;
    // Escala como GuiMainMenu: 1.8 * 100 / (ancho + 32) (los textos largos salen mas pequenos), ajustada a la DS
    // (un poco menor que el 2x fijo de antes) y limitada a 1.75 y a lo que cabe en la textura.
    int scq = 175 * 256 / (w + 32);
    if (scq > 448) scq = 448;                      // 1.75
    for (;;) {
        int ex, ey;
        rot_extent(w, h, scq, &ex, &ey);
        if ((ex <= SPL_W / 2 - 3 && ey <= SPL_H / 2 - 3) || scq <= 128) break;
        scq -= 8;
    }
    static const int pulse[SPL_VARIANTS] = {1000, 972, 944};
    for (int i = 0; i < SPL_VARIANTS; i++)
        bake_one(out + i * SPL_W * SPL_H, w, h, scq * pulse[i] / 1000, i == 0 ? half_w : NULL, i == 0 ? half_h : NULL);
}

int text_bake_outlined(const char *s, const u8 *font, const u8 *fontw, u16 *out, int stride, int x, int y)
{
    int w = build_mask(s, font, fontw, 0);
    for (int py = 0; py < 8; py++)
        for (int px = 0; px < w; px++)
            if (s_mask[py][px] & 1) out[(y + py) * stride + x + px] = C_WHITE;
    outline_pass(out, stride, y + 10, x - 1, y - 1, x + w, y + 8, C_WHITE, C_WHITE);
    return w;
}
