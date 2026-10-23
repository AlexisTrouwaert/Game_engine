"""Writes assets/models/fantome/fantome.glb: the slice's ghost (milestone 9 bis, part 2), the model
that checks the transparent pass (glTF alphaMode BLEND): a hooded wraith of pale, see-through cloth,
its hem torn, two glowing eyes in the dark of its hood.

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup --python tools/models/make_ghost.py

Blender in the background (no window, no user settings); the output is the same from one run to
the next (no randomness that is not seeded). About 1.8 m tall, standing on y = 0, facing glTF +Z.

- The robe: a surface of revolution (radius by height), its hem cut into rags, the front of the
  hood open; double-sided, alpha blended. Its base color texture fades the alpha from the chest
  (about 0.6) to the hem (0): the ghost thins out towards the ground.
- The sleeves: two tapered tubes hanging from the shoulders, the same material.
- The eyes: two small emissive spheres, opaque (an engine draws them in the opaque pass).
"""

import math
import os
import random

import bmesh
import bpy

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "models", "fantome", "fantome.glb")

SEGMENTS = 40
# (height, radius) of the robe, from the hem to the top of the hood.
PROFILE = [
    (0.10, 0.46), (0.30, 0.40), (0.55, 0.33), (0.80, 0.28), (1.05, 0.29), (1.25, 0.32),
    (1.38, 0.30), (1.48, 0.21), (1.56, 0.22), (1.66, 0.23), (1.75, 0.20), (1.82, 0.13), (1.86, 0.0),
]
HOOD_FROM = 7  # the rings of the hood (index in PROFILE)
HOOD_OPENING = math.radians(62)  # half the angle of the open front of the hood


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def fade_texture():
    """A 4 x 64 image: white, its alpha from 0 (hem, bottom rows) to 0.62 (chest and above)."""
    image = bpy.data.images.new("fantome_fondu", width=4, height=64, alpha=True)
    pixels = []
    for y in range(64):
        t = y / 63.0  # 0 at the bottom of the image (the hem), 1 at the top
        alpha = 0.62 * min(1.0, max(0.0, (t - 0.02) / 0.55)) ** 1.4
        for _ in range(4):
            pixels += [1.0, 1.0, 1.0, alpha]
    image.pixels = pixels
    image.file_format = "PNG"
    image.pack()
    return image


def robe_material(image):
    m = bpy.data.materials.new("fantome_voile")
    m.use_nodes = True
    m.surface_render_method = "BLENDED"
    m.use_backface_culling = False
    nodes = m.node_tree.nodes
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.62, 0.80, 0.88, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.85
    bsdf.inputs["Emission Color"].default_value = (0.20, 0.42, 0.50, 1.0)
    bsdf.inputs["Emission Strength"].default_value = 0.6
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Linear"
    m.node_tree.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
    return m


def eye_material():
    m = bpy.data.materials.new("fantome_yeux")
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.0, 0.0, 0.0, 1.0)
    bsdf.inputs["Emission Color"].default_value = (0.55, 0.95, 1.0, 1.0)
    bsdf.inputs["Emission Strength"].default_value = 12.0
    return m


def build_robe(material):
    rng = random.Random(9)
    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.new("UVMap")
    rings = []
    for k, (h, r) in enumerate(PROFILE):
        ring = []
        for i in range(SEGMENTS):
            a = 2.0 * math.pi * i / SEGMENTS  # 0: towards +Z (the front)
            y = h
            if k == 0:
                # Rags: every other point hangs lower, by a varying length.
                y -= (0.16 + rng.uniform(0.0, 0.10)) if i % 2 == 0 else rng.uniform(0.0, 0.04)
            # The hood leans forward a little.
            z_shift = 0.05 * max(0.0, (h - 1.45) / 0.4)
            ring.append(bm.verts.new((r * math.sin(a), y, r * math.cos(a) + z_shift)))
        rings.append(ring)
    bm.verts.ensure_lookup_table()
    for k in range(len(PROFILE) - 1):
        for i in range(SEGMENTS):
            j = (i + 1) % SEGMENTS
            a = 2.0 * math.pi * (i + 0.5) / SEGMENTS
            front = min(a, 2.0 * math.pi - a)  # angle from the front
            if k >= HOOD_FROM and k < len(PROFILE) - 3 and front < HOOD_OPENING:
                continue  # the open front of the hood
            face = bm.faces.new((rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]))
            for loop in face.loops:
                v = loop.vert
                u = (math.atan2(v.co.x, v.co.z) / (2.0 * math.pi)) % 1.0
                loop[uv_layer].uv = (u, min(1.0, max(0.0, v.co.y / 1.86)))
    mesh = bpy.data.meshes.new("fantome_robe")
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("robe", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(material)
    subdivide_and_smooth(obj)
    return obj


def build_sleeve(material, side):
    """A tapered tube from the shoulder down along the body, slightly forward."""
    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.new("UVMap")
    steps, around = 8, 12
    rings = []
    for s in range(steps + 1):
        t = s / steps
        cx = side * (0.34 - 0.06 * t)
        cy = 1.36 - 0.62 * t
        cz = 0.05 + 0.10 * t
        r = 0.10 + 0.05 * t
        ring = [bm.verts.new((cx + r * math.cos(2 * math.pi * i / around), cy, cz + r * math.sin(2 * math.pi * i / around)))
                for i in range(around)]
        rings.append(ring)
    for s in range(steps):
        for i in range(around):
            j = (i + 1) % around
            face = bm.faces.new((rings[s][i], rings[s][j], rings[s + 1][j], rings[s + 1][i]))
            for loop in face.loops:
                loop[uv_layer].uv = (i / around, min(1.0, max(0.0, loop.vert.co.y / 1.86)))
    mesh = bpy.data.meshes.new(f"fantome_manche_{'d' if side > 0 else 'g'}")
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(mesh.name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(material)
    subdivide_and_smooth(obj)
    return obj


def build_eye(material, side):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=0.025)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(side * 0.055, 1.60, 0.10))
    mesh = bpy.data.meshes.new(f"fantome_oeil_{'d' if side > 0 else 'g'}")
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(mesh.name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(material)
    for poly in obj.data.polygons:
        poly.use_smooth = True
    return obj


def subdivide_and_smooth(obj):
    bpy.context.view_layer.objects.active = obj
    modifier = obj.modifiers.new("subdivision", "SUBSURF")
    modifier.levels = 1
    modifier.render_levels = 1
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    for poly in obj.data.polygons:
        poly.use_smooth = True


def main():
    clear_scene()
    robe = robe_material(fade_texture())
    eyes = eye_material()
    parts = [build_robe(robe), build_sleeve(robe, 1.0), build_sleeve(robe, -1.0), build_eye(eyes, 1.0), build_eye(eyes, -1.0)]
    # One object: one node in the glTF, two primitives (the veil, the eyes).
    bpy.ops.object.select_all(action="DESELECT")
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    parts[0].name = "fantome"
    # Blender is Z up; the exporter turns it into glTF's Y up. The model was built Y up: turn it.
    parts[0].rotation_euler = (math.radians(90.0), 0.0, 0.0)
    bpy.ops.object.transform_apply(rotation=True)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", export_yup=True, export_apply=True,
                              export_materials="EXPORT", export_image_format="AUTO")
    print("ghost written:", OUT)


main()
