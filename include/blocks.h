// Definicion de bloques (equivalentes a los de Minecraft 1.7.10)
#ifndef BLOCKS_H
#define BLOCKS_H

#include <nds.h>

enum {
    B_AIR, B_STONE, B_GRASS, B_DIRT, B_COBBLE, B_PLANKS, B_BEDROCK, B_WATER,
    B_SAND, B_GRAVEL, B_GOLD_ORE, B_IRON_ORE, B_COAL_ORE, B_LOG, B_LEAVES,
    B_GLASS, B_SANDSTONE, B_DIAMOND_ORE, B_BRICK, B_STONEBRICK, B_CRAFTING,
    B_SNOW, B_TALLGRASS, B_DANDELION, B_ROSE, B_WOOL_WHITE, B_WOOL_RED,
    B_WOOL_BLUE, B_WOOL_GREEN, B_WOOL_YELLOW, B_WOOL_BLACK, B_MOSSY,
    B_OBSIDIAN, B_BOOKSHELF,
    // Horno: 4 orientaciones x apagado/encendido (el mundo no guarda metadatos, asi que la
    // orientacion y el estado van en el propio id). La cara frontal mira a -Z, +X, +Z, -X.
    B_FURNACE, B_FURNACE_E, B_FURNACE_S, B_FURNACE_W,
    B_FURNACE_LIT, B_FURNACE_LIT_E, B_FURNACE_LIT_S, B_FURNACE_LIT_W,
    // Cofre: 4 orientaciones (0: -Z, 1: +X, 2: +Z, 3: -X). Se dibuja como modelo 3D (14x14x14 o 30x14x14 doble).
    B_CHEST, B_CHEST_E, B_CHEST_S, B_CHEST_W,
    // Antorcha: 0 = suelo, 1 = pared -X (inclinada a +X), 2 = pared -Z (a +Z), 3 = pared +X (a -X), 4 = pared +Z (a -Z)
    B_TORCH, B_TORCH_E, B_TORCH_S, B_TORCH_W, B_TORCH_N,
    // Cama (2 bloques, 9/16 de alto). El PIE lleva la orientacion = direccion hacia la cabeza (0: -Z, 1: +X, 2: +Z, 3: -X);
    // la cabeza es un unico id y su orientacion se deduce del pie vecino que apunta a ella (ver bed_facing() en world.h).
    B_BED_HEAD, B_BED_FOOT, B_BED_FOOT_E, B_BED_FOOT_S, B_BED_FOOT_W,
    B_COUNT
};

#define BLK_IS_FURNACE(b)     ((b) >= B_FURNACE && (b) <= B_FURNACE_LIT_W)
#define BLK_FURNACE_LIT(b)    ((b) >= B_FURNACE_LIT && (b) <= B_FURNACE_LIT_W)
#define BLK_FURNACE_FACING(b) (((b) - B_FURNACE) & 3)

#define BLK_IS_CHEST(b)       ((b) >= B_CHEST && (b) <= B_CHEST_W)
#define BLK_CHEST_FACING(b)   (((b) - B_CHEST) & 3)

#define BLK_IS_BED(b)         ((b) >= B_BED_HEAD && (b) <= B_BED_FOOT_W)
#define BLK_IS_BED_FOOT(b)    ((b) >= B_BED_FOOT && (b) <= B_BED_FOOT_W)
#define BLK_BED_FOOT_DIR(b)   ((b) - B_BED_FOOT)

#define BLK_IS_TORCH(b)       ((b) >= B_TORCH && (b) <= B_TORCH_N)
#define BLK_TORCH_DIR(b)      ((b) - B_TORCH)

// Direcciones de cara
enum { D_PX, D_NX, D_PY, D_NY, D_PZ, D_NZ };

#define F_SOLID   1   // colisiona con el jugador
#define F_OPAQUE  2   // oculta las caras vecinas
#define F_LIQUID  4
#define F_PLANT   8   // se dibuja como cruz y es reemplazable
#define F_CUTOUT  16  // transparencia 1 bit (cristal)

typedef struct {
    const char *name;
    u8 tex[6];       // textura por cara (indice en el atlas)
    u8 flags;
    u16 hardness;    // dureza x10 (como en Minecraft)
    u8 drop;         // bloque que suelta al romperse (B_AIR = nada)
} BlockInfo;

extern const BlockInfo g_blocks[B_COUNT];

static inline int blk_solid(u8 b)  { return g_blocks[b].flags & F_SOLID; }
static inline int blk_opaque(u8 b) { return g_blocks[b].flags & F_OPAQUE; }

#endif
