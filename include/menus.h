#ifndef MENUS_H
#define MENUS_H

#include "player.h"

// Cada funcion dibuja la pantalla tactil en ui_buf y procesa el lapiz.
void menu_title(void);
void menu_select(void);
void menu_create(void);
void menu_options(void);
void menu_pause(void);
void menu_death(Player *p);
void menu_loading(int pct);
void menu_inventory(Player *p);
void menu_inventory_close(Player *p);   // devuelve la pila del cursor al inventario
// HUD tactil durante la partida; devuelve 1 = abrir inventario, 2 = abrir menu
int  menu_play_hud(Player *p);
// Combina el boton tactil de salto con las teclas (llamar antes de player_update)
void menu_touch_controls(u32 *held, u32 *down);

u32  menu_play_sig(const Player *p);   // firma del HUD tactil: si no cambia, no hace falta redibujarlo
extern int g_craft_table;     // 1 = mesa de crafteo 3x3 abierta
extern int g_furnace_open;    // indice del horno abierto (-1 = ninguno)
extern int g_chest_open;      // indice del cofre abierto (-1 = ninguno)
extern int g_chest_open2;     // segundo cofre si es un cofre doble (-1 = cofre simple)
extern int g_request_start;   // el boton "Create New World" fue pulsado
extern int g_request_load;    // cargar el mundo guardado g_load_name
extern char g_load_name[32];
extern int g_request_title;   // volver al titulo
extern int g_select_err;       // 1 = el mundo elegido no se pudo abrir (se avisa en la lista)

#endif
