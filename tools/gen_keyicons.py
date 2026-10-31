# Genera source/keyicons.c desde tools/ds_minecraft_icons.png (0 = transparente, 1 = blanco, 5 = negro: indices de paleta UIC_*)
from PIL import Image
im = Image.open('tools/ds_minecraft_icons.png').convert('RGBA'); w, h = im.size
px = []
for y in range(h):
    for x in range(w):
        r, g, b, a = im.getpixel((x, y))
        px.append(0 if a < 128 else (1 if r > 128 else 5))
names = [("DPAD",73,36,16,16),("L",68,0,16,16),("R",85,0,16,16),("A",34,17,15,15),("B",51,17,15,15),
         ("X",68,17,15,15),("Y",85,17,15,15),("SELECT",1,40,32,10),("START",35,40,32,10)]
o = ['/* GENERADO por tools/gen_keyicons.py - no editar */', '#include "keyicons.h"',
     'const KeyIcon key_icons[KEY_ICON_COUNT] = {']
o += ['    {"%s", %d, %d, %d, %d},' % n for n in names] + ['};',
     'const unsigned char key_sheet[%d] = {' % (w*h)]
for i in range(0, len(px), 32): o.append('    ' + ','.join(map(str, px[i:i+32])) + ',')
o.append('};')
open('source/keyicons.c', 'w').write('\n'.join(o) + '\n')
open('include/keyicons.h', 'w').write('''/* GENERADO por tools/gen_keyicons.py */
#ifndef KEYICONS_H
#define KEYICONS_H
#define KEY_SHEET_W %d
#define KEY_ICON_COUNT %d
typedef struct { const char *name; unsigned char x, y, w, h; } KeyIcon;
extern const KeyIcon key_icons[KEY_ICON_COUNT];
extern const unsigned char key_sheet[];
#endif
''' % (w, len(names)))
