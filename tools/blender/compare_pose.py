"""Renders in Blender the pose of the sandbox's --anim-compare scene (milestone 5, part 9).

First, in the engine (the knight in one clip at one moment, in the "base color" view):

    bac_a_sable --anim-compare 1H_Melee_Attack_Chop 0.4 --pixel-size 1280 720 --aa msaa4 --run-seconds 2 --capture engine.png

which writes engine.png and engine.png.json (the model, the hidden parts, the clip and the moment,
the camera). Then, in Blender (the reference is 5.2.2):

    blender -b --factory-startup --python tools/blender/compare_pose.py -- engine.png.json blender.png

and tools/blender/compare_pose_images.py compares the two silhouettes.

The same image, as far as each program allows: the same glTF file, posed by Blender's own importer
and animation system at the same moment of the same clip, the same camera, and flat colours (the
Workbench engine, texture colours, no light, the "Standard" view transform): the engine's base
color view shows the textures' sRGB colours the same way. The background is left transparent.

Conventions: the engine is Y up, Blender Z up; glTF import turns (x, y, z) into (x, -z, y), and so
does this script for the camera.
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
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(prog="compare_pose.py")
    parser.add_argument("scene", help="the .json written by the engine next to its capture")
    parser.add_argument("out", help="the PNG to write")
    args = parser.parse_args(argv)
    return args.scene, args.out


def pose(scene, clip, seconds):
    """Puts the imported armature in `clip` at `seconds` (the importer keeps one action per clip)."""
    armature = next(o for o in scene.objects if o.type == "ARMATURE")
    action = next((a for a in bpy.data.actions if a.name == clip or a.name.startswith(clip + "_")), None)
    if action is None:
        raise SystemExit(f"no action for the clip '{clip}'")
    armature.animation_data.action = action
    if hasattr(action, "slots") and len(action.slots):  # Blender 4.4 and later: slotted actions
        armature.animation_data.action_slot = action.slots[0]
    fps = scene.render.fps / scene.render.fps_base
    frame = seconds * fps
    scene.frame_set(int(math.floor(frame)), subframe=frame - math.floor(frame))


def main():
    scene_path, out_path = arguments()
    with open(scene_path, encoding="utf-8") as f:
        description = json.load(f)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(description["assets_dir"], description["model"]))
    scene = bpy.context.scene
    hidden = set(description["hidden_nodes"])
    for obj in scene.objects:
        if obj.name.split(".")[0] in hidden:
            obj.hide_render = True
    pose(scene, description["clip"], description["seconds"])

    image = description["image"]
    scene.render.resolution_x = image["width"]
    scene.render.resolution_y = image["height"]
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.render_aa = "16"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"

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
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    bpy.ops.render.render(write_still=True)
    print(f"written {out_path}: '{description['clip']}' at {description['seconds']} s")


if __name__ == "__main__":
    main()
