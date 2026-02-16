#!/usr/bin/env python3
"""
PNG to RGB565 converter for ESP32/LovyanGFX.

Converts a PNG image (with alpha) to a C header file containing
an RGB565 pixel array suitable for pushImage() with transparency.

Usage:
    python png_to_rgb565.py <input.png> <output.h> <width> <height> <array_name>

Example:
    python png_to_rgb565.py isotipo.png lemon_isotipo_64.h 64 64 lemon_isotipo_64
"""

import sys
from PIL import Image


def png_to_rgb565(input_path, output_path, target_w, target_h, array_name):
    img = Image.open(input_path).convert("RGBA")
    img = img.resize((target_w, target_h), Image.LANCZOS)

    # Composite onto black background (our BG_BASE = 0x0000)
    bg = Image.new("RGBA", (target_w, target_h), (0, 0, 0, 255))
    # Keep track of alpha for transparency mask
    pixels_rgba = list(img.getdata())
    bg.paste(img, (0, 0), img)
    pixels_rgb = list(bg.convert("RGB").getdata())

    rgb565_data = []
    for i, (r, g, b) in enumerate(pixels_rgb):
        alpha = pixels_rgba[i][3]
        if alpha < 128:
            # Transparent pixel -> use 0x0000 as transparent key
            rgb565_data.append(0x0000)
        else:
            val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            # Avoid 0x0000 for non-transparent pixels (would be treated as transparent)
            if val == 0x0000:
                val = 0x0001  # Near-black but not transparent
            rgb565_data.append(val)

    # Write C header
    with open(output_path, "w") as f:
        f.write(f"#pragma once\n")
        f.write(f"#include <cstdint>\n")
        f.write(f"#include <pgmspace.h>\n\n")
        f.write(f"// Auto-generated from {input_path.split('/')[-1].split(chr(92))[-1]}\n")
        f.write(f"// Size: {target_w}x{target_h}, Format: RGB565, Transparent color: 0x0000\n\n")
        f.write(f"static const uint16_t {array_name}[{target_w * target_h}] PROGMEM = {{\n")

        for row in range(target_h):
            f.write("    ")
            for col in range(target_w):
                idx = row * target_w + col
                f.write(f"0x{rgb565_data[idx]:04X}")
                if idx < len(rgb565_data) - 1:
                    f.write(",")
            f.write("\n")

        f.write("};\n")

    total_bytes = target_w * target_h * 2
    print(f"Generated {output_path}: {target_w}x{target_h} = {total_bytes} bytes")


if __name__ == "__main__":
    if len(sys.argv) != 6:
        print(f"Usage: {sys.argv[0]} <input.png> <output.h> <width> <height> <array_name>")
        sys.exit(1)

    png_to_rgb565(sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5])
