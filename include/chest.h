// Cofres: almacenamiento (27 huecos por bloque, 54 al unirse dos cofres adyacentes) y animacion de tapa
#ifndef CHEST_H
#define CHEST_H

#include <nds.h>
#include "player.h"

#define MAX_CHEST        32
#define CHEST_SLOTS      27
#define CHEST_ANIM_TICKS 30      // 10 ticks de MC (0,5 s) a 60 Hz

typedef struct {
    u8   used, y;
    u16  x, z;
    u8   open_t;                 // 0 (cerrado) .. CHEST_ANIM_TICKS (abierto del todo, 90 grados)
    u8   pair_dir;               // 0 = cofre individual; 1 = +X, 2 = -X, 3 = +Z, 4 = -Z
    Slot slots[CHEST_SLOTS];
} Chest;

extern Chest g_chest[MAX_CHEST];

void chest_clear(void);
int  chest_find(int x, int y, int z);
int  chest_add(int x, int y, int z);
int  chest_get(int x, int y, int z);
void chest_remove(int x, int y, int z, int drop_contents);

// Enlaza dos cofres adyacentes como cofre doble.
void chest_link(int x1, int y, int z1, int x2, int z2);

// Si el cofre en (x, y, z) forma un cofre doble con un vecino adyacente, devuelve 1 y escribe (px, pz).
int  chest_partner(int x, int y, int z, int *px, int *pz);

// Calcula el bloque orientado para colocar un cofre en (x, y, z). Si hay un cofre individual vecino libre,
// escribe su posicion en (*jx, *jz) para unirlos en cofre doble; si al lado ya hay un cofre doble, deja
// colocar el nuevo cofre como un cofre individual (*jx = -1, *jz = -1).
int  chest_plan_place(int x, int y, int z, int fx, int fz, u8 *out_block, int *jx, int *jz);

// Devuelve 1 si el cofre (y su pareja si es doble) no tiene un bloque opaco encima y puede abrirse.
int  chest_can_open(int x, int y, int z);

// Obtiene el indice principal y secundario (o -1 si es simple) al abrir el cofre en (x, y, z).
int  chest_open_pair(int x, int y, int z, int *c1, int *c2);

// Avanza 1 tick (60 Hz) la animacion de apertura/cierre de las tapas y reproduce chestclosed.
void chest_update(int open_c1, int open_c2);

#endif
