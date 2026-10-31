// Texto del menu principal pre-renderizado en CPU (frase amarilla girada + textos inferiores).
// Se dibuja luego como UN quad 1:1 (pixel exacto) en vez de cientos de quads de 8x8 girados.
#ifndef SPLASH_H
#define SPLASH_H

#include <nds.h>

#define SPL_W 256            // textura de la frase: 256 x 64 ARGB1555 (se hornean SPL_VARIANTS tamanos)
#define SPL_H 64
#define SPL_VARIANTS 3       // tamanos del latido: 100 %, 97 %, 94 % (la frase de MC late entre 1.8 y 1.7)
#define LBL_W 256            // textura de los textos inferiores: 256 x 32 ARGB1555 (3 bandas)
#define LBL_H 32
#define LBL_LINE_H 10        // alto de cada banda (texto 8 px + contorno de 1 px arriba y abajo); 3 x 10 = 30 <= 32

// Ancho en pixeles de un texto con la fuente (suma de fontw).
int text_width(const char *s, const u8 *fontw);

// Frase amarilla de 1.7.10 (amarillo + sombra amarillo oscuro, contorno negro de 1 px), girada -20 grados,
// con supersampling 2x2. Como en Minecraft, la escala depende de la longitud (textos largos mas pequenos).
// out = SPL_VARIANTS * SPL_W*SPL_H pixeles ARGB1555 (variante 0 = tamano maximo), cada una centrada en
// (SPL_W/2, SPL_H/2). half_w / half_h: semiextension visible de la variante 0.
void splash_bake(const char *s, const u8 *font, const u8 *fontw, u16 *out, int *half_w, int *half_h);

// Texto blanco con contorno negro de 1 px, sin girar. La esquina superior izquierda del TEXTO queda en (x, y)
// (el contorno ocupa x-1 .. x+w, y-1 .. y+8). Devuelve el ancho del texto.
int text_bake_outlined(const char *s, const u8 *font, const u8 *fontw, u16 *out, int stride, int x, int y);

#endif
