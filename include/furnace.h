// Hornos: estado por bloque (combustible, fundicion) y ciclo de fundir como en Minecraft 1.7.10
#ifndef FURNACE_H
#define FURNACE_H

#include <nds.h>
#include "player.h"

#define MAX_FURN 16
#define SMELT_TICKS 600          // 200 ticks de MC (10 s) a 60 Hz

typedef struct {
    u8  used, y;
    u16 x, z;
    u16 burn, burn_max;          // ticks de combustible que quedan / del combustible actual
    u16 cook;                    // progreso de la fundicion (0..SMELT_TICKS)
    Slot in, fuel, out;
} Furnace;

extern Furnace g_furn[MAX_FURN];

void furnace_clear(void);
int  furnace_add(int x, int y, int z);          // registra un horno recien puesto; indice o -1
int  furnace_find(int x, int y, int z);         // indice o -1
int  furnace_get(int x, int y, int z);          // find + crea el registro si falta (mundo antiguo)
void furnace_remove(int x, int y, int z, int drop_contents);   // al romper el bloque
void furnace_update(void);                      // 1 tick (60 Hz): consume combustible y funde
int  furnace_can_smelt(const Furnace *f);

#endif
