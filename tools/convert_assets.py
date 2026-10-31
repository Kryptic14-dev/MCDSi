#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Convierte las texturas de Minecraft 1.7.10 (assets/minecraft/textures) a los
binarios que usa el proyecto (carpeta data/) y regenera las cabeceras
include/tiles.h e include/ui_layout.h.

Uso:
    python3 tools/convert_assets.py ruta/al/textures.zip
    python3 tools/convert_assets.py ruta/a/la/carpeta/textures

Requiere: Pillow, numpy
"""
import io
import os
import struct
import sys
import zipfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")
INC = os.path.join(ROOT, "include")


# --------------------------------------------------------------------------
# Acceso a las texturas (zip o carpeta)
# --------------------------------------------------------------------------
class Source:
    def __init__(self, path):
        self.zip = None
        self.dir = None
        if os.path.isdir(path):
            self.dir = path
        else:
            self.zip = zipfile.ZipFile(path)
            names = self.zip.namelist()
            self.prefix = ""
            for n in names:
                if n.endswith("blocks/stone.png"):
                    self.prefix = n[: -len("blocks/stone.png")]
                    break

    def img(self, name):
        if self.zip:
            data = self.zip.read(self.prefix + name)
            return Image.open(io.BytesIO(data)).convert("RGBA")
        return Image.open(os.path.join(self.dir, name)).convert("RGBA")


PANO_BLUR_SIGMA = 1.0     # difuminado del panorama (texels de 128x128); 0 = sin difuminar


def to_1555(im):
    """Imagen RGBA -> bytes little-endian ARGB1555 (formato GL_RGBA de la DS)."""
    a = np.array(im.convert("RGBA"))
    r = a[..., 0].astype(np.uint16) >> 3
    g = a[..., 1].astype(np.uint16) >> 3
    b = a[..., 2].astype(np.uint16) >> 3
    al = (a[..., 3] >= 128).astype(np.uint16)
    v = r | (g << 5) | (b << 10) | (al << 15)
    return v.astype("<u2").tobytes()


def write(name, data):
    os.makedirs(DATA, exist_ok=True)
    with open(os.path.join(DATA, name), "wb") as f:
        f.write(data)
    print("  data/%-14s %7d bytes" % (name, len(data)))


def tint(im, rgb):
    a = np.array(im).astype(np.float32)
    a[..., 0] *= rgb[0] / 255.0
    a[..., 1] *= rgb[1] / 255.0
    a[..., 2] *= rgb[2] / 255.0
    return Image.fromarray(a.clip(0, 255).astype(np.uint8), "RGBA")


def first_frame(im):
    w, h = im.size
    return im.crop((0, 0, w, w)) if h > w else im


def extract_chest_tiles(src):
    """Extrae las caras superior, lateral y frontal (16x16) desde entity/chest/normal.png."""
    im = src.img("entity/chest/normal.png")
    top14 = im.crop((14, 0, 28, 14))
    side14 = Image.new("RGBA", (14, 14), (0, 0, 0, 0))
    side14.paste(im.crop((0, 14, 14, 19)), (0, 0))
    side14.paste(im.crop((0, 34, 14, 43)), (0, 5))
    front14 = Image.new("RGBA", (14, 14), (0, 0, 0, 0))
    front14.paste(im.crop((14, 14, 28, 19)), (0, 0))
    front14.paste(im.crop((14, 34, 28, 43)), (0, 5))
    front14.paste(im.crop((1, 1, 3, 5)), (6, 3))
    return (
        top14.resize((16, 16), Image.NEAREST),
        side14.resize((16, 16), Image.NEAREST),
        front14.resize((16, 16), Image.NEAREST),
    )


# --------------------------------------------------------------------------
# Atlas del terreno (128x128 = 8x8 tiles de 16x16)
# --------------------------------------------------------------------------
def build_terrain(src):
    grass_tint = tuple(int(v) for v in np.array(src.img("colormap/grass.png"))[173, 51][:3])
    leaf_tint = tuple(int(v) for v in np.array(src.img("colormap/foliage.png"))[173, 51][:3])
    print("  tinte hierba", grass_tint, "hojas", leaf_tint)

    def blk(n):
        return first_frame(src.img("blocks/%s.png" % n))

    tiles = []  # (NOMBRE, imagen)

    def add(name, im):
        tiles.append((name, im))

    add("STONE", blk("stone"))
    add("GRASS_TOP", tint(blk("grass_top"), grass_tint))
    side = blk("grass_side").copy()
    side.alpha_composite(tint(blk("grass_side_overlay"), grass_tint))
    add("GRASS_SIDE", side)
    add("DIRT", blk("dirt"))
    add("COBBLE", blk("cobblestone"))
    add("PLANKS", blk("planks_oak"))
    add("BEDROCK", blk("bedrock"))
    add("WATER", blk("water_still"))
    add("SAND", blk("sand"))
    add("GRAVEL", blk("gravel"))
    add("GOLD_ORE", blk("gold_ore"))
    add("IRON_ORE", blk("iron_ore"))
    add("COAL_ORE", blk("coal_ore"))
    add("LOG_SIDE", blk("log_oak"))
    add("LOG_TOP", blk("log_oak_top"))
    add("LEAVES", tint(blk("leaves_oak_opaque"), leaf_tint))
    add("GLASS", blk("glass"))
    add("SANDSTONE_SIDE", blk("sandstone_normal"))
    add("SANDSTONE_TOP", blk("sandstone_top"))
    add("SANDSTONE_BOTTOM", blk("sandstone_bottom"))
    add("DIAMOND_ORE", blk("diamond_ore"))
    add("BRICK", blk("brick"))
    add("STONEBRICK", blk("stonebrick"))
    add("CRAFT_TOP", blk("crafting_table_top"))
    add("CRAFT_FRONT", blk("crafting_table_front"))
    add("CRAFT_SIDE", blk("crafting_table_side"))
    add("SNOW", blk("snow"))
    add("TALLGRASS", tint(blk("tallgrass"), grass_tint))
    add("DANDELION", blk("flower_dandelion"))
    add("ROSE", blk("flower_rose"))
    add("WOOL_WHITE", blk("wool_colored_white"))
    add("WOOL_RED", blk("wool_colored_red"))
    add("WOOL_BLUE", blk("wool_colored_blue"))
    add("WOOL_GREEN", blk("wool_colored_green"))
    add("WOOL_YELLOW", blk("wool_colored_yellow"))
    add("WOOL_BLACK", blk("wool_colored_black"))
    add("MOSSY", blk("cobblestone_mossy"))
    add("OBSIDIAN", blk("obsidian"))
    add("BOOKSHELF", blk("bookshelf"))
    add("PLANKS_SIDE_DUMMY", blk("planks_oak"))  # reservado
    add("FURNACE_FRONT_OFF", blk("furnace_front_off"))
    add("FURNACE_FRONT_ON", blk("furnace_front_on"))
    add("FURNACE_SIDE", blk("furnace_side"))
    add("FURNACE_TOP", blk("furnace_top"))
    for i in range(10):
        add("DESTROY_%d" % i, blk("destroy_stage_%d" % i))
    chest_top, chest_side, chest_front = extract_chest_tiles(src)
    add("CHEST_TOP", chest_top)
    add("CHEST_SIDE", chest_side)
    add("CHEST_FRONT", chest_front)
    add("TORCH", blk("torch_on"))
    # cama (los laterales/extremos solo usan las 9 filas de abajo = 9/16 de bloque)
    add("BED_FEET_TOP", blk("bed_feet_top"))
    add("BED_FEET_SIDE", blk("bed_feet_side"))
    add("BED_FEET_END", blk("bed_feet_end"))
    add("BED_HEAD_TOP", blk("bed_head_top"))
    add("BED_HEAD_SIDE", blk("bed_head_side"))
    add("BED_HEAD_END", blk("bed_head_end"))

    assert len(tiles) <= 64
    atlas = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    lines = ["/* GENERADO por tools/convert_assets.py - no editar */",
             "#ifndef TILES_H", "#define TILES_H", ""]
    for i, (n, im) in enumerate(tiles):
        atlas.paste(im.resize((16, 16), Image.NEAREST), ((i % 8) * 16, (i // 8) * 16))
        lines.append("#define T_%-18s %d" % (n, i))
    lines += ["", "#define T_COUNT %d" % len(tiles), "", "#endif", ""]
    with open(os.path.join(INC, "tiles.h"), "w") as f:
        f.write("\n".join(lines))
    write("terrain.bin", to_1555(atlas))
    return dict((n, im) for n, im in tiles)


# --------------------------------------------------------------------------
# Iconos isometricos de bloques (para hotbar / inventario)
# --------------------------------------------------------------------------
# (nombre_bloque, top, izquierda(+Z), derecha(+X), plano)  -- se mantiene en
# sincronia con source/blocks.c (el orden es el enum B_* de blocks.h)
ICON_BLOCKS = [
    ("AIR", None, None, None, False),
    ("STONE", "STONE", "STONE", "STONE", False),
    ("GRASS", "GRASS_TOP", "GRASS_SIDE", "GRASS_SIDE", False),
    ("DIRT", "DIRT", "DIRT", "DIRT", False),
    ("COBBLE", "COBBLE", "COBBLE", "COBBLE", False),
    ("PLANKS", "PLANKS", "PLANKS", "PLANKS", False),
    ("BEDROCK", "BEDROCK", "BEDROCK", "BEDROCK", False),
    ("WATER", "WATER", "WATER", "WATER", False),
    ("SAND", "SAND", "SAND", "SAND", False),
    ("GRAVEL", "GRAVEL", "GRAVEL", "GRAVEL", False),
    ("GOLD_ORE", "GOLD_ORE", "GOLD_ORE", "GOLD_ORE", False),
    ("IRON_ORE", "IRON_ORE", "IRON_ORE", "IRON_ORE", False),
    ("COAL_ORE", "COAL_ORE", "COAL_ORE", "COAL_ORE", False),
    ("LOG", "LOG_TOP", "LOG_SIDE", "LOG_SIDE", False),
    ("LEAVES", "LEAVES", "LEAVES", "LEAVES", False),
    ("GLASS", "GLASS", "GLASS", "GLASS", False),
    ("SANDSTONE", "SANDSTONE_TOP", "SANDSTONE_SIDE", "SANDSTONE_SIDE", False),
    ("DIAMOND_ORE", "DIAMOND_ORE", "DIAMOND_ORE", "DIAMOND_ORE", False),
    ("BRICK", "BRICK", "BRICK", "BRICK", False),
    ("STONEBRICK", "STONEBRICK", "STONEBRICK", "STONEBRICK", False),
    ("CRAFTING", "CRAFT_TOP", "CRAFT_SIDE", "CRAFT_FRONT", False),
    ("SNOW", "SNOW", "SNOW", "SNOW", False),
    ("TALLGRASS", "TALLGRASS", None, None, True),
    ("DANDELION", "DANDELION", None, None, True),
    ("ROSE", "ROSE", None, None, True),
    ("WOOL_WHITE", "WOOL_WHITE", "WOOL_WHITE", "WOOL_WHITE", False),
    ("WOOL_RED", "WOOL_RED", "WOOL_RED", "WOOL_RED", False),
    ("WOOL_BLUE", "WOOL_BLUE", "WOOL_BLUE", "WOOL_BLUE", False),
    ("WOOL_GREEN", "WOOL_GREEN", "WOOL_GREEN", "WOOL_GREEN", False),
    ("WOOL_YELLOW", "WOOL_YELLOW", "WOOL_YELLOW", "WOOL_YELLOW", False),
    ("WOOL_BLACK", "WOOL_BLACK", "WOOL_BLACK", "WOOL_BLACK", False),
    ("MOSSY", "MOSSY", "MOSSY", "MOSSY", False),
    ("OBSIDIAN", "OBSIDIAN", "OBSIDIAN", "OBSIDIAN", False),
    ("BOOKSHELF", "PLANKS", "BOOKSHELF", "BOOKSHELF", False),
] + [("FURNACE%d" % i, "FURNACE_TOP", "FURNACE_FRONT_OFF", "FURNACE_SIDE", False) for i in range(8)] \
  + [("CHEST%d" % i, "CHEST_TOP", "CHEST_FRONT", "CHEST_SIDE", False) for i in range(4)] \
  + [("TORCH%d" % i, "TORCH", None, None, True) for i in range(5)] \
  + [("BED%d" % i, None, None, None, False) for i in range(5)]      # cama: nunca se ve como bloque en el inventario (se usa el item)


# Iconos planos de items (orden = enum I_* de include/items.h, a partir de I_FIRST = 64)
ITEM_ICONS = ["stick", "coal", "iron_ingot", "gold_ingot", "diamond"] + [
    "%s_%s" % (m, k) for m in ("wood", "stone", "iron", "gold", "diamond")
    for k in ("pickaxe", "axe", "shovel", "sword", "hoe")]
# armaduras: id = I_LEATHER_HELMET + material * 4 + pieza (casco, peto, pantalones, botas)
ARMOR_MATS = ("leather", "chainmail", "iron", "gold", "diamond")
ARMOR_PIECES = ("helmet", "chestplate", "leggings", "boots")
ITEM_ICONS += ["%s_%s" % (m, k) for m in ARMOR_MATS for k in ARMOR_PIECES]
# comida y materiales que sueltan los mobs (mismo orden que el enum de include/items.h)
ITEM_ICONS += ["porkchop_raw", "porkchop_cooked", "beef_raw", "beef_cooked", "chicken_raw", "chicken_cooked",
               "leather", "feather", "bone", "string", "gunpowder", "rotten_flesh", "spider_eye", "arrow",
               "bow_standby", "bow_pulling_0", "bow_pulling_1", "bow_pulling_2",   # I_BOW + 3 ids reservados (fotogramas del arco)
               "bed"]
LEATHER_RGB = (160, 101, 64)      # 0xA06540: color por defecto de la armadura de cuero en 1.7.10


def make_item_icon(src, name):
    im = first_frame(src.img("items/%s.png" % name))
    if name.startswith("leather_") and name.split("_", 1)[1] in ARMOR_PIECES:
        # el cuero se tiñe de marron; la capa "overlay" (remaches) va sin teñir
        im = tint(im, LEATHER_RGB)
        im.alpha_composite(first_frame(src.img("items/%s_overlay.png" % name)))
    if im.size != (16, 16):
        im = im.resize((16, 16), Image.NEAREST)
    return im


def make_icon(tiles, spec):
    name, top, left, right, flat = spec
    if top is None:
        return Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    if flat:
        return tiles[top].copy()
    S = 4  # supersampling
    N = 16 * S
    T = (N * 0.5, N * 0.03)           # vertice superior
    TR = (N * 0.93, N * 0.265)
    BR = (N * 0.93, N * 0.735)
    B = (N * 0.5, N * 0.97)
    BL = (N * 0.07, N * 0.735)
    TL = (N * 0.07, N * 0.265)
    C = (N * 0.5, N * 0.5)
    faces = [
        # origen, eje u, eje v, textura, sombreado
        (T, (TR[0] - T[0], TR[1] - T[1]), (TL[0] - T[0], TL[1] - T[1]), tiles[top], 1.0),
        (TL, (C[0] - TL[0], C[1] - TL[1]), (BL[0] - TL[0], BL[1] - TL[1]), tiles[left], 0.8),
        (C, (TR[0] - C[0], TR[1] - C[1]), (B[0] - C[0], B[1] - C[1]), tiles[right], 0.6),
    ]
    out = np.zeros((N, N, 4), np.float32)
    for (o, u, v, tex, shade) in faces:
        det = u[0] * v[1] - u[1] * v[0]
        ta = np.array(tex)
        for py in range(N):
            for px in range(N):
                dx, dy = px + 0.5 - o[0], py + 0.5 - o[1]
                a = (dx * v[1] - dy * v[0]) / det
                b = (u[0] * dy - u[1] * dx) / det
                if 0 <= a < 1 and 0 <= b < 1:
                    t = ta[int(b * 16), int(a * 16)]
                    if t[3] >= 128:
                        out[py, px] = (t[0] * shade, t[1] * shade, t[2] * shade, 255)
    # reduccion 4x4 con promedio ponderado por alfa
    res = np.zeros((16, 16, 4), np.uint8)
    for y in range(16):
        for x in range(16):
            blk = out[y * S:(y + 1) * S, x * S:(x + 1) * S].reshape(-1, 4)
            m = blk[:, 3] > 0
            if m.sum() * 2 >= len(blk):
                c = blk[m][:, :3].mean(axis=0)
                res[y, x] = (int(c[0]), int(c[1]), int(c[2]), 255)
    return Image.fromarray(res, "RGBA")


# --------------------------------------------------------------------------
# Fuente
# --------------------------------------------------------------------------
def build_font(src):
    f = src.img("font/ascii.png")
    write("font.bin", to_1555(f))
    a = np.array(f)
    widths = []
    for ch in range(256):
        cx, cy = (ch % 16) * 8, (ch // 16) * 8
        cell = a[cy:cy + 8, cx:cx + 8, 3]
        if ch == 32:
            widths.append(4)
            continue
        cols = np.where(cell.max(axis=0) > 0)[0]
        widths.append(int(cols.max()) + 2 if len(cols) else 4)
    write("fontw.bin", bytes(widths))
    return widths


# --------------------------------------------------------------------------
# Atlas del HUD 3D (256x256, ARGB1555)
# --------------------------------------------------------------------------
HUD_ICON_Y = 48
HUD_ATLAS_H = 256         # atlas 256x256 (128 KB): caben 13 filas de 16 iconos desde y = 48
HUD_ARMOR_X = 110
HUD_BUBBLE_X = 150        # burbujas de oxigeno bajo el agua (llena, explotando)         # iconos de armadura (vacio, lleno, medio) en la fila de estadisticas
HUD_STAT_X, HUD_FOOD_X, HUD_STAT_Y, HUD_STAT_S = 30, 70, 24, 11   # zona libre del atlas (x>=24, y 22..47); celda 11x11 = icono 9x9 original + 1 px transparente por lado


def build_hud(src, icons):
    w = src.img("gui/widgets.png")
    ic = src.img("gui/icons.png")
    assert HUD_ICON_Y + ((len(icons) + 15) // 16) * 16 <= HUD_ATLAS_H, "demasiados iconos"
    hud = Image.new("RGBA", (256, HUD_ATLAS_H), (0, 0, 0, 0))
    hud.paste(w.crop((0, 0, 182, 22)), (0, 0))
    hud.paste(w.crop((0, 22, 24, 46)), (0, 22))
    cross = ic.crop((0, 0, 16, 16))
    # la mira original se dibuja con inversion de color; en la DS la hacemos blanca
    ca = np.array(cross)
    ca[..., :3] = 255
    hud.paste(Image.fromarray(ca, "RGBA"), (184, 0))
    # Corazones (y=20) y hambre (y=30). Componemos lleno/medio sobre el
    # icono vacio durante la conversion: durante el juego se dibuja un solo quad
    # por hueco y se evitan capas superpuestas que parpadean en la DS real.
    C = HUD_STAT_S          # celda
    # icono original 9x9 sin escalar; el margen transparente absorbe cualquier desfase de 1 texel de la DS
    # (fila en icons.png, columna destino, x del vacio, x del lleno, x del medio)
    for (sy, dx, ex, fx, hx) in ((0, HUD_STAT_X, 16, 52, 61), (27, HUD_FOOD_X, 16, 52, 61), (9, HUD_ARMOR_X, 16, 34, 25)):
        empty = ic.crop((ex, sy, ex + 9, sy + 9))
        full = ic.crop((fx, sy, fx + 9, sy + 9))
        half = ic.crop((hx, sy, hx + 9, sy + 9))
        full_composed = empty.copy()
        full_composed.alpha_composite(full)
        half_composed = empty.copy()
        half_composed.alpha_composite(half)
        for k, piece in enumerate((empty, full_composed, half_composed)):
            # 1x exacto: el quad de la celda (11x11) se dibuja 1:1 con la textura
            hud.paste(piece, (dx + k * C + 1, HUD_STAT_Y + 1))
    # Burbujas de agua (icons.png fila y=18: x=16 llena, x=25 explotando)
    hud.paste(ic.crop((16, 18, 25, 27)), (HUD_BUBBLE_X + 1, HUD_STAT_Y + 1))
    hud.paste(ic.crop((25, 18, 34, 27)), (HUD_BUBBLE_X + C + 1, HUD_STAT_Y + 1))
    for i, im in enumerate(icons):
        hud.paste(im, ((i % 16) * 16, HUD_ICON_Y + (i // 16) * 16))
    write("hud.bin", to_1555(hud))


# --------------------------------------------------------------------------
# Steve, cofres, logo, panorama
# --------------------------------------------------------------------------
def dirt_1710_rgb555(src):
    """En Minecraft 1.7.10 (GuiScreen.drawBackground), options_background.png se multiplica por
    el color de vertice 0x404040 (64/255). Redondeamos directamente a RGB555 (0..31) para no
    perder brillo ni tono marron al cuantizar a 15 bits."""
    d = np.array(src.img("gui/options_background.png").convert("RGB"), dtype=np.float32) * (64.0 / 255.0)
    return np.clip(np.floor(d * (31.0 / 255.0) + 0.5), 0, 31).astype(np.uint16)


def build_misc(src):
    write("steve.bin", to_1555(src.img("entity/steve.png")))
    write("chest.bin", to_1555(src.img("entity/chest/normal.png").crop((0, 0, 64, 64))))
    write("chestdbl.bin", to_1555(src.img("entity/chest/normal_double.png").crop((0, 0, 128, 64))))
    q = dirt_1710_rgb555(src)
    v = q[..., 0] | (q[..., 1] << 5) | (q[..., 2] << 10) | (1 << 15)
    write("dirtbg.bin", v.astype("<u2").tobytes())
    # logo propio (tools/minecraft_logo.png) en una textura de 256x128
    lp = os.path.join(ROOT, "tools", "minecraft_logo.png")
    lg = Image.open(lp).convert("RGBA")
    if lg.width > 256 or lg.height > 128:
        k = min(256 / lg.width, 128 / lg.height)
        lg = lg.resize((int(lg.width * k), int(lg.height * k)), Image.LANCZOS)
    bb = lg.getchannel("A").point(lambda v: 255 if v > 16 else 0).getbbox() or (0, 0, lg.width, lg.height)
    canvas = Image.new("RGBA", (256, 128), (0, 0, 0, 0))
    canvas.paste(lg, (0, 0))
    write("logo.bin", to_1555(canvas))
    with open(os.path.join(INC, "logo.h"), "w") as f:
        f.write("/* GENERADO por tools/convert_assets.py */\n#ifndef MCDS_LOGO_H\n#define MCDS_LOGO_H\n"
                "#define LOGO_W %d\n#define LOGO_H %d\n"
                "#define LOGO_BX0 %d\n#define LOGO_BY0 %d\n#define LOGO_BX1 %d\n#define LOGO_BY1 %d\n#endif\n"
                % ((lg.width, lg.height) + bb))
    build_panorama(src)


def build_panorama(src):
    """Panorama del menu: se difumina el cubo como en 1.7.10 (ver tools/pano_blur.py) y se cuantiza a RGB15
    con dithering ordenado Bayer 4x4 sobre el valor float para evitar bandas de 5 bits en el cielo."""
    import pano_blur
    imgs = [src.img("gui/title/background/panorama_%d.png" % i).convert("RGB") for i in range(6)]
    bayer4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16.0 - 0.5
    dither = np.tile(bayer4, (128 // 4, 128 // 4))[..., None]
    faces = [np.asarray(im.resize((128, 128), Image.LANCZOS), np.float32) for im in imgs]
    for i, o in enumerate(pano_blur.blur_cube(faces, PANO_BLUR_SIGMA)):
        q = np.clip(np.floor(o * (31.0 / 255.0) + dither + 0.5), 0, 31).astype(np.uint16)
        v = q[..., 0] | (q[..., 1] << 5) | (q[..., 2] << 10) | (1 << 15)
        write("pano%d.bin" % i, v.astype("<u2").tobytes())


# --------------------------------------------------------------------------
# Texturas de mobs (64x32 cada una) y de capas de armadura (64x64 = capa 1 + capa 2)
# --------------------------------------------------------------------------
# Orden = enum MT_* de include/mobs.h
MOB_TEXTURES = ["pig/pig", "cow/cow", "sheep/sheep", "sheep/sheep_fur", "chicken", "zombie/zombie",
                "skeleton/skeleton", "creeper/creeper", "spider/spider"]


def armor_layer(src, mat, layer):
    """Textura de armadura 64x32 de un material y capa (1 = casco/peto/botas, 2 = pantalones); el cuero va teñido."""
    im = src.img("models/armor/%s_layer_%d.png" % (mat, layer)).convert("RGBA")
    if mat == "leather":
        im = tint(im, LEATHER_RGB)
        im.alpha_composite(src.img("models/armor/leather_layer_%d_overlay.png" % layer).convert("RGBA"))
    return im.crop((0, 0, 64, 32))


def build_mobs(src):
    out = b""
    for n in MOB_TEXTURES:
        im = src.img("entity/%s.png" % n)
        im = im.crop((0, 0, 64, 32))        # el zombie viene en 64x64 pero solo usa la mitad de arriba
        out += to_1555(im)
    write("mobs.bin", out)


def build_armor(src):
    out = b""
    for m in ARMOR_MATS:
        sheet = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        for layer in (1, 2):
            sheet.paste(armor_layer(src, m, layer), (0, (layer - 1) * 32))
        out += to_1555(sheet)
    write("armor.bin", out)


# --------------------------------------------------------------------------
# Hoja 2D (paleta de 8 bits) para la pantalla tactil
# --------------------------------------------------------------------------
# Indices fijos de paleta para el texto
FIXED = [
    (0, 0, 0),        # 0 transparente
    (255, 255, 255),  # 1 blanco
    (63, 63, 63),     # 2 sombra
    (255, 255, 160),  # 3 amarillo (hover)
    (160, 160, 160),  # 4 gris (deshabilitado)
    (0, 0, 0),        # 5 negro
    (255, 85, 85),    # 6 rojo
    (224, 224, 224),  # 7 gris claro
    (64, 64, 64),     # 8 gris oscuro (texto de inventario)
    (128, 255, 32),   # 9 verde (barra de carga)
]


def build_steve_preview(src):
    """Steve de frente (skin 64x32) ampliado x2 -> 32x64, para el recuadro negro del inventario."""
    skin = src.img("entity/steve.png")
    def part(x, y, w, h, flip=False):
        im = skin.crop((x, y, x + w, y + h))
        return im.transpose(Image.FLIP_LEFT_RIGHT) if flip else im
    out = Image.new("RGBA", (16, 32), (0, 0, 0, 0))
    out.alpha_composite(part(20, 20, 8, 12), (4, 8))              # cuerpo
    out.alpha_composite(part(44, 20, 4, 12), (0, 8))              # brazo derecho (a la izquierda de la imagen)
    out.alpha_composite(part(44, 20, 4, 12, True), (12, 8))       # brazo izquierdo (espejo)
    out.alpha_composite(part(4, 20, 4, 12), (4, 20))              # pierna derecha
    out.alpha_composite(part(4, 20, 4, 12, True), (8, 20))        # pierna izquierda (espejo)
    out.alpha_composite(part(8, 8, 8, 8), (4, 0))                 # cabeza
    out.alpha_composite(part(40, 8, 8, 8), (4, 0))                # capa del sombrero
    return out.resize((32, 64), Image.NEAREST)


def build_armor_preview(src, mat, piece):
    """Una pieza de armadura de frente, alineada con build_steve_preview (mismas posiciones)."""
    def part(im, x, y, w, h, flip=False):
        c = im.crop((x, y, x + w, y + h))
        return c.transpose(Image.FLIP_LEFT_RIGHT) if flip else c
    l1, l2 = armor_layer(src, mat, 1), armor_layer(src, mat, 2)
    out = Image.new("RGBA", (16, 32), (0, 0, 0, 0))
    if piece == 0:                                                   # casco
        out.alpha_composite(part(l1, 8, 8, 8, 8), (4, 0))
    elif piece == 1:                                                 # peto: cuerpo + brazos
        out.alpha_composite(part(l1, 20, 20, 8, 12), (4, 8))
        out.alpha_composite(part(l1, 44, 20, 4, 12), (0, 8))
        out.alpha_composite(part(l1, 44, 20, 4, 12, True), (12, 8))
    elif piece == 2:                                                 # pantalones: cintura + piernas
        out.alpha_composite(part(l2, 20, 20, 8, 12), (4, 8))
        out.alpha_composite(part(l2, 4, 20, 4, 12), (4, 20))
        out.alpha_composite(part(l2, 4, 20, 4, 12, True), (8, 20))
    else:                                                            # botas
        out.alpha_composite(part(l1, 4, 20, 4, 12), (4, 20))
        out.alpha_composite(part(l1, 4, 20, 4, 12, True), (8, 20))
    return out.resize((32, 64), Image.NEAREST)


def build_chest_uis(src):
    """Construye los paneles de cofre simple (176x166) y cofre doble (176x192) desde generic_54.png."""
    g54 = src.img("gui/container/generic_54.png")
    # Cofre simple (3 filas): 176x166 (inventario en oy+84 y hotbar en oy+142, igual que INV/CRAFT/FURNACE)
    single = Image.new("RGBA", (176, 166), (0, 0, 0, 0))
    single.paste(g54.crop((0, 0, 176, 71)), (0, 0))
    single.paste(g54.crop((0, 127, 176, 222)), (0, 71))
    # Cofre doble (6 filas + 3 inventario + 1 hotbar = 10 filas de 18 px = 180 px + bordes = 192 px)
    dbl = Image.new("RGBA", (176, 192), (0, 0, 0, 0))
    dbl.paste(g54.crop((0, 0, 176, 3)), (0, 0))         # borde superior (3 px)
    dbl.paste(g54.crop((0, 17, 176, 125)), (0, 3))      # 6 filas del cofre (108 px -> oy + 4 + r*18)
    dbl.paste(g54.crop((0, 125, 176, 128)), (0, 111))   # separador (3 px)
    dbl.paste(g54.crop((0, 139, 176, 193)), (0, 114))   # 3 filas de inventario (54 px -> oy + 115 + r*18)
    dbl.paste(g54.crop((0, 193, 176, 196)), (0, 168))   # separador (3 px)
    dbl.paste(g54.crop((0, 197, 176, 215)), (0, 171))   # hotbar (18 px -> oy + 172)
    dbl.paste(g54.crop((0, 219, 176, 222)), (0, 189))   # borde inferior (3 px)
    return single, dbl


def build_ui(src, icons):
    # (clave, imagen)
    items = []
    # Fondo de tierra de 1.7.10 (GuiScreen.drawBackground: options_background.png * 0x404040).
    # Reservamos sus colores RGB555 exactos en la paleta para que MEDIANCUT no los mezcle ni oscurezca.
    dirt_q5 = dirt_1710_rgb555(src)
    dirt_pal5 = sorted(set(map(tuple, dirt_q5.reshape(-1, 3).tolist())))
    dirt_rgb8 = np.zeros((16, 16, 4), dtype=np.uint8)
    dirt_rgb8[..., :3] = (dirt_q5 * 255 // 31).astype(np.uint8)
    dirt_rgb8[..., 3] = 255
    dirt = Image.fromarray(dirt_rgb8, "RGBA")

    w = src.img("gui/widgets.png")
    items.append(("DIRT", dirt))
    items.append(("BTN_DISABLED", w.crop((0, 46, 200, 66))))
    items.append(("BTN_NORMAL", w.crop((0, 66, 200, 86))))
    items.append(("BTN_HOVER", w.crop((0, 86, 200, 106))))
    items.append(("HOTBAR", w.crop((0, 0, 182, 22))))
    items.append(("SELECTOR", w.crop((0, 22, 24, 46))))
    items.append(("INV", src.img("gui/container/inventory.png").crop((0, 0, 176, 166))))
    items.append(("CREA", src.img("gui/container/creative_inventory/tab_items.png").crop((0, 0, 195, 136))))
    items.append(("CRAFT", src.img("gui/container/crafting_table.png").crop((0, 0, 176, 166))))
    tabs = src.img("gui/container/creative_inventory/tabs.png")
    fur = src.img("gui/container/furnace.png")
    items.append(("FURNACE", fur.crop((0, 0, 176, 166))))
    items.append(("FURN_FLAME", fur.crop((176, 0, 190, 14))))
    items.append(("FURN_ARROW", fur.crop((176, 14, 200, 30))))
    chest_single, chest_dbl = build_chest_uis(src)
    items.append(("CHEST", chest_single))
    items.append(("CHEST_DBL", chest_dbl))
    items.append(("SCROLL", tabs.crop((232, 0, 244, 15))))
    items.append(("SCROLL_OFF", tabs.crop((244, 0, 256, 15))))
    # inventario de supervivencia: iconos de hueco de armadura vacio + Steve de frente
    for nm, key in (("helmet", "ARMOR_HELMET"), ("chestplate", "ARMOR_CHEST"),
                    ("leggings", "ARMOR_LEGS"), ("boots", "ARMOR_BOOTS")):
        items.append((key, src.img("items/empty_armor_slot_%s.png" % nm)))
    items.append(("STEVE_PREVIEW", build_steve_preview(src)))
    for mi, m in enumerate(ARMOR_MATS):
        for pi in range(4):
            items.append(("ARMPV%d_%d" % (mi, pi), build_armor_preview(src, m, pi)))

    # empaquetado simple por filas en una hoja de 256 de ancho
    sheet_w = 256
    pos = {}
    y = 0
    x = 0
    row_h = 0
    for key, im in items:
        if x + im.width > sheet_w:
            x = 0
            y += row_h
            row_h = 0
        pos[key] = (x, y, im.width, im.height)
        x += im.width
        row_h = max(row_h, im.height)
    y += row_h
    # iconos
    icon_y0 = y
    y += ((len(icons) + 15) // 16) * 16
    sheet_h = y
    sheet = Image.new("RGBA", (sheet_w, sheet_h), (0, 0, 0, 0))
    for key, im in items:
        sheet.paste(im, pos[key][:2])
    for i, im in enumerate(icons):
        sheet.paste(im, ((i % 16) * 16, icon_y0 + (i // 16) * 16))

    # Reservamos 10 colores fijos de texto + los colores exactos del fondo de tierra de 1.7.10
    n_dirt_pal = len(dirt_pal5)
    base_idx = len(FIXED) + n_dirt_pal
    n_quant = 256 - base_idx

    a = np.array(sheet)
    opaque = a[..., 3] >= 128
    rgb = Image.fromarray(a[..., :3].copy(), "RGB")
    q = rgb.quantize(colors=n_quant, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[: n_quant * 3]
    idx = np.array(q).astype(np.uint8) + base_idx
    idx[~opaque] = 0

    # Asignacion directa y exacta de los indices de paleta de DIRT (16x16)
    dx0, dy0, dw, dh = pos["DIRT"]
    dirt_map = {c5: len(FIXED) + i for i, c5 in enumerate(dirt_pal5)}
    for py in range(dh):
        for px in range(dw):
            c5 = tuple(int(v) for v in dirt_q5[py, px])
            idx[dy0 + py, dx0 + px] = dirt_map[c5]

    full_pal5 = [(r >> 3, g >> 3, b >> 3) for (r, g, b) in FIXED]
    full_pal5 += [(int(r5), int(g5), int(b5)) for (r5, g5, b5) in dirt_pal5]
    for i in range(n_quant):
        r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
        full_pal5.append(((r * 31 + 127) // 255, (g * 31 + 127) // 255, (b * 31 + 127) // 255))
    while len(full_pal5) < 256:
        full_pal5.append((0, 0, 0))

    write("ui2d.bin", idx.tobytes())
    pal_bytes = b"".join(
        struct.pack("<H", r5 | (g5 << 5) | (b5 << 10) | 0x8000) for (r5, g5, b5) in full_pal5
    )
    write("ui2dpal.bin", pal_bytes)

    L = ["/* GENERADO por tools/convert_assets.py - no editar */",
         "#ifndef UI_LAYOUT_H", "#define UI_LAYOUT_H", "",
         "#define UI_SHEET_W %d" % sheet_w,
         "#define UI_SHEET_H %d" % sheet_h, ""]
    for key, (px, py, pw, ph) in pos.items():
        L.append("#define UI_%s_X %d" % (key, px))
        L.append("#define UI_%s_Y %d" % (key, py))
        L.append("#define UI_%s_W %d" % (key, pw))
        L.append("#define UI_%s_H %d" % (key, ph))
    L += ["", "/* sprites de armadura del Steve del inventario: [material][pieza] = {x, y} (32x64 cada uno) */",
          "static const unsigned short UI_ARMPV_XY[5][4][2] = {"]
    for mi in range(5):
        L.append("    {" + ", ".join("{%d, %d}" % pos["ARMPV%d_%d" % (mi, pi)][:2] for pi in range(4)) + "},")
    L += ["};", "", "#define UI_ICON_Y0 %d" % icon_y0,
          "#define HUD_ICON_Y0 %d" % HUD_ICON_Y,
          "#define HUD_STAT_X %d" % HUD_STAT_X,
          "#define HUD_FOOD_X %d" % HUD_FOOD_X,
          "#define HUD_ARMOR_X %d" % HUD_ARMOR_X,
          "#define HUD_BUBBLE_X %d" % HUD_BUBBLE_X,
          "#define HUD_ATLAS_H %d" % HUD_ATLAS_H,
          "#define HUD_STAT_Y %d" % HUD_STAT_Y,
          "#define HUD_STAT_S %d" % HUD_STAT_S,
          "#define ICON_COUNT %d" % len(icons), "",
          "/* indices fijos de paleta */",
          "#define UIC_WHITE 1", "#define UIC_SHADOW 2", "#define UIC_YELLOW 3",
          "#define UIC_GRAY 4", "#define UIC_BLACK 5", "#define UIC_RED 6",
          "#define UIC_LGRAY 7", "#define UIC_DGRAY 8", "#define UIC_GREEN 9", "", "#endif", ""]
    with open(os.path.join(INC, "ui_layout.h"), "w") as f:
        f.write("\n".join(L))
    return sheet


# --------------------------------------------------------------------------
# Icono de la ROM (BMP 32x32 de 4 bits)
# --------------------------------------------------------------------------
def build_icon(path):
    im = Image.open(path).convert("RGBA")
    if im.size != (32, 32):
        im = im.resize((32, 32), Image.NEAREST)
    a = np.array(im)
    rgb = Image.fromarray(a[..., :3].copy(), "RGB")
    q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    qp = q.getpalette()[: 15 * 3]
    pal = [(255, 0, 255)] + [(qp[i * 3], qp[i * 3 + 1], qp[i * 3 + 2]) for i in range(15)]
    idx = np.array(q).astype(np.uint8) + 1
    idx[a[..., 3] < 128] = 0
    rows = []
    for y in range(31, -1, -1):  # BMP va de abajo hacia arriba
        row = bytearray()
        for x in range(0, 32, 2):
            row.append((int(idx[y, x]) << 4) | int(idx[y, x + 1]))
        rows.append(bytes(row))
    pix = b"".join(rows)
    hdr = struct.pack("<2sIHHI", b"BM", 14 + 40 + 64 + len(pix), 0, 0, 14 + 40 + 64)
    info = struct.pack("<IiiHHIIiiII", 40, 32, 32, 1, 4, 0, len(pix), 2835, 2835, 16, 16)
    pb = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in pal)
    with open(os.path.join(ROOT, "icon.bmp"), "wb") as f:
        f.write(hdr + info + pb + pix)
    print("  icon.bmp")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    src = Source(sys.argv[1])
    os.makedirs(INC, exist_ok=True)
    print("Atlas del terreno...")
    tiles = build_terrain(src)
    print("Iconos...")
    icons = [make_icon(tiles, s) for s in ICON_BLOCKS]
    icons += [make_item_icon(src, n) for n in ITEM_ICONS]   # indice de icono = B_COUNT + (id - I_FIRST)
    print("Fuente...")
    build_font(src)
    print("HUD...")
    build_hud(src, icons)
    print("Steve / cofres / logo / panorama...")
    build_misc(src)
    print("Mobs y armaduras...")
    build_mobs(src)
    build_armor(src)
    print("Hoja 2D...")
    sheet = build_ui(src, icons)
    sheet.save("/tmp/ui_sheet_preview.png")
    icon = os.path.join(ROOT, "tools", "icon.png")
    if os.path.exists(icon):
        build_icon(icon)
    print("Listo.")


if __name__ == "__main__":
    main()
