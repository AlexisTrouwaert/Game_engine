"""The engine's blockout pieces (milestone 9, part 8): plain boxes to lay out a level before its art.

    python tools/models/make_blockout.py

Writes, under assets/:
- models/blockout/block_light.glb and block_dark.glb: a box of 1 x 1 x 1 m standing on its base
  (x and z from -0.5 to 0.5, y from 0 to 1), light grey or dark grey, flat normals, one unit of
  texture per metre;
- prefabs/blockout/*.prefab.json: the pieces a migrated map or a test level uses, each a scaled box
  with its collision box (StaticCollision): floor (1 x 1 m slab, its top at y 0), wall (1 x 2 x 1 m),
  pillar (0.6 x 3 x 0.6 m), crate (0.8 m, an obstacle), step (1 x 0.25 x 1 m).
The .meta files are made by the engine the first time it runs on the assets.
"""

import json
import os
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def box_glb(color):
    faces = [  # normal, then the four corners counter-clockwise seen from outside
        ((0, 1, 0), [(-.5, 1, -.5), (-.5, 1, .5), (.5, 1, .5), (.5, 1, -.5)]),
        ((0, -1, 0), [(-.5, 0, -.5), (.5, 0, -.5), (.5, 0, .5), (-.5, 0, .5)]),
        ((1, 0, 0), [(.5, 0, -.5), (.5, 1, -.5), (.5, 1, .5), (.5, 0, .5)]),
        ((-1, 0, 0), [(-.5, 0, -.5), (-.5, 0, .5), (-.5, 1, .5), (-.5, 1, -.5)]),
        ((0, 0, 1), [(-.5, 0, .5), (.5, 0, .5), (.5, 1, .5), (-.5, 1, .5)]),
        ((0, 0, -1), [(-.5, 0, -.5), (-.5, 1, -.5), (.5, 1, -.5), (.5, 0, -.5)]),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for normal, corners in faces:
        base = len(positions)
        for p in corners:
            positions.append(p)
            normals.append(normal)
            # One unit per metre, from the two axes the face spans.
            axes = [i for i in range(3) if normal[i] == 0]
            uvs.append((p[axes[0]] + 0.5, p[axes[1]]))
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    blob = b"".join(struct.pack("<3f", *p) for p in positions)
    normal_offset = len(blob)
    blob += b"".join(struct.pack("<3f", *n) for n in normals)
    uv_offset = len(blob)
    blob += b"".join(struct.pack("<2f", *t) for t in uvs)
    index_offset = len(blob)
    blob += b"".join(struct.pack("<H", i) for i in indices)
    while len(blob) % 4:
        blob += b"\0"
    gltf = {
        "asset": {"version": "2.0", "generator": "moteur tools/models/make_blockout.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "block", "mesh": 0}],
        "meshes": [{"name": "block", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3,
                                                       "material": 0}]}],
        "materials": [{"name": "blockout", "pbrMetallicRoughness": {"baseColorFactor": color, "metallicFactor": 0.0,
                                                                   "roughnessFactor": 0.85}}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": normal_offset, "target": 34962},
            {"buffer": 0, "byteOffset": normal_offset, "byteLength": uv_offset - normal_offset, "target": 34962},
            {"buffer": 0, "byteOffset": uv_offset, "byteLength": index_offset - uv_offset, "target": 34962},
            {"buffer": 0, "byteOffset": index_offset, "byteLength": len(indices) * 2, "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3", "min": [-.5, 0, -.5], "max": [.5, 1, .5]},
            {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
    }
    text = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(text) % 4:
        text += b" "
    body = struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(blob), 0x004E4942) + blob
    return struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body


def prefab(model, scale, offset, top, sides):
    """A box of `scale` metres whose base sits at `offset` (its root's place), with its collision."""
    return {
        "version": 1,
        "entities": [{
            "id": 1,
            "name": "block",
            "components": {
                "transform": {"position": offset, "scale": scale},
                "model": {"model": model},
                "static_collision": {"from_model": False, "boxes": [
                    {"centre": [0, 0.5, 0], "half_size": [0.5, 0.5, 0.5], "top": top, "sides": sides}]},
            },
        }],
    }


def main():
    models = os.path.join(ROOT, "assets", "models", "blockout")
    prefabs = os.path.join(ROOT, "assets", "prefabs", "blockout")
    os.makedirs(models, exist_ok=True)
    os.makedirs(prefabs, exist_ok=True)
    for name, color in (("block_light", [0.62, 0.6, 0.57, 1.0]), ("block_dark", [0.3, 0.29, 0.31, 1.0])):
        with open(os.path.join(models, name + ".glb"), "wb") as file:
            file.write(box_glb(color))
    light, dark = "models/blockout/block_light.glb", "models/blockout/block_dark.glb"
    pieces = {
        "floor": prefab(light, [1, 0.2, 1], [0, -0.2, 0], "walkable", "wall"),
        # A ground tile of 8 m from its corner (LevelGround places its tiles at the cells' corners).
        "floor_8m": prefab(light, [8, 0.2, 8], [4, -0.2, 4], "walkable", "wall"),
        "wall": prefab(dark, [1, 2, 1], [0, 0, 0], "wall", "wall"),
        "pillar": prefab(dark, [0.6, 3, 0.6], [0, 0, 0], "wall", "wall"),
        "crate": prefab(light, [0.8, 0.8, 0.8], [0, 0, 0], "obstacle", "obstacle"),
        "step": prefab(light, [1, 0.25, 1], [0, 0, 0], "walkable", "wall"),
    }
    for name, document in pieces.items():
        with open(os.path.join(prefabs, name + ".prefab.json"), "w", encoding="utf-8", newline="\n") as file:
            json.dump(document, file, indent=2)
            file.write("\n")
    print(f"blockout: 2 models, {len(pieces)} prefabs")


if __name__ == "__main__":
    main()
