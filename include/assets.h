// Datos binarios embebidos desde data/*.bin (BlocksDS los convierte con bin2c/bin2o)
#ifndef ASSETS_H
#define ASSETS_H

#include <nds.h>

extern const u8 terrain_bin[];   // 128x128 ARGB1555
extern const u8 hud_bin[];       // 256x256 ARGB1555
extern const u8 mobs_bin[];      // 9 texturas de 64x32 (pig, cow, sheep, fur, chicken, zombie, skeleton, creeper, spider)
extern const u8 armor_bin[];     // 5 texturas de 64x64 (capa 1 arriba, capa 2 abajo): cuero, malla, hierro, oro, diamante
extern const u8 font_bin[];      // 128x128 ARGB1555 (ascii.png)
extern const u8 fontw_bin[];     // 256 anchos de glifo
extern const u8 dirtbg_bin[];    // 16x16 ARGB1555 (fondo de tierra oscuro)
extern const u8 steve_bin[];     // 64x32 ARGB1555
extern const u8 chest_bin[];     // 64x64 ARGB1555 (entity/chest/normal.png)
extern const u8 chestdbl_bin[];  // 128x64 ARGB1555 (entity/chest/normal_double.png)
extern const u8 logo_bin[];      // 256x128 ARGB1555
extern const u8 pano0_bin[];     // 128x128 x6
extern const u8 pano1_bin[];
extern const u8 pano2_bin[];
extern const u8 pano3_bin[];
extern const u8 pano4_bin[];
extern const u8 pano5_bin[];
extern const u8 ui2d_bin[];      // hoja 2D de 8 bits (256 x UI_SHEET_H)
extern const u8 ui2dpal_bin[];   // paleta de 256 colores RGB555

#endif
