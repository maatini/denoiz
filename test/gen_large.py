#!/usr/bin/env python3
"""Generate a larger test image for GPU benchmarking."""
import struct
import zlib
import random
random.seed(42)

def create_noisy_png(w, h, noise, path):
    """Create a noisy RGB PNG."""
    def write_chunk(buf, chunk_type, data):
        chunk = chunk_type + data
        buf.extend(struct.pack(">I", len(data)))
        buf.extend(chunk)
        buf.extend(struct.pack(">I", zlib.crc32(chunk) & 0xFFFFFFFF))

    buf = bytearray()
    buf.extend(b'\x89PNG\r\n\x1a\n')
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    write_chunk(buf, b'IHDR', ihdr)

    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            for c in range(3):
                v = int(max(0, min(1, 0.5 + random.gauss(0, noise))) * 255.0 + 0.5)
                raw.append(v)
    compressed = zlib.compress(bytes(raw))
    write_chunk(buf, b'IDAT', compressed)
    write_chunk(buf, b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(bytes(buf))
    print(f"Created {path}: {w}x{h}x3 ({len(bytes(buf))} bytes)")

create_noisy_png(512, 512, 0.05, "test/noisy_large.png")
create_noisy_png(256, 256, 0.05, "test/noisy_256.png")
