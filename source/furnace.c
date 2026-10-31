#include <string.h>
#include "furnace.h"
#include "world.h"
#include "blocks.h"
#include "items.h"
#include "drops.h"

Furnace g_furn[MAX_FURN];

void furnace_clear(void) { memset(g_furn, 0, sizeof(g_furn)); }

int furnace_find(int x, int y, int z)
{
    for (int i = 0; i < MAX_FURN; i++)
        if (g_furn[i].used && g_furn[i].x == x && g_furn[i].y == y && g_furn[i].z == z) return i;
    return -1;
}

int furnace_add(int x, int y, int z)
{
    int i = furnace_find(x, y, z);
    if (i >= 0) return i;
    for (i = 0; i < MAX_FURN; i++)
        if (!g_furn[i].used) {
            memset(&g_furn[i], 0, sizeof(Furnace));
            g_furn[i].used = 1;
            g_furn[i].x = (u16)x; g_furn[i].y = (u8)y; g_furn[i].z = (u16)z;
            return i;
        }
    return -1;     // demasiados hornos: el nuevo funciona solo como bloque decorativo
}

int furnace_get(int x, int y, int z)
{
    int i = furnace_find(x, y, z);
    return i >= 0 ? i : furnace_add(x, y, z);
}

static void drop_slot(Furnace *f, Slot s)
{
    if (!s.id || !s.count) return;
    drops_spawn_slot((f->x << 12) + 2048, (f->y << 12) + 2048, (f->z << 12) + 2048, s, 0, 150, 0, 15);
}

void furnace_remove(int x, int y, int z, int drop_contents)
{
    int i = furnace_find(x, y, z);
    if (i < 0) return;
    Furnace *f = &g_furn[i];
    if (drop_contents) { drop_slot(f, f->in); drop_slot(f, f->fuel); drop_slot(f, f->out); }
    memset(f, 0, sizeof(*f));
}

// hay algo que fundir y cabe el resultado en la salida
int furnace_can_smelt(const Furnace *f)
{
    if (!f->in.id || !f->in.count) return 0;
    int r = smelt_result(f->in.id);
    if (!r) return 0;
    if (!f->out.id) return 1;
    if (f->out.id != r) return 0;
    return f->out.count < item_max_stack((u8)r);
}

static void set_lit(Furnace *f, int lit)
{
    u8 b = world_get(f->x, f->y, f->z);
    if (!BLK_IS_FURNACE(b)) return;
    if ((BLK_FURNACE_LIT(b) ? 1 : 0) == lit) return;
    u8 nb = (u8)((lit ? B_FURNACE_LIT : B_FURNACE) + BLK_FURNACE_FACING(b));
    world_set(f->x, f->y, f->z, nb);
}

static void smelt_one(Furnace *f)
{
    int r = smelt_result(f->in.id);
    if (!f->out.id) { f->out.id = (u8)r; f->out.count = 1; f->out.dmg = 0; }
    else f->out.count++;
    if (--f->in.count == 0) { f->in.id = 0; f->in.dmg = 0; }
}

void furnace_update(void)
{
    for (int i = 0; i < MAX_FURN; i++) {
        Furnace *f = &g_furn[i];
        if (!f->used) continue;
        if (!world_col_loaded(f->x, f->z)) continue;                 // como en MC, solo funcionan en chunks cargados
        if (!BLK_IS_FURNACE(world_get(f->x, f->y, f->z))) {            // el bloque ya no existe
            furnace_remove(f->x, f->y, f->z, 1);
            continue;
        }
        if (f->burn > 0) f->burn--;

        if (furnace_can_smelt(f)) {
            if (f->burn == 0 && f->fuel.id && f->fuel.count) {
                int t = fuel_time(f->fuel.id);
                if (t > 0) {
                    f->burn = (u16)t; f->burn_max = (u16)t;
                    if (--f->fuel.count == 0) { f->fuel.id = 0; f->fuel.dmg = 0; }
                }
            }
            if (f->burn > 0) {
                if (++f->cook >= SMELT_TICKS) { f->cook = 0; smelt_one(f); }
            } else f->cook = 0;
        } else if (f->cook > 0) {
            f->cook = f->cook > 2 ? (u16)(f->cook - 2) : 0;       // se enfria al faltar material
        }

        set_lit(f, f->burn > 0);      // no hace nada si el bloque ya tiene el estado correcto
    }
}
