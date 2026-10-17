"""Copies the ARPG's first assets into the engine's test assets: the gothic street lamp and the two
wet cobblestone grounds, which the playable slice uses (its floor, a street lamp per room).

    python tools/models/fetch_arpg_models.py [ARPG_DIR] [--ktx path] [--force]

ARPG_DIR: the game's folder (default: ../ARPG next to the engine). Its files are the source; the
copies land in assets/models/arpg/ (not kept in the engine's Git, like the other test assets):

    assets/models/arpg/lampadaire_gothique/lampadaire_gothique.gltf (+ .bin, textures, .prop.json)
    assets/models/arpg/sol_paves_humides_a_8m/sol_paves_humides_a_8m.gltf (+ ...)
    assets/models/arpg/sol_paves_humides_b_8m/sol_paves_humides_b_8m.gltf (+ ...)

Each .glb is unpacked into a .gltf, its .bin and its images, then its textures are encoded to
KTX2 by tools/textures/convert_gltf_textures.py (UASTC, mipmaps; the engine transcodes them to
BC7 / BC5, and the cook keeps them in BC7 / BC5 for the shipped game). The street lamp's light
(.prop.json, written by its Blender pipeline) is copied as it is. A copy newer than its source is
kept (--force to redo everything). The authors are credited in assets/credits.json.
"""

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "assets", "models", "arpg")

# The game's file, and what goes with it.
MODELS = [
    ("models/props/lampadaire_gothique.glb", ["models/props/lampadaire_gothique.prop.json"]),
    ("models/sols/sol_paves_humides_a_8m.glb", []),
    ("models/sols/sol_paves_humides_b_8m.glb", []),
]

MIME_EXTENSIONS = {"image/png": ".png", "image/jpeg": ".jpg", "image/ktx2": ".ktx2"}


def unpack_glb(glb_path, folder):
    """Writes <name>.gltf, <name>.bin and the images of `glb_path` into `folder`."""
    name = os.path.splitext(os.path.basename(glb_path))[0]
    with open(glb_path, "rb") as file:
        data = file.read()
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2 or length != len(data):
        raise SystemExit(f"{glb_path}: not a glTF 2.0 binary file")
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise SystemExit(f"{glb_path}: the first chunk is not JSON")
    gltf = json.loads(data[20:20 + json_length])
    binary = b""
    offset = 20 + json_length
    if offset < len(data):
        bin_length, bin_type = struct.unpack_from("<II", data, offset)
        if bin_type != 0x004E4942:
            raise SystemExit(f"{glb_path}: the second chunk is not BIN")
        binary = data[offset + 8:offset + 8 + bin_length]

    # The images become files; the buffer keeps only the geometry, each view 4-byte aligned.
    image_views = set()
    for index, image in enumerate(gltf.get("images", [])):
        if "bufferView" not in image:
            continue
        view = gltf["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        extension = MIME_EXTENSIONS.get(image.get("mimeType", ""), ".bin")
        file_name = f"{image.get('name') or f'image_{index}'}{extension}"
        with open(os.path.join(folder, file_name), "wb") as file:
            file.write(binary[start:start + view["byteLength"]])
        image_views.add(image["bufferView"])
        image["uri"] = file_name
        del image["bufferView"]
        image.pop("mimeType", None)
    new_binary = bytearray()
    remap = {}
    views = []
    for index, view in enumerate(gltf.get("bufferViews", [])):
        if index in image_views:
            continue
        start = view.get("byteOffset", 0)
        while len(new_binary) % 4:
            new_binary.append(0)
        view = dict(view, byteOffset=len(new_binary))
        new_binary += binary[start:start + view["byteLength"]]
        remap[index] = len(views)
        views.append(view)
    for accessor in gltf.get("accessors", []):
        if "bufferView" in accessor:
            accessor["bufferView"] = remap[accessor["bufferView"]]
        sparse = accessor.get("sparse")
        if sparse:
            sparse["indices"]["bufferView"] = remap[sparse["indices"]["bufferView"]]
            sparse["values"]["bufferView"] = remap[sparse["values"]["bufferView"]]
    gltf["bufferViews"] = views
    gltf["buffers"] = [{"byteLength": len(new_binary), "uri": f"{name}.bin"}]
    with open(os.path.join(folder, f"{name}.bin"), "wb") as file:
        file.write(new_binary)
    gltf_path = os.path.join(folder, f"{name}.gltf")
    with open(gltf_path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(gltf, file, indent=1, ensure_ascii=False)
        file.write("\n")
    return gltf_path


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("arpg", nargs="?", default=os.path.normpath(os.path.join(ROOT, "..", "ARPG")))
    parser.add_argument("--ktx", help="the ktx tool (default: found by convert_gltf_textures.py)")
    parser.add_argument("--force", action="store_true", help="copy and convert again even if up to date")
    args = parser.parse_args()
    assets = os.path.join(args.arpg, "assets")
    if not os.path.isdir(assets):
        print(f"no ARPG assets in {assets}: give the game's folder")
        return 2

    converted = []
    for model, extras in MODELS:
        source = os.path.join(assets, *model.split("/"))
        if not os.path.exists(source):
            print(f"missing in the ARPG: {model}")
            return 1
        name = os.path.splitext(os.path.basename(source))[0]
        folder = os.path.join(OUT, name)
        gltf_path = os.path.join(folder, f"{name}.gltf")
        fresh = os.path.exists(gltf_path) and os.path.getmtime(gltf_path) >= os.path.getmtime(source)
        if fresh and not args.force:
            print(f"{name}: up to date")
        else:
            shutil.rmtree(folder, ignore_errors=True)
            os.makedirs(folder)
            unpack_glb(source, folder)
            print(f"{name}: unpacked")
            converted.append(gltf_path)
        for extra in extras:
            shutil.copy2(os.path.join(assets, *extra.split("/")), folder)

    if converted:
        command = [sys.executable, os.path.join(ROOT, "tools", "textures", "convert_gltf_textures.py")] + converted
        if args.ktx:
            command += ["--ktx", args.ktx]
        if args.force:
            command.append("--force")
        result = subprocess.run(command)
        if result.returncode != 0:
            print("the KTX2 conversion failed: the models keep their PNG textures (the engine reads them too)")
            return result.returncode
    print(f"done: {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
