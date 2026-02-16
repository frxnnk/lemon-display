# Lemon Brand Color Guidelines

Extracted from official Lemon brand PDF (page 20).

## Primary Palette

| Name       | HEX       | RGB565   | Usage                        |
|------------|-----------|----------|------------------------------|
| BLACK      | #000000   | 0x0000   | Background (50% distribution)|
| GREENT     | #00F068   | 0x078D   | Primary accent (25%)         |
| EVERGREENT | #00A849   | 0x0549   | Secondary green              |
| NEBULA     | #806CF2   | 0x837E   | Purple accent (10%)          |
| SOLAR      | #FF8700   | 0xFC20   | Orange accent (5%)           |
| STARLIGHT  | #E7E7E7   | 0xE73C   | Light text (5%)              |
| MOON       | #5B5B5B   | 0x5ACB   | Muted surfaces (5%)          |

## Distribution Rules

- Black: 50% of total palette usage
- Greent: 25% — primary brand accent, CTAs, positive states
- Nebula: 10% — secondary accent, SOL coin color
- Solar: 5% — BTC accent, highlights
- Starlight: 5% — light text on dark backgrounds
- Moon: 5% — muted/disabled surfaces

## Combination Rules

- Primary backgrounds: BLACK
- Card backgrounds: dark green-tinted (#080C08)
- Positive states: GREENT
- Negative states: #FF1A3B (vivid red, not in brand palette but standard for markets)
- BTC accent: SOLAR
- ETH accent: #4A90D9 (Ethereum blue, not Lemon brand)
- SOL accent: NEBULA

## RGB565 Conversion Notes

RGB565 packs 16-bit color: 5 bits red, 6 bits green, 5 bits blue.

Formula: `((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)`

| Color      | R   | G   | B   | R5  | G6  | B5  | RGB565 |
|------------|-----|-----|-----|-----|-----|-----|--------|
| GREENT     | 0   | 240 | 104 | 0   | 60  | 13  | 0x078D |
| EVERGREENT | 0   | 168 | 73  | 0   | 42  | 9   | 0x0549 |
| NEBULA     | 128 | 108 | 242 | 16  | 27  | 30  | 0x837E |
| SOLAR      | 255 | 135 | 0   | 31  | 33  | 0   | 0xFC20 |
| STARLIGHT  | 231 | 231 | 231 | 28  | 57  | 28  | 0xE73C |
| MOON       | 91  | 91  | 91  | 11  | 22  | 11  | 0x5ACB |
