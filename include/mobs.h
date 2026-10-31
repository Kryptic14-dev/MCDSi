// Mobs: animales pasivos y monstruos con IA parecida a la de Minecraft 1.7.10
#ifndef MOBS_H
#define MOBS_H

#include <nds.h>
#include "player.h"

#define MAX_MOBS   28
#define MAX_ARROWS 16

enum { MT_PIG, MT_COW, MT_SHEEP, MT_CHICKEN, MT_ZOMBIE, MT_SKELETON, MT_CREEPER, MT_SPIDER, MT_COUNT };

typedef struct {
    u8 active, type;
    u8 on_ground, in_water;
    s16 hp;
    int x, y, z;            // pies (centro), 4.12
    int vx, vy, vz;         // velocidad propia (4.12 por tick de 60 Hz)
    int kx, kz;             // retroceso por golpes
    int yaw;                // 15 bits, 0 = mira a -Z
    int walk_dist, walk_speed;
    u16 age;
    u16 invuln;             // ticks sin poder ser herido
    u16 hurt_t;             // ticks de tinte rojo
    u16 swing;              // animacion de ataque
    u16 atk_cd;
    u16 ai_t;               // temporizador de la IA (caminar / esperar)
    u16 ai_check;           // ticks hasta la siguiente comprobacion de objetivo
    int ai_yaw;
    u8 ai_mode;             // 0 quieto, 1 camina
    u8 target;              // 1 = persigue al jugador
    u8 provoked;            // fue atacado (las aranas de dia solo atacan asi)
    u16 panic;              // ticks huyendo
    u16 fuse;               // creeper: progreso de la mecha
    u16 burn;               // ticks ardiendo
    u16 snd_t;
    u8 dying;
    u16 die_t;
    u8 shoot_cd;
} Mob;

typedef struct {
    u8 active, stuck, from_player, dmg;
    int x, y, z;
    int vx, vy, vz;
    int yaw, pitch;
    u16 age;
} Arrow;

extern Mob g_mobs[MAX_MOBS];
extern Arrow g_arrows[MAX_ARROWS];

void mobs_clear(void);
void mobs_populate(const Player *p);                 // animales iniciales alrededor del jugador
void mobs_update(Player *p);                         // 1 tick (60 Hz)
int  mobs_target(const Player *p, int *dist);        // mob apuntado (indice) o -1; dist en 4.12
void mob_hurt(int idx, int dmg, int kbx, int kbz, Player *p);   // kb = direccion unitaria 4.12
int  mob_is_hostile(int type);
void mobs_explode(Player *p, int x, int y, int z, int power);
void mobs_player_shoot_arrow(const Player *p, int bow_t);    // TNT / creeper (x, y, z en 4.12)

// geometria para el renderizado
int  mob_half_width(int type);
int  mob_height(int type);

// utilidades matematicas compartidas
int  atan2_a(int y, int x);                         // angulo 15 bits del vector (x, y)

#endif
