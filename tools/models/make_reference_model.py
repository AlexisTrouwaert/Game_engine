"""Writes assets/models/reference.glb: the model that checks the glTF chain end to end.

    python tools/models/make_reference_model.py

No dependency beyond Python 3. What the model shows, once loaded by the engine:

- a 1 m cube standing on the ground (its base at y = 0), so the scale can be checked against the
  1 m grid of the 3D scene;
- on each face of the cube, a texture split in four colored quadrants, with the image's top-left
  (texture coordinate 0, 0) red, top-right green, bottom-left blue and bottom-right white: a flipped
  or mirrored texture is obvious;
- on each face of the cube, a normal map with a dome in relief in the middle, and no tangents in
  the file (so the engine computes them): lit from above, a dome must be bright on its upper half
  on the side faces; a normal map read upside down shows a hollow lit from below instead;
- a red arrow pointing to +X, a green marker to +Y and a blue marker to +Z (the usual axis colors):
  a model turned the wrong way is obvious too;
- the arrow is a *child node* of the cube, offset in the cube's own space: a node hierarchy that is
  ignored or composed in the wrong order puts it in the wrong place.

The output is deterministic: running the script twice gives the same bytes.
"""

import json
import os
import struct
import zlib


def png(width, height, pixel):
    """PNG bytes of an RGBA image; pixel(x, y) returns (r, g, b, a)."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    rows = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")


def quadrants(x, y, size=64):
    border = x < 2 or y < 2 or x >= size - 2 or y >= size - 2
    if border:
        return (40, 40, 40, 255)
    left, top = x < size // 2, y < size // 2
    if top:
        return (220, 40, 40, 255) if left else (40, 180, 60, 255)
    return (50, 80, 220, 255) if left else (240, 240, 240, 255)


def dome(x, y, size=64):
    """Tangent-space normal of a dome in relief in the middle of the image (glTF: +X right, +Y up
    in the image, +Z out of the surface), encoded as RGB = normal * 0.5 + 0.5."""
    radius = size * 0.35
    dx = (x + 0.5 - size / 2) / radius
    dy = (size / 2 - (y + 0.5)) / radius  # up in the image is +Y
    d2 = dx * dx + dy * dy
    if d2 >= 1.0:
        return (128, 128, 255, 255)
    nz = (1.0 - d2) ** 0.5
    return tuple(int(round((c * 0.5 + 0.5) * 255)) for c in (dx, dy, nz)) + (255,)


def box(min_corner, max_corner):
    """Positions, normals, texture coordinates and indices of a box; faces counter-clockwise from outside."""
    (x0, y0, z0), (x1, y1, z1) = min_corner, max_corner
    faces = [  # corners: bottom-left, bottom-right, top-right, top-left, seen from outside
        ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)]),
        ((1, 0, 0), [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)]),
        ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
        ((0, 1, 0), [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]),
        ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for normal, corners in faces:
        first = len(positions)
        positions += corners
        normals += [normal] * 4
        uvs += [(0, 1), (1, 1), (1, 0), (0, 0)]  # glTF: v = 0 is the top of the image
        indices += [first, first + 1, first + 2, first, first + 2, first + 3]
    return positions, normals, uvs, indices


def pyramid(base_x, half, length):
    """A square pyramid along +X: base at x = base_x, tip at base_x + length. Flat-shaded."""
    tip = (base_x + length, 0.0, 0.0)
    b = [(base_x, -half, -half), (base_x, -half, half), (base_x, half, half), (base_x, half, -half)]
    triangles = [(b[1], b[0], tip), (b[2], b[1], tip), (b[3], b[2], tip), (b[0], b[3], tip),  # sides
                 (b[0], b[1], b[2]), (b[0], b[2], b[3])]  # base, facing -X
    positions, normals, uvs, indices = [], [], [], []
    for a, c, d in triangles:
        u = [c[i] - a[i] for i in range(3)]
        v = [d[i] - a[i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length_n = sum(x * x for x in n) ** 0.5
        n = tuple(x / length_n for x in n)
        first = len(positions)
        positions += [a, c, d]
        normals += [n] * 3
        uvs += [(0, 0)] * 3
        indices += [first, first + 1, first + 2]
    return positions, normals, uvs, indices


def merge(*parts):
    positions, normals, uvs, indices = [], [], [], []
    for p, n, t, i in parts:
        offset = len(positions)
        positions += p
        normals += n
        uvs += t
        indices += [offset + k for k in i]
    return positions, normals, uvs, indices


class Builder:
    def __init__(self):
        self.binary = bytearray()
        self.gltf = {"asset": {"version": "2.0", "generator": "moteur make_reference_model.py"},
                     "buffers": [], "bufferViews": [], "accessors": [], "meshes": [], "materials": [],
                     "nodes": [], "images": [], "textures": [], "samplers": []}

    def view(self, data, target=None):
        while len(self.binary) % 4:
            self.binary += b"\x00"
        entry = {"buffer": 0, "byteOffset": len(self.binary), "byteLength": len(data)}
        if target is not None:
            entry["target"] = target
        self.binary += data
        self.gltf["bufferViews"].append(entry)
        return len(self.gltf["bufferViews"]) - 1

    def accessor(self, values, kind, component, fmt, target, with_bounds=False):
        flat = [x for value in values for x in (value if isinstance(value, tuple) else (value,))]
        view = self.view(struct.pack("<" + fmt * len(flat), *flat), target)
        entry = {"bufferView": view, "componentType": component, "count": len(values), "type": kind}
        if with_bounds:
            size = len(values[0])
            entry["min"] = [min(v[i] for v in values) for i in range(size)]
            entry["max"] = [max(v[i] for v in values) for i in range(size)]
        self.gltf["accessors"].append(entry)
        return len(self.gltf["accessors"]) - 1

    def mesh(self, name, geometry, material):
        positions, normals, uvs, indices = geometry
        attributes = {
            "POSITION": self.accessor(positions, "VEC3", 5126, "f", 34962, with_bounds=True),
            "NORMAL": self.accessor(normals, "VEC3", 5126, "f", 34962),
            "TEXCOORD_0": self.accessor(uvs, "VEC2", 5126, "f", 34962),
        }
        index_accessor = self.accessor(indices, "SCALAR", 5123, "H", 34963)
        self.gltf["meshes"].append({"name": name, "primitives": [
            {"attributes": attributes, "indices": index_accessor, "material": material}]})
        return len(self.gltf["meshes"]) - 1

    def material(self, name, color, texture=None):
        pbr = {"baseColorFactor": list(color), "metallicFactor": 0.0, "roughnessFactor": 0.8}
        if texture is not None:
            pbr["baseColorTexture"] = {"index": texture}
        self.gltf["materials"].append({"name": name, "pbrMetallicRoughness": pbr})
        return len(self.gltf["materials"]) - 1

    def node(self, node):
        self.gltf["nodes"].append(node)
        return len(self.gltf["nodes"]) - 1

    def glb(self):
        while len(self.binary) % 4:
            self.binary += b"\x00"
        self.gltf["buffers"].append({"byteLength": len(self.binary)})
        text = json.dumps(self.gltf, separators=(",", ":"), sort_keys=True).encode()
        text += b" " * (-len(text) % 4)
        total = 12 + 8 + len(text) + 8 + len(self.binary)
        return (struct.pack("<III", 0x46546C67, 2, total) + struct.pack("<II", len(text), 0x4E4F534A) + text +
                struct.pack("<II", len(self.binary), 0x004E4942) + bytes(self.binary))


def main():
    b = Builder()
    image_view = b.view(png(64, 64, quadrants))
    b.gltf["images"].append({"name": "quadrants", "bufferView": image_view, "mimeType": "image/png"})
    b.gltf["samplers"].append({"magFilter": 9729, "minFilter": 9729})
    b.gltf["textures"].append({"source": 0, "sampler": 0})
    normal_view = b.view(png(64, 64, dome))
    b.gltf["images"].append({"name": "dome normal", "bufferView": normal_view, "mimeType": "image/png"})
    b.gltf["textures"].append({"source": 1, "sampler": 0})

    grey = b.material("cube", (0.9, 0.9, 0.9, 1.0), texture=0)
    b.gltf["materials"][grey]["normalTexture"] = {"index": 1}
    red = b.material("axis x", (0.9, 0.15, 0.1, 1.0))
    green = b.material("axis y", (0.15, 0.8, 0.2, 1.0))
    blue = b.material("axis z", (0.15, 0.3, 0.95, 1.0))

    cube_mesh = b.mesh("cube", box((-0.5, -0.5, -0.5), (0.5, 0.5, 0.5)), grey)
    # The arrow in its own space: shaft from x = 0 to 0.8, head from 0.8 to 1.1.
    arrow_mesh = b.mesh("arrow x", merge(box((0.0, -0.05, -0.05), (0.8, 0.05, 0.05)), pyramid(0.8, 0.12, 0.3)), red)
    y_mesh = b.mesh("marker y", box((-0.05, 0.0, -0.05), (0.05, 0.6, 0.05)), green)
    z_mesh = b.mesh("marker z", box((-0.05, -0.05, 0.0), (0.05, 0.05, 0.6)), blue)

    # Hierarchy: the cube node lifts the cube so it stands on the ground; the arrow and the markers
    # are its children, placed on its faces in the cube's own space.
    arrow = b.node({"name": "arrow x", "mesh": arrow_mesh, "translation": [0.5, 0.0, 0.0]})
    marker_y = b.node({"name": "marker y", "mesh": y_mesh, "translation": [0.0, 0.5, 0.0]})
    marker_z = b.node({"name": "marker z", "mesh": z_mesh, "translation": [0.0, 0.0, 0.5]})
    cube = b.node({"name": "cube", "mesh": cube_mesh, "translation": [0.0, 0.5, 0.0], "children": [arrow, marker_y, marker_z]})
    root = b.node({"name": "reference", "children": [cube]})
    b.gltf["scenes"] = [{"name": "reference", "nodes": [root]}]
    b.gltf["scene"] = 0

    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.normpath(os.path.join(here, "..", "..", "assets", "models", "reference.glb"))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = b.glb()
    with open(out, "wb") as f:
        f.write(data)
    print(f"{out}: {len(data)} bytes")


if __name__ == "__main__":
    main()
