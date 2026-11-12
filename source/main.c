// Minecraft: Nintendo DS Edition (BlocksDS)
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game.h"
#include "drops.h"
#include "player.h"
#include "render.h"
#include "ui.h"
#include "menus.h"
#include "world.h"
#include "sfx.h"
#include "mobs.h"
#include "furnace.h"
#include "chest.h"
#include "daynight.h"

GameState g_state = ST_TITLE;
GameState g_options_return = ST_TITLE;
Options g_opt = {1, 1, 0, 1, 1};
NewWorld g_new = {0, WT_DEFAULT, 12345, ""};
int g_frame;
int g_fps;

static Player g_player;
static volatile int g_vblanks;
static int g_last_vbl;   // vblank al inicio del fotograma anterior
static const char *g_splash;

static void vblank_handler(void) { g_vblanks++; }

static const char *splashes[] = {
    "Made by Mojang!", "Awesome!", "Also try Terraria!", "Pocket sized!", "Hello, DS!",
    "Now with stylus!", "Pure DS power!", "100% pixels!", "Dig dig dig!",
    "Don't look down!", "Press START!",
};

static int g_last_pct = -1;
static void gen_progress(int pct)
{
    if (pct == g_last_pct) return;
    g_last_pct = pct;
    menu_loading(pct);
    swiWaitForVBlank();
}

static void blank_frames(void)
{
    for (int i = 0; i < 2; i++) {
        render_blank(0, 0, 0);
        glFlush(0);
        swiWaitForVBlank();
    }
}

static void fill_meta_from_player(void)
{
    g_meta.px = g_player.x; g_meta.py = g_player.y; g_meta.pz = g_player.z;
    g_meta.yaw = g_player.yaw; g_meta.pitch = g_player.pitch;
    g_meta.sel = (u8)g_player.sel;
    for (int i = 0; i < 36; i++) {
        g_meta.inv_id[i] = g_player.inv[i].id;
        g_meta.inv_count[i] = g_player.inv[i].count;
        g_meta.inv_dmg[i] = g_player.inv[i].dmg;
    }
    g_meta.has_player = 1;
}

// extra.dat: datos que level.dat no tiene (armadura, vida/hambre, hornos, hora del dia)
typedef struct {
    char magic[8];
    int health, food, food_sat, food_exh;
    int daytime;
    Slot armor[4];
    Furnace furn[MAX_FURN];
} ExtraSave;

typedef struct { char magic[8]; int has, x, y, z; } BedSave;

// Piezas de extra.dat. Cada fichero se escribe de forma atomica (ver world_file_write).
static void save_extra_player(void)
{
    static ExtraSave ex;
    memset(&ex, 0, sizeof(ex));
    memcpy(ex.magic, "MCDSX001", 8);
    ex.health = g_player.health; ex.food = g_player.food;
    ex.food_sat = g_player.food_sat; ex.food_exh = g_player.food_exh;
    ex.daytime = g_daytime;
    memcpy(ex.armor, g_player.armor, sizeof(ex.armor));
    { struct { char magic[8]; Slot slot; } oh; memset(&oh, 0, sizeof(oh)); memcpy(oh.magic, "MCDSOH01", 8); oh.slot = g_player.offhand; world_file_write("offhand.dat", &oh, (int)sizeof(oh)); }
    memcpy(ex.furn, g_furn, sizeof(ex.furn));
    world_file_write("extra.dat", &ex, (int)sizeof(ex));
    {   // cama de reaparicion (fichero aparte: no cambia el formato de extra.dat)
        BedSave bs;
        memset(&bs, 0, sizeof(bs));
        memcpy(bs.magic, "MCDSBED1", 8);
        bs.has = g_player.has_bed; bs.x = g_player.bed_x; bs.y = g_player.bed_y; bs.z = g_player.bed_z;
        world_file_write("bed.dat", &bs, (int)sizeof(bs));
    }
}
static void save_extra_chests(void) { world_file_write("chests.dat", g_chest, (int)sizeof(g_chest)); }
static void save_extra_drops(void)  { world_file_write("drops.dat", g_drops, (int)sizeof(g_drops)); }

static void save_extra(void)
{
    save_extra_player();
    save_extra_chests();
    save_extra_drops();
}

static void load_extra(void)
{
    static ExtraSave ex;
    furnace_clear();
    chest_clear();
    { struct { char magic[8]; Slot slot; } oh; memset(&oh, 0, sizeof(oh)); if (world_file_read("offhand.dat", &oh, (int)sizeof(oh)) && memcmp(oh.magic, "MCDSOH01", 8) == 0) g_player.offhand = oh.slot; }
    world_file_read("drops.dat", g_drops, (int)sizeof(g_drops));
    {
        BedSave bs;
        if (world_file_read("bed.dat", &bs, (int)sizeof(bs)) && memcmp(bs.magic, "MCDSBED1", 8) == 0 && bs.has) {
            g_player.has_bed = 1; g_player.bed_x = bs.x; g_player.bed_y = bs.y; g_player.bed_z = bs.z;
        }
    }
    if (world_file_read("chests.dat", g_chest, (int)sizeof(g_chest))) {
        for (int i = 0; i < MAX_CHEST; i++) g_chest[i].open_t = 0;
    }
    if (!world_file_read("extra.dat", &ex, (int)sizeof(ex)) || memcmp(ex.magic, "MCDSX001", 8) != 0) return;
    g_player.health = ex.health > 0 ? ex.health : 20; g_player.food = ex.food;
    g_player.food_sat = ex.food_sat; g_player.food_exh = ex.food_exh;
    daynight_set(ex.daytime);
    memcpy(g_player.armor, ex.armor, sizeof(ex.armor));
    memcpy(g_furn, ex.furn, sizeof(ex.furn));
}

// Orden de guardado: primero los ficheros auxiliares (jugador, cofres, objetos), despues las columnas y por ULTIMO
// level.dat, que es el que "confirma" el guardado (ver world.c). Si se corta a mitad se prefiere repetir algo antes
// que perder objetos.
static int g_as_stage;
static void save_game(void)
{
    g_as_stage = 0;
    world_save_begin();
    fill_meta_from_player();
    g_meta.daytime = (u32)g_daytime;
    save_extra();
    world_save_all();
}

// Autoguardado repartido en fotogramas (antes se escribia todo de golpe y el juego se quedaba parado en la SD)
static void autosave_step(void)
{
    switch (g_as_stage) {
    case 1: world_save_begin(); save_extra_player(); g_as_stage = 2; break;
    case 2: save_extra_chests(); g_as_stage = 3; break;
    case 3: save_extra_drops(); g_as_stage = 4; break;
    case 4: if (world_save_step() == 0) g_as_stage = 5; break;        // una columna por fotograma
    case 5:
        fill_meta_from_player(); g_meta.daytime = (u32)g_daytime;
        world_save_flush();
        if (world_save_failed()) { g_player.tip_msg = "Could not save the world!"; g_player.tip_t = 180; }
        g_as_stage = 0;
        break;
    default: g_as_stage = 0; break;
    }
}

static u32 g_ui_sig;
static int g_ui_dirty = 1;

static void start_world(int load)
{
    g_state = ST_LOADING;
    menu_loading(0);
    render_free_world();
    render_loading_bg();
    for (int i = 0; i < 2; i++) { glFlush(0); swiWaitForVBlank(); }

    g_last_pct = -1;
    if (load) {
        if (!world_open(g_load_name)) {
            // level.dat ilegible o danado: antes se creaba un mundo NUEVO en silencio; ahora se vuelve a la lista con un aviso
            g_select_err = 1;
            blank_frames();
            render_free_world();
            gfx_load_menu();
            gfx_prepare_title(g_splash);
            g_state = ST_SELECT;
            return;
        }
    } else {
        world_new(g_new.name, g_new.seed, g_new.worldtype, g_new.gamemode);
    }

    // El punto de aparicion del MUNDO se calcula siempre de la semilla (es determinista). Antes, al cargar una partida
    // se pisaba con la ultima posicion del jugador (p. ej. en pleno salto o volando) y "Respawn" te dejaba ahi: en el aire.
    world_spawn_find();
    int load_x = g_spawn_x, load_z = g_spawn_z;      // columnas que hay que cargar antes de empezar
    if (g_meta.has_player) { load_x = g_meta.px >> 12; load_z = g_meta.pz >> 12; }

    // carga sincrona de las columnas de alrededor (con barra de progreso)
    int total = world_stream_pending(load_x, load_z);
    if (total < 1) total = 1;
    int stalls = 0;
    while (1) {
        int left = world_stream_pending(load_x, load_z);
        gen_progress((total - left) * 100 / total);
        if (left == 0) break;
        world_stream(load_x, load_z, 1);
        // Si malloc() falla (p. ej. ROM arrancada en modo DS con 4 MB de RAM en vez de DSi con 16 MB) la columna no se
        // carga y este bucle no terminaba nunca: la pantalla de carga se quedaba parada. Ahora se detecta y se avisa.
        if (world_stream_pending(load_x, load_z) >= left) {
            if (++stalls >= 3) {
                world_close();
                blank_frames();
                render_free_world();
                gfx_load_menu();
                gfx_prepare_title(g_splash);
                g_select_err = 2;
                g_state = ST_SELECT;
                return;
            }
        } else {
            stalls = 0;
        }
    }

    player_init(&g_player, g_meta.gamemode);
    drops_clear();
    mobs_clear();
    daynight_set(g_meta.daytime ? (int)g_meta.daytime : 1000);
    if (g_meta.has_player) {
        g_player.x = g_meta.px; g_player.y = g_meta.py; g_player.z = g_meta.pz;
        g_player.yaw = g_meta.yaw; g_player.pitch = g_meta.pitch;
        g_player.sel = g_meta.sel < 9 ? g_meta.sel : 0;
        for (int i = 0; i < 36; i++) { g_player.inv[i].id = g_meta.inv_id[i]; g_player.inv[i].count = g_meta.inv_count[i]; g_player.inv[i].dmg = g_meta.inv_dmg[i]; }
    } else {
        // despejar el punto de aparicion (arboles, etc.)
        for (int dy = 0; dy < 3; dy++) world_set(g_spawn_x, g_spawn_y + dy, g_spawn_z, B_AIR);
    }
    load_extra();
    mobs_populate(&g_player);
    render_prebuild(&g_player, 400);   // antes de tocar la VRAM: el fondo sigue visible
    gfx_load_game();
    menu_loading(100);
    g_state = ST_PLAY;
    g_last_vbl = g_vblanks;   // la carga no cuenta como tiempo de juego
}

static void quit_to_title(void)
{
    fill_meta_from_player();
    g_meta.daytime = (u32)g_daytime;
    save_extra();
    world_close();
    blank_frames();
    render_free_world();
    gfx_load_menu();
    g_splash = splashes[rand() % (sizeof(splashes) / sizeof(splashes[0]))];
    gfx_prepare_title(g_splash);
    g_state = ST_TITLE;
}

int main(void)
{
    srand((unsigned)time(NULL));
    if (isDSiMode()) setCpuClock(true);     // DSi: ARM9 a 134 MHz (el doble que la DS), muy notable en FPS
    gfx_init();
    ui_hw_init();
    sfx_init();
    lcdMainOnTop();
    world_sd_init();

    irqSet(IRQ_VBLANK, vblank_handler);
    irqEnable(IRQ_VBLANK);

    gfx_load_menu();
    g_splash = splashes[rand() % (sizeof(splashes) / sizeof(splashes[0]))];
    gfx_prepare_title(g_splash);

    g_last_vbl = g_vblanks;
    int fps_mark = g_vblanks, fps_count = 0;

    while (1) {
        // Ticks de fisica transcurridos desde el inicio del fotograma anterior.
        // (Antes se tomaba la marca al FINAL del fotograma, asi que cuando un
        // fotograma tardaba mas de un vblank se perdian ticks y Steve iba lento.)
        int now_vbl = g_vblanks;
        int ticks = now_vbl - g_last_vbl;
        g_last_vbl = now_vbl;
        if (ticks < 1) ticks = 1;
        if (ticks > 6) ticks = 6;

        {   // al cambiar de estado hay que redibujar la pantalla tactil
            static int prev_state = -1;
            if ((int)g_state != prev_state) { g_ui_dirty = 1; prev_state = (int)g_state; }
        }

        scanKeys();
        u32 down = keysDown(), held = keysHeld();
        ui_poll();
        g_frame++;
        fps_count++;
        if (g_vblanks - fps_mark >= 60) { g_fps = fps_count * 60 / (g_vblanks - fps_mark); fps_count = 0; fps_mark = g_vblanks; }

        static int s_prev_state, s_b_lock;
        int was_state = s_prev_state;
        s_prev_state = g_state;
        switch (g_state) {
            case ST_TITLE:
            case ST_SELECT:
            case ST_CREATE:
            case ST_OPTIONS:
                // Desde la partida (START -> Options) las texturas cargadas son las del juego:
                // no existen el panorama ni el logo, y render_title() salia todo blanco.
                // En ese caso se dibuja el mundo congelado detras, como hace la pausa
                // (ademas el cambio de FOV / distancia se ve en directo).
                if (g_state == ST_OPTIONS && g_options_return == ST_PAUSE)
                    render_game(&g_player, 1);
                else
                    render_title(g_frame, g_splash);
                ui_begin();
                if (g_state == ST_TITLE) menu_title();
                else if (g_state == ST_SELECT) menu_select();
                else if (g_state == ST_CREATE) menu_create();
                else menu_options();
                ui_present();
                if (g_request_start) { g_request_start = 0; glFlush(0); start_world(g_request_load); g_request_load = 0; }
                break;

            case ST_PLAY: {
                if (down & KEY_SELECT) g_player.cam_mode = (g_player.cam_mode + 1) % 3;
                if (down & KEY_START) { g_state = ST_PAUSE; save_game(); }
                if (g_player.want_craft) { g_player.want_craft = 0; g_craft_table = 1; g_furnace_open = -1; g_chest_open = -1; g_chest_open2 = -1; g_state = ST_INVENTORY; }
                if (g_player.want_furnace) {
                    g_player.want_furnace = 0;
                    int fi = furnace_get(g_player.fx, g_player.fy, g_player.fz);
                    if (fi >= 0) { g_craft_table = 0; g_furnace_open = fi; g_chest_open = -1; g_chest_open2 = -1; g_state = ST_INVENTORY; }
                }
                if (g_player.want_chest) {
                    g_player.want_chest = 0;
                    if (chest_can_open(g_player.fx, g_player.fy, g_player.fz)) {
                        int c1 = -1, c2 = -1;
                        if (chest_open_pair(g_player.fx, g_player.fy, g_player.fz, &c1, &c2)) {
                            g_craft_table = 0; g_furnace_open = -1;
                            g_chest_open = c1; g_chest_open2 = c2;
                            sfx_chest_open();
                            g_state = ST_INVENTORY;
                        }
                    }
                }
                world_stream_slice(g_player.x >> 12, g_player.z >> 12);
                menu_touch_controls(&held, &down);
                // B cierra inventario / mesa / horno: ese B no debe contar como "agacharse" al volver al juego
                if (was_state == ST_INVENTORY) s_b_lock = 1;
                if (s_b_lock) {
                    if (held & KEY_B) { held &= ~KEY_B; down &= ~KEY_B; }
                    else s_b_lock = 0;
                }
                for (int t = 0; t < ticks; t++) {
                    player_update(&g_player, held, t == 0 ? down : 0);
                    drops_update(&g_player);
                    mobs_update(&g_player);
                    furnace_update();
                    chest_update(g_chest_open, g_chest_open2);
                    daynight_tick();
                }
                if (g_player.health <= 0) {
                    g_state = ST_DEATH;
                    ui_begin();
                    menu_death(&g_player);
                    ui_present();
                    render_game(&g_player, 1);
                    break;
                }

                // rendimiento: la pantalla tactil solo se vuelve a dibujar si algo cambia (texto, hotbar, lapiz)
                u32 sig = menu_play_sig(&g_player);
                int redraw = g_ui_dirty || sig != g_ui_sig || ui_t.down || ui_t.released;
                if (redraw) ui_begin(); else ui_draw_on = 0;
                int act = menu_play_hud(&g_player);
                ui_draw_on = 1;
                if (act == 1) { g_craft_table = 0; g_furnace_open = -1; g_chest_open = -1; g_chest_open2 = -1; g_state = ST_INVENTORY; }
                else if (act == 2) { g_state = ST_PAUSE; save_game(); }
                if (g_frame % 1800 == 0 && !g_as_stage) g_as_stage = 1;   // autoguardado (~1 min a 30 FPS), repartido
                if (g_as_stage) autosave_step();
                if (redraw) { ui_present(); g_ui_sig = sig; g_ui_dirty = 0; }
                render_game(&g_player, 1);
                break;
            }

            case ST_PAUSE:
                if (down & KEY_START) g_state = ST_PLAY;
                ui_begin();
                menu_pause();
                ui_present();
                render_game(&g_player, 1);
                if (g_request_title) { g_request_title = 0; glFlush(0); quit_to_title(); }
                break;

            case ST_INVENTORY:
                for (int t = 0; t < ticks; t++) { furnace_update(); chest_update(g_chest_open, g_chest_open2); daynight_tick(); }   // el horno sigue trabajando
                if (g_player.health <= 0) { menu_inventory_close(&g_player); player_drop_all(&g_player); g_state = ST_DEATH; break; }
                if (down & KEY_START) g_state = ST_PLAY;   // B lo gestiona el inventario (devolver item / cerrar)
                ui_begin();
                menu_inventory(&g_player);
                ui_present();
                render_game(&g_player, 1);
                if (g_state != ST_INVENTORY) menu_inventory_close(&g_player);
                break;

            case ST_DEATH:
                world_stream_slice(g_player.x >> 12, g_player.z >> 12);
                for (int t = 0; t < ticks; t++) {
                    player_update(&g_player, 0, 0);
                    drops_update(&g_player);
                    mobs_update(&g_player);
                    furnace_update();
                    chest_update(-1, -1);
                    daynight_tick();
                }
                ui_begin();
                menu_death(&g_player);
                ui_present();
                render_game(&g_player, 1);
                if (g_request_title) { g_request_title = 0; glFlush(0); quit_to_title(); }
                break;

            default:
                g_state = ST_TITLE;
                break;
        }

        glFlush(0);
        // FPS anclados a 30: un fotograma dura como minimo 2 vblanks (60 Hz)
        do { swiWaitForVBlank(); } while (g_vblanks - now_vbl < 2);
    }
    return 0;
}
