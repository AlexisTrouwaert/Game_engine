"""Renders in Blender the scene of the sandbox's "Comparaison avec Blender" test (milestone 3, part 7).

First, in the engine (1280 x 720 pixels, MSAA for clean edges):

    bac_a_sable --blender-compare --pixel-size 1280 720 --aa msaa4 --run-seconds 2 --capture engine.png

which writes engine.png and engine.png.json (the camera, the environment, where each model and
sphere stands). Then, in Blender 4.2 or later (for the "Khronos PBR Neutral" view transform; the
reference is 5.2.2, the version the Blender MCP runs in):

    blender -b --factory-startup --python tools/blender/compare_render.py -- engine.png.json blender.png [--gpu] [--eevee] [--samples 256]

or, from the Blender MCP, run this file with sys.argv set the same way. Finally
tools/blender/compare_images.py puts both images side by side.

The same scene, as far as each program allows: the same glTF files at the same places, the same
spheres and materials, the environment as the only light with the same strength, the same camera,
and the same tone mapping (Khronos PBR Neutral, then sRGB). The background is left transparent: the
comparison lays it on the engine's background colour. Expected differences: Blender traces the
light, so objects shade themselves (cavities, the inside of the lantern) where the engine only has
the occlusion maps; the engine's environment lighting is approximated (spherical harmonics and a
prefiltered image).

Conventions: the engine is Y up, Blender Z up. glTF import turns (x, y, z) into (x, -z, y); so does
this script for the positions of the JSON file. Both programs then read the equirectangular
environment the same way (u = atan2(z, x) / 2 pi + 0.5 in the engine's axes), so it is not rotated.
"""

import argparse
import json
import math
import os
import sys

import bpy
from mathutils import Vector


def to_blender(v):
    """Engine (Y up) to Blender (Z up), as the glTF importer does."""
    return Vector((v[0], -v[2], v[1]))


def arguments():
    # Blender keeps its own arguments before "--", the script's come after.
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="compare_render.py")
    parser.add_argument("scene", help="the .json written by the engine next to its capture")
    parser.add_argument("out", help="the PNG to write")
    parser.add_argument("--eevee", action="store_true", help="EEVEE instead of Cycles")
    parser.add_argument("--samples", type=int, default=256, help="Cycles samples per pixel")
    parser.add_argument("--gpu", action="store_true", help="Cycles on the GPU (OptiX, CUDA, HIP, Metal or oneAPI)")
    args = parser.parse_args(argv)
    return args.scene, args.out, args.eevee, args.samples, args.gpu


def use_gpu(scene):
    """Cycles on the first GPU backend that has a device; stays on the CPU otherwise."""
    preferences = bpy.context.preferences.addons["cycles"].preferences
    for kind in ("OPTIX", "CUDA", "HIP", "METAL", "ONEAPI"):
        try:
            preferences.compute_device_type = kind
        except TypeError:
            continue  # not offered by this build of Blender
        preferences.get_devices()
        devices = [d for d in preferences.devices if d.type == kind]
        if devices:
            for device in preferences.devices:
                device.use = device.type == kind
            scene.cycles.device = "GPU"
            print(f"Blender comparison: Cycles on {kind} ({', '.join(d.name for d in devices)})")
            return
    print("Blender comparison: no GPU found, Cycles on the CPU")


def main():
    scene_path, out_path, eevee, samples, gpu = arguments()
    with open(scene_path, encoding="utf-8") as f:
        description = json.load(f)
    assets = description["assets_dir"]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene

    # The image: size, transparent background, the engine's tone mapping.
    image = description["image"]
    scene.render.resolution_x = image["width"]
    scene.render.resolution_y = image["height"]
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.display_settings.display_device = "sRGB"
    scene.view_settings.view_transform = description["tone_mapping"]["view_transform"]
    scene.view_settings.look = "None"
    scene.view_settings.exposure = math.log2(description["tone_mapping"]["exposure"])
    scene.view_settings.gamma = 1.0
    if eevee:
        scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in {
            e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items} else "BLENDER_EEVEE"
    else:
        scene.render.engine = "CYCLES"
        scene.cycles.samples = samples
        scene.cycles.use_denoising = True
        if gpu:
            use_gpu(scene)

    # The environment, the only light.
    world = bpy.data.worlds.new("compare")
    scene.world = world
    world.use_nodes = True
    nodes = world.node_tree.nodes
    background = nodes["Background"]
    environment = description["environment"]
    if environment["path"]:
        texture = nodes.new("ShaderNodeTexEnvironment")
        texture.image = bpy.data.images.load(os.path.join(assets, environment["path"]))
        texture.projection = "EQUIRECTANGULAR"
        world.node_tree.links.new(texture.outputs["Color"], background.inputs["Color"])
    background.inputs["Strength"].default_value = environment["intensity"]

    # The models, their origin moved to the same place as in the engine.
    for model in description["models"]:
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=os.path.join(assets, model["path"]))
        imported = [o for o in bpy.data.objects if o not in before]
        anchor = bpy.data.objects.new("offset " + os.path.basename(model["path"]), None)
        scene.collection.objects.link(anchor)
        anchor.location = to_blender(model["offset"])
        for obj in imported:
            if obj.parent is None:
                obj.parent = anchor

    # The spheres: a Principled BSDF with the same metal / roughness values as the glTF material.
    for index, sphere in enumerate(description["spheres"]):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, radius=sphere["radius"],
                                             location=to_blender(sphere["center"]))
        obj = bpy.context.active_object
        bpy.ops.object.shade_smooth()
        material = bpy.data.materials.new(f"sphere {index}")
        material.use_nodes = True
        bsdf = material.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Base Color"].default_value = (*sphere["base_color_linear"], 1.0)
        bsdf.inputs["Metallic"].default_value = sphere["metallic"]
        bsdf.inputs["Roughness"].default_value = sphere["roughness"]
        obj.data.materials.append(material)

    # The camera: same eye, same target, same vertical field of view.
    camera_data = description["camera"]
    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    scene.collection.objects.link(camera)
    eye = to_blender(camera_data["eye"])
    target = to_blender(camera_data["target"])
    camera.location = eye
    camera.rotation_mode = "QUATERNION"
    camera.rotation_quaternion = (target - eye).to_track_quat("-Z", "Y")
    camera.data.sensor_fit = "VERTICAL"
    camera.data.angle = math.radians(camera_data["vertical_fov_degrees"])
    camera.data.clip_start = 0.05
    camera.data.clip_end = 1000.0
    scene.camera = camera

    scene.render.filepath = os.path.abspath(out_path)
    bpy.ops.render.render(write_still=True)
    print(f"Blender comparison: {scene.render.engine}, written to {scene.render.filepath}")


main()
