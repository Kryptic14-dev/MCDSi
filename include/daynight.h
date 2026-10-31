// Ciclo de dia y noche (24000 ticks de Minecraft = 20 minutos; el juego corre a 60 Hz: 1 tick MC = 3 ticks)
#ifndef DAYNIGHT_H
#define DAYNIGHT_H

#include <nds.h>

extern int g_daytime;               // 0..23999 (0 amanecer, 6000 mediodia, 12000 atardecer, 18000 medianoche)

void daynight_set(int t);
void daynight_tick(void);           // 1 tick a 60 Hz
int  daynight_light(void);          // brillo del mundo 12..31 (escala de color de la DS)
int  daynight_is_night(void);       // 1 = noche cerrada (los mobs hostiles salen en la superficie)
void daynight_sky(int *r, int *g, int *b);   // color del cielo (0..31)

#endif
