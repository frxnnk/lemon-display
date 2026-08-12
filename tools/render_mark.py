#!/usr/bin/env python3
"""Rasteriza el isotipo de Ferced (public/logo.svg) a un header RGB565.

El SVG son cuatro poligonos con degrade lineal. En vez de depender de cairosvg
—que no esta instalado— se dibujan los poligonos a mano con PIL sobre un lienzo
8x y se promedia al bajar: el antialiasing sale del downsample, que es
exactamente lo que hace el proxy con las miniaturas.

El pixel totalmente transparente se emite como 0x0000, el centinela que ya usa
ferced_mark_11.h para no pintar.
"""
import sys
import numpy as np
from PIL import Image, ImageDraw

# Los cuatro planos, con las curvas Q —redondeos de una decima de pixel—
# aproximadas por su vertice. A esta escala no se distinguen.
PLANOS = [
    # (poligono, (x1,y1,color1), (x2,y2,color2))
    ([(178, 154.5), (179.5, 151.9), (276, 95), (276, 156.5), (178, 216)],
     (178, 216, 0x39D64E), (276, 96, 0x75FF84)),
    ([(286, 91), (319.8, 71.1), (322, 72.4), (322, 132), (286, 154)],
     (286, 154, 0xD64BD9), (322, 70, 0xFF99FF)),
    ([(178, 227), (322, 140), (322, 207), (230, 261), (230, 325), (178, 292)],
     (178, 292, 0x1DCCC5), (322, 140, 0x7FFFF8)),
    ([(178, 302), (230, 335), (322, 281), (322, 343), (181.8, 425.7), (178, 423.5)],
     (178, 426, 0x0D8FE0), (322, 281, 0x70C9FA)),
]

# Caja util del isotipo dentro del viewBox de 500x500.
BBOX = (178.0, 71.0, 322.0, 427.0)

SS = 8  # supermuestreo


def rgb(v):
    return ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)


def render(w, h):
    x0, y0, x1, y1 = BBOX
    bw, bh = x1 - x0, y1 - y0
    W, H = w * SS, h * SS
    sx, sy = W / bw, H / bh

    # Rejilla de coordenadas en el espacio del SVG, para evaluar el degrade.
    gx = x0 + (np.arange(W) + 0.5) / sx
    gy = y0 + (np.arange(H) + 0.5) / sy
    GX, GY = np.meshgrid(gx, gy)

    acc = np.zeros((H, W, 3), dtype=np.float64)
    alpha = np.zeros((H, W), dtype=np.float64)

    for poly, (ax, ay, ca), (bx, by, cb) in PLANOS:
        mask_img = Image.new("L", (W, H), 0)
        pts = [((px - x0) * sx, (py - y0) * sy) for px, py in poly]
        ImageDraw.Draw(mask_img).polygon(pts, fill=255)
        m = np.asarray(mask_img, dtype=np.float64) / 255.0

        # Proyeccion escalar sobre el eje del degrade, igual que userSpaceOnUse.
        dx, dy = bx - ax, by - ay
        den = dx * dx + dy * dy
        t = ((GX - ax) * dx + (GY - ay) * dy) / den
        t = np.clip(t, 0.0, 1.0)

        c0, c1 = np.array(rgb(ca), float), np.array(rgb(cb), float)
        grad = c0[None, None, :] + (c1 - c0)[None, None, :] * t[:, :, None]

        acc += grad * m[:, :, None]
        alpha = np.maximum(alpha, m)

    img = np.dstack([acc, alpha * 255.0]).astype(np.uint8)
    small = Image.fromarray(img, "RGBA").resize((w, h), Image.LANCZOS)
    return small


def to_header(img, name, path, canvas=(0x0E, 0x10, 0x11)):
    w, h = img.size
    px = np.asarray(img, dtype=np.float64)
    rgbf, a = px[:, :, :3], px[:, :, 3:4] / 255.0
    # Se compone contra el canvas: asi el borde antialiaseado ya viene fundido y
    # el firmware solo copia pixeles.
    comp = rgbf * a + np.array(canvas, float)[None, None, :] * (1.0 - a)
    comp = np.clip(comp, 0, 255).astype(np.uint16)

    r = (comp[:, :, 0] >> 3).astype(np.uint16)
    g = (comp[:, :, 1] >> 2).astype(np.uint16)
    b = (comp[:, :, 2] >> 3).astype(np.uint16)
    v = (r << 11) | (g << 5) | b
    # 0x0000 es el centinela de "no pintar": ningun pixel opaco puede caer ahi
    # porque todos los planos son claros, pero se fuerza por las dudas.
    v = np.where((a[:, :, 0] < 0.02), 0, np.maximum(v, 1))

    out = [
        "#pragma once",
        "",
        "// Isotipo de Ferced a color, generado desde ferced-landing-page/public/logo.svg",
        "// con tools/render_mark.py. Los cuatro planos con sus degrades, compuestos",
        f"// contra el canvas (#0e1011). 0x0000 = no pintar.",
        f"// {w}x{h} px, RGB565 en orden nativo.",
        "",
        f"const uint16_t {name}[{w * h}] PROGMEM = {{",
    ]
    flat = v.reshape(-1)
    for i in range(0, flat.size, 12):
        chunk = ", ".join(f"0x{int(x):04X}" for x in flat[i:i + 12])
        out.append("    " + chunk + ",")
    out.append("};")
    out.append("")
    with open(path, "w", newline="\r\n") as f:
        f.write("\n".join(out))
    print(f"  {name}: {w}x{h}, {w*h*2} bytes")


if __name__ == "__main__":
    w, h, name, path = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4]
    to_header(render(w, h), name, path)
