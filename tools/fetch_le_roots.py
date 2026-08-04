#!/usr/bin/env python3
"""Genera src/data/le_roots.h con las raices de Let's Encrypt.

Se bajan de la fuente oficial y NO se escriben a mano: un certificado mal
transcripto falla recien en tiempo de ejecucion, con un error de TLS opaco que
cuesta muchisimo diagnosticar.

Van las DOS raices, no solo la que sirve el servidor hoy:

- X1 es RSA, X2 es ECDSA. Caddy sirve una cadena u otra segun lo que negocie el
  cliente, y mbedTLS del ESP32 negocia distinto que un cliente de escritorio.
  Medido el 2026-08-04, un cliente Windows recibe la cadena que termina en X2;
  no hay garantia de que el aparato reciba la misma.
- Solo hace falta la raiz. Los intermedios los manda el servidor en el
  handshake, y ademas rotan: pinear un intermedio seria una bomba de tiempo.

Uso:
    python tools/fetch_le_roots.py
"""
import re
import ssl
import sys
import urllib.request
from pathlib import Path

FUENTES = [
    ("ISRG Root X1 (RSA)",   "https://letsencrypt.org/certs/isrgrootx1.pem"),
    ("ISRG Root X2 (ECDSA)", "https://letsencrypt.org/certs/isrg-root-x2.pem"),
]

SALIDA = Path(__file__).resolve().parent.parent / "src" / "data" / "le_roots.h"


def bajar(url):
    ctx = ssl.create_default_context()
    with urllib.request.urlopen(url, context=ctx, timeout=30) as fh:
        return fh.read().decode("ascii")


def validar(pem, nombre):
    """Chequeo minimo de cordura: que sea un PEM con base64 decodificable."""
    if "BEGIN CERTIFICATE" not in pem or "END CERTIFICATE" not in pem:
        raise SystemExit("ERROR: %s no parece un PEM" % nombre)
    cuerpo = "".join(l for l in pem.splitlines() if "BEGIN" not in l and "END" not in l)
    import base64
    try:
        der = base64.b64decode(cuerpo, validate=True)
    except Exception as e:
        raise SystemExit("ERROR: el base64 de %s no decodifica: %s" % (nombre, e))
    if len(der) < 300:
        raise SystemExit("ERROR: %s quedo sospechosamente corto (%d bytes)" % (nombre, len(der)))
    return len(der)


def main():
    lineas = [
        "#pragma once",
        "",
        "// Raices de Let's Encrypt para validar la descarga del firmware.",
        "//",
        "// Generado por tools/fetch_le_roots.py. NO editar a mano.",
        "//",
        "// Van las dos porque el servidor sirve una cadena u otra segun lo que",
        "// negocie el cliente, y mbedTLS del ESP32 negocia distinto que un cliente",
        "// de escritorio. Solo hacen falta las raices: los intermedios los manda el",
        "// servidor en el handshake y ademas rotan.",
        "//",
        "// El feed sigue usando setInsecure(): para noticias publicas el token ya",
        "// autentica y el contenido no es ejecutable. Un binario si lo es.",
        "",
        "static const char LE_ROOTS[] PROGMEM =",
    ]

    for nombre, url in FUENTES:
        pem = bajar(url)
        n = validar(pem, nombre)
        print("  %-22s %d bytes DER, desde %s" % (nombre, n, url))
        lineas.append('    // %s' % nombre)
        for l in pem.splitlines():
            if l.strip():
                lineas.append('    "%s\\n"' % l.strip())

    lineas.append("    ;")
    lineas.append("")

    SALIDA.parent.mkdir(parents=True, exist_ok=True)
    SALIDA.write_text("\n".join(lineas), encoding="ascii", newline="\n")
    print("escrito %s (%d bytes)" % (SALIDA, SALIDA.stat().st_size))


if __name__ == "__main__":
    sys.exit(main())
