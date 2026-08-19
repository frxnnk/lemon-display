#!/usr/bin/env python3
"""Lemon Box flash helper. Run via: python -m skills.lemon-box-flasher.scripts.flash"""

import hashlib
import os
import subprocess
import sys
import time


def esptool(*args):
    cmd = [sys.executable, "-m", "esptool"] + list(args)
    r = subprocess.run(cmd)
    if r.returncode != 0:
        raise RuntimeError(f"esptool failed: {' '.join(args[:4])}")


def detect_port():
    import serial.tools.list_ports as lp

    for p in sorted(lp.comports(), key=lambda x: x.device):
        if p.hwid and "BTHENUM" in p.hwid.upper():
            continue
        if p.vid == 0x303A:
            return p.device
        if p.vid is not None:
            return p.device
    return None


def wait_for_port(timeout=20):
    import serial.tools.list_ports as lp

    deadline = time.time() + timeout
    while time.time() < deadline:
        for p in lp.comports():
            if p.vid is not None and (p.hwid and "BTHENUM" not in p.hwid.upper()):
                return p.device
        time.sleep(0.25)
    return None


def check_boot_loop(port, duration=12):
    import serial.tools.list_ports as lp

    end = time.time() + duration
    prev = None
    loops = 0
    while time.time() < end:
        ports = {p.device for p in lp.comports()}
        if ports != prev:
            loops += 1
            prev = ports
        time.sleep(0.25)
    return loops > 2


def find_bins(worktree=None, env="matouch_esp32s3_40", firmware_override=None):
    script_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    repo = os.path.normpath(os.path.join(script_dir, "..", ".."))
    if worktree:
        repo = worktree
    build = os.path.join(repo, ".pio", "build", env)

    firmware_path = firmware_override or os.path.join(build, "firmware.bin")
    bins = {
        "bootloader": os.path.join(build, "bootloader.bin"),
        "partitions": os.path.join(build, "partitions.bin"),
        "firmware": firmware_path,
        "spiffs": os.path.join(build, "spiffs.bin"),
    }
    boot_app0 = os.path.expanduser(
        os.path.join(
            "~",
            ".platformio",
            "packages",
            "framework-arduinoespressif32",
            "tools",
            "partitions",
            "boot_app0.bin",
        )
    )
    if os.path.isfile(boot_app0):
        bins["boot_app0"] = boot_app0

    missing = [k for k, v in bins.items() if not os.path.isfile(v)]
    if missing:
        print(f"Missing bins: {missing}")
        print(f"Build dir: {build}")
        return None
    return bins


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def flash_normal(port, bins):
    print(f"Normal flash -> {port}")
    esptool(
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        "921600",
        "--before",
        "default-reset",
        "--after",
        "hard-reset",
        "write-flash",
        "--flash-mode",
        "keep",
        "--flash-size",
        "keep",
        "0x10000",
        bins["firmware"],
    )


def flash_recovery(port, bins):
    print(f"Recovery flash -> {port}")
    print("Step 1: Erase...")
    esptool(
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        "921600",
        "--before",
        "default-reset",
        "--after",
        "no-reset",
        "erase-flash",
    )

    print("Step 2: Write all partitions...")
    esptool(
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        "921600",
        "--before",
        "no-reset",
        "--after",
        "no-reset",
        "write-flash",
        "--flash-mode",
        "keep",
        "--flash-size",
        "keep",
        "0x0",
        bins["bootloader"],
        "0x8000",
        bins["partitions"],
        "0xe000",
        bins["boot_app0"],
        "0x10000",
        bins["firmware"],
        "0xc90000",
        bins["spiffs"],
    )

    print("Step 3: Verify...")
    esptool(
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        "921600",
        "--before",
        "no-reset",
        "--after",
        "hard-reset",
        "verify-flash",
        "0x0",
        bins["bootloader"],
        "0x8000",
        bins["partitions"],
        "0xe000",
        bins["boot_app0"],
        "0x10000",
        bins["firmware"],
        "0xc90000",
        bins["spiffs"],
    )


def main():
    recovery = "--recovery" in sys.argv or "-r" in sys.argv
    worktree = None
    env = "matouch_esp32s3_40"
    firmware_override = None
    for a in sys.argv:
        if a.startswith("--worktree="):
            worktree = a.split("=", 1)[1]
        elif a.startswith("--env="):
            env = a.split("=", 1)[1]
        elif a.startswith("--firmware="):
            firmware_override = a.split("=", 1)[1]

    bins = find_bins(worktree, env=env, firmware_override=firmware_override)
    if not bins:
        print("ERROR: Could not find build artifacts. Run build first.")
        sys.exit(1)

    print(f"Firmware MD5: {md5(bins['firmware'])[:16]}...")
    print(f"Bootloader MD5: {md5(bins['bootloader'])[:16]}...")

    port = detect_port()
    if not port:
        print("No device detected. Enter bootloader (BOOT + RESET, release BOOT)")
        port = wait_for_port(30)
    if not port:
        print("ERROR: No device found after 30s")
        sys.exit(1)

    print(f"Device: {port}")

    if recovery:
        flash_recovery(port, bins)
    else:
        flash_normal(port, bins)

    print("Waiting for boot...")
    time.sleep(3)
    if check_boot_loop(port, 10):
        print("WARNING: Boot loop detected! Try recovery flash with --recovery")
    else:
        print("OK: Device stable, firmware running")


if __name__ == "__main__":
    main()
