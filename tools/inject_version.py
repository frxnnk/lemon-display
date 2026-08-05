"""Inyecta version, commit y fecha de compilacion.

El commit lleva sufijo -dirty si el arbol tiene cambios sin commitear: sin eso
no hay forma de saber que lo flasheado no corresponde a ningun commit, que es
el caso mas enganoso.
"""
import subprocess
from datetime import datetime

VERSION = "1.1.1"


def _git(*args):
    return subprocess.check_output(("git",) + args, text=True).strip()


def commit_id():
    try:
        short = _git("rev-parse", "--short", "HEAD")
        sucio = _git("status", "--porcelain")
    except Exception:
        return "desconocido"
    return short + "-dirty" if sucio else short


def build_date():
    return datetime.now().strftime("%Y-%m-%d %H:%M")


if __name__ != "__main__":
    try:
        Import("env")  # noqa: F821  (lo provee PlatformIO)
    except NameError:
        pass
    else:
        env.Append(CPPDEFINES=[  # noqa: F821
            ("FERCED_VERSION", env.StringifyMacro(VERSION)),        # noqa: F821
            ("FERCED_COMMIT", env.StringifyMacro(commit_id())),     # noqa: F821
            ("FERCED_BUILD_DATE", env.StringifyMacro(build_date())),# noqa: F821
        ])
        print("[version] %s %s (%s)" % (VERSION, commit_id(), build_date()))
