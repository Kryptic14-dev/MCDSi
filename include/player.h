#ifndef PLAYER_H
#define PLAYER_H

#include <nds.h>
#include "world.h"

// Punto fijo: 4096 = 1 bloque
#define FP 4096

// Duracion de la animacion de golpe (ticks a 60 Hz). Minecraft: 6 ticks de 20 Hz = 18
#define SWING_LEN 18
// Aire maximo bajo el agua (ticks a 60 Hz). Minecraft: 300 ticks de 20 Hz = 900 (15 s)
#define MAX_AIR   900
// Duracion de la sacudida de camara al recibir dano (ticks a 60 Hz). Minecraft: 10 ticks de 20 Hz = 20
#define HURT_LEN  20

typedef struct { u8 id; u8 count; u16 dmg; } Slot;   // dmg = usos gastados (herramientas)

typedef struct {
    int x, y, z;          // posicion de los pies (centro)
    int vx, vy, vz;
    int yaw, pitch;       // angulos de 15 bits (32768 = 360 grados)
    int on_ground, flying, sneaking, sprinting, in_water, head_water;
    Slot inv[36];         // 0..8 hotbar, 9..35 inventario
    int sel;              // slot activo
    int gamemode;         // 0 survival, 1 creative
    int health, food;     // 0..20 (medios corazones / medios muslos); solo se muestran en survival
    int air;              // 0..MAX_AIR (oxigeno bajo el agua)
    int hurt_t;           // 0..HURT_LEN (inclinacion de camara al recibir dano)
    int death_t;          // ticks transcurridos desde la muerte (pantalla de muerte)
    int cam_mode;         // 0 primera persona, 1 tercera (detras), 2 tercera (delante)
    int walk_dist;        // para animar a Steve
    int walk_speed;       // velocidad horizontal actual (animacion)
    int swing;            // animacion del golpe
    int limb_ph, limb_amt;   // fase (15 bits) y amplitud (4.12) de la marcha, como limbSwing / limbSwingAmount
    int age;              // ticks a 60 Hz desde que existe (balanceo de brazos en reposo)
    int body_yaw;         // orientacion del cuerpo (renderYawOffset); la cabeza gira respecto a ella
    // bloque apuntado
    int has_target, tx, ty, tz, tface;
    int place_x, place_y, place_z;
    // rotura
    int brk_x, brk_y, brk_z, brk_prog, brk_total;
    int act_cd;           // cooldown de picar/poner
    int want_craft;       // R sobre una mesa de crafteo: abrir la cuadricula 3x3
    int last_a_frame, last_fwd_frame, fwd_was_down;
    // supervivencia
    Slot armor[4];        // casco, peto, pantalones, botas
    int invuln;           // ticks (60 Hz) sin recibir dano tras un golpe
    int food_sat, food_exh;   // saturacion y agotamiento x100
    int regen_t;          // temporizador de regeneracion / hambre
    int eat_t;            // progreso de comer (ticks)
    int bow_t;            // progreso de tensar el arco (ticks a 60 Hz)
    int fall_y;           // altura maxima desde que dejo el suelo
    int msg_t;            // ticks que se muestra "You died!"
    int want_furnace;     // R sobre un horno
    int want_chest;       // R sobre un cofre
    int fx, fy, fz;       // coordenadas del horno / cofre apuntado
    int target_dist;      // distancia al bloque apuntado (4.12)
    int mob_target;       // mob apuntado (indice) o -1
    // cama
    int sleep_t;          // >0: durmiendo (ticks a 60 Hz desde que se acosto)
    int sleep_ov;         // oscurecimiento de la pantalla (sleepTimer de MC, en ticks a 60 Hz): sube al dormir y baja al despertar
    int sleep_x, sleep_y, sleep_z;   // celda de la cama donde duerme
    int has_bed, bed_x, bed_y, bed_z; // punto de reaparicion (cama donde durmio por ultima vez)
    int tip_t;            // ticks que se muestra tip_msg (pantalla tactil)
    const char *tip_msg;  // mensaje breve ("You can only sleep at night", ...)
} Player;

// Dormir, como Minecraft 1.7.10 (EntityPlayer.sleepTimer / GuiIngame):
//  - 100 ticks de MC (5 s) acostado = 300 ticks a 60 Hz: entonces se salta la noche y Steve se levanta.
//  - Mientras tanto la pantalla se oscurece poco a poco hasta 220/255 de opacidad con el color 0x101020.
//  - Al despertar el velo se aclara en 10 ticks de MC (0,5 s) = 30 ticks a 60 Hz.
#define SLEEP_FULL    300
#define SLEEP_FADEOUT 30
int  player_sleep_alpha(const Player *p);           // opacidad del velo de dormir (0..31, escala de la DS)

void player_init(Player *p, int gamemode);
void player_update(Player *p, u32 keys_held, u32 keys_down);
void player_look(Player *p, int dx, int dy);
void player_eye(const Player *p, int *ex, int *ey, int *ez);
void player_forward(const Player *p, int *fx, int *fy, int *fz);  // 4.12
int  inv_add(Player *p, u8 id, int n);
int  inv_add_slot(Player *p, Slot s);   // conserva la durabilidad
int  inv_give(Player *p, Slot s);       // devuelve las unidades que NO caben
void player_drop_slot(Player *p, Slot s); // suelta el slot al suelo (item drop)
void player_drop_all(Player *p);        // suelta todo el inventario y armadura en el lugar de la muerte
u8   player_held(const Player *p);
// Dano al jugador (medios corazones). (sx, sz) = origen del golpe (retroceso). bypass_armor: caidas, hambre.
void player_hurt(Player *p, int dmg, int sx, int sz, int bypass_armor);
int  player_armor_points(const Player *p);
void player_respawn(Player *p);
int  player_equip_armor(Player *p, int inv_slot);   // equipa la pieza del slot (intercambia); 1 = ok
void player_wake(Player *p);                        // se levanta de la cama (si dormia)

#endif
