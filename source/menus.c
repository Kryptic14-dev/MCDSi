#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "menus.h"
#include "furnace.h"
#include "chest.h"
#include "game.h"
#include "ui.h"
#include "blocks.h"
#include "world.h"
#include "items.h"
#include "drops.h"

int g_request_start;
int g_request_title;

#define CX 128

// ---------------------------------------------------------------------------
// Titulo
// ---------------------------------------------------------------------------
void menu_title(void)
{
    // columna centrada (200 px) en vertical: 3 botones + fila de 2
    ui_button(28, 30, 200, "Singleplayer", 1);
    ui_button(28, 56, 200, "Multiplayer", 0);
    ui_button(28, 82, 200, "Minecraft Realms", 0);
    ui_button(28, 122, 98, "Options...", 1);
    ui_button(130, 122, 98, "Quit Game", 1);

    if (ui_clicked(28, 30, 200, 20)) g_state = ST_SELECT;
    if (ui_clicked(28, 122, 98, 20)) { g_options_return = ST_TITLE; g_state = ST_OPTIONS; }
    if (ui_clicked(130, 122, 98, 20)) exit(0);
}

// ---------------------------------------------------------------------------
// Seleccion de mundo: lista las carpetas de sd:/MinecraftDS/Worlds
// ---------------------------------------------------------------------------
int g_request_load;
char g_load_name[32];
int g_select_err;       // 1 = el ultimo mundo elegido no se pudo abrir; 2 = no hay RAM suficiente para cargarlo

void menu_select(void)
{
    static char names[24][32];
    static int n, sel = -1, scroll, last_frame = -10;
    static int confirm_del;                 // 1 = pidiendo confirmacion para borrar names[sel]
    if (g_frame - last_frame > 1) {         // acabamos de entrar en el menu: releer la SD
        n = world_list(names, 24);
        sel = -1; scroll = 0; confirm_del = 0;
    }
    last_frame = g_frame;

    // ---- confirmacion de borrado ----
    if (confirm_del && sel >= 0 && sel < n) {
        ui_text_center(CX, 30, "Are you sure you want to delete", UIC_WHITE, 1);
        ui_text_center(CX, 44, "this world?", UIC_WHITE, 1);
        ui_text_center(CX, 66, names[sel], UIC_YELLOW, 1);
        ui_text_center(CX, 86, "will be lost forever! (A long time!)", UIC_GRAY, 0);
        ui_button(28, 114, 200, "Delete", 1);
        ui_button(28, 140, 200, "Cancel", 1);
        if (ui_clicked(28, 114, 200, 20)) {
            world_delete(names[sel]);
            n = world_list(names, 24);       // releer la lista
            sel = -1; scroll = 0; confirm_del = 0;
        } else if (ui_clicked(28, 140, 200, 20)) {
            confirm_del = 0;
        }
        return;
    }
    confirm_del = 0;

    ui_text_center(CX, 8, "Select World", UIC_WHITE, 1);
    ui_rect(20, 24, 196, 84, UIC_BLACK);
    ui_rect(21, 25, 194, 82, UIC_DGRAY);
    ui_rect(22, 26, 192, 80, UIC_BLACK);
    if (n == 0) {
        ui_text_center(CX, 58, world_sd_init() ? "No saved worlds" : "SD card not found", UIC_GRAY, 0);
        ui_text_center(CX, 70, "sd:/MinecraftDS/Worlds", UIC_GRAY, 0);
    }
    for (int i = 0; i < 4; i++) {
        int k = scroll + i;
        if (k >= n) break;
        int y = 28 + i * 19;
        if (k == sel) ui_rect(23, y - 1, 190, 18, UIC_DGRAY);
        ui_text(28, y + 5, names[k], k == sel ? UIC_YELLOW : UIC_WHITE, 1);
        if (ui_clicked(23, y - 1, 190, 18)) sel = k;
    }
    ui_button_h(218, 24, 18, 40, "^", scroll > 0);
    ui_button_h(218, 68, 18, 40, "v", scroll + 4 < n);
    if (scroll > 0 && ui_clicked(218, 24, 18, 40)) scroll--;
    if (scroll + 4 < n && ui_clicked(218, 68, 18, 40)) scroll++;

    // fila 1: jugar (ancho) + borrar
    ui_button(28, 114, 134, "Play Selected World", sel >= 0);
    ui_button(166, 114, 62, "Delete", sel >= 0);
    ui_button(28, 140, 98, "Create New World", 1);
    ui_button(130, 140, 98, "Cancel", 1);
    if (sel >= 0 && ui_clicked(28, 114, 134, 20)) {
        strncpy(g_load_name, names[sel], 31);
        g_load_name[31] = 0;
        g_request_load = 1;
        g_request_start = 1;
    }
    if (sel >= 0 && ui_clicked(166, 114, 62, 20)) confirm_del = 1;
    if (ui_clicked(28, 140, 98, 20)) {
        g_new.seed = hash32(g_frame, 7, 99) ^ (u32)time(NULL);
        g_state = ST_CREATE;
    }
    if (ui_clicked(130, 140, 98, 20)) g_state = ST_TITLE;

    if (g_select_err) {
        if (g_select_err == 2) {
            ui_text_center(CX, 166, "Not enough RAM to load the world", UIC_RED, 0);
            ui_text_center(CX, 177, "(run the ROM in DSi mode: 16 MB)", UIC_GRAY, 0);
        } else {
            ui_text_center(CX, 166, "Could not open that world", UIC_RED, 0);
            ui_text_center(CX, 177, "(damaged level.dat - not overwritten)", UIC_GRAY, 0);
        }
        if (ui_t.pressed) g_select_err = 0;
    }
}

// ---------------------------------------------------------------------------
// Crear mundo
// ---------------------------------------------------------------------------
static int kb_open, kb_shift;

// teclado en pantalla para el nombre del mundo
static void name_keyboard(void)
{
    static const char *rows[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_-"};
    int len = (int)strlen(g_new.name);

    ui_text_center(CX, 6, "World Name", UIC_WHITE, 1);
    ui_rect(28, 20, 200, 16, UIC_BLACK);
    ui_rect(29, 21, 198, 14, UIC_DGRAY);
    char shown[32];
    snprintf(shown, sizeof(shown), "%s%s", g_new.name, (g_frame & 16) ? "_" : "");
    ui_text(34, 24, shown, UIC_WHITE, 1);

    for (int r = 0; r < 4; r++) {
        int n = (int)strlen(rows[r]);
        int x0 = 128 - (n * 22) / 2;
        for (int i = 0; i < n; i++) {
            char c = rows[r][i];
            if (c >= 'A' && c <= 'Z' && !kb_shift) c = (char)(c + 32);
            char lab[2] = {c, 0};
            int x = x0 + i * 22, y = 42 + r * 20;
            ui_button_h(x, y, 20, 18, lab, 1);
            if (ui_clicked(x, y, 20, 18) && len < 20) { g_new.name[len] = c; g_new.name[len + 1] = 0; len++; }
        }
    }
    ui_button_h(28, 124, 50, 18, kb_shift ? "ABC" : "abc", 1);
    ui_button_h(82, 124, 92, 18, "Space", 1);
    ui_button_h(178, 124, 50, 18, "Del", 1);
    if (ui_clicked(28, 124, 50, 18)) kb_shift ^= 1;
    if (ui_clicked(82, 124, 92, 18) && len > 0 && len < 20) { g_new.name[len] = '_'; g_new.name[len + 1] = 0; }
    if (ui_clicked(178, 124, 50, 18) && len > 0) g_new.name[len - 1] = 0;

    ui_button(28, 150, 98, "Done", 1);
    ui_button(130, 150, 98, "Clear", 1);
    if (ui_clicked(28, 150, 98, 20)) kb_open = 0;
    if (ui_clicked(130, 150, 98, 20)) g_new.name[0] = 0;
}

void menu_create(void)
{
    char buf[64];
    if (kb_open) { name_keyboard(); return; }
    ui_text_center(CX, 6, "Create New World", UIC_WHITE, 1);

    snprintf(buf, sizeof(buf), "Game Mode: %s", g_new.gamemode ? "Creative" : "Survival");
    ui_button(28, 24, 200, buf, 1);
    ui_text_center(CX, 46, g_new.gamemode ? "Unlimited resources, free flying, instant mining"
                                           : "Mine blocks, collect them and build", UIC_GRAY, 0);

    snprintf(buf, sizeof(buf), "World Type: %s", g_new.worldtype == WT_FLAT ? "Superflat" : "Default");
    ui_button(28, 62, 200, buf, 1);

    snprintf(buf, sizeof(buf), "Seed: %u", (unsigned)g_new.seed);
    ui_button(28, 90, 200, buf, 1);
    ui_text_center(CX, 112, "Tap the seed button to randomize", UIC_GRAY, 0);

    snprintf(buf, sizeof(buf), "Name: %s", g_new.name[0] ? g_new.name : "(auto)");
    ui_button(28, 124, 200, buf, 1);

    ui_button(28, 156, 98, "Create New World", 1);
    ui_button(130, 156, 98, "Cancel", 1);

    if (ui_clicked(28, 24, 200, 20)) g_new.gamemode ^= 1;
    if (ui_clicked(28, 62, 200, 20)) g_new.worldtype = (g_new.worldtype == WT_FLAT) ? WT_DEFAULT : WT_FLAT;
    if (ui_clicked(28, 90, 200, 20)) g_new.seed = hash32(g_new.seed, g_frame, 31337);
    if (ui_clicked(28, 124, 200, 20)) kb_open = 1;
    if (ui_clicked(28, 156, 98, 20)) { g_request_load = 0; g_request_start = 1; }
    if (ui_clicked(130, 156, 98, 20)) g_state = ST_SELECT;
}

// ---------------------------------------------------------------------------
// Opciones
// ---------------------------------------------------------------------------
void menu_options(void)
{
    static const char *rd[4] = {"Tiny", "Short", "Normal", "Far"};
    static const char *sn[3] = {"Low", "Medium", "High"};
    char buf[48];

    ui_text_center(CX, 8, "Options", UIC_WHITE, 1);
    snprintf(buf, sizeof(buf), "Render Distance: %s", rd[g_opt.render_dist & 3]);
    ui_button(28, 28, 200, buf, 1);
    snprintf(buf, sizeof(buf), "Camera Sensitivity: %s", sn[g_opt.sens % 3]);
    ui_button(28, 54, 200, buf, 1);
    snprintf(buf, sizeof(buf), "Invert Y: %s", g_opt.invert_y ? "ON" : "OFF");
    ui_button(28, 80, 200, buf, 1);
    snprintf(buf, sizeof(buf), "FOV: %d", opt_fov_degrees());
    ui_button(28, 106, 200, buf, 1);
    snprintf(buf, sizeof(buf), "Auto-Jump: %s", g_opt.autojump ? "ON" : "OFF");
    ui_button(28, 132, 200, buf, 1);
    ui_button(28, 160, 200, "Done", 1);

    if (ui_clicked(28, 28, 200, 20)) g_opt.render_dist = (g_opt.render_dist + 1) & 3;
    if (ui_clicked(28, 54, 200, 20)) g_opt.sens = (g_opt.sens + 1) % 3;
    if (ui_clicked(28, 80, 200, 20)) g_opt.invert_y ^= 1;
    if (ui_clicked(28, 106, 200, 20)) g_opt.fov = (g_opt.fov + 1) % 3;
    if (ui_clicked(28, 132, 200, 20)) g_opt.autojump ^= 1;
    if (ui_clicked(28, 160, 200, 20)) g_state = g_options_return;
}

// ---------------------------------------------------------------------------
// Pausa
// ---------------------------------------------------------------------------
void menu_pause(void)
{
    ui_text_center(CX, 20, "Game menu", UIC_WHITE, 1);
    ui_button(28, 56, 200, "Back to Game", 1);
    ui_button(28, 84, 200, "Options...", 1);
    ui_button(28, 112, 200, "Save and Quit to Title", 1);
    // resultado del guardado que se hizo al entrar en la pausa
    if (!world_has_sd())          ui_text_center(CX, 146, "No SD card: progress is NOT saved", UIC_RED, 0);
    else if (world_save_failed()) ui_text_center(CX, 146, "Could not save! Check the SD card", UIC_RED, 0);
    else                          ui_text_center(CX, 146, "World saved", UIC_GRAY, 0);
    if (ui_clicked(28, 56, 200, 20)) g_state = ST_PLAY;
    if (ui_clicked(28, 84, 200, 20)) { g_options_return = ST_PAUSE; g_state = ST_OPTIONS; }
    if (ui_clicked(28, 112, 200, 20)) g_request_title = 1;
}

// ---------------------------------------------------------------------------
// Pantalla de muerte (GuiGameOver de Minecraft 1.7.10)
// ---------------------------------------------------------------------------
void menu_death(Player *p)
{
    int ready = (p->death_t >= 20);
    ui_text_center(CX, 28, "You died!", UIC_RED, 1);
    ui_text_center(CX, 46, "Your items dropped where you died", UIC_GRAY, 0);
    ui_button(28, 78, 200, "Respawn", ready);
    ui_button(28, 106, 200, "Title screen", ready);
    if (ready && (ui_clicked(28, 78, 200, 20) || (keysDown() & (KEY_A | KEY_START)))) {
        player_respawn(p);
        g_state = ST_PLAY;
    } else if (ready && ui_clicked(28, 106, 200, 20)) {
        player_respawn(p);
        g_request_title = 1;
    }
}

// ---------------------------------------------------------------------------
// Carga
// ---------------------------------------------------------------------------
void menu_loading(int pct)
{
    ui_begin();
    ui_text_center(CX, 66, "Generating level", UIC_WHITE, 1);
    ui_text_center(CX, 80, "Building terrain", UIC_GRAY, 0);
    ui_rect(78, 100, 100, 4, UIC_BLACK);
    ui_rect(78, 100, pct, 4, UIC_GREEN);
    ui_present();
}

// ---------------------------------------------------------------------------
// Inventario
// ---------------------------------------------------------------------------
int g_craft_table;          // 0 = cuadricula 2x2 del inventario, 1 = mesa de crafteo 3x3
int g_furnace_open = -1;    // indice del horno abierto (-1 = ninguno)
int g_chest_open = -1;      // indice del cofre abierto (-1 = ninguno)
int g_chest_open2 = -1;     // segundo cofre si es cofre doble (-1 = simple)

static Slot g_hand;         // item "en la mano" (cursor)
static Slot g_grid[9];      // cuadricula de crafteo (2x2 usa los indices 0,1,3,4)
static Slot g_out;          // resultado
static int g_cur;           // slot seleccionado (cursor)

enum { K_NONE, K_FIN, K_FFUEL, K_FOUT, K_ARM0 };   // tipos de hueco especial (K_ARM0 + pieza = armadura)
typedef struct { int x, y; Slot *s; int out; int grid; int kind; } Ref;
static Ref refs[96];
static int nref;

static void slot_clear(Slot *s) { s->id = 0; s->count = 0; s->dmg = 0; }

static void recompute_out(void)
{
    u8 ids[9];
    for (int i = 0; i < 9; i++) ids[i] = g_grid[i].id;
    int id, n;
    if (craft_match(ids, &id, &n)) { g_out.id = (u8)id; g_out.count = (u8)n; g_out.dmg = 0; }
    else slot_clear(&g_out);
}

static int can_place(int kind, u8 id)
{
    switch (kind) {
        case K_FFUEL: return fuel_time(id) > 0;
        case K_FOUT: return 0;
        case K_FIN: return 1;
        default: break;
    }
    if (kind >= K_ARM0) return armor_piece(id) == kind - K_ARM0;
    return 1;
}

static void build_refs(Player *p, int ox, int oy)
{
    nref = 0;
    if (g_chest_open >= 0 && g_chest_open2 >= 0) {
        Chest *c1 = &g_chest[g_chest_open], *c2 = &g_chest[g_chest_open2];
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 9; c++)
                refs[nref++] = (Ref){ox + 8 + c * 18, oy + 4 + r * 18, &c1->slots[r * 9 + c], 0, 0, K_NONE};
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 9; c++)
                refs[nref++] = (Ref){ox + 8 + c * 18, oy + 58 + r * 18, &c2->slots[r * 9 + c], 0, 0, K_NONE};
        for (int i = 9; i < 36; i++)
            refs[nref++] = (Ref){ox + 8 + ((i - 9) % 9) * 18, oy + 115 + ((i - 9) / 9) * 18, &p->inv[i], 0, 0, K_NONE};
        for (int i = 0; i < 9; i++)
            refs[nref++] = (Ref){ox + 8 + i * 18, oy + 172, &p->inv[i], 0, 0, K_NONE};
        return;
    } else if (g_chest_open >= 0) {
        Chest *c1 = &g_chest[g_chest_open];
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 9; c++)
                refs[nref++] = (Ref){ox + 8 + c * 18, oy + 18 + r * 18, &c1->slots[r * 9 + c], 0, 0, K_NONE};
    } else if (g_furnace_open >= 0) {
        Furnace *f = &g_furn[g_furnace_open];
        refs[nref++] = (Ref){ox + 56,  oy + 17, &f->in,   0, 0, K_FIN};
        refs[nref++] = (Ref){ox + 56,  oy + 53, &f->fuel, 0, 0, K_FFUEL};
        refs[nref++] = (Ref){ox + 116, oy + 35, &f->out,  0, 0, K_FOUT};
    } else if (g_craft_table) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                refs[nref++] = (Ref){ox + 30 + c * 18, oy + 17 + r * 18, &g_grid[r * 3 + c], 0, 1, K_NONE};
        refs[nref++] = (Ref){ox + 124, oy + 35, &g_out, 1, 0, K_NONE};
    } else {
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 2; c++)
                refs[nref++] = (Ref){ox + 88 + c * 18, oy + 26 + r * 18, &g_grid[r * 3 + c], 0, 1, K_NONE};
        refs[nref++] = (Ref){ox + 144, oy + 36, &g_out, 1, 0, K_NONE};
        for (int a = 0; a < 4; a++)
            refs[nref++] = (Ref){ox + 8, oy + 8 + a * 18, &p->armor[a], 0, 0, K_ARM0 + a};
        refs[nref++] = (Ref){ox + 62, oy + 62, &p->offhand, 0, 0, K_NONE};
    }
    for (int i = 9; i < 36; i++)
        refs[nref++] = (Ref){ox + 8 + ((i - 9) % 9) * 18, oy + 84 + ((i - 9) / 9) * 18, &p->inv[i], 0, 0, K_NONE};
    for (int i = 0; i < 9; i++)
        refs[nref++] = (Ref){ox + 8 + i * 18, oy + 142, &p->inv[i], 0, 0, K_NONE};
}

static void move_cursor(int dirx, int diry)
{
    int best = -1, bs = 0x7FFFFFFF;
    for (int j = 0; j < nref; j++) {
        if (j == g_cur) continue;
        int dx = refs[j].x - refs[g_cur].x, dy = refs[j].y - refs[g_cur].y;
        int prim = dirx ? dx * dirx : dy * diry;
        int perp = dirx ? abs(dy) : abs(dx);
        if (prim <= 0) continue;
        int sc = prim + perp * 3;
        if (sc < bs) { bs = sc; best = j; }
    }
    if (best >= 0) g_cur = best;
}

static void consume_grid(void)
{
    for (int i = 0; i < 9; i++)
        if (g_grid[i].id) { if (--g_grid[i].count == 0) slot_clear(&g_grid[i]); }
}

// A: coger / soltar la pila entera / apilar / intercambiar / sacar el resultado
static void op_a(int i)
{
    Ref *r = &refs[i];
    if (r->out) {
        if (!g_out.id) return;
        int max = item_max_stack(g_out.id);
        if (!g_hand.id) { g_hand = g_out; }
        else if (g_hand.id == g_out.id && max > 1 && g_hand.count + g_out.count <= max) g_hand.count += g_out.count;
        else return;
        consume_grid();
        return;
    }
    Slot *s = r->s;
    if (r->kind == K_FOUT) {                 // salida del horno: solo se puede sacar
        if (!s->id) return;
        int max = item_max_stack(s->id);
        if (!g_hand.id) { g_hand = *s; slot_clear(s); }
        else if (g_hand.id == s->id && max > 1 && g_hand.count + s->count <= max) { g_hand.count += s->count; slot_clear(s); }
        return;
    }
    if (g_hand.id && !can_place(r->kind, g_hand.id)) return;
    if (!g_hand.id) { g_hand = *s; slot_clear(s); }
    else if (!s->id) { *s = g_hand; slot_clear(&g_hand); }
    else if (s->id == g_hand.id && item_max_stack(s->id) > 1) {
        int room = item_max_stack(s->id) - s->count;
        int mv = g_hand.count < room ? g_hand.count : room;
        s->count += mv;
        g_hand.count -= mv;
        if (!g_hand.count) slot_clear(&g_hand);
    } else { Slot t = *s; *s = g_hand; g_hand = t; }
}

// X: dividir la pila / poner de 1 en 1
static void op_x(int i)
{
    Ref *r = &refs[i];
    if (r->out || r->kind == K_FOUT) { op_a(i); return; }
    Slot *s = r->s;
    if (!g_hand.id) {
        if (!s->id) return;
        if (s->count <= 1) { g_hand = *s; slot_clear(s); return; }
        int n = (s->count + 1) / 2;
        g_hand.id = s->id; g_hand.count = (u8)n; g_hand.dmg = 0;
        s->count -= (u8)n;
    } else {
        int max = item_max_stack(g_hand.id);
        if (!can_place(r->kind, g_hand.id)) return;
        if (!s->id) { *s = g_hand; s->count = 1; }
        else if (s->id == g_hand.id && max > 1 && s->count < max) s->count++;
        else return;
        if (--g_hand.count == 0) slot_clear(&g_hand);
    }
}

// R: juntar en la mano todas las pilas iguales.
// Solo recoge del inventario y la hotbar (nunca de la cuadricula de crafteo ni del resultado,
// que era lo que hacia que "desaparecieran" los items). Con la mano vacia, primero coge la pila
// bajo el cursor y despues junta las demas.
static void op_r(void)
{
    if (!g_hand.id) {
        Ref *c = &refs[g_cur];
        if (c->out || c->kind || !c->s->id || item_max_stack(c->s->id) <= 1) return;
        g_hand = *c->s;
        slot_clear(c->s);
    }
    int max = item_max_stack(g_hand.id);
    if (max <= 1) return;
    for (int j = 0; j < nref && g_hand.count < max; j++) {
        if (refs[j].out || refs[j].grid || refs[j].kind) continue;
        Slot *s = refs[j].s;
        if (s->id != g_hand.id) continue;
        int take = max - g_hand.count;
        if (take > s->count) take = s->count;
        g_hand.count += (u8)take;
        s->count -= (u8)take;
        if (!s->count) slot_clear(s);
    }
}

// B: devolver el item de la mano al inventario; con la mano vacia, cerrar
static void op_b(Player *p)
{
    if (g_hand.id) { g_hand.count = (u8)inv_give(p, g_hand); if (!g_hand.count) slot_clear(&g_hand); }
    else g_state = ST_PLAY;
}

static void box(int x, int y, int w, int h, int c)
{
    ui_rect(x, y, w, 1, c); ui_rect(x, y + h - 1, w, 1, c);
    ui_rect(x, y, 1, h, c); ui_rect(x + w - 1, y, 1, h, c);
}

static void inv_survival(Player *p)
{
    const int is_dbl_chest = (g_chest_open >= 0 && g_chest_open2 >= 0);
    const int ox = 40, oy = is_dbl_chest ? 0 : 16;
    if (is_dbl_chest) {
        ui_blit(UI_CHEST_DBL_X, UI_CHEST_DBL_Y, UI_CHEST_DBL_W, UI_CHEST_DBL_H, ox, oy);
    } else if (g_chest_open >= 0) {
        ui_blit(UI_CHEST_X, UI_CHEST_Y, UI_CHEST_W, UI_CHEST_H, ox, oy);
        ui_text(ox + 8, oy + 6, "Chest", UIC_DGRAY, 0);
        ui_text(ox + 8, oy + 72, "Inventory", UIC_DGRAY, 0);
    } else if (g_furnace_open >= 0) {
        const Furnace *f = &g_furn[g_furnace_open];
        ui_blit(UI_FURNACE_X, UI_FURNACE_Y, UI_FURNACE_W, UI_FURNACE_H, ox, oy);
        ui_text(ox + 60, oy + 6, "Furnace", UIC_DGRAY, 0);
        if (f->burn > 0 && f->burn_max > 0) {           // llama (se vacia al gastarse el combustible)
            int n = f->burn * 12 / f->burn_max;
            ui_blit(UI_FURN_FLAME_X, UI_FURN_FLAME_Y + 12 - n, 14, n + 2, ox + 56, oy + 36 + 12 - n);
        }
        int pr = f->cook * 24 / SMELT_TICKS;             // flecha de progreso
        if (pr > 0) ui_blit(UI_FURN_ARROW_X, UI_FURN_ARROW_Y, pr + 1, 16, ox + 79, oy + 34);
    } else if (g_craft_table) ui_blit(UI_CRAFT_X, UI_CRAFT_Y, UI_CRAFT_W, UI_CRAFT_H, ox, oy);
    else {
        ui_blit(UI_INV_X, UI_INV_Y, UI_INV_W, UI_INV_H, ox, oy);
        ui_text(ox + 86, oy + 16, "Crafting", UIC_DGRAY, 0);
        // armadura: iconos de hueco vacio (casco, pechera, pantalones, botas), como en Minecraft
        if (!p->armor[0].id) ui_blit(UI_ARMOR_HELMET_X, UI_ARMOR_HELMET_Y, 16, 16, ox + 8, oy + 8);
        if (!p->armor[1].id) ui_blit(UI_ARMOR_CHEST_X,  UI_ARMOR_CHEST_Y,  16, 16, ox + 8, oy + 26);
        if (!p->armor[2].id) ui_blit(UI_ARMOR_LEGS_X,   UI_ARMOR_LEGS_Y,   16, 16, ox + 8, oy + 44);
        if (!p->armor[3].id) ui_blit(UI_ARMOR_BOOTS_X,  UI_ARMOR_BOOTS_Y,  16, 16, ox + 8, oy + 62);
        ui_text(ox + 60, oy + 54, "Off", UIC_DGRAY, 0);
        ui_slot(&p->offhand, ox + 62, oy + 62, 1);
        // Steve en el recuadro negro (x 26..77, y 8..77): 32x64 centrado
        ui_blit(UI_STEVE_PREVIEW_X, UI_STEVE_PREVIEW_Y, UI_STEVE_PREVIEW_W, UI_STEVE_PREVIEW_H, ox + 36, oy + 11);
        {   // armadura puesta, encima de la skin: pantalones, botas, peto y casco (mismo orden que en Minecraft)
            static const u8 order[4] = {AP_LEGS, AP_BOOTS, AP_CHEST, AP_HELMET};
            for (int k = 0; k < 4; k++) {
                const Slot *a = &p->armor[order[k]];
                if (!a->id || armor_piece(a->id) != order[k]) continue;
                int m = armor_mat(a->id);
                ui_blit(UI_ARMPV_XY[m][order[k]][0], UI_ARMPV_XY[m][order[k]][1], 32, 64, ox + 36, oy + 11);
            }
        }
    }
    build_refs(p, ox, oy);
    if (g_cur >= nref) g_cur = nref - 1;
    if (g_furnace_open < 0 && g_chest_open < 0) recompute_out();

    u32 kd = keysDown();
    if (kd & KEY_RIGHT) move_cursor(1, 0);
    if (kd & KEY_LEFT) move_cursor(-1, 0);
    if (kd & KEY_DOWN) move_cursor(0, 1);
    if (kd & KEY_UP) move_cursor(0, -1);
    if (kd & KEY_A) op_a(g_cur);
    if (kd & KEY_X) op_x(g_cur);
    if (kd & KEY_R) op_r();
    if (kd & KEY_B) op_b(p);
    if ((kd & KEY_Y) && g_hand.id) { player_drop_slot(p, g_hand); slot_clear(&g_hand); }   // Y: tirar al suelo

    for (int i = 0; i < nref; i++) {
        if (ui_clicked(refs[i].x - 1, refs[i].y - 1, 18, 18)) { g_cur = i; op_a(i); }
    }
    if (g_furnace_open < 0 && g_chest_open < 0) recompute_out();

    for (int i = 0; i < nref; i++) ui_slot(refs[i].s, refs[i].x, refs[i].y, 1);
    box(refs[g_cur].x - 1, refs[g_cur].y - 1, 18, 18, UIC_WHITE);

    if (!is_dbl_chest) {
        ui_hint(CX, 0, "{A} Take {X} Split {R} Stack {Y} Drop {B} Back", UIC_GRAY);
        const Slot *under = refs[g_cur].s;
        if (under->id) ui_text_center(CX, 183, item_name(under->id), UIC_WHITE, 1);
    }

    if (g_hand.id) {
        int hx = ui_t.down ? ui_t.x - 8 : refs[g_cur].x + 6;
        int hy = ui_t.down ? ui_t.y - 8 : refs[g_cur].y + 6;
        ui_slot(&g_hand, hx, hy, 1);
    }
    if (!is_dbl_chest) {
        if (ui_clicked(0, 0, 256, 12)) op_b(p);
    } else {
        if (ui_clicked(0, 0, ox, 192) || ui_clicked(ox + 176, 0, 256 - (ox + 176), 192)) op_b(p);
    }
}

// ---------------------------------------------------------------------------
// Inventario creativo: pestañas por categoria + pestaña de inventario completo
// ---------------------------------------------------------------------------
#define CT_N   6
#define CT_INV 5
static int c_page;
static int c_tab;
static const char *const ct_name[CT_N] = {
    "Building Blocks", "Decoration", "Tools & Weapons", "Armor", "Food & Materials", "Inventory"
};

// Categoria de cada id (-1 = no aparece: agua, variantes de orientacion, ids reservados)
static int ct_of(int id)
{
    if (id < B_COUNT) {
        switch (id) {
        case B_AIR: case B_WATER: return -1;
        case B_CRAFTING: case B_FURNACE: case B_CHEST: case B_TORCH:
        case B_TALLGRASS: case B_DANDELION: case B_ROSE: case B_BOOKSHELF: return 1;
        }
        if ((id > B_FURNACE && id <= B_FURNACE_LIT_W) || (id > B_CHEST && id <= B_CHEST_W) ||
            (id > B_TORCH && id <= B_TORCH_N) || BLK_IS_BED(id)) return -1;
        return 0;
    }
    if (id >= I_BOW_RES1 && id <= I_BOW_RES3) return -1;
    if (id == I_BED) return 1;
    if (id >= I_WOOD_PICK && id <= I_DIAMOND_HOE) return 2;
    if (id == I_BOW || id == I_ARROW) return 2;
    if (id >= I_LEATHER_HELMET && id <= I_DIAMOND_BOOTS) return 3;
    return 4;
}

static void creative_list(int tab, u8 *out, int *n)
{
    *n = 0;
    for (int b = 1; b < B_COUNT; b++) if (ct_of(b) == tab) out[(*n)++] = (u8)b;
    for (int i = I_FIRST; i < I_END; i++) if (ct_of(i) == tab) out[(*n)++] = (u8)i;
}

// Coloca el item en la barra: reutiliza un hueco con el mismo item, si no el seleccionado vacio,
// si no el primer hueco libre, y en ultimo caso sustituye el seleccionado.
static void creative_give(Player *p, u8 id)
{
    int slot = -1;
    for (int i = 0; i < 9; i++) if (p->inv[i].id == id) { slot = i; break; }
    if (slot < 0) {
        if (!p->inv[p->sel].id) slot = p->sel;
        else for (int i = 0; i < 9; i++) if (!p->inv[i].id) { slot = i; break; }
    }
    if (slot < 0) slot = p->sel;
    p->inv[slot].id = id;
    p->inv[slot].count = item_max_stack(id) > 1 ? 64 : 1;
    p->inv[slot].dmg = 0;
    p->sel = slot;
}

static const u8 ct_icon[CT_N] = { B_BRICK, B_TORCH, I_IRON_PICK, I_IRON_CHEST, I_BEEF_COOKED, B_CHEST };

static void inv_creative(Player *p)
{
    const int ox = 30, oy = 28;
    u32 kd = keysDown();
    const int was_inv = (c_tab == CT_INV);

    // Pestañas (L / R cambian de pestaña; en el inventario completo R es "apilar", asi que solo L vuelve)
    if (c_tab != CT_INV) {
        if (kd & KEY_L) { c_tab = (c_tab + CT_N - 1) % CT_N; c_page = 0; }
        if (kd & KEY_R) { c_tab = (c_tab + 1) % CT_N; c_page = 0; }
        for (int t = 0; t < CT_N; t++) {
            int tx = ox + t * 33;
            if (ui_clicked(tx, 4, 32, 23) && c_tab != t) { c_tab = t; c_page = 0; }
        }
    }

    if (c_tab == CT_INV) {
        // inventario completo (27 huecos + barra + armadura + crafteo 2x2)
        inv_survival(p);
        if (g_state != ST_INVENTORY) return;
        ui_button_h(2, 90, 36, 20, "Items", 1);
        if (ui_clicked(2, 90, 36, 20) || ((kd & KEY_L) && was_inv)) {
            if (g_hand.id) { g_hand.count = (u8)inv_give(p, g_hand); if (g_hand.count) player_drop_slot(p, g_hand); slot_clear(&g_hand); }
            c_tab = 0;
            c_page = 0;
        }
        return;
    }

    u8 list[B_COUNT + ITEM_COUNT];
    int n;
    creative_list(c_tab, list, &n);
    int pages = (n + 44) / 45;
    if (pages < 1) pages = 1;
    if (c_page >= pages) c_page = pages - 1;

    ui_blit(UI_CREA_X, UI_CREA_Y, UI_CREA_W, UI_CREA_H, ox, oy);
    ui_blit(UI_SCROLL_OFF_X, UI_SCROLL_OFF_Y, UI_SCROLL_OFF_W, UI_SCROLL_OFF_H, ox + 175, oy + 18);

    for (int t = 0; t < CT_N; t++) {
        int tx = ox + t * 33;
        int sel = (t == c_tab);
        ui_rect(tx, 4, 32, 23, sel ? UIC_LGRAY : UIC_DGRAY);
        box(tx, 4, 32, 23, sel ? UIC_WHITE : UIC_BLACK);
        ui_icon(ct_icon[t], tx + 8, 7);
    }

    ui_text(ox + 8, oy + 6, ct_name[c_tab], UIC_DGRAY, 0);
    if (pages > 1) {
        char pg[12];
        snprintf(pg, sizeof pg, "%d/%d", c_page + 1, pages);
        ui_text(ox + 150, oy + 6, pg, UIC_DGRAY, 0);
    }

    for (int k = 0; k < 45; k++) {
        int i = c_page * 45 + k;
        if (i >= n) break;
        int x = ox + 9 + (k % 9) * 18, y = oy + 18 + (k / 9) * 18;
        ui_icon(list[i], x, y);
        if (ui_clicked(x - 1, y - 1, 18, 18)) creative_give(p, list[i]);
    }
    for (int i = 0; i < 9; i++) {
        int x = ox + 9 + i * 18, y = oy + 112;
        ui_slot(&p->inv[i], x, y, 0);
        if (ui_clicked(x - 1, y - 1, 18, 18)) p->sel = i;
    }
    {
        int x = ox + 9 + p->sel * 18 - 1, y = oy + 112 - 1;
        box(x, y, 18, 18, UIC_WHITE);
    }
    const Slot *cur = &p->inv[p->sel];
    if (cur->id) ui_text_center(CX, 164, item_name(cur->id), UIC_WHITE, 1);

    // Los textos de la barra de pestañas van sobre el nombre: el nombre se dibuja bajo la barra
    ui_button(28, 172, 36, "<", c_page > 0);
    ui_button(192, 172, 36, ">", c_page < pages - 1);
    ui_button(68, 172, 56, "Clear", cur->id != 0);
    ui_button(128, 172, 60, "Done", 1);
    if (c_page > 0 && ui_clicked(28, 172, 36, 20)) c_page--;
    if (c_page < pages - 1 && ui_clicked(192, 172, 36, 20)) c_page++;
    if (cur->id && (ui_clicked(68, 172, 56, 20) || (kd & KEY_Y))) slot_clear(&p->inv[p->sel]);
    if (ui_clicked(128, 172, 60, 20) || (kd & KEY_B)) g_state = ST_PLAY;
}

void menu_inventory_close(Player *p)
{
    // Lo que no cabe en el inventario se suelta al suelo (item drop), no se pierde
    if (g_hand.id) { g_hand.count = (u8)inv_give(p, g_hand); if (g_hand.count) player_drop_slot(p, g_hand); }
    slot_clear(&g_hand);
    for (int i = 0; i < 9; i++) {
        if (g_grid[i].id) {
            g_grid[i].count = (u8)inv_give(p, g_grid[i]);
            if (g_grid[i].count) player_drop_slot(p, g_grid[i]);
        }
        slot_clear(&g_grid[i]);
    }
    slot_clear(&g_out);
    g_craft_table = 0;
    g_furnace_open = -1;
    g_chest_open = -1;
    g_chest_open2 = -1;
}

void menu_inventory(Player *p)
{
    if (p->gamemode == 1 && g_furnace_open < 0 && g_chest_open < 0) inv_creative(p); else inv_survival(p);
}

// ---------------------------------------------------------------------------
// HUD tactil en partida
// ---------------------------------------------------------------------------
static int look_active;

#define JUMP_X 172
#define JUMP_Y 136
#define JUMP_W 76
#define JUMP_H 20
static int jump_active;   // el lapiz empezo apoyado sobre el boton de salto

static int in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

// Mezcla el boton tactil de salto con los botones fisicos (se llama antes de la fisica)
void menu_touch_controls(u32 *held, u32 *down)
{
    if (ui_t.pressed) jump_active = in_rect(ui_t.px, ui_t.py, JUMP_X, JUMP_Y, JUMP_W, JUMP_H);
    if (!ui_t.down) jump_active = 0;
    if (jump_active) {
        *held |= KEY_A;
        if (ui_t.pressed) *down |= KEY_A;
    }
}

u32 menu_play_sig(const Player *p)
{
    u32 h = 2166136261u;
#define MIX(v) (h = (h ^ (u32)(v)) * 16777619u)
    MIX(p->x >> 12); MIX(p->y >> 12); MIX(p->z >> 12);
    MIX(g_fps); MIX(p->msg_t > 0); MIX(p->tip_t > 0); MIX(p->sleep_t > 0); MIX(p->cam_mode); MIX(p->gamemode); MIX(p->sel);
    for (int i = 0; i < 9; i++) { MIX(p->inv[i].id); MIX(p->inv[i].count); MIX(p->inv[i].dmg); }
#undef MIX
    return h;
}

int menu_play_hud(Player *p)
{
    char buf[64];
    int ret = 0;

    snprintf(buf, sizeof(buf), "X: %d  Y: %d  Z: %d", p->x >> 12, p->y >> 12, p->z >> 12);
    ui_text(8, 6, buf, UIC_WHITE, 1);
    snprintf(buf, sizeof(buf), "%d FPS", g_fps);
    ui_text_right(248, 6, buf, UIC_GRAY, 0);
    snprintf(buf, sizeof(buf), "%s  |  Camera: %s", p->gamemode ? "Creative" : "Survival",
             p->cam_mode == 0 ? "1st person" : (p->cam_mode == 1 ? "3rd (back)" : "3rd (front)"));
    ui_text(8, 17, buf, UIC_GRAY, 0);

    if (p->sleep_t > 0) {       // acostado: solo se puede levantarse ("Leave Bed" o B), como en 1.7.10
        ui_text_center(CX, 70, "Sleeping...", UIC_WHITE, 1);
        ui_button(38, 100, 180, "Leave Bed", 1);
        if (ui_clicked(38, 100, 180, 20)) player_wake(p);
        return 0;
    }
    ui_button(8, 30, 116, "Inventory", 1);
    ui_button(132, 30, 116, "Game Menu", 1);
    ui_button(JUMP_X, JUMP_Y, JUMP_W, "Jump", 1);

    ui_text_center(CX, 56, "Drag to look  |  SELECT+R: Offhand", UIC_GRAY, 0);
    ui_hint(CX, 86, "{L} Break/Attack   {R} Place/Use", UIC_GRAY);
    ui_hint(CX, 103, "{A} Jump (x2 fly)   {B} Sneak", UIC_GRAY);
    ui_hint(CX, 120, "{X}{Y} Slot  {START} Pause  {SELECT} Camera", UIC_GRAY);
    if (p->msg_t > 0) ui_text_center(CX, 72, "You died!", UIC_RED, 1);
    else if (p->tip_t > 0 && p->tip_msg) ui_text_center(CX, 72, p->tip_msg, UIC_YELLOW, 1);

    // hotbar tactil
    const int hx = 37, hy = 160;
    ui_blit(UI_HOTBAR_X, UI_HOTBAR_Y, UI_HOTBAR_W, UI_HOTBAR_H, hx, hy);
    for (int i = 0; i < 9; i++) {
        ui_slot(&p->inv[i], hx + 3 + i * 20, hy + 3, p->gamemode == 0);
        if (ui_clicked(hx + 2 + i * 20, hy, 20, 22)) p->sel = i;
    }
    ui_blit(UI_SELECTOR_X, UI_SELECTOR_Y, UI_SELECTOR_W, UI_SELECTOR_H, hx - 1 + p->sel * 20, hy - 1);
    ui_slot(&p->offhand, hx + 184, hy + 3, p->gamemode == 0);
    ui_text(hx + 184, hy - 8, "Off", UIC_WHITE, 0);

    if (ui_clicked(8, 30, 116, 20)) ret = 1;
    if (ui_clicked(132, 30, 116, 20)) ret = 2;

    // mirar con el lapiz
    if (ui_t.pressed) {
        look_active = !(in_rect(ui_t.px, ui_t.py, 8, 30, 116, 20) || in_rect(ui_t.px, ui_t.py, 132, 30, 116, 20) ||
                        in_rect(ui_t.px, ui_t.py, hx, hy, 182, 22) ||
                        in_rect(ui_t.px, ui_t.py, JUMP_X, JUMP_Y, JUMP_W, JUMP_H));
    }
    if (!ui_t.down) look_active = 0;
    if (look_active && ui_t.down && !ui_t.pressed) {
        static const int sens[3] = {40, 64, 100};
        int k = sens[g_opt.sens % 3];
        player_look(p, ui_t.dx * k, (ui_t.dy * k) / 2);
    }
    return ret;
}
