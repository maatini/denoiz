#!/usr/bin/env python3
"""Test harness for denoise. Called by ctest."""

import subprocess
import sys
import os
import tempfile
import struct
import zlib
import math

BINARY = os.environ.get("NLM_BINARY", "./denoise")


def create_png(w, h, channels, pixels):
    """Create a minimal valid PNG file in memory."""
    def write_chunk(buf, chunk_type, data):
        chunk = chunk_type + data
        buf.extend(struct.pack(">I", len(data)))
        buf.extend(chunk)
        buf.extend(struct.pack(">I", zlib.crc32(chunk) & 0xFFFFFFFF))

    buf = bytearray()
    buf.extend(b'\x89PNG\r\n\x1a\n')

    color_type = {1: 0, 3: 2}[channels]
    ihdr = struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0)
    write_chunk(buf, b'IHDR', ihdr)

    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            for c in range(channels):
                v = int(max(0, min(1, pixels[y][x][c])) * 255.0 + 0.5)
                raw.append(v)
    compressed = zlib.compress(bytes(raw))
    write_chunk(buf, b'IDAT', compressed)
    write_chunk(buf, b'IEND', b'')
    return bytes(buf)


def read_png_raw(png_data):
    """Read a minimal 8-bit PNG. Returns (w, h, channels, pixels)."""
    import io
    f = io.BytesIO(png_data)
    sig = f.read(8)
    assert sig == b'\x89PNG\r\n\x1a\n', "Not a PNG"

    w = h = channels = 0
    idat_chunks = []
    while True:
        length = struct.unpack(">I", f.read(4))[0]
        chunk_type = f.read(4)
        data = f.read(length)
        crc = f.read(4)
        if chunk_type == b'IHDR':
            w, h, bitd, color_type = struct.unpack(">IIBB", data[:10])
            assert bitd == 8, f"Expected 8-bit, got {bitd}"
            channels = {0: 1, 2: 3}[color_type]
        elif chunk_type == b'IDAT':
            idat_chunks.append(data)
        elif chunk_type == b'IEND':
            break

    raw = zlib.decompress(b''.join(idat_chunks))
    pixels = decode_png_rows(raw, w, h, channels)
    return w, h, channels, pixels


def decode_png_rows(raw, w, h, channels):
    """Decode PNG raw rows with filter byte handling."""
    stride = 1 + w * channels
    prev_row = bytearray(w * channels)
    pixels = []
    for y in range(h):
        row_raw = raw[y * stride : (y+1) * stride]
        ft = row_raw[0]
        filtered = bytearray(row_raw[1:])
        recon = [0] * (w * channels)
        for i in range(w * channels):
            left = recon[i - channels] if i >= channels else 0
            up = prev_row[i]
            up_left = prev_row[i - channels] if i >= channels else 0
            if ft == 0:
                val = filtered[i]
            elif ft == 1:
                val = filtered[i] + left
            elif ft == 2:
                val = filtered[i] + up
            elif ft == 3:
                val = filtered[i] + (left + up) // 2
            elif ft == 4:
                # Paeth
                p = left + up - up_left
                pa = abs(p - left)
                pb = abs(p - up)
                pc = abs(p - up_left)
                if pa <= pb and pa <= pc:
                    pr = left
                elif pb <= pc:
                    pr = up
                else:
                    pr = up_left
                val = filtered[i] + pr
            else:
                raise ValueError(f"Unknown filter type: {ft}")
            recon[i] = val & 0xFF
        prev_row = bytearray(recon)
        px_row = []
        for x in range(w):
            off = x * channels
            px_row.append(tuple(recon[off + c] / 255.0 for c in range(channels)))
        pixels.append(px_row)
    return pixels

def compute_psnr(pixels_a, pixels_b):
    h = len(pixels_a)
    w = len(pixels_a[0])
    mse = 0.0
    n = 0
    for y in range(h):
        for x in range(w):
            for c in range(len(pixels_a[y][x])):
                diff = pixels_a[y][x][c] - pixels_b[y][x][c]
                mse += diff * diff
                n += 1
    mse /= n
    if mse < 1e-10:
        return 100.0
    return 10.0 * math.log10(1.0 / mse)


def create_noisy_pair(w, h, noise_sigma):
    """Create clean + noisy image pair. Returns (clean_png, noisy_png) as bytes."""
    import random
    random.seed(42)
    clean = [[(0.5, 0.5, 0.5) for _ in range(w)] for _ in range(h)]
    noisy = [[
        (max(0, min(1, 0.5 + random.gauss(0, noise_sigma))),
         max(0, min(1, 0.5 + random.gauss(0, noise_sigma))),
         max(0, min(1, 0.5 + random.gauss(0, noise_sigma))))
        for _ in range(w)
    ] for _ in range(h)]
    return create_png(w, h, 3, clean), create_png(w, h, 3, noisy)


def run_nlm(input_path, output_path, extra_args=None):
    args = [BINARY, input_path, output_path]
    if extra_args:
        args.extend(extra_args)
    proc = subprocess.run(args, capture_output=True, text=True)
    return proc.returncode, proc.stdout, proc.stderr


def test_ground_truth(tmpdir):
    clean_png, noisy_png = create_noisy_pair(32, 32, 0.1)
    clean_path = os.path.join(tmpdir, "clean.png")
    noisy_path = os.path.join(tmpdir, "noisy.png")
    output_path = os.path.join(tmpdir, "denoised.png")

    with open(clean_path, 'wb') as f:
        f.write(clean_png)
    with open(noisy_path, 'wb') as f:
        f.write(noisy_png)

    rc, stdout, stderr = run_nlm(noisy_path, output_path,
                                  ["--patch-size", "7", "--search-window", "21", "--h", "0.1"])
    assert rc == 0, f"denoise failed: {stderr}"

    _, _, _, clean_px = read_png_raw(clean_png)
    _, _, _, noisy_px = read_png_raw(noisy_png)

    with open(output_path, 'rb') as f:
        _, _, _, denoised_px = read_png_raw(f.read())

    psnr_noisy = compute_psnr(clean_px, noisy_px)
    psnr_denoised = compute_psnr(clean_px, denoised_px)

    print(f"  PSNR noisy={psnr_noisy:.2f} dB, denoised={psnr_denoised:.2f} dB")
    assert psnr_denoised > psnr_noisy + 1.0, \
        f"Denoising did not improve PSNR: {psnr_noisy:.2f} -> {psnr_denoised:.2f}"
    assert os.path.getsize(output_path) > 0, "Output file empty"
    return True


def test_grayscale(tmpdir):
    w, h = 32, 32
    pixels = [[(0.5,) for _ in range(w)] for _ in range(h)]
    png_data = create_png(w, h, 1, pixels)

    input_path = os.path.join(tmpdir, "gray.png")
    output_path = os.path.join(tmpdir, "gray_denoised.png")
    with open(input_path, 'wb') as f:
        f.write(png_data)

    rc, stdout, stderr = run_nlm(input_path, output_path,
                                  ["--patch-size", "7", "--search-window", "21", "--h", "0.1"])
    assert rc == 0, f"Grayscale denoise failed: {stderr}"
    assert os.path.getsize(output_path) > 0, "Output empty for grayscale"

    with open(output_path, 'rb') as f:
        ow, oh, oc, opx = read_png_raw(f.read())
    assert ow == w and oh == h and oc == 1, \
        f"Grayscale output dims wrong: {ow}x{oh}x{oc} != {w}x{h}x1"

    print(f"  Grayscale {w}x{h} -> {ow}x{oh}x{oc} ok")
    return True


def test_trivial_params(tmpdir):
    w, h = 32, 32
    pixels = [[(0.5, 0.5, 0.5) for _ in range(w)] for _ in range(h)]
    png_data = create_png(w, h, 3, pixels)

    input_path = os.path.join(tmpdir, "trivial.png")
    output_path = os.path.join(tmpdir, "trivial_out.png")
    with open(input_path, 'wb') as f:
        f.write(png_data)

    rc, stdout, stderr = run_nlm(input_path, output_path,
                                  ["--patch-size", "1", "--search-window", "1"])
    assert rc == 0, f"Trivial-params failed: {stderr}"

    with open(output_path, 'rb') as f:
        w2, h2, c2, px = read_png_raw(f.read())
    assert w2 == w and h2 == h, f"Output dims mismatch: {w2}x{h2} != {w}x{h}"

    print(f"  Trivial params (1,1): output {w2}x{h2}x{c2} ok")
    return True


def test_cli_errors():
    proc = subprocess.run([BINARY], capture_output=True, text=True)
    assert proc.returncode != 0, f"No-args: expected non-zero, got {proc.returncode}"

    proc = subprocess.run([BINARY, "/nonexistent.png", "/tmp/out.png"],
                          capture_output=True, text=True)
    assert proc.returncode != 0, f"Bad input: expected non-zero"

    proc = subprocess.run([BINARY, "/tmp/out.png", "/tmp/out.png",
                           "--patch-size", "abc"],
                          capture_output=True, text=True)
    assert proc.returncode != 0, f"Bad patch-size: expected non-zero"

    print("  CLI errors: ok")
    return True


def main():
    tests = [
        ("ground truth", test_ground_truth),
        ("grayscale", test_grayscale),
        ("trivial params", test_trivial_params),
        ("CLI errors", test_cli_errors),
    ]

    failed = 0
    with tempfile.TemporaryDirectory() as tmpdir:
        for name, fn in tests:
            try:
                print(f"TEST: {name}...")
                if fn == test_cli_errors:
                    fn()
                else:
                    fn(tmpdir)
                print(f"  PASS")
            except Exception as e:
                print(f"  FAIL: {e}")
                failed += 1

    if failed:
        print(f"\n{failed}/{len(tests)} FAILED")
        sys.exit(1)
    else:
        print(f"\n{len(tests)}/{len(tests)} PASSED")


if __name__ == "__main__":
    main()
