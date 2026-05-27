#!/usr/bin/env python3
"""Generate test images for NLM denoising benchmarks."""
import sys
import numpy as np
from PIL import Image

def generate_noisy_ramp(w=256, h=256, noise_sigma=0.05):
    """Generate a simple gradient with additive Gaussian noise."""
    x = np.linspace(0, 1, w, dtype=np.float32)
    y = np.linspace(0, 1, h, dtype=np.float32)
    ramp = np.outer(y, x)
    img = np.stack([ramp, ramp, ramp], axis=-1)
    img += np.random.randn(h, w, 3).astype(np.float32) * noise_sigma
    img = np.clip(img, 0, 1)
    return (img * 255).astype(np.uint8)

def generate_checkerboard(w=256, h=256, noise_sigma=0.1):
    """Checkerboard with noise."""
    x = np.arange(w, dtype=np.float32)
    y = np.arange(h, dtype=np.float32)
    checker = ((x[:, None] // 32 + y[None, :] // 32) % 2).astype(np.float32)
    img = np.stack([checker] * 3, axis=-1)
    img += np.random.randn(h, w, 3).astype(np.float32) * noise_sigma
    img = np.clip(img, 0, 1)
    return (img * 255).astype(np.uint8)

def main():
    try:
        Image.fromarray(generate_noisy_ramp()).save("test/noisy_ramp.png")
        Image.fromarray(generate_checkerboard()).save("test/noisy_checker.png")
        print("Test images generated: test/noisy_ramp.png, test/noisy_checker.png")
    except ImportError:
        # Fallback: create minimal PNG using raw bytes
        print("PIL not available, skipping test image generation", file=sys.stderr)

if __name__ == "__main__":
    main()
