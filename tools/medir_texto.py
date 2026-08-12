#!/usr/bin/env python3
"""Mide el ancho en pixeles de un texto en una fuente GFX ya generada.

Existe porque en este proyecto los anchos van medidos, no elegidos a ojo, y
hasta ahora medirlos exigia compilar el simulador, dibujar y mirar la captura.
Aca se lee el .h directamente: LovyanGFX suma el xAdvance de cada glifo, asi que
el numero sale igual sin encender nada.

    python tools\\medir_texto.py src\\data\\GeorgiaItalic28.h "2 h 14 min" "35.4 fps"
"""
import re
import sys
from pathlib import Path


def cargar(path):
    """Devuelve (first, last, {codepoint: xAdvance})."""
    txt = Path(path).read_text(encoding="utf-8", errors="replace")

    # Los glifos son { offset, w, h, xAdvance, xOffset, yOffset }.
    bloque = re.search(r"Glyphs\[\]\s*PROGMEM\s*=\s*\{(.*?)\n\};", txt, re.S)
    if not bloque:
        raise SystemExit(f"{path}: no encontre el array de glifos")
    tuplas = re.findall(r"\{\s*([^}]*?)\s*\}", bloque.group(1))
    avances = []
    for t in tuplas:
        campos = [c.strip() for c in t.split(",")]
        if len(campos) < 4:
            continue
        avances.append(int(campos[3], 0))

    # El rango sale de la declaracion del GFXfont: (..., first, last, yAdvance).
    pie = re.search(r"GFXfont\s+\w+\s*PROGMEM\s*=\s*\{(.*?)\};", txt, re.S)
    nums = re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", pie.group(1)) if pie else []
    first, last = 0x20, 0x20 + len(avances) - 1
    if len(nums) >= 3:
        first, last = int(nums[-3], 0), int(nums[-2], 0)
    return first, last, avances


def ancho(texto, first, last, avances):
    total = 0
    fuera = []
    for ch in texto:
        cp = ord(ch)
        if cp < first or cp > last:
            fuera.append(ch)
            continue
        total += avances[cp - first]
    return total, fuera


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    first, last, avances = cargar(sys.argv[1])
    print(f"{Path(sys.argv[1]).name}: rango 0x{first:02X}-0x{last:02X}, "
          f"{len(avances)} glifos")
    for texto in sys.argv[2:]:
        w, fuera = ancho(texto, first, last, avances)
        aviso = f"   FUERA DE RANGO: {''.join(fuera)!r}" if fuera else ""
        print(f"  {w:4d} px  {texto!r}{aviso}")
