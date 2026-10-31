#include "blocks.h"
#include "tiles.h"
#include "items.h"

#define ALL(t) {t,t,t,t,t,t}
#define SIDES(side, top, bot) {side, side, top, bot, side, side}

#define SOLID_OPAQUE (F_SOLID | F_OPAQUE)

// Horno / cofre: front = textura de la cara frontal, f = orientacion (0: -Z, 1: +X, 2: +Z, 3: -X)
// orden de caras: +X, -X, +Y, -Y, +Z, -Z
#define FURN(front, f) { (f) == 1 ? (front) : T_FURNACE_SIDE, (f) == 3 ? (front) : T_FURNACE_SIDE, \
                         T_FURNACE_TOP, T_FURNACE_TOP, \
                         (f) == 2 ? (front) : T_FURNACE_SIDE, (f) == 0 ? (front) : T_FURNACE_SIDE }

#define CHST(f) { (f) == 1 ? T_CHEST_FRONT : T_CHEST_SIDE, (f) == 3 ? T_CHEST_FRONT : T_CHEST_SIDE, \
                  T_CHEST_TOP, T_CHEST_TOP, \
                  (f) == 2 ? T_CHEST_FRONT : T_CHEST_SIDE, (f) == 0 ? T_CHEST_FRONT : T_CHEST_SIDE }

const BlockInfo g_blocks[B_COUNT] = {
    [B_AIR]         = {"Air",            ALL(0), 0, 0, B_AIR},
    [B_STONE]       = {"Stone",          ALL(T_STONE), SOLID_OPAQUE, 15, B_COBBLE},
    [B_GRASS]       = {"Grass Block",    SIDES(T_GRASS_SIDE, T_GRASS_TOP, T_DIRT), SOLID_OPAQUE, 6, B_DIRT},
    [B_DIRT]        = {"Dirt",           ALL(T_DIRT), SOLID_OPAQUE, 5, B_DIRT},
    [B_COBBLE]      = {"Cobblestone",    ALL(T_COBBLE), SOLID_OPAQUE, 20, B_COBBLE},
    [B_PLANKS]      = {"Oak Planks",     ALL(T_PLANKS), SOLID_OPAQUE, 20, B_PLANKS},
    [B_BEDROCK]     = {"Bedrock",        ALL(T_BEDROCK), SOLID_OPAQUE, 0xFFFF, B_AIR},
    [B_WATER]       = {"Water",          ALL(T_WATER), F_LIQUID, 0xFFFF, B_AIR},
    [B_SAND]        = {"Sand",           ALL(T_SAND), SOLID_OPAQUE, 5, B_SAND},
    [B_GRAVEL]      = {"Gravel",         ALL(T_GRAVEL), SOLID_OPAQUE, 6, B_GRAVEL},
    [B_GOLD_ORE]    = {"Gold Ore",       ALL(T_GOLD_ORE), SOLID_OPAQUE, 30, B_GOLD_ORE},
    [B_IRON_ORE]    = {"Iron Ore",       ALL(T_IRON_ORE), SOLID_OPAQUE, 30, B_IRON_ORE},
    [B_COAL_ORE]    = {"Coal Ore",       ALL(T_COAL_ORE), SOLID_OPAQUE, 30, I_COAL},
    [B_LOG]         = {"Oak Wood",       SIDES(T_LOG_SIDE, T_LOG_TOP, T_LOG_TOP), SOLID_OPAQUE, 20, B_LOG},
    [B_LEAVES]      = {"Oak Leaves",     ALL(T_LEAVES), SOLID_OPAQUE, 2, B_AIR},
    [B_GLASS]       = {"Glass",          ALL(T_GLASS), F_SOLID | F_CUTOUT, 3, B_AIR},
    [B_SANDSTONE]   = {"Sandstone",      SIDES(T_SANDSTONE_SIDE, T_SANDSTONE_TOP, T_SANDSTONE_BOTTOM), SOLID_OPAQUE, 8, B_SANDSTONE},
    [B_DIAMOND_ORE] = {"Diamond Ore",    ALL(T_DIAMOND_ORE), SOLID_OPAQUE, 30, I_DIAMOND},
    [B_BRICK]       = {"Bricks",         ALL(T_BRICK), SOLID_OPAQUE, 20, B_BRICK},
    [B_STONEBRICK]  = {"Stone Bricks",   ALL(T_STONEBRICK), SOLID_OPAQUE, 15, B_STONEBRICK},
    // +X, -X, +Y, -Y, +Z, -Z  (la cara frontal esta en -X y -Z, como en el original)
    [B_CRAFTING]    = {"Crafting Table", {T_CRAFT_SIDE, T_CRAFT_FRONT, T_CRAFT_TOP, T_PLANKS, T_CRAFT_SIDE, T_CRAFT_FRONT}, SOLID_OPAQUE, 25, B_CRAFTING},
    [B_SNOW]        = {"Snow",           ALL(T_SNOW), SOLID_OPAQUE, 2, B_SNOW},
    [B_TALLGRASS]   = {"Tall Grass",     ALL(T_TALLGRASS), F_PLANT, 0, B_AIR},
    [B_DANDELION]   = {"Dandelion",      ALL(T_DANDELION), F_PLANT, 0, B_DANDELION},
    [B_ROSE]        = {"Poppy",          ALL(T_ROSE), F_PLANT, 0, B_ROSE},
    [B_WOOL_WHITE]  = {"White Wool",     ALL(T_WOOL_WHITE), SOLID_OPAQUE, 8, B_WOOL_WHITE},
    [B_WOOL_RED]    = {"Red Wool",       ALL(T_WOOL_RED), SOLID_OPAQUE, 8, B_WOOL_RED},
    [B_WOOL_BLUE]   = {"Blue Wool",      ALL(T_WOOL_BLUE), SOLID_OPAQUE, 8, B_WOOL_BLUE},
    [B_WOOL_GREEN]  = {"Green Wool",     ALL(T_WOOL_GREEN), SOLID_OPAQUE, 8, B_WOOL_GREEN},
    [B_WOOL_YELLOW] = {"Yellow Wool",    ALL(T_WOOL_YELLOW), SOLID_OPAQUE, 8, B_WOOL_YELLOW},
    [B_WOOL_BLACK]  = {"Black Wool",     ALL(T_WOOL_BLACK), SOLID_OPAQUE, 8, B_WOOL_BLACK},
    [B_MOSSY]       = {"Moss Stone",     ALL(T_MOSSY), SOLID_OPAQUE, 20, B_MOSSY},
    [B_OBSIDIAN]    = {"Obsidian",       ALL(T_OBSIDIAN), SOLID_OPAQUE, 500, B_OBSIDIAN},
    [B_BOOKSHELF]   = {"Bookshelf",      SIDES(T_BOOKSHELF, T_PLANKS, T_PLANKS), SOLID_OPAQUE, 15, B_BOOKSHELF},
    [B_FURNACE]     = {"Furnace", FURN(T_FURNACE_FRONT_OFF, 0), SOLID_OPAQUE, 35, B_FURNACE},
    [B_FURNACE_E]   = {"Furnace", FURN(T_FURNACE_FRONT_OFF, 1), SOLID_OPAQUE, 35, B_FURNACE},
    [B_FURNACE_S]   = {"Furnace", FURN(T_FURNACE_FRONT_OFF, 2), SOLID_OPAQUE, 35, B_FURNACE},
    [B_FURNACE_W]   = {"Furnace", FURN(T_FURNACE_FRONT_OFF, 3), SOLID_OPAQUE, 35, B_FURNACE},
    [B_FURNACE_LIT]   = {"Furnace", FURN(T_FURNACE_FRONT_ON, 0), SOLID_OPAQUE, 35, B_FURNACE},
    [B_FURNACE_LIT_E] = {"Furnace", FURN(T_FURNACE_FRONT_ON, 1), SOLID_OPAQUE, 35, B_FURNACE},
    [B_FURNACE_LIT_S] = {"Furnace", FURN(T_FURNACE_FRONT_ON, 2), SOLID_OPAQUE, 35, B_FURNACE},
    [B_FURNACE_LIT_W] = {"Furnace", FURN(T_FURNACE_FRONT_ON, 3), SOLID_OPAQUE, 35, B_FURNACE},
    // El cofre mide 14x14x14: es F_SOLID pero NO F_OPAQUE para que no oculte el suelo ni las paredes detras
    [B_CHEST]       = {"Chest", CHST(0), F_SOLID, 25, B_CHEST},
    [B_CHEST_E]     = {"Chest", CHST(1), F_SOLID, 25, B_CHEST},
    [B_CHEST_S]     = {"Chest", CHST(2), F_SOLID, 25, B_CHEST},
    [B_CHEST_W]     = {"Chest", CHST(3), F_SOLID, 25, B_CHEST},
    // Antorcha: no colisiona con el jugador ni oculta caras, se rompe al instante y suelta B_TORCH
    [B_TORCH]       = {"Torch", ALL(T_TORCH), F_CUTOUT, 0, B_TORCH},
    [B_TORCH_E]     = {"Torch", ALL(T_TORCH), F_CUTOUT, 0, B_TORCH},
    [B_TORCH_S]     = {"Torch", ALL(T_TORCH), F_CUTOUT, 0, B_TORCH},
    [B_TORCH_W]     = {"Torch", ALL(T_TORCH), F_CUTOUT, 0, B_TORCH},
    [B_TORCH_N]     = {"Torch", ALL(T_TORCH), F_CUTOUT, 0, B_TORCH},
    // Cama: solida (para colisiones de mobs y flechas) pero no opaca; se malla a mano en render.c (9/16 de alto)
    [B_BED_HEAD]    = {"Bed", ALL(T_BED_HEAD_TOP), F_SOLID, 2, I_BED},
    [B_BED_FOOT]    = {"Bed", ALL(T_BED_FEET_TOP), F_SOLID, 2, I_BED},
    [B_BED_FOOT_E]  = {"Bed", ALL(T_BED_FEET_TOP), F_SOLID, 2, I_BED},
    [B_BED_FOOT_S]  = {"Bed", ALL(T_BED_FEET_TOP), F_SOLID, 2, I_BED},
    [B_BED_FOOT_W]  = {"Bed", ALL(T_BED_FEET_TOP), F_SOLID, 2, I_BED},
};
