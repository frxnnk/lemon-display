#!/usr/bin/env python3
"""Convierte el icono de un agente a una mascara de alfa para el aparato.

Los iconos oficiales vienen como cuadrados de dos tonos y opacos —el de Claude
es un destello crema sobre coral, el de OpenAI un nudo negro sobre blanco—, asi
que no se pueden empujar tal cual: sobre el canvas del aparato uno perderia el
recorte y el otro directamente no se veria.

Lo que se guarda es la FORMA, no los colores: un byte de alfa por pixel. El
firmware la tine al dibujar, y por eso puede usar el color de marca cuando el
aviso esta sin leer y atenuarla cuando ya se leyo, sin guardar dos copias. Ocupa
la mitad que un RGB565 y es lo unico que hace falta, porque los dos iconos son
de un solo color.

    python tools\\marca_agente.py claude.png 20 claude_20 src\\data\\marca_claude_20.h
    python tools\\marca_agente.py openai.png 20 codex_20  src\\data\\marca_codex_20.h --invertir

--invertir es para los iconos oscuros sobre fondo claro (el de OpenAI): sin eso
la mascara sale del fondo y no del nudo.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

# Supermuestreo antes de bajar: el antialiasing sale del promedio, igual que en
# las miniaturas del proxy.
SS = 4


def mascara(path, invertir):
    im = Image.open(path).convert("RGBA")
    a = np.asarray(im).astype(np.float64)
    rgb, alfa = a[:, :, :3], a[:, :, 3] / 255.0

    # Luma perceptual. Los dos iconos son de dos tonos planos, asi que el umbral
    # entre esos dos tonos separa la marca del fondo sin perder los bordes
    # suavizados, que son los que hacen que se lea a 20 px.
    luma = 0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]
    if invertir:
        luma = 255.0 - luma

    # Los extremos se miden SOLO sobre la parte opaca del icono. Metiendo las
    # esquinas redondeadas —que son transparentes— en el percentil, el minimo
    # lo fijaban ellas y no el fondo del icono, asi que el fondo terminaba en
    # 0,15 en vez de en 0 y la marca salia con un cuadrado de neblina detras.
    opaco = alfa > 0.5
    if not opaco.any():
        raise SystemExit(f"{path}: el icono es todo transparente")
    lo, hi = np.percentile(luma[opaco], 2), np.percentile(luma[opaco], 98)
    if hi - lo < 1.0:
        raise SystemExit(f"{path}: la imagen es de un solo tono, no hay marca que sacar")

    m = np.clip((luma - lo) / (hi - lo), 0.0, 1.0)
    # Y lo transparente no es marca, pase lo que pase con su color.
    return m * alfa


def recortar(m, umbral=0.35):
    """Ajusta al rectangulo de la marca: los iconos traen aire alrededor y a
    20 px ese aire se come la mitad del dibujo."""
    filas = np.where(m.max(axis=1) > umbral)[0]
    cols = np.where(m.max(axis=0) > umbral)[0]
    if not len(filas) or not len(cols):
        return m
    y0, y1 = filas[0], filas[-1] + 1
    x0, x1 = cols[0], cols[-1] + 1
    # Cuadrado centrado, para que no se deforme al escalar.
    lado = max(y1 - y0, x1 - x0)
    cy, cx = (y0 + y1) // 2, (x0 + x1) // 2
    y0, x0 = cy - lado // 2, cx - lado // 2
    rec = np.zeros((lado, lado))
    sy0, sx0 = max(0, y0), max(0, x0)
    sy1, sx1 = min(m.shape[0], y0 + lado), min(m.shape[1], x0 + lado)
    rec[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0] = m[sy0:sy1, sx0:sx1]
    return rec


def emitir(m, lado, nombre, salida, origen):
    img = Image.fromarray((m * 255).astype(np.uint8), "L")
    img = img.resize((lado * SS, lado * SS), Image.LANCZOS)
    img = img.resize((lado, lado), Image.LANCZOS)
    v = np.clip(np.asarray(img).astype(int), 0, 255)

    out = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <pgmspace.h>",
        "",
        f"// Mascara de alfa de {lado}x{lado}, generada con tools/marca_agente.py",
        f"// desde {Path(origen).name}. Un byte por pixel: 0 = nada, 255 = marca.",
        "// El color lo pone el firmware al dibujar.",
        "",
        f"static const uint8_t {nombre}[{lado * lado}] PROGMEM = {{",
    ]
    plano = v.reshape(-1)
    for i in range(0, plano.size, 16):
        out.append("    " + ", ".join(str(int(x)) for x in plano[i:i + 16]) + ",")
    out.append("};")
    out.append("")
    Path(salida).write_text("\n".join(out), newline="\r\n")
    print(f"  {nombre}: {lado}x{lado}, {lado*lado} bytes de flash -> {salida}")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("png")
    p.add_argument("lado", type=int)
    p.add_argument("nombre")
    p.add_argument("salida")
    p.add_argument("--invertir", action="store_true")
    args = p.parse_args()

    m = recortar(mascara(args.png, args.invertir))
    emitir(m, args.lado, args.nombre, args.salida, args.png)
