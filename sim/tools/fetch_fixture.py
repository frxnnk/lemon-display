"""Captura contenido real del proxy y lo deja como fixture del simulador.

Asi el simulador muestra exactamente lo mismo que la caja: mismos titulares ya
normalizados a ASCII y mismas imagenes en RGB565, tal como las sirve el proxy.

Uso:
    python tools/fetch_fixture.py                      # contra el VPS
    python tools/fetch_fixture.py http://192.168.1.34:9110
"""
import json
import os
import ssl
import sys
import urllib.request

BASE = sys.argv[1] if len(sys.argv) > 1 else "https://feed.ferced.com"
TOKEN_FILE = os.path.join(os.path.dirname(__file__), "..", "..", "output", "FEED_TOKEN.txt")
OUT = os.path.join(os.path.dirname(__file__), "..", "data")

token = ""
if os.path.exists(TOKEN_FILE):
    with open(TOKEN_FILE, encoding="utf-8") as fh:
        token = fh.read().strip()

CTX = ssl.create_default_context()


def get(path):
    req = urllib.request.Request(BASE + path)
    if token:
        req.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(req, timeout=60, context=CTX) as r:
        return r.read()


os.makedirs(OUT, exist_ok=True)
data = json.loads(get("/v1/feed?n=20"))
items = data.get("items", [])
print("bajados %d items" % len(items))

lines = ["# texto|autor|handle|epoch|origen|imagen"]
imgs = 0
for it in items:
    img = ""
    key = it.get("k", "")
    if key:
        try:
            raw = get("/v1/img?k=" + key)
            name = key + ".bin"
            with open(os.path.join(OUT, name), "wb") as fh:
                fh.write(raw)
            img = key  # sin extension: el firmware trunca a 20 chars
            imgs += 1
        except Exception as e:
            print("  imagen %s fallo: %s" % (key, e))

    lines.append("|".join([
        it.get("t", "").replace("|", "/"),
        it.get("a", ""),
        it.get("h", ""),
        str(it.get("ts", 0)),
        it.get("o", "rss"),
        img,
    ]))

dest = os.path.join(OUT, "fixture.txt")
with open(dest, "w", encoding="utf-8", newline="\n") as fh:
    fh.write("\n".join(lines) + "\n")

print("escrito %s (%d items, %d imagenes)" % (dest, len(items), imgs))
