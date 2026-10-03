"""Writes the particle textures of assets/particles/ (milestone 6, part 7).

Generated rather than downloaded: small, reproducible (fixed seeds), with no licence to track.
White shapes on a transparent background (straight alpha): the effects tint them with their color
curves. Run from anywhere:

    python tools/textures/make_particle_textures.py

Needs numpy and Pillow.
"""

from pathlib import Path

import numpy as np
from PIL import Image

OUT = Path(__file__).resolve().parents[2] / "assets" / "particles"
SIZE = 64


def grid(size=SIZE):
    """Coordinates of the pixel centres in [-1, 1]."""
    c = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    return np.meshgrid(c, c)


def value_noise(size, cells, seed):
    """Smooth noise in [0, 1]: random values on a coarse grid, interpolated (smoothstep), tiling."""
    rng = np.random.default_rng(seed)
    values = rng.random((cells, cells))
    t = np.arange(size) / size * cells
    i0 = np.floor(t).astype(int) % cells
    i1 = (i0 + 1) % cells
    f = t - np.floor(t)
    f = f * f * (3 - 2 * f)
    rows0 = values[i0][:, i0] * (1 - f)[None, :] + values[i0][:, i1] * f[None, :]
    rows1 = values[i1][:, i0] * (1 - f)[None, :] + values[i1][:, i1] * f[None, :]
    return rows0 * (1 - f)[:, None] + rows1 * f[:, None]


def fractal(size, seed):
    return (value_noise(size, 4, seed) * 0.55 + value_noise(size, 8, seed + 1) * 0.3 +
            value_noise(size, 16, seed + 2) * 0.15)


def save(name, alpha, rgb=None):
    alpha = np.clip(alpha, 0.0, 1.0)
    if rgb is None:
        rgb = np.ones(alpha.shape + (3,))
    rgba = np.dstack([np.clip(rgb, 0, 1), alpha])
    Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), "RGBA").save(OUT / name, optimize=True)
    print("wrote", OUT / name)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    x, y = grid()
    r = np.sqrt(x * x + y * y)

    # A soft round glow: embers, magic, halos.
    save("glow.png", (1 - np.minimum(r, 1)) ** 2)

    # A spark: a bright core and a thin halo.
    save("spark.png", np.exp(-(r / 0.18) ** 2) + 0.35 * np.exp(-(r / 0.5) ** 2) * (r < 1))

    # A puff of smoke: noise inside a soft disc.
    puff = (1 - np.minimum(r, 1)) ** 1.5 * (0.45 + 0.55 * fractal(SIZE, 7))
    shade = 0.75 + 0.25 * fractal(SIZE, 11)
    save("smoke.png", puff * 1.3, np.dstack([shade] * 3))

    # Flames: four images of a tongue of fire (a drop shape, its tip waving), side by side.
    frames = []
    fx, fy = grid()
    for k in range(4):
        wave = 0.12 * np.sin(fy * 3.0 + k * 1.6)
        width = np.clip(0.55 * (1 - (-fy + 1) / 2) ** 0.6, 0.0, None) + 0.02  # wide at the bottom
        across = np.abs(fx - wave * (1 - fy) * 0.5) / np.maximum(width, 1e-3)
        body = np.clip(1 - across, 0, 1) * np.clip((fy + 1.0) / 0.4, 0, 1) * np.clip((1.0 - fy) / 0.3, 0, 1)
        body *= 0.6 + 0.4 * fractal(SIZE, 20 + k)
        frames.append(np.clip(body * 1.6, 0, 1))  # row 0 is the top: fy = -1 there, the narrow tip
    save("flame.png", np.hstack(frames))

    # A blood drop / splash: a lumpy blob.
    blob = np.clip((0.75 - r + 0.35 * (fractal(SIZE, 31) - 0.5)) * 6.0, 0, 1)
    save("blood.png", blob)

    # A ring, for marks on the ground (a magic circle).
    save("ring.png", np.exp(-((r - 0.78) / 0.07) ** 2) + 0.15 * np.exp(-((r - 0.55) / 0.04) ** 2))


if __name__ == "__main__":
    main()
