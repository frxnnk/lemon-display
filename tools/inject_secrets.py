"""Inyecta el endpoint y el token del proxy en tiempo de compilacion.

El token no se versiona: sale de output/FEED_TOKEN.txt, que esta en .gitignore.
Sin ese archivo el build sigue, pero avisa y queda sin autenticacion.
"""
import os

Import("env")  # noqa: F821  (lo provee PlatformIO)

ROOT = env.subst("$PROJECT_DIR")  # noqa: F821
TOKEN_FILE = os.path.join(ROOT, "output", "FEED_TOKEN.txt")
ENDPOINT = "https://feed.ferced.com/v1/feed?n=20"

token = ""
if os.path.exists(TOKEN_FILE):
    with open(TOKEN_FILE, "r", encoding="utf-8") as fh:
        token = fh.read().strip()

if token:
    print("[secrets] token cargado desde output/FEED_TOKEN.txt (%d chars)" % len(token))
else:
    print("[secrets] AVISO: falta output/FEED_TOKEN.txt; el firmware ira sin token")

env.Append(CPPDEFINES=[  # noqa: F821
    ("FEED_ENDPOINT", env.StringifyMacro(ENDPOINT)),  # noqa: F821
    ("FEED_TOKEN", env.StringifyMacro(token)),        # noqa: F821
])
