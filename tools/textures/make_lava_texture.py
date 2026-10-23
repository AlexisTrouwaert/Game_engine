"""Writes assets/textures/test/lave.png and lave_emission.png, the textures of the material file
assets/materials/test/lave.material.json (milestone 9 bis, part 2: the sandbox's "Materials" test):
cooled dark crust with glowing cracks, tileable (built from periodic waves), 128 x 128.

    python tools/textures/make_lava_texture.py

No dependency beyond Python 3 (the PNG writer of tools/models/make_reference_model.py); the output
is the same bytes on every run.
"""

import math
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "models"))
from make_reference_model import png  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "textures", "test")
SIZE = 128
TAU = 2.0 * math.pi


def heat(x, y):
    """0 (crust) to 1 (molten): the sum of periodic waves, sharpened into cracks."""
    u, v = x / SIZE, y / SIZE
    n = (math.sin(TAU * (2 * u + 1 * v)) + math.sin(TAU * (3 * u - 2 * v) + 1.3) + 0.6 * math.sin(TAU * (5 * u + 4 * v) + 2.1)
         + 0.4 * math.sin(TAU * (7 * u - 6 * v) + 0.4))
    crack = 1.0 - min(1.0, abs(n) / 0.55)
    return crack ** 2.2


def color(x, y):
    h = heat(x, y)
    crust = (40, 28, 24)
    hot = (255, 150, 40)
    return tuple(int(round(c + (w - c) * h)) for c, w in zip(crust, hot)) + (255,)


def emission(x, y):
    h = heat(x, y)
    return (int(round(255 * h)), int(round(140 * h * h)), int(round(30 * h ** 3)), 255)


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, function in (("lave.png", color), ("lave_emission.png", emission)):
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(png(SIZE, SIZE, function))
    print("written:", OUT)


if __name__ == "__main__":
    main()
