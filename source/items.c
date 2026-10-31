#include <string.h>
#include <stdlib.h>
#include "items.h"

static const char *k_names[5] = {"Pickaxe", "Axe", "Shovel", "Sword", "Hoe"};
static const char *m_names[5] = {"Wooden", "Stone", "Iron", "Golden", "Diamond"};
static const int m_dur[5]   = {59, 131, 250, 32, 1561};
static const int m_tier[5]  = {0, 1, 2, 0, 3};
static const int m_speed[5] = {2, 4, 6, 12, 8};

const char *item_name(u8 id)
{
    static char buf[24];
    switch (id) {
        case I_STICK: return "Stick";
        case I_COAL: return "Coal";
        case I_IRON_INGOT: return "Iron Ingot";
        case I_GOLD_INGOT: return "Gold Ingot";
        case I_DIAMOND: return "Diamond";
    }
    if (id >= I_WOOD_PICK && id < I_TOOL_END) {
        int t = id - I_WOOD_PICK;
        strcpy(buf, m_names[t / 5]);
        strcat(buf, " ");
        strcat(buf, k_names[t % 5]);
        return buf;
    }
    if (id >= I_LEATHER_HELMET && id <= I_DIAMOND_BOOTS) {
        static const char *am[5] = {"Leather", "Chain", "Iron", "Golden", "Diamond"};
        static const char *ap[4] = {"Helmet", "Chestplate", "Leggings", "Boots"};
        int t = id - I_LEATHER_HELMET;
        strcpy(buf, am[t / 4]);
        strcat(buf, " ");
        strcat(buf, ap[t % 4]);
        return buf;
    }
    switch (id) {
        case I_PORK_RAW: return "Raw Porkchop";
        case I_PORK_COOKED: return "Cooked Porkchop";
        case I_BEEF_RAW: return "Raw Beef";
        case I_BEEF_COOKED: return "Steak";
        case I_CHICKEN_RAW: return "Raw Chicken";
        case I_CHICKEN_COOKED: return "Cooked Chicken";
        case I_LEATHER: return "Leather";
        case I_FEATHER: return "Feather";
        case I_BONE: return "Bone";
        case I_STRING: return "String";
        case I_GUNPOWDER: return "Gunpowder";
        case I_ROTTEN_FLESH: return "Rotten Flesh";
        case I_SPIDER_EYE: return "Spider Eye";
        case I_ARROW: return "Arrow";
        case I_BOW: return "Bow";
        case I_BED: return "Bed";
    }
    return id < B_COUNT ? g_blocks[id].name : "?";
}

int tool_kind(u8 id) { return (id >= I_WOOD_PICK && id < I_TOOL_END) ? (id - I_WOOD_PICK) % 5 : -1; }
static int tool_mat(u8 id) { return (id - I_WOOD_PICK) / 5; }
int tool_tier(u8 id) { return tool_kind(id) < 0 ? -1 : m_tier[tool_mat(id)]; }
int tool_speed(u8 id) { return tool_kind(id) < 0 ? 1 : m_speed[tool_mat(id)]; }
// armadura: puntos de defensa y durabilidad por pieza (casco, peto, pantalones, botas)
static const int a_def[5][4] = {{1, 3, 2, 1}, {2, 5, 4, 1}, {2, 6, 5, 2}, {2, 5, 3, 1}, {3, 8, 6, 3}};
static const int a_dur_mul[5] = {5, 15, 15, 7, 33};
static const int a_dur_base[4] = {11, 16, 15, 13};

int armor_piece(u8 id) { return (id >= I_LEATHER_HELMET && id <= I_DIAMOND_BOOTS) ? (id - I_LEATHER_HELMET) & 3 : -1; }
int armor_mat(u8 id)   { return (id >= I_LEATHER_HELMET && id <= I_DIAMOND_BOOTS) ? (id - I_LEATHER_HELMET) >> 2 : -1; }
int armor_defense(u8 id)
{
    int p = armor_piece(id);
    return p < 0 ? 0 : a_def[armor_mat(id)][p];
}

int item_max_dmg(u8 id)
{
    if (id == I_BOW) return 384;
    if (tool_kind(id) >= 0) return m_dur[tool_mat(id)];
    int p = armor_piece(id);
    if (p >= 0) return a_dur_base[p] * a_dur_mul[armor_mat(id)];
    return 0;
}
int item_max_stack(u8 id) { return (tool_kind(id) < 0 && armor_piece(id) < 0 && id != I_BOW && id != I_BED) ? 64 : 1; }

// Dano de 1.7.10: espada 4 + bonus, pico 2 + bonus, hacha 3 + bonus, pala 1 + bonus (bonus del material:
// madera 0, piedra 1, hierro 2, oro 0, diamante 3); mano y resto de items = 1.
int attack_damage(u8 id)
{
    static const int bonus[5] = {0, 1, 2, 0, 3};
    int k = tool_kind(id);
    if (k < 0) return 1;
    int b = bonus[tool_mat(id)];
    switch (k) {
        case TK_SWORD: return 4 + b;
        case TK_AXE: return 3 + b;
        case TK_PICK: return 2 + b;
        case TK_SHOVEL: return 1 + b;
        default: return 1;
    }
}

int food_value(u8 id, int *sat)
{
    int f = 0, s = 0;
    switch (id) {
        case I_PORK_RAW: f = 3; s = 180; break;
        case I_PORK_COOKED: f = 8; s = 1280; break;
        case I_BEEF_RAW: f = 3; s = 180; break;
        case I_BEEF_COOKED: f = 8; s = 1280; break;
        case I_CHICKEN_RAW: f = 2; s = 120; break;
        case I_CHICKEN_COOKED: f = 6; s = 720; break;
        case I_ROTTEN_FLESH: f = 4; s = 80; break;
        case I_SPIDER_EYE: f = 2; s = 320; break;
    }
    if (sat) *sat = s;
    return f;
}

// Tiempos de 1.7.10 (ticks a 20 Hz) x 3 para el tick de 60 Hz del juego
int fuel_time(u8 id)
{
    switch (id) {
        case I_COAL: return 1600 * 3;
        case B_PLANKS: case B_LOG: case B_CRAFTING: case B_BOOKSHELF:
        case B_CHEST: case B_CHEST_E: case B_CHEST_S: case B_CHEST_W: return 300 * 3;
        case I_STICK: return 100 * 3;
        case I_WOOD_PICK: case I_WOOD_AXE: case I_WOOD_SHOVEL: case I_WOOD_SWORD: case I_WOOD_HOE:
        case I_BOW: return 200 * 3;
    }
    return 0;
}

int smelt_result(u8 id)
{
    switch (id) {
        case B_IRON_ORE: return I_IRON_INGOT;
        case B_GOLD_ORE: return I_GOLD_INGOT;
        case B_SAND: return B_GLASS;
        case B_COBBLE: return B_STONE;
        case I_PORK_RAW: return I_PORK_COOKED;
        case I_BEEF_RAW: return I_BEEF_COOKED;
        case I_CHICKEN_RAW: return I_CHICKEN_COOKED;
    }
    return 0;
}

void block_tool_req(u8 b, int *kind, int *tier)
{
    *kind = -1; *tier = -1;
    switch (b) {
        case B_STONE: case B_COBBLE: case B_SANDSTONE: case B_BRICK: case B_STONEBRICK: case B_MOSSY:
            *kind = TK_PICK; *tier = 0; break;
        case B_COAL_ORE: *kind = TK_PICK; *tier = 0; break;
        case B_IRON_ORE: *kind = TK_PICK; *tier = 1; break;
        case B_GOLD_ORE: case B_DIAMOND_ORE: *kind = TK_PICK; *tier = 2; break;
        case B_OBSIDIAN: *kind = TK_PICK; *tier = 3; break;
        case B_LOG: case B_PLANKS: case B_CRAFTING: case B_BOOKSHELF:
        case B_CHEST: case B_CHEST_E: case B_CHEST_S: case B_CHEST_W: *kind = TK_AXE; break;
        case B_FURNACE: case B_FURNACE_E: case B_FURNACE_S: case B_FURNACE_W:
        case B_FURNACE_LIT: case B_FURNACE_LIT_E: case B_FURNACE_LIT_S: case B_FURNACE_LIT_W:
            *kind = TK_PICK; *tier = 0; break;
        case B_DIRT: case B_GRASS: case B_SAND: case B_GRAVEL: case B_SNOW: *kind = TK_SHOVEL; break;
        default: break;
    }
}

// --------------------------------------------------------------------------
// Recetas
// --------------------------------------------------------------------------
typedef struct { u8 w, h; u8 in[9]; u8 out, n, shapeless; } Recipe;
#define MAX_RECIPES 128
static Recipe g_rec[MAX_RECIPES];
static int g_nrec;

static void add(int w, int h, const u8 *in, u8 out, u8 n, int shapeless)
{
    if (g_nrec >= MAX_RECIPES) return;
    Recipe *r = &g_rec[g_nrec++];
    memset(r, 0, sizeof(*r));
    r->w = (u8)w; r->h = (u8)h;
    memcpy(r->in, in, (size_t)(w * h));
    r->out = out; r->n = n; r->shapeless = (u8)shapeless;
}

static void init_recipes(void)
{
    g_nrec = 0;
    const u8 P = B_PLANKS, S = I_STICK;
    { u8 a[] = {B_LOG}; add(1, 1, a, P, 4, 1); }
    { u8 a[] = {P, P}; add(1, 2, a, S, 4, 0); }
    { u8 a[] = {I_COAL, S}; add(1, 2, a, B_TORCH, 4, 0); }
    { u8 a[] = {P, P, P, P}; add(2, 2, a, B_CRAFTING, 1, 0); }
    { u8 a[] = {B_SAND, B_SAND, B_SAND, B_SAND}; add(2, 2, a, B_SANDSTONE, 1, 0); }
    { u8 a[] = {B_STONE, B_STONE, B_STONE, B_STONE}; add(2, 2, a, B_STONEBRICK, 4, 0); }
    { u8 a[] = {B_COBBLE, B_COBBLE, B_COBBLE, B_COBBLE}; add(2, 2, a, B_MOSSY, 1, 0); }
    // horno: 8 cobblestone en anillo (la fundicion se hace ahora en el propio horno)
    { u8 C = B_COBBLE; u8 a[] = {C, C, C, C, 0, C, C, C, C}; add(3, 3, a, B_FURNACE, 1, 0); }
    // cofre: 8 tablones de madera en anillo
    { u8 a[] = {P, P, P, P, 0, P, P, P, P}; add(3, 3, a, B_CHEST, 1, 0); }
    // arco: 3 palos + 3 cuerdas (craft_match admite espejo horizontal)
    { u8 ST = I_STRING; u8 a[] = {0, S, ST, S, 0, ST, 0, S, ST}; add(3, 3, a, I_BOW, 1, 0); }
    // cama: 3 lanas (de cualquier color) + 3 tablones
    {
        static const u8 wools[6] = {B_WOOL_WHITE, B_WOOL_RED, B_WOOL_BLUE, B_WOOL_GREEN, B_WOOL_YELLOW, B_WOOL_BLACK};
        for (int i = 0; i < 6; i++) { u8 W = wools[i]; u8 a[] = {W, W, W, P, P, P}; add(3, 2, a, I_BED, 1, 0); }
    }
    // flechas: grava (o lingote de hierro) + palo + pluma -> 4 flechas
    { u8 a[] = {B_GRAVEL, S, I_FEATHER}; add(1, 3, a, I_ARROW, 4, 0); }
    { u8 a[] = {I_IRON_INGOT, S, I_FEATHER}; add(1, 3, a, I_ARROW, 4, 0); }
    // herramientas (M = material, S = palo)
    const u8 mats[5] = {B_PLANKS, B_COBBLE, I_IRON_INGOT, I_GOLD_INGOT, I_DIAMOND};
    for (int m = 0; m < 5; m++) {
        u8 M = mats[m], base = (u8)(I_WOOD_PICK + m * 5);
        { u8 a[] = {M, M, M, 0, S, 0, 0, S, 0}; add(3, 3, a, base + TK_PICK, 1, 0); }
        { u8 a[] = {M, M, M, S, 0, S};          add(2, 3, a, base + TK_AXE, 1, 0); }
        { u8 a[] = {M, S, S};                   add(1, 3, a, base + TK_SHOVEL, 1, 0); }
        { u8 a[] = {M, M, S};                   add(1, 3, a, base + TK_SWORD, 1, 0); }
        { u8 a[] = {M, M, 0, S, 0, S};          add(2, 3, a, base + TK_HOE, 1, 0); }
    }
    // armaduras (cuero, hierro, oro, diamante; la de malla no se fabrica, como en el original)
    const u8 amat[5] = {I_LEATHER, 0, I_IRON_INGOT, I_GOLD_INGOT, I_DIAMOND};
    for (int m = 0; m < 5; m++) {
        if (!amat[m]) continue;
        u8 M = amat[m], base = (u8)(I_LEATHER_HELMET + m * 4);
        { u8 a[] = {M, M, M, M, 0, M};             add(3, 2, a, base + AP_HELMET, 1, 0); }
        { u8 a[] = {M, 0, M, M, M, M, M, M, M};    add(3, 3, a, base + AP_CHEST, 1, 0); }
        { u8 a[] = {M, M, M, M, 0, M, M, 0, M};    add(3, 3, a, base + AP_LEGS, 1, 0); }
        { u8 a[] = {M, 0, M, M, 0, M};             add(3, 2, a, base + AP_BOOTS, 1, 0); }
    }
}

static int cmp_u8(const void *a, const void *b) { return (int)*(const u8 *)a - (int)*(const u8 *)b; }

int craft_match(const u8 grid[9], int *out_id, int *out_n)
{
    if (!g_nrec) init_recipes();
    int minr = 3, maxr = -1, minc = 3, maxc = -1, cnt = 0;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            if (grid[r * 3 + c]) {
                if (r < minr) minr = r;
                if (r > maxr) maxr = r;
                if (c < minc) minc = c;
                if (c > maxc) maxc = c;
                cnt++;
            }
    if (!cnt) return 0;
    int w = maxc - minc + 1, h = maxr - minr + 1;

    u8 sorted[9];
    int ns = 0;
    for (int i = 0; i < 9; i++) if (grid[i]) sorted[ns++] = grid[i];
    qsort(sorted, (size_t)ns, 1, cmp_u8);

    for (int i = 0; i < g_nrec; i++) {
        const Recipe *r = &g_rec[i];
        int ok = 0;
        if (r->shapeless) {
            u8 t[9];
            int nt = 0;
            for (int k = 0; k < r->w * r->h; k++) if (r->in[k]) t[nt++] = r->in[k];
            if (nt == ns) {
                qsort(t, (size_t)nt, 1, cmp_u8);
                ok = memcmp(t, sorted, (size_t)nt) == 0;
            }
        } else if (r->w == w && r->h == h) {
            int same = 1, mirr = 1;
            for (int y = 0; y < h && (same || mirr); y++)
                for (int x = 0; x < w; x++) {
                    u8 g = grid[(minr + y) * 3 + minc + x];
                    if (g != r->in[y * w + x]) same = 0;
                    if (g != r->in[y * w + (w - 1 - x)]) mirr = 0;
                }
            ok = same || mirr;
        }
        if (ok) { *out_id = r->out; *out_n = r->n; return 1; }
    }
    return 0;
}
