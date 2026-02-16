# Tools

Conversion utilities for generating embedded assets (font headers and bitmap headers) from source files.

## png_to_rgb565.py

Converts a PNG image (with alpha transparency) to a C header containing an RGB565 pixel array for use with LovyanGFX `pushImage()`.

**Requirements:** `pip install Pillow`

**Usage:**

```bash
python png_to_rgb565.py <input.png> <output.h> <width> <height> <array_name>
```

**Example:**

```bash
python tools/png_to_rgb565.py tools/isotipo_v2.png src/data/lemon_isotipo_64.h 64 64 lemon_isotipo_64
```

Transparent pixels (alpha < 128) are encoded as `0x0000`. Non-transparent pure-black pixels are shifted to `0x0001` to avoid collision with the transparency key.

## ttf_to_gfx.py

Converts a TTF font file to an Adafruit GFX / LovyanGFX compatible C header with 1-bit packed bitmaps.

**Requirements:** `pip install freetype-py`

**Usage:**

```bash
python ttf_to_gfx.py <input.ttf> <size_pt> <output.h> <font_name>
```

**Example:**

```bash
python tools/ttf_to_gfx.py tools/fonts/Satoshi_Complete/Fonts/OTF/Satoshi-Regular.otf 12 src/data/Satoshi12.h Satoshi12
```

Renders at 141 DPI (Adafruit GFX standard). Character range: 0x20–0x7E (printable ASCII).

## Brand Assets

Source PNG files used to generate embedded bitmaps:

| File | Description | Generated Header |
|------|-------------|-----------------|
| `isotipo_v2.png` | Lemon isotipo (icon only) | `lemon_isotipo_28.h`, `lemon_isotipo_64.h` |
| `isotipo_cropped.png` | Cropped isotipo variant | — |
| `isotipo_raw.png` | Original unprocessed isotipo | — |
| `lemon_imagotipo_hdr.png` | Imagotipo for header bar | `lemon_imagotipo_122.h` |
| `lemon_imagotipo_load.png` | Imagotipo for loading screen | `lemon_imagotipo_244.h` |
| `brand_colors_p*.png` | Brand guideline page screenshots | — (reference only) |

## Fonts

The project uses **Satoshi** by Indian Type Foundry as the primary typeface (Lemon's brand secondary font).

- Source: `fonts/satoshi.zip`
- License: [Indian Type Foundry EULA](https://www.fontshare.com/fonts/satoshi) — free for personal and commercial use
- The `fonts/Satoshi_Complete/` directory contains the extracted zip (not committed to git — extract locally if needed)

Generated font headers in `src/data/`:

| Header | Font | Size |
|--------|------|------|
| `Satoshi9.h` | Satoshi Regular | 9pt |
| `Satoshi12.h` | Satoshi Regular | 12pt |
| `SatoshiMedium18.h` | Satoshi Medium | 18pt |
| `SatoshiBold24.h` | Satoshi Bold | 24pt |
| `SatoshiBold40.h` | Satoshi Bold | 40pt |
