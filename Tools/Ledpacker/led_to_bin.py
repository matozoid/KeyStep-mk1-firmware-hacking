#!/usr/bin/env python3
"""
led_to_bin.py

Unpack a KeyStep Mk1 .led update file into a flat binary image for analysis.

Usage:
    python3 led_to_bin.py firmware.led out.bin

The .led is ASCII hex. Raw layout: N frames of
    18-byte header (struct >7sH7sH, field 1 = flash page, address = 0x08000000 + page<<8)
    0x800 data bytes
    12-byte footer (last 2 bytes = little-endian checksum)
followed by an optional 12-byte tail. Frames are written at their header address,
so the output starts at the first frame's address (printed on stdout).
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

HEADER, DATA, FOOTER = 18, 0x800, 12
FRAME = HEADER + DATA + FOOTER


def checksum_ok(header: bytes, data: bytes, footer: bytes) -> bool:
    total = sum(header[2:]) + sum(data) + sum(footer[:-2])
    expected = (0x10000 - (total & 0xFFFF)) & 0xFFFF
    return expected == struct.unpack_from("<H", footer, len(footer) - 2)[0]


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    text = re.sub(rb"\s+", b"", Path(sys.argv[1]).read_bytes())
    raw = bytes.fromhex(text.decode("ascii"))

    count = len(raw) // FRAME
    tail = raw[count * FRAME:]
    if len(tail) not in (0, 12):
        raise SystemExit(f"unexpected size: {len(raw)} bytes")

    image: dict[int, bytes] = {}
    bad = 0
    for i in range(count):
        f = raw[i * FRAME:(i + 1) * FRAME]
        header, data, footer = f[:HEADER], f[HEADER:HEADER + DATA], f[HEADER + DATA:]
        _, page, _, _ = struct.unpack(">7sH7sH", header)
        addr = 0x08000000 + (page << 8)
        if i == count - 1 and tail:
            footer = footer + tail
        if not checksum_ok(header, data, footer):
            bad += 1
            print(f"frame {i}: checksum mismatch @ 0x{addr:08X}")
        image[addr] = data

    base = min(image)
    end = max(image) + DATA
    out = bytearray(b"\xFF" * (end - base))
    for addr, data in image.items():
        out[addr - base:addr - base + DATA] = data

    Path(sys.argv[2]).write_bytes(out)
    sp, pc = struct.unpack_from("<II", out, 0)
    print(f"frames: {count}, bad checksums: {bad}, tail: {len(tail)} bytes")
    print(f"base: 0x{base:08X}  end: 0x{end:08X}  size: {len(out)}")
    print(f"initial SP: 0x{sp:08X}  reset: 0x{pc:08X}")


if __name__ == "__main__":
    main()
