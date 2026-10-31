// Mundo en voxeles 640x640x128 con streaming por columnas de 16x16 (cache en RAM + guardado en SD)
#ifndef WORLD_H
#define WORLD_H

#include <nds.h>
#include "blocks.h"

#define WX 640          // largo
#define WZ 640          // ancho
#define WY 128          // alto
#define SEA_LEVEL 44

// Secciones de 16x16x16 para el cache de mallas
#define SEC 16
#define SXN (WX / SEC)
#define SYN (WY / SEC)
#define SZN (WZ / SEC)
#define NSEC (SXN * SYN * SZN)

// Columnas (16x16x128) = unidad de streaming y de guardado
#define CXN SXN
#define CZN SZN
#define NCOL (CXN * CZN)
#define COL_BYTES (16 * 16 * WY)     // 32 KB
#define COL_LOAD_R 3                 // radio de columnas cargadas alrededor del jugador

enum { WT_DEFAULT = 0, WT_FLAT = 1 };

extern u8 *g_cols[NCOL];          // NULL = columna no cargada
extern u8 g_sec_dirty[NSEC];      // 1 = hay que reconstruir la malla
extern int g_spawn_x, g_spawn_y, g_spawn_z;

#define CIDX(x, y, z) (((y) << 8) | (((z) & 15) << 4) | ((x) & 15))
#define SECIDX(sx, sy, sz) (((sy) * SZN + (sz)) * SXN + (sx))

// Devuelve el bloque; fuera del mundo: roca madre en los lados/fondo, aire arriba.
// Columna no cargada: piedra (oculta caras en los bordes de lo cargado).
static inline u8 world_get(int x, int y, int z)
{
    if (y >= WY) return B_AIR;
    if ((unsigned)x >= WX || (unsigned)z >= WZ || y < 0) return B_BEDROCK;
    const u8 *c = g_cols[(z >> 4) * CXN + (x >> 4)];
    if (!c) return B_STONE;
    return c[CIDX(x, y, z)];
}

// ---- camas ----
// direccion (0: -Z, 1: +X, 2: +Z, 3: -X) -> desplazamiento
static inline int bed_dx(int d) { return (d & 1) ? 2 - d : 0; }
static inline int bed_dz(int d) { return (d & 1) ? 0 : d - 1; }

// Orientacion de la cama en (x, y, z): direccion del pie hacia la cabeza; -1 si ahi no hay cama.
// El pie la guarda en su id; la cabeza la deduce del pie vecino que apunta a ella.
static inline int bed_facing(int x, int y, int z)
{
    u8 b = world_get(x, y, z);
    if (BLK_IS_BED_FOOT(b)) return BLK_BED_FOOT_DIR(b);
    if (b == B_BED_HEAD) {
        for (int d = 0; d < 4; d++)
            if (world_get(x - bed_dx(d), y, z - bed_dz(d)) == B_BED_FOOT + d) return d;
        return 0;
    }
    return -1;
}

static inline int world_col_loaded(int x, int z)
{
    if ((unsigned)x >= WX || (unsigned)z >= WZ) return 0;
    return g_cols[(z >> 4) * CXN + (x >> 4)] != NULL;
}

// ---- ciclo de vida ----
void world_alloc(void);
int  world_sd_init(void);                         // monta la SD; 1 = ok
void world_new(const char *name, u32 seed, int type, int gamemode);   // crea la carpeta del mundo
void world_close(void);                           // guarda y libera
void world_free_all(void);
void world_spawn_find(void);                      // calcula g_spawn_*
void world_stream(int px, int pz, int max_loads); // carga/descarga columnas alrededor de (px,pz) en bloques
void world_stream_slice(int px, int pz);          // igual que world_stream pero repartido en fases (juego)
void world_save_begin(void);                      // empieza una ronda de guardado (borra el aviso de error)
int  world_save_step(void);                       // autoguardado: 1 columna por llamada, devuelve las que quedan (0 si falla)
int  world_save_flush(void);                      // fflush + confirma level.dat tras world_save_step; 1 = ok
int  world_save_failed(void);                     // 1 = alguna escritura fallo en esta ronda
int  world_has_sd(void);                          // 1 = hay SD y se puede guardar
int  world_stream_pending(int px, int pz);        // columnas que faltan por cargar
int  world_ensure_loaded(int x, int z);            // carga YA la columna que contiene (x, z); 1 = cargada
int  world_save_all(void);                        // escribe columnas modificadas y confirma level.dat; 1 = ok
void world_set(int x, int y, int z, u8 id);       // marca secciones sucias
int  world_height(int x, int z);                  // y del bloque solido mas alto (columna cargada)

// ---- metadatos del mundo (level.dat) ----
typedef struct {
    char name[32];
    u32 seed;
    u8 worldtype, gamemode;
    u32 daytime;
    int px, py, pz, yaw, pitch;
    u8 inv_id[36], inv_count[36];
    u16 inv_dmg[36];
    u8 sel;
    u8 has_player;
} WorldMeta;

extern WorldMeta g_meta;

int  world_list(char names[][32], int max);       // lista carpetas en Worlds
int  world_open(const char *name);                // carga level.dat, 1 = ok
int  world_delete(const char *name);              // borra la carpeta del mundo (level.dat, chunks.dat...), 1 = ok
void world_save_meta(void);
int  world_file_write(const char *name, const void *buf, int n);   // fichero auxiliar del mundo
int  world_file_read(const char *name, void *buf, int n);          // 1 = leido entero

// Ruido simple
u32 hash32(u32 a, u32 b, u32 c);

// hook de render: la columna se descarga y hay que liberar sus mallas
void render_column_unloaded(int cx, int cz);

#endif
