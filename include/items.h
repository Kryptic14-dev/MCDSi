// Items (herramientas y materiales) y recetas de crafteo
#ifndef ITEMS_H
#define ITEMS_H

#include <nds.h>
#include "blocks.h"

#define I_FIRST 64
enum {
    I_STICK = I_FIRST, I_COAL, I_IRON_INGOT, I_GOLD_INGOT, I_DIAMOND,
    // 5 materiales x 5 herramientas: id = I_WOOD_PICK + mat * 5 + clase
    I_WOOD_PICK, I_WOOD_AXE, I_WOOD_SHOVEL, I_WOOD_SWORD, I_WOOD_HOE,
    I_STONE_PICK, I_STONE_AXE, I_STONE_SHOVEL, I_STONE_SWORD, I_STONE_HOE,
    I_IRON_PICK, I_IRON_AXE, I_IRON_SHOVEL, I_IRON_SWORD, I_IRON_HOE,
    I_GOLD_PICK, I_GOLD_AXE, I_GOLD_SHOVEL, I_GOLD_SWORD, I_GOLD_HOE,
    I_DIAMOND_PICK, I_DIAMOND_AXE, I_DIAMOND_SHOVEL, I_DIAMOND_SWORD, I_DIAMOND_HOE,
    // 5 materiales x 4 piezas: id = I_LEATHER_HELMET + mat * 4 + pieza
    I_LEATHER_HELMET, I_LEATHER_CHEST, I_LEATHER_LEGS, I_LEATHER_BOOTS,
    I_CHAIN_HELMET, I_CHAIN_CHEST, I_CHAIN_LEGS, I_CHAIN_BOOTS,
    I_IRON_HELMET, I_IRON_CHEST, I_IRON_LEGS, I_IRON_BOOTS,
    I_GOLD_HELMET, I_GOLD_CHEST, I_GOLD_LEGS, I_GOLD_BOOTS,
    I_DIAMOND_HELMET, I_DIAMOND_CHEST, I_DIAMOND_LEGS, I_DIAMOND_BOOTS,
    // comida, drops de mobs y arco
    I_PORK_RAW, I_PORK_COOKED, I_BEEF_RAW, I_BEEF_COOKED, I_CHICKEN_RAW, I_CHICKEN_COOKED,
    I_LEATHER, I_FEATHER, I_BONE, I_STRING, I_GUNPOWDER, I_ROTTEN_FLESH, I_SPIDER_EYE, I_ARROW,
    I_BOW,
    // reservados: sus iconos son los fotogramas del arco tensado (icon_index(I_BOW) + 1..3), no son items reales
    I_BOW_RES1, I_BOW_RES2, I_BOW_RES3,
    I_BED,
    I_END
};
#define I_TOOL_END (I_DIAMOND_HOE + 1)
#define ITEM_COUNT (I_END - I_FIRST)

enum { TK_PICK, TK_AXE, TK_SHOVEL, TK_SWORD, TK_HOE };
enum { TM_WOOD, TM_STONE, TM_IRON, TM_GOLD, TM_DIAMOND };
enum { AP_HELMET, AP_CHEST, AP_LEGS, AP_BOOTS };
enum { AM_LEATHER, AM_CHAIN, AM_IRON, AM_GOLD, AM_DIAMOND };

static inline int id_is_item(u8 id) { return id >= I_FIRST; }

// indice de icono en las hojas (bloques 0..B_COUNT-1, items a continuacion)
static inline int icon_index(u8 id) { return id < B_COUNT ? id : B_COUNT + (id - I_FIRST); }

const char *item_name(u8 id);
int  item_max_stack(u8 id);              // 1 para herramientas y arco
int  item_max_dmg(u8 id);                // usos; 0 = sin durabilidad
int  tool_kind(u8 id);                   // TK_* o -1
int  tool_tier(u8 id);                   // 0 madera/oro, 1 piedra, 2 hierro, 3 diamante
int  tool_speed(u8 id);                  // multiplicador de velocidad de picado

// armadura
int  armor_piece(u8 id);                 // AP_* o -1 si no es armadura
int  armor_defense(u8 id);               // puntos de armadura (cada uno reduce el dano un 4 %)
int  armor_mat(u8 id);                   // AM_* o -1

// combate
int  attack_damage(u8 id);               // dano (medios corazones) al golpear con este item

// comida (food_value > 0 = se puede comer). sat = saturacion x100
int  food_value(u8 id, int *sat);

// horno
int  fuel_time(u8 id);                   // ticks a 60 Hz que arde (0 = no es combustible)
int  smelt_result(u8 id);                // item resultante de fundir (0 = no se funde)

// herramienta efectiva (TK_* o -1) y nivel minimo para obtener el drop (-1 = cualquiera)
void block_tool_req(u8 block, int *kind, int *tier);

// Receta para una cuadricula 3x3 (ids; 0 = vacio). Devuelve 1 si hay resultado.
int craft_match(const u8 grid[9], int *out_id, int *out_n);

#endif
