#include <string.h>
#include "ui.h"
#include "assets.h"
#include "blocks.h"
#include "items.h"
#include "sfx.h"
#include "keyicons.h"

u8 ui_buf[UI_W * UI_H];
TouchState ui_t;
int ui_draw_on = 1;

static u8 bg_buf[UI_W * UI_H];
static int bg_ready;

static inline void put_px(int x, int y, u8 c)
{
    if ((unsigned)x < UI_W && (unsigned)y < UI_H) ui_buf[y * UI_W + x] = c;
}

void ui_begin(void)
{
    if (!bg_ready) {
        for (int y = 0; y < UI_H; y++)
            for (int x = 0; x < UI_W; x++)
                bg_buf[y * UI_W + x] = ui2d_bin[(UI_DIRT_Y + (y & 15)) * UI_SHEET_W + UI_DIRT_X + (x & 15)];
        bg_ready = 1;
    }
    memcpy(ui_buf, bg_buf, sizeof(ui_buf));
}

void ui_blit(int sx, int sy, int w, int h, int dx, int dy)
{
    if (!ui_draw_on) return;
    for (int y = 0; y < h; y++) {
        int py = dy + y;
        if ((unsigned)py >= UI_H) continue;
        const u8 *src = &ui2d_bin[(sy + y) * UI_SHEET_W + sx];
        u8 *dst = &ui_buf[py * UI_W];
        for (int x = 0; x < w; x++) {
            int px = dx + x;
            if ((unsigned)px >= UI_W) continue;
            u8 c = src[x];
            if (c) dst[px] = c;
        }
    }
}

void ui_icon(int block, int dx, int dy)
{
    if (block <= 0) return;
    if (block >= B_COUNT && (block < I_FIRST || block >= I_END)) return;
    int ic = icon_index((u8)block);
    ui_blit((ic & 15) * 16, UI_ICON_Y0 + (ic >> 4) * 16, 16, 16, dx, dy);
}

// icono + cantidad + barra de durabilidad
void ui_slot(const Slot *s, int dx, int dy, int show_count)
{
    if (!s->id) return;
    ui_icon(s->id, dx, dy);
    if (show_count && s->count > 1) {
        char b[8];
        int n = s->count, i = 0;
        char t[4];
        while (n > 0 && i < 3) { t[i++] = (char)('0' + n % 10); n /= 10; }
        int k = 0;
        while (i > 0) b[k++] = t[--i];
        b[k] = 0;
        ui_text_right(dx + 17, dy + 9, b, UIC_WHITE, 1);
    }
    int md = item_max_dmg(s->id);
    if (md > 0 && s->dmg > 0) {
        int w = 13 * (md - s->dmg) / md;
        if (w < 1) w = 1;
        int col = (w * 4 > 13 * 2) ? UIC_GREEN : (w * 4 > 13 ? UIC_YELLOW : UIC_RED);
        ui_rect(dx + 2, dy + 13, 13, 2, UIC_BLACK);
        ui_rect(dx + 2, dy + 13, w, 1, col);
    }
}

void ui_rect(int x, int y, int w, int h, int color)
{
    if (!ui_draw_on) return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) put_px(x + i, y + j, (u8)color);
}

// --------------------------------------------------------------------------
// Texto con la fuente ascii.png de Minecraft (ancho proporcional)
// --------------------------------------------------------------------------
static void glyph(int x, int y, int ch, int color)
{
    int gx = (ch & 15) * 8, gy = (ch >> 4) * 8;
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) {
            // font_bin es ARGB1555 little-endian; el bit de alfa esta en el byte alto
            if (font_bin[((gy + j) * 128 + gx + i) * 2 + 1] & 0x80) put_px(x + i, y + j, (u8)color);
        }
}

int ui_text_width(const char *s)
{
    int w = 0;
    for (; *s; s++) w += fontw_bin[(u8)*s];
    return w;
}

void ui_text(int x, int y, const char *s, int color, int shadow)
{
    if (!ui_draw_on) return;
    if (shadow) {
        int sx = x;
        for (const char *p = s; *p; p++) {
            glyph(sx + 1, y + 1, (u8)*p, UIC_SHADOW);
            sx += fontw_bin[(u8)*p];
        }
    }
    for (const char *p = s; *p; p++) {
        glyph(x, y, (u8)*p, color);
        x += fontw_bin[(u8)*p];
    }
}

void ui_text_center(int cx, int y, const char *s, int color, int shadow)
{
    ui_text(cx - ui_text_width(s) / 2, y, s, color, shadow);
}

void ui_text_right(int rx, int y, const char *s, int color, int shadow)
{
    ui_text(rx - ui_text_width(s), y, s, color, shadow);
}

// --------------------------------------------------------------------------
// Botones estilo Minecraft (widgets.png)
// --------------------------------------------------------------------------
void ui_button_h(int x, int y, int w, int h, const char *label, int enabled)
{
    // hover mientras el lapiz esta apoyado encima y el toque empezo dentro
    int hover = enabled && ui_t.down && ui_hit(x, y, w, h) &&
            (ui_t.px >= x && ui_t.px < x + w && ui_t.py >= y && ui_t.py < y + h);
    int sy = !enabled ? UI_BTN_DISABLED_Y : (hover ? UI_BTN_HOVER_Y : UI_BTN_NORMAL_Y);
    int sx = !enabled ? UI_BTN_DISABLED_X : (hover ? UI_BTN_HOVER_X : UI_BTN_NORMAL_X);
    int half = w / 2;
    ui_blit(sx, sy, half, 20, x, y);
    ui_blit(sx + 200 - (w - half), sy, w - half, 20, x + half, y);
    int col = !enabled ? UIC_GRAY : (hover ? UIC_YELLOW : UIC_LGRAY);
    ui_text_center(x + w / 2, y + (20 - 8) / 2, label, col, 1);
    (void)h;
}

void ui_button(int x, int y, int w, const char *label, int enabled)
{
    ui_button_h(x, y, w, 20, label, enabled);
}

// --------------------------------------------------------------------------
// Entrada tactil
// --------------------------------------------------------------------------
int ui_hit(int x, int y, int w, int h)
{
    if (w == 0 && h == 0) return 0;
    return ui_t.down && ui_t.x >= x && ui_t.x < x + w && ui_t.y >= y && ui_t.y < y + h;
}

int ui_clicked(int x, int y, int w, int h)
{
    if (!ui_t.released) return 0;
    int hit = ui_t.x >= x && ui_t.x < x + w && ui_t.y >= y && ui_t.y < y + h &&
              ui_t.px >= x && ui_t.px < x + w && ui_t.py >= y && ui_t.py < y + h;
    if (hit) sfx_click();
    return hit;
}

// ---------------------------------------------------------------------------
// Iconos de botones: ui_hint(cx, y, "{L} Break  {R} Place") -> icono + texto, centrado (fila de 16 px)
// ---------------------------------------------------------------------------
#define HINT_H 16

static void key_draw(const KeyIcon *k, int dx, int dy)
{
    for (int y = 0; y < k->h; y++) {
        int py = dy + y;
        if ((unsigned)py >= UI_H) continue;
        const u8 *src = &key_sheet[(k->y + y) * KEY_SHEET_W + k->x];
        for (int x = 0; x < k->w; x++) {
            int px = dx + x;
            if (src[x] && (unsigned)px < UI_W) ui_buf[py * UI_W + px] = src[x];
        }
    }
}

static int hint_run(int x, int y, const char *s, int color, int draw)
{
    char seg[48];
    int w = 0;
    while (*s) {
        if (*s == '{') {
            const char *e = strchr(s, '}');
            if (!e) break;
            for (int i = 0; i < KEY_ICON_COUNT; i++) {
                const KeyIcon *k = &key_icons[i];
                if ((int)strlen(k->name) == e - s - 1 && !strncmp(k->name, s + 1, e - s - 1)) {
                    if (draw) key_draw(k, x + w, y + (HINT_H - k->h) / 2);
                    w += k->w;
                    break;
                }
            }
            s = e + 1;
        } else {
            int n = 0;
            while (s[n] && s[n] != '{' && n < 47) { seg[n] = s[n]; n++; }
            seg[n] = 0;
            if (draw) ui_text(x + w, y + (HINT_H - 8) / 2, seg, color, 0);
            w += ui_text_width(seg);
            s += n;
        }
    }
    return w;
}

void ui_hint(int cx, int y, const char *fmt, int color)
{
    if (!ui_draw_on) return;
    hint_run(cx - hint_run(0, y, fmt, color, 0) / 2, y, fmt, color, 1);
}
