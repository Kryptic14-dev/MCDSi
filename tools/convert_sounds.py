#!/usr/bin/env python3
"""Convierte los .ogg de Minecraft 1.7.10 a un unico data/sfx.bin (PCM 8 bits con signo, mono)
+ include/sfx_ids.h, adaptado al hardware de la Nintendo DS.

Uso:
    python3 tools/convert_sounds.py                     # usa los zip de tools/sounds/
    python3 tools/convert_sounds.py a.zip b.zip carpeta # zips y/o carpetas con dig/ step/ random/ liquid/
    (los sonidos de jugador y mobs -comer, dano, explosion, flecha, voces de mobs- se incluyen siempre)

Acepta cualquier combinacion de zips o carpetas: cada .ogg se identifica por sus dos ultimos
componentes de ruta ("dig/stone1.ogg"), asi que da igual si vienen en "sounds/dig/..." o en "dig/...".
Requiere ffmpeg y numpy.

Que hace para la DS (y por que):
  * Tono como en Minecraft 1.7.10, pero ya aplicado al convertir. La DS reproduce cada canal por
    "muestra y mantener" (sin interpolar): bajar la tasa en tiempo real (p. ej. a la mitad) mete
    imagenes de aliasing y suena rasposo. Mejor guardar el sonido ya cambiado de tono:
        romper / poner bloque = sonido "dig" a 0.8x      golpeo al picar = sonido "step" a 0.5x
        recoger item          = random.pop a 2.0x
  * Filtro paso alto a 150 Hz (el altavoz de la DS no reproduce graves) y paso bajo escalonado a
    0.4 x tasa antes de reducir la tasa (evita el aliasing por muestra y mantener).
  * La tasa de cada sonido se elige entre 8000/11025/16000/22050 Hz: la mas baja que conserva el
    98 % de la energia. Los sonidos "de ruido" (arena, hierba) suben solos; los golpes graves
    (piedra, madera) bajan y ocupan menos memoria.
  * Normalizacion POR GRUPO (todas las variantes conservan su nivel relativo) y compresion suave
    (tanh): el PCM de 8 bits solo tiene ~48 dB, asi que se sube el nivel de las colas para que
    no queden como ruido de cuantizacion.
  * Recorte automatico del silencio final, fundido de entrada (2 ms) y de salida (hasta 40 ms): sin
    chasquidos ni siseo al final del sonido.
"""
import sys, os, io, zipfile, subprocess, struct, glob
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WORK = 44100                      # tasa de trabajo (los originales son 44100 Hz mono)
OUT_RATE = 32768                  # la DS mezcla su salida a 32768 Hz (33513982/1024): a esta tasa no hay "muestra y mantener" que deforme el tono
RATES = (8000, 11025, 16000, 22050, 32768)
TONE_TOL_DB = 2.0                 # distancia espectral extra (dB) permitida respecto a guardarlo a 32768 Hz
MAX_RATE = OUT_RATE               # --max-rate=22050 limita la tasa (menos memoria, algo mas de aliasing)
HF_LIMIT = 0.02                   # energia maxima permitida por encima del corte del paso bajo
HP_HZ = 150
DRIVE = 1.6                       # fuerza de la compresion tanh
PEAK = 0.95

SHORT_S = 0.08                    # sonidos mas cortos que esto (pop, clic) se guardan siempre a la tasa maxima: casi no ocupan
# random.pop en 1.7.10: pitch = ((r1 - r2) * 0.7 + 1) * 2 y el motor lo recorta a [0.5, 2.0]  ->  ~50 % de las veces 2.0
# y el resto repartido por debajo. Se guardan 5 variantes (cuantiles de esa distribucion); sfx.c elige con peso 4:1:1:1:1.
POP_PITCHES = (1.10, 1.46, 1.71, 1.91, 2.00)

# (grupo, carpeta/prefijo, max. variantes (0 = todas), duracion maxima en s, tono  o  tupla de tonos = una variante por tono)
GROUPS = [
    # romper / poner bloque: dig a 0.8x (como RenderGlobal / ItemBlock de 1.7.10)
    ("DIG_GRASS",  "dig/grass",  0, 0.70, 0.8), ("DIG_GRAVEL", "dig/gravel", 0, 0.70, 0.8),
    ("DIG_SAND",   "dig/sand",   0, 0.70, 0.8), ("DIG_SNOW",   "dig/snow",   0, 0.70, 0.8),
    ("DIG_STONE",  "dig/stone",  0, 0.70, 0.8), ("DIG_WOOD",   "dig/wood",   0, 0.70, 0.8),
    ("DIG_CLOTH",  "dig/cloth",  0, 0.70, 0.8),
    # pasos
    ("STEP_GRASS", "step/grass", 0, 0.35, 1.0), ("STEP_GRAVEL","step/gravel",0, 0.35, 1.0),
    ("STEP_SAND",  "step/sand",  0, 0.35, 1.0), ("STEP_SNOW",  "step/snow",  0, 0.35, 1.0),
    ("STEP_STONE", "step/stone", 0, 0.35, 1.0), ("STEP_WOOD",  "step/wood",  0, 0.35, 1.0),
    ("STEP_CLOTH", "step/cloth", 0, 0.35, 1.0),
    # golpeo mientras se pica: en 1.7.10 es el sonido de PASO a 0.5x de tono (3 variantes bastan)
    ("HIT_GRASS",  "step/grass", 3, 0.45, 0.5), ("HIT_GRAVEL", "step/gravel",3, 0.45, 0.5),
    ("HIT_SAND",   "step/sand",  3, 0.45, 0.5), ("HIT_SNOW",   "step/snow",  3, 0.45, 0.5),
    ("HIT_STONE",  "step/stone", 3, 0.45, 0.5), ("HIT_WOOD",   "step/wood",  3, 0.45, 0.5),
    ("HIT_CLOTH",  "step/cloth", 3, 0.45, 0.5),
    ("GLASS",      "random/glass", 0, 0.80, 0.8),
    ("SWIM",       "liquid/swim",  0, 0.80, 1.0), ("SPLASH", "liquid/splash", 0, 1.20, 1.0),
    ("POP",        "random/pop",   0, 0.20, POP_PITCHES), ("CLICK",  "random/click",  0, 0.30, 1.0),
    ("TOOLBREAK",  "random/break", 0, 0.80, 1.0),
]

# Jugador, combate y mobs (el 6o valor = tasa maxima de ese grupo: las voces de los mobs no necesitan 32 kHz
# y asi sfx.bin ocupa mucho menos RAM).
MOBR = 16000
MOB_MIN_RATE = 16000              # las voces a 8 kHz sonaban apagadas y 'crujientes': minimo 16 kHz
# ajuste de volumen por grupo (la normalizacion por pico deja muy fuertes los sonidos de ruido o muy graves)
LEVEL = {"DIG_SAND": 0.5, "ZOMBIE_DEATH": 0.7, "ZOMBIE_SAY": 0.8, "CREEPER_DEATH": 0.7}
MOB_GROUPS = [
    ("EAT",        "random/eat",   0, 0.60, 1.0), ("BURP",  "random/burp",   0, 0.80, 1.0),
    ("HURT",       "random/classic_hurt", 0, 0.90, 1.0),
    ("DAMAGE_HIT", "damage/hit",   0, 0.50, 1.0),       # el golpe de 1.7.10 (opcional: damage.zip)
    ("EXPLODE",    "random/explode", 3, 1.60, 1.0, 22050), ("FUSE", "random/fuse", 0, 1.60, 1.0, MOBR),
    ("BOW",        "random/bow",   0, 0.40, 1.0), ("BOWHIT", "random/bowhit", 2, 0.65, 1.0),
    ("PIG_SAY",    "pig/say",      0, 0.70, 1.0, MOBR), ("PIG_DEATH", "pig/death", 0, 0.80, 1.0, MOBR),
    ("COW_SAY",    "cow/say",      3, 1.40, 1.0, MOBR), ("COW_HURT", "cow/hurt", 2, 0.70, 1.0, MOBR),
    ("SHEEP_SAY",  "sheep/say",    0, 1.05, 1.0, MOBR),
    ("CHICKEN_SAY","chicken/say",  0, 0.75, 1.0, MOBR), ("CHICKEN_HURT", "chicken/hurt", 0, 0.55, 1.0, MOBR),
    ("ZOMBIE_SAY", "zombie/say",   0, 1.90, 1.0, MOBR), ("ZOMBIE_HURT", "zombie/hurt", 0, 0.75, 1.0, MOBR),
    ("ZOMBIE_DEATH", "zombie/death", 0, 1.00, 1.0, MOBR),
    ("SKEL_SAY",   "skeleton/say", 0, 0.90, 1.0, MOBR), ("SKEL_HURT", "skeleton/hurt", 2, 0.60, 1.0, MOBR),
    ("SKEL_DEATH", "skeleton/death", 0, 1.05, 1.0, MOBR),
    ("CREEPER_HURT", "creeper/say", 2, 0.70, 1.0, MOBR), ("CREEPER_DEATH", "creeper/death", 0, 1.10, 1.0, MOBR),
    ("SPIDER_SAY", "spider/say",   3, 0.80, 1.0, MOBR), ("SPIDER_DEATH", "spider/death", 0, 1.30, 1.0, MOBR),
    ("CHESTOPEN",  "random/chestopen",   0, 0.70, 0.95, MOBR),
    ("CHESTCLOSE", "random/chestclosed", 0, 0.70, 0.95, MOBR),
    # pasos de los mobs (mob.<x>.step de 1.7.10; el creeper usa el paso del bloque) y flecha acertada
    ("PIG_STEP",     "pig/step",      3, 0.30, 1.0, MOBR), ("COW_STEP",   "cow/step",      3, 0.30, 1.0, MOBR),
    ("SHEEP_STEP",   "sheep/step",    3, 0.30, 1.0, MOBR), ("CHICKEN_STEP", "chicken/step", 2, 0.20, 1.0, MOBR),
    ("ZOMBIE_STEP",  "zombie/step",   3, 0.35, 1.0, MOBR), ("SKEL_STEP",  "skeleton/step", 3, 0.30, 1.0, MOBR),
    ("SPIDER_STEP",  "spider/step",   3, 0.30, 1.0, MOBR),
    ("SUCCESSHIT",   "random/successful_hit", 0, 0.40, 1.0, MOBR),   # "ding" al clavar una flecha propia en un mob
]
EXTRA_GROUPS = MOB_GROUPS         # --extras sigue funcionando (ahora ya van incluidos siempre)


# ---------------------------------------------------------------------------------------------
def index_sources(paths):
    """clave 'dir/nombre' (sin .ogg) -> funcion que devuelve los bytes del .ogg"""
    idx = {}
    depth = {}

    def add(rel, getter):
        parts = rel.replace("\\", "/").split("/")
        if len(parts) < 2 or "__MACOSX" in parts or parts[-1].startswith(".") or not parts[-1].endswith(".ogg"):
            return
        key = "/".join(parts[-2:])[:-4]
        # misma clave en varias rutas (p. ej. mob/skeleton/death y mob/horse/skeleton/death): gana la menos profunda,
        # que es la del mob normal y no la del caballo esqueleto / zombi
        if key in depth and depth[key] <= len(parts): return
        depth[key] = len(parts)
        idx[key] = getter

    expanded = []
    for p in paths:                       # una carpeta puede contener .ogg sueltos y/o .zip
        if os.path.isdir(p):
            expanded += sorted(glob.glob(os.path.join(p, "*.zip")))
            for f in glob.glob(os.path.join(p, "**", "*.ogg"), recursive=True):
                add(os.path.relpath(f, p), lambda f=f: open(f, "rb").read())
        else:
            expanded.append(p)
    for p in expanded:
        if True:
            zf = zipfile.ZipFile(p)
            for n in zf.namelist():
                add(n, lambda zf=zf, n=n: zf.read(n))
    return idx


def ffmpeg_f32(data, af, rate):
    """Pasa el .ogg por ffmpeg (mono, tasa 'rate', filtros 'af') y devuelve float32."""
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", "pipe:0", "-ac", "1", "-af", af,
                          "-ar", str(rate), "-f", "f32le", "pipe:1"],
                         input=data, capture_output=True, check=True).stdout
    return np.frombuffer(out, dtype="<f4").astype(np.float32)


def pitch_filter(pitch):
    # cambio de tono por velocidad (como hace Minecraft) y vuelta a la tasa de trabajo
    return "asetrate=%d,aresample=%d,highpass=f=%d" % (round(WORK * pitch), WORK, HP_HZ)


def band_levels(y, fs):
    """Nivel (dB) por tercios de octava entre 200 Hz y 12.5 kHz (la banda util del altavoz de la DS)."""
    n = 1 << int(np.ceil(np.log2(max(len(y), 512))))
    spec = np.abs(np.fft.rfft(y * np.hanning(len(y)), n)) ** 2
    f = np.fft.rfftfreq(n, 1.0 / fs)
    edges = 200 * 2 ** (np.arange(0, 6.2, 1 / 3))
    edges = edges[edges <= 12500]
    return np.array([10 * np.log10(spec[(f >= a) & (f < b)].sum() + 1e-20) for a, b in zip(edges[:-1], edges[1:])])


def spectral_distance(ref, y):
    """Distancia (dB, RMS por bandas) entre el sonido original y lo que sale de la DS."""
    a, b = band_levels(ref, WORK), band_levels(y, OUT_RATE)
    floor = a.max() - 45
    return float(np.sqrt(np.mean((np.maximum(a, floor) - np.maximum(b, floor)) ** 2)))


def ds_hold(x, rate):
    """Lo que hace el hardware de la DS: cada muestra se mantiene hasta la siguiente (SIN interpolar)
    y la mezcla sale a 32768 Hz. Los emuladores suelen interpolar, por eso alli suena mas oscuro."""
    n = int(len(x) * OUT_RATE / rate)
    return x[np.minimum((np.arange(n) * rate // OUT_RATE).astype(int), len(x) - 1)]


def choose_rate(data, pitch, max_rate=None, min_rate=0):
    """Tasa mas baja con la que, REPRODUCIDO COMO LO HACE LA DS, el sonido se parece al original casi tanto
    como guardado a 32768 Hz. (Antes se elegia por energia bajo 0.4 x tasa; pero la DS no filtra al
    reproducir: aparecen copias espejo del sonido alrededor de la tasa y suena mas agudo que en el emulador.)"""
    ref = ffmpeg_f32(data, pitch_filter(pitch) + ",lowpass=f=16000", WORK)
    cand = [r for r in RATES if min_rate <= r <= (max_rate or MAX_RATE)] or [max_rate or MAX_RATE]
    best = spectral_distance(ref, ds_hold(render(data, pitch, cand[-1]), cand[-1]))
    for r in cand[:-1]:
        if spectral_distance(ref, ds_hold(render(data, pitch, r), r)) <= best + TONE_TOL_DB:
            return r
    return cand[-1]


def render(data, pitch, rate):
    """Filtrado final y reduccion de tasa. Paso bajo de 4 etapas (48 dB/oct) antes de decimar."""
    fc = int(0.4 * rate)
    af = pitch_filter(pitch) + (",lowpass=f=%d" % fc) * 4
    return ffmpeg_f32(data, af, rate)


def trim_and_fade(x, rate, cap):
    # fin audible: ultima ventana de 10 ms con energia a menos de 42 dB del maximo
    w = max(1, rate // 100)
    n = len(x) // w
    if n:
        env = np.sqrt((x[: n * w].reshape(n, w) ** 2).mean(axis=1))
        keep = np.nonzero(env > env.max() * 10 ** (-42 / 20))[0]
        if len(keep):
            x = x[: (keep[-1] + 2) * w]
    x = x[: int(cap * rate)].copy()
    fi = max(1, int(0.002 * rate))
    fo = min(int(0.04 * rate), len(x) // 3)
    x[:fi] *= np.linspace(0, 1, fi)
    if fo > 1:
        x[-fo:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, fo)))
    return x


def to_s8(y, rng):
    y = np.tanh(DRIVE * y) / np.tanh(DRIVE)          # compresion suave: sube las colas
    k = max(1, len(y) // 400)
    env = np.convolve(np.abs(y), np.ones(k) / k, mode="same")
    dither = (rng.random(len(y)) - rng.random(len(y))) * 0.5 * np.minimum(1.0, env / 0.02)
    s8 = np.clip(np.round(y * PEAK * 127 + dither), -127, 127).astype(np.int8)
    s8[0] = 0
    # libnds pide un tamano multiplo de 4; se anaden >= 4 ceros finales (sin escalon al terminar)
    pad = 4 + (-len(s8)) % 4
    return np.concatenate([s8, np.zeros(pad, np.int8)]).tobytes()


def main():
    global MAX_RATE
    for a in sys.argv[1:]:
        if a.startswith("--max-rate="):
            MAX_RATE = int(a.split("=")[1])
    args = [a for a in sys.argv[1:] if a != "--extras" and not a.startswith("--max-rate=")]
    groups = GROUPS + MOB_GROUPS
    idx = index_sources(args or [os.path.join(HERE, "sounds")])
    if not idx:
        sys.exit("No se encontro ningun .ogg en: " + ", ".join(args))
    rng = np.random.default_rng(1)
    blobs, ids, report = [], [], []
    for gname, path, maxv, cap, pitch, *opt in groups:
        gmax = min(opt[0], MAX_RATE) if opt else MAX_RATE
        # variantes: "nombre.ogg" y/o "nombre1.ogg", "nombre2.ogg"... (p. ej. liquid/splash + splash2)
        keys = ([path] if path in idx else []) + [path + str(i) for i in range(1, 64) if path + str(i) in idx]
        if maxv:
            keys = keys[:maxv]
        if not keys:
            print("AVISO: faltan los sonidos de %s (%s), se omite el grupo" % (gname, path))
            continue
        pitches = list(pitch) if isinstance(pitch, tuple) else [pitch] * len(keys)
        if isinstance(pitch, tuple):
            keys = keys[:1] * len(pitches)          # un solo .ogg, varios tonos
        items = []
        for k, pt in zip(keys, pitches):
            data = idx[k]()
            rate = choose_rate(data, pt, gmax, MOB_MIN_RATE if opt else 0)
            x = trim_and_fade(render(data, pt, rate), rate, cap)
            if len(x) / rate < SHORT_S and rate < gmax:
                rate = gmax
                x = trim_and_fade(render(data, pt, rate), rate, cap)
            items.append((x, rate))
        # normalizacion por grupo: la variante mas fuerte llega al pico
        pk = max(float(np.abs(x).max()) for x, _ in items) or 1.0
        gain = min(1.0 / pk, 16.0) * LEVEL.get(gname, 1.0)
        first = len(blobs)
        for x, rate in items:
            blobs.append((to_s8(np.clip(x * gain, -1, 1), rng), rate))
        ids.append((gname, first, len(blobs) - first))
        report.append("%-10s x%d  %-22s %.2fs  tono %.1f  ganancia x%.1f" % (
            gname, len(items), "/".join(str(r) for _, r in items),
            max(len(x) / r for x, r in items),
            (sum(pitches) / len(pitches)), gain))

    head = 4 + 12 * len(blobs)
    out, ent, off = io.BytesIO(), [], head
    for b, r in blobs:
        ent.append((off, len(b), r))
        off += len(b)
    out.write(struct.pack("<I", len(blobs)))
    for o, s, r in ent:
        out.write(struct.pack("<III", o, s, r))
    for b, _ in blobs:
        out.write(b)
    os.makedirs(os.path.join(ROOT, "data"), exist_ok=True)
    with open(os.path.join(ROOT, "data", "sfx.bin"), "wb") as f:
        f.write(out.getvalue())
    h = ["// Generado por tools/convert_sounds.py - no editar",
         "#ifndef SFX_IDS_H", "#define SFX_IDS_H", "", "enum {"]
    h += ["    SFXG_%s," % g for g, _, _ in ids] + ["    SFXG_COUNT", "};", ""]
    h += ["#define HAVE_SFXG_%s 1" % g for g, _, _ in ids] + [""]
    h.append("static const unsigned short sfx_group_first[SFXG_COUNT] = {%s};" % ",".join(str(f) for _, f, _ in ids))
    h.append("static const unsigned char sfx_group_count[SFXG_COUNT] = {%s};" % ",".join(str(c) for _, _, c in ids))
    h += ["", "#endif", ""]
    with open(os.path.join(ROOT, "include", "sfx_ids.h"), "w") as f:
        f.write("\n".join(h))
    print("\n".join(report))
    print("sfx.bin: %d sonidos, %.1f KB" % (len(blobs), len(out.getvalue()) / 1024))


main()
