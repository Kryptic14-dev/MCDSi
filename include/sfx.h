// Efectos de sonido (PCM 8 bits embebido en data/sfx.bin, generado por tools/convert_sounds.py)
#ifndef SFX_H
#define SFX_H

#include <nds.h>

void sfx_init(void);
void sfx_play_group(int group, int vol);     // variante aleatoria (nunca la misma dos veces seguidas)
void sfx_dig(u8 block, int vol);             // romper / colocar un bloque
void sfx_step(u8 block, int vol);            // paso sobre un bloque
void sfx_hit(u8 block);                      // golpeo mientras se pica un bloque
void sfx_click(void);                        // boton de menu
void sfx_pop(void);                          // recoger un item
void sfx_splash(void);                       // entrar al agua
void sfx_swim(void);                         // nadar
void sfx_tool_break(void);                   // se rompe una herramienta
void sfx_hurt(void);                         // el jugador recibe dano
void sfx_eat(void);                          // mordisco al comer
void sfx_burp(void);                         // termina de comer
void sfx_chest_open(void);                   // abrir cofre
void sfx_chest_close(void);                  // cerrar cofre
// sonido con posicion: (dx, dz) = fuente relativa al jugador en 4.12, yaw = giro del jugador
void sfx_play_group_3d(int group, int vol, int dx, int dz, int yaw);
void sfx_step_3d(u8 block, int vol, int dx, int dz, int yaw);   // paso con el material del bloque (mobs sin sonido propio)

#endif
