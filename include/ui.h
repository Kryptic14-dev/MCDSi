// Interfaz 2D por software para la pantalla tactil (pantalla inferior, 8 bits)
#ifndef MCDS_UI_H
#define MCDS_UI_H

#include <nds.h>
#include "ui_layout.h"
#include "player.h"

#define UI_W 256
#define UI_H 192

typedef struct {
    int down;       // el lapiz esta apoyado
    int pressed;    // acaba de apoyarse este frame
    int released;   // acaba de levantarse este frame
    int x, y;       // posicion actual (o ultima conocida)
    int px, py;     // donde empezo el toque
    int dx, dy;     // movimiento respecto al frame anterior
    int moved;      // se ha movido mas de un umbral desde que se apoyo
} TouchState;

extern u8 ui_buf[UI_W * UI_H];
extern TouchState ui_t;
extern int ui_draw_on;   // 0 = las funciones de dibujo no hacen nada (la entrada tactil sigue funcionando)

// hardware (ui_hw.c)
void ui_hw_init(void);
void ui_present(void);
void ui_poll(void);

// dibujo (ui.c)
void ui_begin(void);                       // fondo de tierra
void ui_blit(int sx, int sy, int w, int h, int dx, int dy);
void ui_icon(int block, int dx, int dy);
void ui_slot(const Slot *s, int dx, int dy, int show_count);   // icono + cantidad + durabilidad
void ui_rect(int x, int y, int w, int h, int color);
void ui_text(int x, int y, const char *s, int color, int shadow);
void ui_text_center(int cx, int y, const char *s, int color, int shadow);
void ui_text_right(int rx, int y, const char *s, int color, int shadow);
int  ui_text_width(const char *s);
void ui_hint(int cx, int y, const char *fmt, int color);   // "{A} Jump  {START} Pause": iconos de botones (A B X Y L R START SELECT DPAD)
void ui_button(int x, int y, int w, const char *label, int enabled);
void ui_button_h(int x, int y, int w, int h, const char *label, int enabled);

// entrada
int ui_hit(int x, int y, int w, int h);          // el lapiz esta sobre el rectangulo
int ui_clicked(int x, int y, int w, int h);      // toque completo (apoyar y levantar dentro)

#endif
