"""Writes assets/models/alpha_mask_reference.glb: the model that checks alpha-tested materials (glTF
alphaMode MASK, review of milestones 1 to 7, 3.1), in the scene and in the shadows.

    python tools/models/make_alpha_mask_model.py

No dependency beyond Python 3 (and make_reference_model.py next to it). The output is
deterministic: running the script twice gives the same bytes.

A panel (one quad), 1 m wide and 1 m high, standing on the ground (y = 0 to 1, z = 0), double-sided,
its texture an 8 x 8 checkerboard of opaque and empty squares inside an opaque frame. With the
alpha test, the empty squares let the ground through and the sun's shadow (and a torch's) is the
same checkerboard; without it, the panel is a plain wall casting a plain shadow. The alpha of the
texture is not 0 or 1 everywhere: the edges of the squares fade over a few texels, so that the
cutoff (0.5) decides where the edge falls, and the material's alpha factor (0.9) takes part.
"""

import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_reference_model import Builder, png  # noqa: E402

SIZE = 128
CELLS = 8


def checker(x, y):
    """RGBA of the panel's texture: a warm frame, opaque and empty squares, soft edges."""
    frame = 8
    if x < frame or y < frame or x >= SIZE - frame or y >= SIZE - frame:
        return (200, 150, 60, 255)
    cell = (SIZE - 2 * frame) / CELLS
    u = (x - frame + 0.5) / cell
    v = (y - frame + 0.5) / cell
    opaque = (int(u) + int(v)) % 2 == 0
    # Distance to the nearest edge of the square, in texels: the alpha ramps over 3 texels.
    edge = min(u - int(u), 1.0 - (u - int(u)), v - int(v), 1.0 - (v - int(v))) * cell
    ramp = min(1.0, edge / 3.0)
    alpha = 0.5 + 0.5 * ramp if opaque else 0.5 - 0.5 * ramp
    return (60, 120, 200, int(round(alpha * 255)))


def main():
    b = Builder()
    image_view = b.view(png(SIZE, SIZE, checker))
    b.gltf["images"].append({"name": "checker", "bufferView": image_view, "mimeType": "image/png"})
    b.gltf["samplers"].append({"magFilter": 9729, "minFilter": 9987})
    b.gltf["textures"].append({"source": 0, "sampler": 0})

    material = b.material("alpha mask", (1.0, 1.0, 1.0, 0.9), texture=0)
    b.gltf["materials"][material].update({"alphaMode": "MASK", "alphaCutoff": 0.5, "doubleSided": True})

    # One quad, seen from both sides (doubleSided): a box would show a second checkerboard through
    # the holes of the first, mirrored, which fills them.
    quad = ([(-0.5, 0.0, 0.0), (0.5, 0.0, 0.0), (0.5, 1.0, 0.0), (-0.5, 1.0, 0.0)], [(0, 0, 1)] * 4,
            [(0, 1), (1, 1), (1, 0), (0, 0)], [0, 1, 2, 0, 2, 3])
    panel_mesh = b.mesh("panel", quad, material)
    panel = b.node({"name": "panel", "mesh": panel_mesh})
    b.gltf["scenes"] = [{"name": "alpha mask reference", "nodes": [panel]}]
    b.gltf["scene"] = 0

    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.normpath(os.path.join(here, "..", "..", "assets", "models", "alpha_mask_reference.glb"))
    data = b.glb()
    with open(out, "wb") as f:
        f.write(data)
    print(f"{out}: {len(data)} bytes")


if __name__ == "__main__":
    main()
