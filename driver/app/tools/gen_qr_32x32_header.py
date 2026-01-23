"""Generate a QR header (raw module matrix) for Zephyr simple_ui.

This script generates a QR code for the provided text/URL and emits the raw QR
module matrix (NxN) as a 1bpp, row-major, MSB-first packed byte array.

Why matrix (not a fixed 32x32 bitmap):
- The OLED is 88x48. A fixed 32x32 bitmap cannot be cleanly enlarged to near
    the screen height without introducing uneven scaling.
- Rendering from the module matrix lets the firmware scale each module by an
    integer factor (nearest-neighbor) for a crisp, scanner-friendly result.

Usage (PowerShell):
        python -m pip install segno
        python ./tools/gen_qr_32x32_header.py "http://baidu.com" ./qr_32x32.h

Conventions:
- Stored data represents QR modules only (no quiet zone).
- 1 = black module
- 0 = white module
- Packed row-major, MSB-first, stride = ceil(N/8) bytes.
"""

from __future__ import annotations

import sys
from pathlib import Path

try:
    import segno
except ImportError as e:
    raise SystemExit("segno not installed. Run: python -m pip install segno") from e


def pack_matrix_1bpp_msb(matrix: list[list[int]]) -> tuple[int, bytes]:
    n = len(matrix)
    if n <= 0 or any(len(row) != n for row in matrix):
        raise ValueError("Expected square matrix NxN")

    stride = (n + 7) // 8
    out = bytearray()
    for y in range(n):
        row = matrix[y]
        for xb in range(0, n, 8):
            byte = 0
            for bit in range(8):
                x = xb + bit
                if x >= n:
                    continue
                if row[x]:
                    byte |= 1 << (7 - bit)
            out.append(byte)
    return n, bytes(out)


def gen_qr_matrix_bits(data: str) -> tuple[int, bytes]:
    # Use low error correction to keep the symbol small.
    qr = segno.make(data, error="L", micro=False)
    matrix = qr.matrix  # rows of 0/1 (no quiet zone)
    return pack_matrix_1bpp_msb(matrix)


def write_header(out_path: Path, n: int, data: bytes, content: str) -> None:
    stride = (n + 7) // 8
    expected = n * stride
    if len(data) != expected:
        raise ValueError(f"Expected {expected} bytes for {n}x{n}, got {len(data)}")

    lines: list[str] = []
    lines.append("#pragma once\n\n")
    lines.append("#include <stdint.h>\n\n")
    lines.append("/*\n")
    lines.append(" * QR module matrix (1bpp).\n")
    lines.append(" * - Layout: row-major, MSB-first, stride=ceil(N/8) bytes\n")
    lines.append(" * - Value:  1 = black module\n")
    lines.append(" * - Content: ")
    lines.append(content.replace("*/", "*\\/") + "\n")
    lines.append(" */\n\n")
    lines.append(f"#define QR_MODULES {n}\n")
    lines.append(f"#define QR_STRIDE_BYTES {stride}\n")
    lines.append(f"#define QR_BITS_LEN {len(data)}\n\n")
    lines.append("static const uint8_t qr_module_bits[QR_BITS_LEN] = {\n")

    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        hexes = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hexes},\n")

    lines.append("};\n")
    out_path.write_text("".join(lines), encoding="utf-8")


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print('Usage: gen_qr_32x32_header.py <text-or-url> <out.h>')
        return 2

    content = argv[1]
    out_path = Path(argv[2])

    n, data = gen_qr_matrix_bits(content)
    write_header(out_path, n, data, content)
    print(f"Wrote {out_path} ({len(data)} bytes) modules={n} for: {content}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
