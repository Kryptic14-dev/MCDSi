#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fat.h>
#include "world.h"

u8 *g_cols[NCOL];
u8 g_sec_dirty[NSEC];
int g_spawn_x, g_spawn_y, g_spawn_z;
WorldMeta g_meta;

static u8  g_col_dirty[NCOL];     // modificada: hay que guardarla
static u16 g_col_slot[NCOL];      // 0 = nunca guardada (se regenera); n = hueco n-1 del fichero (tabla viva, en RAM)
static u16 g_nslots;
static int g_nloaded;

// Guardado a prueba de cortes (copy-on-write): una columna modificada NUNCA se escribe encima de su hueco
// confirmado; va a un hueco nuevo y solo cuando level.dat (que lleva la tabla de huecos) se escribe entero
// y bien, el hueco antiguo pasa a poder reutilizarse. Si se va la luz a mitad, level.dat sigue apuntando a
// huecos intactos y el mundo carga tal como estaba en el ultimo guardado completo.
#define MAX_SLOTS (2 * NCOL + 16)
static u16 g_commit_slot[NCOL];   // tabla del ultimo level.dat confirmado en la SD
static u16 g_free[MAX_SLOTS];     // huecos libres ya reutilizables
static int g_nfree;
static u16 g_pend[MAX_SLOTS];     // huecos antiguos: se liberan al confirmar level.dat
static int g_npend;
static int g_save_err;            // 1 = alguna escritura fallo desde world_save_begin()

static u32 g_seed;
static u32 g_rng;
static int g_type;

static FILE *g_chunkf;
static char g_base[64];           // "sd:/MinecraftDS/"
static char g_worlds[80];         // "sd:/MinecraftDS/Worlds/"
static char g_dir[128];           // carpeta del mundo actual (con '/' final)
static int g_sd_ok;

// --------------------------------------------------------------------------
// Utilidades
// --------------------------------------------------------------------------
u32 hash32(u32 a, u32 b, u32 c)
{
    u32 h = a * 374761393u + b * 668265263u + c * 2246822519u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 15;
    return h;
}

static u32 rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
static int rnd_n(int n) { return (int)(rnd() % (u32)n); }

static void rng_seed(u32 a, u32 b, u32 c)
{
    g_rng = hash32(a, b, c);
    if (!g_rng) g_rng = 0xDEADBEEF;
}

void world_alloc(void) {}

// --------------------------------------------------------------------------
// SD
// --------------------------------------------------------------------------
static int dir_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

int world_sd_init(void)
{
    if (g_sd_ok) return 1;
    if (!fatInitDefault()) return 0;
    const char *root = dir_exists("sd:/") ? "sd:/" : (dir_exists("fat:/") ? "fat:/" : NULL);
    if (!root) return 0;
    snprintf(g_base, sizeof(g_base), "%sMinecraftDS/", root);
    snprintf(g_worlds, sizeof(g_worlds), "%sWorlds/", g_base);
    mkdir(g_base, 0777);
    mkdir(g_worlds, 0777);
    g_sd_ok = dir_exists(g_worlds);
    return g_sd_ok;
}

int world_list(char names[][32], int max)
{
    int n = 0;
    if (!world_sd_init()) return 0;
    DIR *d = opendir(g_worlds);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.') continue;
        char p[160];
        snprintf(p, sizeof(p), "%s%s/level.dat", g_worlds, e->d_name);
        struct stat st;
        if (stat(p, &st) != 0) {          // un corte justo al confirmar puede dejar solo level.dat.tmp (valido)
            snprintf(p, sizeof(p), "%s%s/level.dat.tmp", g_worlds, e->d_name);
            if (stat(p, &st) != 0) continue;
        }
        strncpy(names[n], e->d_name, 31);
        names[n][31] = 0;
        n++;
    }
    closedir(d);
    return n;
}

// Borra un mundo: todos los ficheros de su carpeta y despues la carpeta.
// Solo se llama desde el menu (con ningun mundo abierto).
int world_delete(const char *name)
{
    if (!name || !name[0] || !world_sd_init()) return 0;
    // el nombre viene de la lista de carpetas, pero por seguridad no se admiten rutas
    if (strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) return 0;

    char dir[128];
    snprintf(dir, sizeof(dir), "%s%s", g_worlds, name);
    if (!dir_exists(dir)) return 0;

    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            char p[200];
            snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
            if (dir_exists(p)) rmdir(p);    // subcarpeta (vacia)
            else remove(p);
        }
        closedir(d);
    }
    // si por algun motivo quedaba abierto el fichero de este mundo, soltarlo
    if (g_chunkf && g_dir[0] && strncmp(g_dir, dir, strlen(dir)) == 0) { fclose(g_chunkf); g_chunkf = NULL; g_dir[0] = 0; }
    rmdir(dir);
    return !dir_exists(dir);
}

// --------------------------------------------------------------------------
// Ficheros del mundo: escritura atomica + CRC32
// --------------------------------------------------------------------------
// Antes cada fichero se abria con "wb" y se escribia encima: un corte (bateria, sacar la SD, cuelgue) a mitad
// dejaba el fichero vacio o a medias, y si era level.dat (que lleva la tabla de columnas) se perdia TODO el mundo.
// Ahora: se escribe "<nombre>.tmp" entero con cabecera {magic, tamano, CRC32}, se comprueba y solo entonces se
// sustituye al bueno. Al leer vale el fichero que tenga cabecera correcta (el .tmp tiene prioridad: si existe y
// es valido es la version mas nueva); los ficheros antiguos sin cabecera se siguen leyendo (se actualizan al guardar).
static u32 crc32_calc(const void *buf, size_t n)
{
    static const u32 tab[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C };
    const u8 *d = (const u8 *)buf;
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        c = (c >> 4) ^ tab[c & 15];
        c = (c >> 4) ^ tab[c & 15];
    }
    return ~c;
}

typedef struct { char magic[8]; u32 len; u32 crc; } FileHdr;
#define FILE_MAGIC "MCDSF001"

// Ultimo contenido escrito de cada fichero: si no ha cambiado no se vuelve a tocar la SD (cada escritura atomica son
// varias operaciones FAT: crear .tmp, borrar, renombrar; en el autoguardado cofres, objetos y cama casi nunca cambian).
#define WF_CACHE 8
static struct { char name[16]; u32 len, crc; } g_wf_last[WF_CACHE];
static int g_wf_skipped;          // escrituras evitadas (para las pruebas)
static void wf_cache_reset(void) { memset(g_wf_last, 0, sizeof(g_wf_last)); }

// Lee un fichero con cabecera y lo valida; solo toca out si todo es correcto
static int wf_read(const char *path, void *out, int n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = 0;
    FileHdr h;
    u8 *tmp = (u8 *)malloc((size_t)n);
    if (tmp && fread(&h, sizeof(h), 1, f) == 1 && memcmp(h.magic, FILE_MAGIC, 8) == 0 && h.len == (u32)n &&
        fread(tmp, 1, (size_t)n, f) == (size_t)n && crc32_calc(tmp, (size_t)n) == h.crc) {
        memcpy(out, tmp, (size_t)n);
        ok = 1;
    }
    free(tmp);
    fclose(f);
    return ok;
}

// Formato antiguo: datos en bruto, sin cabecera (tamano exacto)
static int legacy_read(const char *path, void *out, int n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = 0;
    u8 *tmp = (u8 *)malloc((size_t)n);
    if (tmp && fread(tmp, 1, (size_t)n, f) == (size_t)n && fgetc(f) == EOF) {
        memcpy(out, tmp, (size_t)n);
        ok = 1;
    }
    free(tmp);
    fclose(f);
    return ok;
}

// Fichero auxiliar dentro de la carpeta del mundo (extra.dat, chests.dat, level.dat...). 1 = guardado
int world_file_write(const char *name, const void *buf, int n)
{
    if (!g_sd_ok || !g_dir[0]) return 0;
    char p[160], t[170];
    snprintf(p, sizeof(p), "%s%s", g_dir, name);
    snprintf(t, sizeof(t), "%s.tmp", p);

    u32 crc = crc32_calc(buf, (size_t)n);
    int slot = -1;                                  // entrada de la cache para este nombre
    for (int i = 0; i < WF_CACHE; i++) {
        if (g_wf_last[i].name[0] && strncmp(g_wf_last[i].name, name, 15) == 0) { slot = i; break; }
        if (slot < 0 && !g_wf_last[i].name[0]) slot = i;
    }
    if (slot >= 0 && g_wf_last[slot].len == (u32)n + 1 && g_wf_last[slot].crc == crc &&
        strncmp(g_wf_last[slot].name, name, 15) == 0) { g_wf_skipped++; return 1; }

    FILE *f = fopen(t, "wb");
    if (!f) { g_save_err = 1; return 0; }
    FileHdr h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, FILE_MAGIC, 8);
    h.len = (u32)n;
    h.crc = crc;
    int ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(buf, 1, (size_t)n, f) == (size_t)n;
    if (fflush(f) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(t); g_save_err = 1; return 0; }

    // El .tmp ya esta completo y valido. libfat no siempre deja renombrar sobre un fichero existente, asi que se
    // quita el viejo y se renombra; si algo se corta entre medias, al leer se usa el .tmp (valido).
    remove(p);
    rename(t, p);          // si el renombrado falla, los datos siguen a salvo en el .tmp (world_file_read lo usa)
    if (slot >= 0) {
        strncpy(g_wf_last[slot].name, name, 15); g_wf_last[slot].name[15] = 0;
        g_wf_last[slot].len = (u32)n + 1;           // +1: 0 = entrada vacia
        g_wf_last[slot].crc = crc;
    }
    return 1;
}

int world_file_read(const char *name, void *buf, int n)
{
    if (!g_sd_ok || !g_dir[0]) return 0;
    char p[160], t[170];
    snprintf(p, sizeof(p), "%s%s", g_dir, name);
    snprintf(t, sizeof(t), "%s.tmp", p);
    if (wf_read(t, buf, n)) return 1;      // version mas nueva (corte justo al confirmar)
    if (wf_read(p, buf, n)) return 1;
    return legacy_read(p, buf, n);         // mundos guardados antes de este sistema
}

// --------------------------------------------------------------------------
// level.dat (metadatos + tabla de huecos) y gestion de huecos
// --------------------------------------------------------------------------
typedef struct {
    char magic[8];
    u32 version;
    WorldMeta meta;
    u16 nslots;
    u16 slot[NCOL];
} LevelFile;

static void free_push(u16 s)
{
    if (s && g_nfree < MAX_SLOTS) g_free[g_nfree++] = s;
}

static void pend_push(u16 s)
{
    if (s && g_npend < MAX_SLOTS) g_pend[g_npend++] = s;
}

// Hueco libre para escribir una columna: uno reutilizable o uno nuevo al final del fichero. 0 = no hay
static u16 alloc_slot(void)
{
    if (g_nfree > 0) return g_free[--g_nfree];
    if (g_nslots >= MAX_SLOTS) return 0;
    return ++g_nslots;
}

// Reconstruye la lista de huecos libres: todo hueco del fichero que ninguna columna usa (incluye los que
// quedaron huerfanos tras un corte)
static void rebuild_free_slots(void)
{
    g_nfree = g_npend = 0;
    if (g_nslots == 0) return;
    u8 *used = (u8 *)calloc((size_t)g_nslots + 1, 1);
    if (!used) return;
    for (int i = 0; i < NCOL; i++) if (g_col_slot[i]) used[g_col_slot[i]] = 1;
    for (int sl = g_nslots; sl >= 1; sl--) if (!used[sl]) free_push((u16)sl);
    free(used);
}

// Confirma el guardado: escribe level.dat (atomico). Solo si sale bien los huecos antiguos pasan a ser libres.
static int commit_level(void)
{
    if (!g_sd_ok || !g_dir[0]) return 0;
    static LevelFile lf;
    memset(&lf, 0, sizeof(lf));
    memcpy(lf.magic, "MCDSW003", 8);
    lf.version = 2;
    lf.meta = g_meta;
    lf.nslots = g_nslots;
    memcpy(lf.slot, g_col_slot, sizeof(lf.slot));
    if (!world_file_write("level.dat", &lf, (int)sizeof(lf))) { g_save_err = 1; return 0; }
    memcpy(g_commit_slot, g_col_slot, sizeof(g_commit_slot));
    for (int i = 0; i < g_npend; i++) free_push(g_pend[i]);
    g_npend = 0;
    return 1;
}

void world_save_meta(void)
{
    if (!g_sd_ok || !g_dir[0]) return;
    if (g_chunkf) fflush(g_chunkf);       // los huecos que apunta level.dat tienen que estar ya en la SD
    commit_level();
}

static void open_chunk_file(int truncate)
{
    if (g_chunkf) { fclose(g_chunkf); g_chunkf = NULL; }
    char p[160];
    snprintf(p, sizeof(p), "%schunks.dat", g_dir);
    if (!truncate) g_chunkf = fopen(p, "r+b");
    if (!g_chunkf) g_chunkf = fopen(p, "w+b");
}

void world_new(const char *name, u32 seed, int type, int gamemode)
{
    world_free_all();
    world_sd_init();
    char nm[32];
    if (name && name[0]) { strncpy(nm, name, 31); nm[31] = 0; }
    else {
        for (int i = 1; i < 1000; i++) {
            snprintf(nm, sizeof(nm), "World%d", i);
            char p[160];
            snprintf(p, sizeof(p), "%s%s", g_worlds, nm);
            if (!dir_exists(p)) break;
        }
    }
    {   // si ya existe una carpeta con ese nombre, anadir un numero
        char base[32], p[160];
        strncpy(base, nm, 27); base[27] = 0;
        snprintf(p, sizeof(p), "%s%s", g_worlds, nm);
        for (int i = 2; dir_exists(p) && i < 1000; i++) {
            snprintf(nm, sizeof(nm), "%s%d", base, i);
            snprintf(p, sizeof(p), "%s%s", g_worlds, nm);
        }
    }
    snprintf(g_dir, sizeof(g_dir), "%s%s/", g_worlds, nm);
    mkdir(g_dir, 0777);

    memset(&g_meta, 0, sizeof(g_meta));
    strncpy(g_meta.name, nm, 31);
    g_meta.seed = seed;
    g_meta.worldtype = (u8)type;
    g_meta.gamemode = (u8)gamemode;
    g_meta.daytime = 1000;
    g_seed = seed;
    g_type = type;
    memset(g_col_slot, 0, sizeof(g_col_slot));
    memset(g_commit_slot, 0, sizeof(g_commit_slot));
    memset(g_col_dirty, 0, sizeof(g_col_dirty));
    g_nslots = 0;
    g_nfree = g_npend = 0;
    g_save_err = 0;
    wf_cache_reset();
    open_chunk_file(1);
    world_save_meta();
}

int world_open(const char *name)
{
    world_free_all();
    if (!world_sd_init()) return 0;
    snprintf(g_dir, sizeof(g_dir), "%s%s/", g_worlds, name);
    static LevelFile lf;
    if (!world_file_read("level.dat", &lf, (int)sizeof(lf)) || memcmp(lf.magic, "MCDSW003", 8) != 0) {
        g_dir[0] = 0;
        return 0;
    }
    g_meta = lf.meta;
    g_nslots = lf.nslots > MAX_SLOTS ? 0 : lf.nslots;
    memcpy(g_col_slot, lf.slot, sizeof(g_col_slot));
    for (int i = 0; i < NCOL; i++) if (g_col_slot[i] > g_nslots) g_col_slot[i] = 0;   // hueco imposible: se regenera
    memcpy(g_commit_slot, g_col_slot, sizeof(g_commit_slot));
    memset(g_col_dirty, 0, sizeof(g_col_dirty));
    g_save_err = 0;
    wf_cache_reset();
    rebuild_free_slots();
    g_seed = g_meta.seed;
    g_type = g_meta.worldtype;
    open_chunk_file(0);
    return 1;
}

// --------------------------------------------------------------------------
// Ruido de valor 2D en punto fijo. Devuelve 0..4095
// --------------------------------------------------------------------------
static int vnoise(int x, int z, int shift, u32 salt)
{
    int gx = x >> shift, gz = z >> shift;
    int mask = (1 << shift) - 1;
    int fx = ((x & mask) << 12) >> shift;
    int fz = ((z & mask) << 12) >> shift;
    fx = (fx * fx >> 12) * (3 * 4096 - 2 * fx) >> 12;
    fz = (fz * fz >> 12) * (3 * 4096 - 2 * fz) >> 12;
    int a = hash32(gx, gz, g_seed + salt) & 4095;
    int b = hash32(gx + 1, gz, g_seed + salt) & 4095;
    int c = hash32(gx, gz + 1, g_seed + salt) & 4095;
    int d = hash32(gx + 1, gz + 1, g_seed + salt) & 4095;
    int ab = a + (((b - a) * fx) >> 12);
    int cd = c + (((d - c) * fx) >> 12);
    return ab + (((cd - ab) * fz) >> 12);
}

// altura del terreno en (x,z): funcion pura, vale para cualquier columna
static int terrain_h(int x, int z)
{
    int n = vnoise(x, z, 6, 1) * 8 + vnoise(x, z, 5, 2) * 4 + vnoise(x, z, 4, 3) * 2 + vnoise(x, z, 3, 4);
    n /= 15;
    int big = vnoise(x, z, 8, 9);
    int h = SEA_LEVEL - 12 + ((n * 44) >> 12) + (((big - 2048) * 16) >> 12);
    if (h < 6) h = 6;
    if (h > WY - 16) h = WY - 16;
    return h;
}

static int is_desert(int x, int z) { return vnoise(x, z, 7, 77) > 2950; }

// --------------------------------------------------------------------------
// Generacion de una columna (16x16x128)
// --------------------------------------------------------------------------
static inline void cput(u8 *col, int lx, int y, int lz, u8 id)
{
    if ((unsigned)lx < 16 && (unsigned)lz < 16 && (unsigned)y < WY) col[CIDX(lx, y, lz)] = id;
}

static inline void cput_if_air(u8 *col, int lx, int y, int lz, u8 id)
{
    if ((unsigned)lx < 16 && (unsigned)lz < 16 && (unsigned)y < WY) {
        u8 *p = &col[CIDX(lx, y, lz)];
        if (*p == B_AIR || (g_blocks[*p].flags & F_PLANT)) *p = id;
    }
}

static void make_tree(u8 *col, int lx, int y, int lz, u32 hh)
{
    int h = 4 + (hh % 3);
    if (y + h + 2 >= WY) return;
    for (int dy = h - 3; dy <= h + 1; dy++) {
        int r = (dy >= h) ? 1 : 2;
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                if (r == 2 && abs(dx) == 2 && abs(dz) == 2 && (hash32(hh, dx + 9, dz + dy * 7) & 1)) continue;
                if (r == 1 && dy == h + 1 && abs(dx) + abs(dz) == 2) continue;
                cput_if_air(col, lx + dx, y + dy, lz + dz, B_LEAVES);
            }
    }
    for (int dy = 0; dy < h + 1; dy++) cput(col, lx, y + dy, lz, B_LOG);
}

static void vein(u8 *col, int x, int y, int z, int size, u8 id)
{
    for (int i = 0; i < size; i++) {
        if ((unsigned)x < 16 && (unsigned)z < 16 && (unsigned)y < WY) {
            u8 *p = &col[CIDX(x, y, z)];
            if (*p == B_STONE) *p = id;
        }
        switch (rnd_n(6)) {
            case 0: x++; break;
            case 1: x--; break;
            case 2: y++; break;
            case 3: y--; break;
            case 4: z++; break;
            default: z--; break;
        }
    }
}

// ---- cuevas: dos campos de ruido 3D (red de 8 bloques, interpolacion trilineal) ----
#define CLX 3
#define CLY (WY / 8 + 1)
typedef struct { s16 a[CLY][CLX][CLX]; } Lat;   // [y][z][x]
static Lat lat1, lat2, lat3;

static void build_lat(Lat *L, int wx0, int wz0, u32 salt)
{
    for (int iy = 0; iy < CLY; iy++)
        for (int iz = 0; iz < CLX; iz++)
            for (int ix = 0; ix < CLX; ix++)
                L->a[iy][iz][ix] = (s16)(hash32(wx0 / 8 + ix, iy * 131 + salt, wz0 / 8 + iz + g_seed * 3u) & 4095);
}

static inline int lat_at(const Lat *L, int x, int y, int z)   // x,z locales (0..15) y y absoluta
{
    int ix = x >> 3, fx = x & 7, iz = z >> 3, fz = z & 7, iy = y >> 3, fy = y & 7;
    const s16 (*A)[CLX][CLX] = L->a;
    int c00 = A[iy][iz][ix] * (8 - fx) + A[iy][iz][ix + 1] * fx;
    int c10 = A[iy][iz + 1][ix] * (8 - fx) + A[iy][iz + 1][ix + 1] * fx;
    int c01 = A[iy + 1][iz][ix] * (8 - fx) + A[iy + 1][iz][ix + 1] * fx;
    int c11 = A[iy + 1][iz + 1][ix] * (8 - fx) + A[iy + 1][iz + 1][ix + 1] * fx;
    int d0 = c00 * (8 - fz) + c10 * fz;
    int d1 = c01 * (8 - fz) + c11 * fz;
    return (d0 * (8 - fy) + d1 * fy) >> 9;     // /(8*8*8)
}

static void gen_flat_col(u8 *col)
{
    memset(col, B_AIR, COL_BYTES);
    for (int z = 0; z < 16; z++)
        for (int x = 0; x < 16; x++) {
            col[CIDX(x, 0, z)] = B_BEDROCK;
            col[CIDX(x, 1, z)] = B_DIRT;
            col[CIDX(x, 2, z)] = B_DIRT;
            col[CIDX(x, 3, z)] = B_GRASS;
        }
}

// Generacion por fases: una columna entera tardaba decenas de ms (terreno + cuevas + menas + arboles) y
// provocaba tirones al cruzar de columna. Se divide en 8 trozos pequenos que se reparten entre fotogramas.
typedef struct {
    u8 *col;
    int cx, cz, phase;
    u32 rng;
    int hm[16][16];
} GenJob;
static GenJob g_job;
static int g_job_active;

#define GEN_PHASES 8

// Ejecuta una fase. Devuelve 1 cuando la columna esta completa.
static int gen_default_step(GenJob *J)
{
    u8 *col = J->col;
    const int cx = J->cx, cz = J->cz;
    const int wx0 = cx * 16, wz0 = cz * 16;
    const int ph = J->phase++;

    if (ph == 0) {
        memset(col, B_AIR, COL_BYTES);
        build_lat(&lat1, wx0, wz0, 11);
        build_lat(&lat2, wx0, wz0, 22);
        build_lat(&lat3, wx0, wz0, 33);
        rng_seed(g_seed, cx, cz * 7919u + 1);
        J->rng = g_rng;
        return 0;
    }
    g_rng = J->rng;

    if (ph >= 1 && ph <= 4) {
        // terreno + cuevas, 4 filas (z) por fase
        for (int lz = (ph - 1) * 4; lz < (ph - 1) * 4 + 4; lz++) {
            for (int lx = 0; lx < 16; lx++) {
                int x = wx0 + lx, z = wz0 + lz;
                int h = terrain_h(x, z);
                int isdesert = is_desert(x, z);
                int beach = (h <= SEA_LEVEL + 1);
                int gravelpatch = (h < SEA_LEVEL) && (vnoise(x, z, 3, 55) > 3200);
                J->hm[lz][lx] = h;
                for (int y = 0; y <= h; y++) {
                    u8 b = B_STONE;
                    if (y == 0) b = B_BEDROCK;
                    else if (y == 1 && (rnd() & 1)) b = B_BEDROCK;
                    else if (y >= h - 3) {
                        if (isdesert || beach) b = B_SAND;
                        else if (gravelpatch) b = B_GRAVEL;
                        else b = (y == h) ? B_GRASS : B_DIRT;
                        if (isdesert && y == h - 3) b = B_SANDSTONE;
                        if (h < SEA_LEVEL && !gravelpatch && !isdesert) b = (y == h) ? B_SAND : B_DIRT;
                    }
                    col[CIDX(lx, y, lz)] = b;
                }
                for (int y = h + 1; y <= SEA_LEVEL; y++) col[CIDX(lx, y, lz)] = B_WATER;

                // cuevas
                int top = (h > SEA_LEVEL + 2) ? h : h - 3;
                for (int y = 3; y <= top; y++) {
                    u8 *p = &col[CIDX(lx, y, lz)];
                    if (*p == B_BEDROCK || *p == B_WATER) continue;
                    if (y < WY - 1 && col[CIDX(lx, y + 1, lz)] == B_WATER) continue;   // no inundar
                    int a = lat_at(&lat1, lx, y, lz) - 2048;
                    int b2 = lat_at(&lat2, lx, y, lz) - 2048;
                    int cave = (a * a + b2 * b2) < 280 * 280;
                    if (!cave && y < 40) cave = lat_at(&lat3, lx, y, lz) > 3650;     // cavernas grandes
                    if (cave) *p = B_AIR;
                }
            }
        }
    } else if (ph == 5) {
        // minerales (limitados a la columna)
        for (int i = 0; i < 20; i++) vein(col, rnd_n(16), 4 + rnd_n(60), rnd_n(16), 6 + rnd_n(6), B_COAL_ORE);
        for (int i = 0; i < 12; i++) vein(col, rnd_n(16), 4 + rnd_n(50), rnd_n(16), 4 + rnd_n(4), B_IRON_ORE);
        for (int i = 0; i < 4; i++)  vein(col, rnd_n(16), 3 + rnd_n(26), rnd_n(16), 4 + rnd_n(3), B_GOLD_ORE);
        for (int i = 0; i < 2; i++)  vein(col, rnd_n(16), 2 + rnd_n(14), rnd_n(16), 3 + rnd_n(3), B_DIAMOND_ORE);
        for (int i = 0; i < 6; i++)  vein(col, rnd_n(16), 8 + rnd_n(50), rnd_n(16), 8 + rnd_n(6), B_GRAVEL);
    } else if (ph == 6) {
        // plantas dentro de la columna
        for (int lz = 0; lz < 16; lz++)
            for (int lx = 0; lx < 16; lx++) {
                int h = J->hm[lz][lx];
                if (h <= SEA_LEVEL + 1 || h >= WY - 16) continue;
                if (col[CIDX(lx, h, lz)] != B_GRASS || col[CIDX(lx, h + 1, lz)] != B_AIR) continue;
                int r = hash32(wx0 + lx, wz0 + lz, g_seed + 5) % 1000;
                if (r < 10) continue;            // aqui va un arbol
                if (r < 110) col[CIDX(lx, h + 1, lz)] = B_TALLGRASS;
                else if (r < 118) col[CIDX(lx, h + 1, lz)] = B_DANDELION;
                else if (r < 124) col[CIDX(lx, h + 1, lz)] = B_ROSE;
            }
    } else {
        // arboles: candidatos en una franja de 2 bloques alrededor para que las copas crucen columnas
        for (int dz = -2; dz < 18; dz++)
            for (int dx = -2; dx < 18; dx++) {
                int x = wx0 + dx, z = wz0 + dz;
                if ((unsigned)x >= WX || (unsigned)z >= WZ) continue;
                u32 hh = hash32(x, z, g_seed + 5);
                if (hh % 1000 >= 10) continue;
                int h = terrain_h(x, z);
                if (h <= SEA_LEVEL + 1 || h >= WY - 16 || is_desert(x, z)) continue;
                if (dx >= 0 && dx < 16 && dz >= 0 && dz < 16 && col[CIDX(dx, h, dz)] != B_GRASS) continue;
                make_tree(col, dx, h + 1, dz, hh >> 10);
            }
    }
    J->rng = g_rng;
    return J->phase >= GEN_PHASES;
}

// Generacion completa de golpe (pantalla de carga). Antes termina cualquier trabajo repartido en curso,
// porque comparten las tablas de ruido lat1..3.
static void gen_default_col(u8 *col, int cx, int cz)
{
    static GenJob J;
    J.col = col; J.cx = cx; J.cz = cz; J.phase = 0;
    while (!gen_default_step(&J)) {}
}

static void gen_column(u8 *col, int cx, int cz)
{
    if (g_type == WT_FLAT) gen_flat_col(col);
    else gen_default_col(col, cx, cz);
}

// --------------------------------------------------------------------------
// Streaming de columnas
// --------------------------------------------------------------------------
static void mark_col_sections(int cx, int cz)
{
    if ((unsigned)cx >= CXN || (unsigned)cz >= CZN) return;
    for (int sy = 0; sy < SYN; sy++) g_sec_dirty[SECIDX(cx, sy, cz)] = 1;
}

// Guarda una columna modificada en un hueco NUEVO (copy-on-write, ver arriba). 1 = guardada (o nada que guardar).
// Si la escritura falla, la columna sigue sucia y la tabla sigue apuntando a su version anterior (intacta).
static int save_column(int i)
{
    if (!g_chunkf || !g_cols[i] || !g_col_dirty[i]) return 1;
    u16 slot = alloc_slot();
    if (!slot) { g_save_err = 1; return 0; }
    long off = (long)(slot - 1) * COL_BYTES;
    int ok = fseek(g_chunkf, off, SEEK_SET) == 0 && fwrite(g_cols[i], 1, COL_BYTES, g_chunkf) == COL_BYTES;
    if (!ok) {
        if (slot == g_nslots) g_nslots--; else free_push(slot);
        g_save_err = 1;
        return 0;
    }
    u16 old = g_col_slot[i];
    g_col_slot[i] = slot;
    if (old) {
        if (old == g_commit_slot[i]) pend_push(old);      // lo usa el level.dat de la SD: no tocar hasta confirmar
        else free_push(old);                              // guardado intermedio sin confirmar: ya no hace falta
    }
    g_col_dirty[i] = 0;
    return 1;
}

// Descarga una columna. Si no se puede guardar NO se descarga (se perderian los cambios). 1 = descargada.
static int unload_column(int i)
{
    if (!g_cols[i]) return 1;
    if (!save_column(i)) return 0;
    int cx = i % CXN, cz = i / CXN;
    render_column_unloaded(cx, cz);
    free(g_cols[i]);
    g_cols[i] = NULL;
    g_nloaded--;
    return 1;
}

static void install_column(int cx, int cz, u8 *buf)
{
    int i = cz * CXN + cx;
    g_cols[i] = buf;
    g_nloaded++;
    mark_col_sections(cx, cz);
    mark_col_sections(cx - 1, cz);
    mark_col_sections(cx + 1, cz);
    mark_col_sections(cx, cz - 1);
    mark_col_sections(cx, cz + 1);
}

// Intenta leer la columna de la SD. 1 = leida (buf listo)
static int read_column_file(int i, u8 *buf)
{
    if (!g_col_slot[i] || !g_chunkf) return 0;
    long off = (long)(g_col_slot[i] - 1) * COL_BYTES;
    return fseek(g_chunkf, off, SEEK_SET) == 0 && fread(buf, 1, COL_BYTES, g_chunkf) == COL_BYTES;
}

// Termina el trabajo repartido en curso (si lo hay) de golpe
static void job_finish_now(void)
{
    if (!g_job_active) return;
    while (!gen_default_step(&g_job)) {}
    int i = g_job.cz * CXN + g_job.cx;
    g_job_active = 0;
    g_col_dirty[i] = 0;
    install_column(g_job.cx, g_job.cz, g_job.col);
}

static void job_cancel(void)
{
    if (!g_job_active) return;
    free(g_job.col);
    g_job.col = NULL;
    g_job_active = 0;
}

static int load_column(int cx, int cz)
{
    int i = cz * CXN + cx;
    if (g_cols[i]) return 1;
    u8 *buf = (u8 *)malloc(COL_BYTES);
    if (!buf) return 0;
    if (!read_column_file(i, buf)) { gen_column(buf, cx, cz); g_col_dirty[i] = 0; }
    install_column(cx, cz, buf);
    return 1;
}

int world_stream_pending(int px, int pz)
{
    int pcx = px >> 4, pcz = pz >> 4, n = 0;
    for (int dz = -COL_LOAD_R; dz <= COL_LOAD_R; dz++)
        for (int dx = -COL_LOAD_R; dx <= COL_LOAD_R; dx++) {
            int cx = pcx + dx, cz = pcz + dz;
            if ((unsigned)cx >= CXN || (unsigned)cz >= CZN) continue;
            if (!g_cols[cz * CXN + cx]) n++;
        }
    return n;
}

void world_stream(int px, int pz, int max_loads)
{
    job_finish_now();
    if ((unsigned)px >= WX) px = px < 0 ? 0 : WX - 1;
    if ((unsigned)pz >= WZ) pz = pz < 0 ? 0 : WZ - 1;
    int pcx = px >> 4, pcz = pz >> 4;

    // descargar lo lejano
    int x0 = pcx - COL_LOAD_R - 1, x1 = pcx + COL_LOAD_R + 1;
    int z0 = pcz - COL_LOAD_R - 1, z1 = pcz + COL_LOAD_R + 1;
    if (g_nloaded > (2 * COL_LOAD_R + 1) * (2 * COL_LOAD_R + 1)) {
        for (int i = 0; i < NCOL; i++) {
            if (!g_cols[i]) continue;
            int cx = i % CXN, cz = i / CXN;
            if (cx < x0 || cx > x1 || cz < z0 || cz > z1) unload_column(i);     // si no se puede guardar, se queda en RAM
        }
    }

    // cargar de dentro hacia fuera
    for (int r = 0; r <= COL_LOAD_R && max_loads > 0; r++)
        for (int dz = -r; dz <= r && max_loads > 0; dz++)
            for (int dx = -r; dx <= r && max_loads > 0; dx++) {
                if (abs(dx) != r && abs(dz) != r) continue;
                int cx = pcx + dx, cz = pcz + dz;
                if ((unsigned)cx >= CXN || (unsigned)cz >= CZN) continue;
                if (g_cols[cz * CXN + cx]) continue;
                load_column(cx, cz);
                max_loads--;
            }
}

// Version para el juego: reparte el trabajo. Cada llamada hace como mucho UNA de estas cosas:
// descargar una columna lejana, leer una columna de la SD, o ejecutar una fase de la generacion.
void world_stream_slice(int px, int pz)
{
    static int s_unload_cool;      // fotogramas sin intentar descargar tras un fallo de escritura (SD llena / sacada)
    if ((unsigned)px >= WX) px = px < 0 ? 0 : WX - 1;
    if ((unsigned)pz >= WZ) pz = pz < 0 ? 0 : WZ - 1;
    int pcx = px >> 4, pcz = pz >> 4;
    if (s_unload_cool > 0) s_unload_cool--;

    if (s_unload_cool == 0 && g_nloaded > (2 * COL_LOAD_R + 1) * (2 * COL_LOAD_R + 1)) {
        int x0 = pcx - COL_LOAD_R - 1, x1 = pcx + COL_LOAD_R + 1;
        int z0 = pcz - COL_LOAD_R - 1, z1 = pcz + COL_LOAD_R + 1;
        for (int i = 0; i < NCOL; i++) {
            if (!g_cols[i]) continue;
            int cx = i % CXN, cz = i / CXN;
            if (cx < x0 || cx > x1 || cz < z0 || cz > z1) {
                if (unload_column(i)) return;
                s_unload_cool = 150;           // no se pudo guardar: reintentar dentro de ~5 s
                break;
            }
        }
    }

    if (g_job_active) {
        if (gen_default_step(&g_job)) {
            int i = g_job.cz * CXN + g_job.cx;
            g_job_active = 0;
            g_col_dirty[i] = 0;
            install_column(g_job.cx, g_job.cz, g_job.col);
        }
        return;
    }

    for (int r = 0; r <= COL_LOAD_R; r++)
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dz) != r) continue;
                int cx = pcx + dx, cz = pcz + dz;
                if ((unsigned)cx >= CXN || (unsigned)cz >= CZN) continue;
                int i = cz * CXN + cx;
                if (g_cols[i]) continue;
                if (g_type == WT_FLAT) { load_column(cx, cz); return; }
                u8 *buf = (u8 *)malloc(COL_BYTES);
                if (!buf) return;
                if (read_column_file(i, buf)) { install_column(cx, cz, buf); return; }
                g_job.col = buf; g_job.cx = cx; g_job.cz = cz; g_job.phase = 0;
                g_job_active = 1;
                gen_default_step(&g_job);     // fase 0 (ruido base), barata
                return;
            }
}

// ---- API de guardado ----
void world_save_begin(void) { g_save_err = 0; }
int  world_save_failed(void) { return g_save_err; }
int  world_has_sd(void) { return g_sd_ok; }

// Autoguardado repartido: escribe UNA columna modificada por llamada. Devuelve cuantas quedan.
// Si la escritura falla devuelve 0 (se abandona esta ronda: las columnas siguen sucias y se reintentan en la
// siguiente; world_save_failed() avisa) para no quedarse reintentando cada fotograma.
int world_save_step(void)
{
    static int cur;
    if (!g_chunkf) return 0;
    for (int n = 0; n < NCOL; n++) {
        int i = cur;
        cur = (cur + 1) % NCOL;
        if (g_cols[i] && g_col_dirty[i]) {
            if (!save_column(i)) return 0;
            break;
        }
    }
    int left = 0;
    for (int i = 0; i < NCOL; i++) if (g_cols[i] && g_col_dirty[i]) left++;
    return left;
}

// Cierra el guardado: vuelca el fichero de columnas y confirma level.dat (con el jugador en g_meta). 1 = ok
int world_save_flush(void)
{
    if (!g_chunkf) return 0;
    if (fflush(g_chunkf) != 0) { g_save_err = 1; return 0; }
    return commit_level();
}

// Guardado completo e inmediato (pausa, salir). 1 = todo guardado
int world_save_all(void)
{
    if (!g_chunkf) return 0;
    int ok = 1;
    for (int i = 0; i < NCOL; i++)
        if (g_cols[i] && g_col_dirty[i] && !save_column(i)) ok = 0;
    if (!world_save_flush()) ok = 0;
    return ok;
}

// Carga YA la columna que contiene (x, z) (termina antes el trabajo repartido en curso). 1 = cargada.
// Imprescindible antes de leer el terreno de una zona lejana: world_get() devuelve piedra en columnas descargadas.
int world_ensure_loaded(int x, int z)
{
    if ((unsigned)x >= WX || (unsigned)z >= WZ) return 0;
    int cx = x >> 4, cz = z >> 4;
    if (g_cols[cz * CXN + cx]) return 1;
    job_finish_now();
    if (g_cols[cz * CXN + cx]) return 1;
    return load_column(cx, cz);
}

void world_free_all(void)
{
    job_cancel();
    for (int i = 0; i < NCOL; i++) {
        if (g_cols[i]) { render_column_unloaded(i % CXN, i / CXN); free(g_cols[i]); g_cols[i] = NULL; }
    }
    g_nloaded = 0;
    wf_cache_reset();
    memset(g_col_dirty, 0, sizeof(g_col_dirty));
    if (g_chunkf) { fclose(g_chunkf); g_chunkf = NULL; }
}

void world_close(void)
{
    world_save_all();
    world_free_all();
    g_dir[0] = 0;
}

// --------------------------------------------------------------------------
// Acceso
// --------------------------------------------------------------------------
void world_set(int x, int y, int z, u8 id)
{
    if ((unsigned)x >= WX || (unsigned)y >= WY || (unsigned)z >= WZ) return;
    int ci = (z >> 4) * CXN + (x >> 4);
    u8 *c = g_cols[ci];
    if (!c) return;
    u8 old = c[CIDX(x, y, z)];
    c[CIDX(x, y, z)] = id;
    g_col_dirty[ci] = 1;
    int sx = x / SEC, sy = y / SEC, sz = z / SEC;
    // La rejilla de luz de cada seccion (render.c, LG_PAD = 2) solo ve 2 celdas mas alla de su borde, asi que un
    // cambio solo afecta a las secciones vecinas si el bloque esta a menos de 2 celdas de ese borde.
    int lx = x % SEC, ly = y % SEC, lz = z % SEC;
    int dx0 = (lx < 2 && sx > 0) ? -1 : 0, dx1 = (lx >= SEC - 2 && sx < SXN - 1) ? 1 : 0;
    int dz0 = (lz < 2 && sz > 0) ? -1 : 0, dz1 = (lz >= SEC - 2 && sz < SZN - 1) ? 1 : 0;
    int dy0 = (ly < 2 && sy > 0) ? -1 : 0, dy1 = (ly >= SEC - 2 && sy < SYN - 1) ? 1 : 0;
    if (BLK_IS_TORCH(old) || BLK_IS_TORCH(id) || BLK_FURNACE_LIT(old) || BLK_FURNACE_LIT(id)) {
        // Antes se marcaban SIEMPRE las 27 secciones (3x3x3) alrededor: al poner una antorcha se reconstruian
        // decenas de mallas seguidas y el juego se quedaba tirado unos instantes. Ahora normalmente es 1 (max. 8).
        for (int dy = dy0; dy <= dy1; dy++)
            for (int dz = dz0; dz <= dz1; dz++)
                for (int dx = dx0; dx <= dx1; dx++)
                    g_sec_dirty[SECIDX(sx + dx, sy + dy, sz + dz)] = 1;
        return;
    }
    // Al colocar o quitar un bloque opaco cambia la luz del cielo y el vertex AO de los vecinos (y debajo del techo)
    for (int dy = dy0; dy <= dy1; dy++)
        for (int dz = dz0; dz <= dz1; dz++)
            for (int dx = dx0; dx <= dx1; dx++)
                g_sec_dirty[SECIDX(sx + dx, sy + dy, sz + dz)] = 1;
    if (blk_opaque(old) != blk_opaque(id) && y > 0) {
        int by = y - 1;
        while (by > 0 && !blk_opaque(c[CIDX(lx, by, lz)])) by--;
        int bsy = by / SEC;
        if (bsy < sy) {
            for (int dz = dz0; dz <= dz1; dz++)
                for (int dx = dx0; dx <= dx1; dx++)
                    g_sec_dirty[SECIDX(sx + dx, bsy, sz + dz)] = 1;
        }
    }
}

int world_height(int x, int z)
{
    for (int y = WY - 1; y >= 0; y--) {
        u8 b = world_get(x, y, z);
        if (b != B_AIR && b != B_WATER && !(g_blocks[b].flags & F_PLANT)) return y;
    }
    return 0;
}

void world_spawn_find(void)
{
    g_seed = g_meta.seed;
    g_type = g_meta.worldtype;
    g_spawn_x = WX / 2;
    g_spawn_z = WZ / 2;
    if (g_type == WT_FLAT) { g_spawn_y = 4; return; }
    g_spawn_y = terrain_h(g_spawn_x, g_spawn_z) + 1;
    for (int rad = 0; rad < 160; rad += 2) {
        int found = 0;
        for (int a = -rad; a <= rad && !found; a += 2)
            for (int b = -rad; b <= rad && !found; b += 2) {
                if (abs(a) != rad && abs(b) != rad) continue;
                int x = WX / 2 + a, z = WZ / 2 + b;
                int h = terrain_h(x, z);
                if (h > SEA_LEVEL + 1 && !is_desert(x, z)) {
                    g_spawn_x = x; g_spawn_z = z; g_spawn_y = h + 1; found = 1;
                }
            }
        if (found) break;
    }
}
