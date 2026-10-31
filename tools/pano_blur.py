"""Difuminado del panorama del menu principal, como en Minecraft 1.7.10.

1.7.10 dibuja el panorama 16 veces con pequenos desplazamientos y despues lo difumina 7 veces
(GuiMainMenu.drawPanorama / rotateAndBlurSkybox), asi que se ve suave. La DS muestrea las texturas sin
filtrado y se veria en bloques; aqui se difumina el CUBO ya en las texturas, teniendo en cuenta las caras
vecinas (no cada cara por separado, que dejaria lineas en las costuras).

Las caras usan la misma geometria que source/render.c (pano_vt + uv_c).
"""
import numpy as np

# vertices de cada cara, en el orden de pano_vt en render.c; uv de cada vertice: (0,0) (1,0) (1,1) (0,1)
PANO_VT = [
    [(-1, 1, -1), (1, 1, -1), (1, -1, -1), (-1, -1, -1)],   # 0 frente
    [(1, 1, -1), (1, 1, 1), (1, -1, 1), (1, -1, -1)],       # 1 derecha
    [(1, 1, 1), (-1, 1, 1), (-1, -1, 1), (1, -1, 1)],       # 2 atras
    [(-1, 1, 1), (-1, 1, -1), (-1, -1, -1), (-1, -1, 1)],   # 3 izquierda
    [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)],       # 4 arriba
    [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)],   # 5 abajo
]
_P0 = np.array([np.array(f[0], float) for f in PANO_VT])
_EU = np.array([np.array(f[1], float) - np.array(f[0], float) for f in PANO_VT])
_EV = np.array([np.array(f[3], float) - np.array(f[0], float) for f in PANO_VT])
_N = np.array([(np.array(f).mean(axis=0)) for f in PANO_VT])        # normal (eje unitario) de cada cara


def face_point(f, u, v):
    """Punto 3D de la cara f en coordenadas (u, v) de 0..1 (puede salirse de la cara)."""
    return _P0[f] + u[..., None] * _EU[f] + v[..., None] * _EV[f]


def sample_cube(faces, d):
    """Muestrea el cubo (6 imagenes HxWx3 float) en las direcciones d (...,3), bilinear."""
    h, w = faces[0].shape[:2]
    out = np.zeros(d.shape[:-1] + (3,), np.float32)
    dots = np.einsum("...k,fk->...f", d, _N)
    which = dots.argmax(axis=-1)
    for f in range(6):
        m = which == f
        if not m.any():
            continue
        p = d[m] / dots[m][..., f][..., None]                        # proyeccion al plano de la cara
        u = np.einsum("nk,k->n", p - _P0[f], _EU[f]) / np.dot(_EU[f], _EU[f])
        v = np.einsum("nk,k->n", p - _P0[f], _EV[f]) / np.dot(_EV[f], _EV[f])
        x = np.clip(u * w - 0.5, 0, w - 1.001)
        y = np.clip(v * h - 0.5, 0, h - 1.001)
        x0 = x.astype(int); y0 = y.astype(int)
        fx = (x - x0)[:, None]; fy = (y - y0)[:, None]
        im = faces[f]
        out[m] = (im[y0, x0] * (1 - fx) * (1 - fy) + im[y0, x0 + 1] * fx * (1 - fy)
                  + im[y0 + 1, x0] * (1 - fx) * fy + im[y0 + 1, x0 + 1] * fx * fy)
    return out


def blur_cube(faces, sigma=1.5):
    """Gaussiana sobre el cubo. faces: lista de 6 arrays HxWx3 float32. sigma en texels."""
    h, w = faces[0].shape[:2]
    r = int(np.ceil(sigma * 2.5))
    offs = np.arange(-r, r + 1)
    k = np.exp(-(offs ** 2) / (2 * sigma ** 2)); k /= k.sum()
    uu, vv = np.meshgrid((np.arange(w) + 0.5) / w, (np.arange(h) + 0.5) / h)
    res = []
    for f in range(6):
        acc = np.zeros((h, w, 3), np.float32)
        for j, ky in zip(offs, k):
            for i, kx in zip(offs, k):
                d = face_point(f, uu + i / w, vv + j / h)
                acc += (kx * ky) * sample_cube(faces, d)
        res.append(acc)
    return res


def blur_panorama_images(pil_images, size=128, sigma=1.5):
    """6 imagenes PIL (RGB) -> 6 imagenes PIL RGB de size x size, difuminadas."""
    from PIL import Image
    faces = [np.asarray(im.convert("RGB").resize((size, size), Image.LANCZOS), np.float32) for im in pil_images]
    out = blur_cube(faces, sigma)
    return [Image.fromarray(np.clip(o + 0.5, 0, 255).astype(np.uint8), "RGB") for o in out]
