"""Convert a black/white 32x32 PNG into qr_32x32.h format.

Usage (PowerShell):
  python -m pip install pillow
  python .\tools\qr_png_to_header.py .\qr_32x32.png .\qr_32x32.h

Notes:
- Input image should be exactly 32x32 pixels.
- Any non-white pixel is treated as black.
- Output is row-major, MSB-first per byte (8 pixels per byte).
"""

from __future__ import annotations

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as e:
    raise SystemExit("Pillow not installed. Run: python -m pip install pillow") from e


def to_bytes_1bpp(img: Image.Image) -> bytes:
    if img.size != (32, 32):
        raise ValueError(f"Expected 32x32, got {img.size}")

    img = img.convert("L")
    out = bytearray()
    for y in range(32):
        for xb in range(0, 32, 8):
            byte = 0
            for bit in range(8):
                x = xb + bit
                px = img.getpixel((x, y))
                is_black = px < 128
                if is_black:
                    byte |= 1 << (7 - bit)
            out.append(byte)
    return bytes(out)


def write_header(out_path: Path, data: bytes) -> None:
    if len(data) != 128:
        raise ValueError(f"Expected 128 bytes, got {len(data)}")

    lines = []
    lines.append("#pragma once\n")
    lines.append("\n#include <stdint.h>\n")
    lines.append("\n/* 32x32 QR code bitmap (1bpp), row-major, MSB-first, 1=black */\n")
    lines.append("static const uint8_t qr_32x32_bits[128] = {\n")

    for i in range(0, 128, 16):
        chunk = data[i : i + 16]
        hexes = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hexes},\n")

    lines.append("};\n")
    out_path.write_text("".join(lines), encoding="utf-8")


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("Usage: qr_png_to_header.py <in.png> <out.h>")
        return 2

    in_path = Path(argv[1])
    out_path = Path(argv[2])

    img = Image.open(in_path)
    data = to_bytes_1bpp(img)
    write_header(out_path, data)

    print(f"Wrote {out_path} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
