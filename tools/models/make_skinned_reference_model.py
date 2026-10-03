"""Writes assets/models/skinned_reference.glb: the model that checks the skeleton and animation
chain (milestone 5), with values known in advance.

    python tools/models/make_skinned_reference_model.py

No dependency beyond Python 3 (and make_reference_model.py next to it). The output is
deterministic: running the script twice gives the same bytes.

Skeleton, in the bind pose (world space, metres):

    armature (not a joint)  at (1, 0, 0)     its translation must still apply to the character
      root                  at (1, 0, 0)
        upper               at (1, 1, 0)
          lower             at (1, 2, 0)
            marker          (not a joint) rigid 0.2 m cube 0.25 m above "lower": a mesh that
                            follows a bone without skinning, as the KayKit weapons do

Traps on purpose:

- nodes are listed children first (lower, upper, root, armature): the loader must sort joints
  parents first;
- skin.joints is [upper, root, lower], not the node order: JOINTS_0 indexes skin.joints;
- the skinned mesh's node has a translation of (5, 5, 5), which glTF says to ignore;
- JOINTS_0 is UNSIGNED_SHORT (the KayKit models use UNSIGNED_BYTE: both are covered).

Mesh: a 0.2 m square column from y = 0 to y = 2, centred on x = 1, z = 0, with vertices at
y = 0, 1 and 2 only. Weights: y = 0 -> root 1; y = 1 -> upper 0.75, root 0.25; y = 2 -> upper 1.

Clips (times in seconds; "clip time" counts from the clip's first key):

- "Bend": keys at 0.5 and 1.5 s (the clip does not start at 0; its duration is 1 s). "upper"
  turns around +Z from 0 to 90 degrees. At clip time 0.5: 45 degrees, so "lower" is at
  (1 - sqrt(0.5), 1 + sqrt(0.5), 0).
- "Step": STEP keys on "root": translation (0, 0, 0) at 0 s, (0, 0, 1) at 0.5 s, (0, 0, 2) at
  1 s. At 0.25 s: (0, 0, 0); at 0.75 s: (0, 0, 1).
- "Cubic": CUBICSPLINE keys on "lower": translation (0, 1, 0) at 0 s to (1, 1, 0) at 1 s, all
  tangents zero, so x follows 3t^2 - 2t^3: 0.15625 at 0.25 s, 0.5 at 0.5 s.
- "Flip": "root" rotation keys (0, 0, 0, 1) at 0 s and (0, 0, 0, -1) at 1 s: the same rotation
  written with opposite signs. Taking the short path, the root never turns; taking the long
  one, it turns 360 degrees (180 at 0.5 s).
- "Scale": "upper" scale from 1 to 2 over 1 s: 1.5 at 0.5 s, so "lower" is at (1, 2.5, 0).
"""

import math
import os
import struct
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_reference_model import Builder, box, merge  # noqa: E402

FLOAT, USHORT = 5126, 5123
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


def column(x, half, heights):
    """A square column along Y, flat-shaded, with a ring of vertices at each height."""
    corners = [(x - half, -half), (x + half, -half), (x + half, half), (x - half, half)]  # (x, z)
    positions, normals, uvs, indices = [], [], [], []
    for side in range(4):
        (x0, z0), (x1, z1) = corners[side], corners[(side + 1) % 4]
        nx, nz = z1 - z0, -(x1 - x0)  # outward, given the order of the corners
        length = math.hypot(nx, nz)
        normal = (nx / length, 0.0, nz / length)
        for low, high in zip(heights, heights[1:]):
            first = len(positions)
            positions += [(x0, low, z0), (x1, low, z1), (x1, high, z1), (x0, high, z0)]
            normals += [normal] * 4
            uvs += [(0, 1), (1, 1), (1, 0), (0, 0)]
            indices += [first, first + 2, first + 1, first, first + 3, first + 2]
    for y, normal, order in ((heights[-1], (0.0, 1.0, 0.0), (0, 3, 2, 1)), (heights[0], (0.0, -1.0, 0.0), (0, 1, 2, 3))):
        first = len(positions)
        positions += [(corners[i][0], y, corners[i][1]) for i in order]
        normals += [normal] * 4
        uvs += [(0, 0)] * 4
        indices += [first, first + 1, first + 2, first, first + 2, first + 3]
    return positions, normals, uvs, indices


def translation_matrix(x, y, z):
    """Column-major 4x4, as glTF stores matrices."""
    return (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, x, y, z, 1.0)


def z_rotation(degrees):
    half = math.radians(degrees) / 2
    return (0.0, 0.0, math.sin(half), math.cos(half))


def main():
    b = Builder()
    b.gltf["asset"]["generator"] = "moteur make_skinned_reference_model.py"
    b.gltf["skins"] = []
    b.gltf["animations"] = []
    grey = b.material("column", (0.8, 0.8, 0.8, 1.0))
    orange = b.material("marker", (0.95, 0.5, 0.1, 1.0))

    # Joint indices in skin.joints: 0 = upper, 1 = root, 2 = lower.
    UPPER, ROOT = 0, 1
    positions, normals, uvs, indices = column(1.0, 0.1, [0.0, 1.0, 2.0])
    joints, weights = [], []
    for _, y, _ in positions:
        joints.append((UPPER, ROOT, 0, 0))
        if y < 0.5:
            weights.append((0.0, 1.0, 0.0, 0.0))
        elif y < 1.5:
            weights.append((0.75, 0.25, 0.0, 0.0))
        else:
            weights.append((1.0, 0.0, 0.0, 0.0))
    attributes = {
        "POSITION": b.accessor(positions, "VEC3", FLOAT, "f", ARRAY_BUFFER, with_bounds=True),
        "NORMAL": b.accessor(normals, "VEC3", FLOAT, "f", ARRAY_BUFFER),
        "TEXCOORD_0": b.accessor(uvs, "VEC2", FLOAT, "f", ARRAY_BUFFER),
        "JOINTS_0": b.accessor(joints, "VEC4", USHORT, "H", ARRAY_BUFFER),
        "WEIGHTS_0": b.accessor(weights, "VEC4", FLOAT, "f", ARRAY_BUFFER),
    }
    b.gltf["meshes"].append({"name": "column", "primitives": [
        {"attributes": attributes, "indices": b.accessor(indices, "SCALAR", USHORT, "H", ELEMENT_ARRAY_BUFFER),
         "material": grey}]})
    column_mesh = len(b.gltf["meshes"]) - 1
    marker_mesh = b.mesh("marker", merge(box((-0.1, -0.1, -0.1), (0.1, 0.1, 0.1))), orange)

    # Children first, on purpose (see the docstring).
    marker = b.node({"name": "marker", "mesh": marker_mesh, "translation": [0.0, 0.25, 0.0]})
    lower = b.node({"name": "lower", "translation": [0.0, 1.0, 0.0], "children": [marker]})
    upper = b.node({"name": "upper", "translation": [0.0, 1.0, 0.0], "children": [lower]})
    root = b.node({"name": "root", "children": [upper]})
    armature = b.node({"name": "armature", "translation": [1.0, 0.0, 0.0], "children": [root]})
    skinned = b.node({"name": "column", "mesh": column_mesh, "skin": 0, "translation": [5.0, 5.0, 5.0]})

    # Inverse bind matrices: the inverse of each joint's world matrix in the bind pose (in the
    # order of skin.joints). Every joint is only translated, so the inverse is the opposite shift.
    bind_positions = {upper: (1.0, 1.0, 0.0), root: (1.0, 0.0, 0.0), lower: (1.0, 2.0, 0.0)}
    skin_joints = [upper, root, lower]
    inverse_binds = [translation_matrix(*(-c for c in bind_positions[j])) for j in skin_joints]
    b.gltf["skins"].append({"name": "column rig", "joints": skin_joints, "skeleton": root,
                            "inverseBindMatrices": b.accessor(inverse_binds, "MAT4", FLOAT, "f", None)})

    def animation(name, channels):
        """channels: (node, path, interpolation, times, values), values already flattened per key."""
        entry = {"name": name, "samplers": [], "channels": []}
        for node, path, interpolation, times, values in channels:
            kind = "VEC4" if path == "rotation" else "VEC3"
            time_accessor = b.accessor(times, "SCALAR", FLOAT, "f", None, with_bounds=False)
            b.gltf["accessors"][time_accessor]["min"] = [min(times)]
            b.gltf["accessors"][time_accessor]["max"] = [max(times)]
            entry["samplers"].append({"input": time_accessor, "interpolation": interpolation,
                                      "output": b.accessor(values, kind, FLOAT, "f", None)})
            entry["channels"].append({"sampler": len(entry["samplers"]) - 1, "target": {"node": node, "path": path}})
        b.gltf["animations"].append(entry)

    zero = (0.0, 0.0, 0.0)
    animation("Bend", [(upper, "rotation", "LINEAR", [0.5, 1.5], [z_rotation(0), z_rotation(90)])])
    animation("Step", [(root, "translation", "STEP", [0.0, 0.5, 1.0], [zero, (0.0, 0.0, 1.0), (0.0, 0.0, 2.0)])])
    # CUBICSPLINE: in-tangent, value, out-tangent for each key.
    animation("Cubic", [(lower, "translation", "CUBICSPLINE", [0.0, 1.0],
                         [zero, (0.0, 1.0, 0.0), zero, zero, (1.0, 1.0, 0.0), zero])])
    animation("Flip", [(root, "rotation", "LINEAR", [0.0, 1.0], [(0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0, -1.0)])])
    animation("Scale", [(upper, "scale", "LINEAR", [0.0, 1.0], [(1.0, 1.0, 1.0), (2.0, 2.0, 2.0)])])

    b.gltf["scenes"] = [{"name": "skinned reference", "nodes": [armature, skinned]}]
    b.gltf["scene"] = 0
    # Builder starts with image lists this model does not use; glTF forbids empty arrays.
    for key in ("images", "textures", "samplers"):
        if not b.gltf[key]:
            del b.gltf[key]

    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.normpath(os.path.join(here, "..", "..", "assets", "models", "skinned_reference.glb"))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = b.glb()
    with open(out, "wb") as f:
        f.write(data)
    print(f"{out}: {len(data)} bytes")


if __name__ == "__main__":
    main()
