// Parte dependiente del hardware de la pantalla tactil (sub motor, bitmap de 8 bits)
#include <string.h>
#include <stdlib.h>
#include "ui.h"
#include "assets.h"

static int g_bg;

void ui_hw_init(void)
{
    videoSetModeSub(MODE_5_2D | DISPLAY_BG3_ACTIVE);
    // H (32 KB) + I (16 KB) = 48 KB contiguos = 256x192 a 8 bits
    vramSetBankH(VRAM_H_SUB_BG);
    vramSetBankI(VRAM_I_SUB_BG_0x06208000);
    g_bg = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 0, 0);

    // la paleta embebida puede no estar alineada: copiamos a un buffer
    u16 *pal = (u16 *)malloc(512);
    memcpy(pal, ui2dpal_bin, 512);
    dmaCopy(pal, BG_PALETTE_SUB, 512);
    free(pal);

    memset(ui_buf, 0, sizeof(ui_buf));
    ui_present();
}

void ui_present(void)
{
    dmaCopy(ui_buf, bgGetGfxPtr(g_bg), UI_W * UI_H);
}

void ui_poll(void)
{
    touchPosition tp;
    u32 held = keysHeld();
    int was_down = ui_t.down;
    int lastx = ui_t.x, lasty = ui_t.y;

    ui_t.pressed = ui_t.released = 0;
    ui_t.dx = ui_t.dy = 0;
    if (held & KEY_TOUCH) {
        touchRead(&tp);
        if (!was_down) {
            ui_t.pressed = 1;
            ui_t.px = tp.px;
            ui_t.py = tp.py;
            ui_t.moved = 0;
        } else {
            ui_t.dx = tp.px - lastx;
            ui_t.dy = tp.py - lasty;
            if (abs(tp.px - ui_t.px) > 4 || abs(tp.py - ui_t.py) > 4) ui_t.moved = 1;
        }
        ui_t.x = tp.px;
        ui_t.y = tp.py;
        ui_t.down = 1;
    } else {
        if (was_down) ui_t.released = 1;
        ui_t.down = 0;
    }
}
