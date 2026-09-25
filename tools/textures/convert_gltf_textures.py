"""Converts the textures of glTF models to KTX2 (UASTC, with mipmaps) and declares them in the
models with the KHR_texture_basisu extension.

    python tools/textures/convert_gltf_textures.py [model.gltf | directory ...] [--ktx path] [--force]

With no argument, every .gltf under assets/models/ is converted. For each image a material uses:

- base color and emissive: colors, encoded as sRGB (--format ..._SRGB);
- normal map: linear, two channels (--normal-mode: x in RGB, y in alpha), which the engine
  transcodes to BC5 and the shader completes (z rebuilt from x and y);
- metal / roughness, occlusion: linear data.

Data textures are declared linear with --assign-tf linear: without it, ktx create takes a JPEG for
sRGB and "converts" the values, which would change the normals and the roughness.

The .ktx2 file is written next to the image (same name, .ktx2 extension). The model keeps its
PNG / JPEG images as the fallback of KHR_texture_basisu (not in extensionsRequired), so Blender and
other tools still open it; the engine prefers the KTX2 version (sandbox option --no-ktx2 to compare).
A .ktx2 newer than its image is not encoded again (--force to redo them all). Running the script
twice gives the same model.

The ktx tool (KTX-Software, installed by vcpkg with the "tools" feature of the ktx port) is looked
for in the build directories, then in the PATH; --ktx gives it explicitly.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
EXTENSION = "KHR_texture_basisu"

# Encoding settings: UASTC level 2 (the default quality), Zstandard level 18 on disk.
COMMON = ["--encode", "uastc", "--uastc-quality", "2", "--zstd", "18", "--generate-mipmap"]


def find_ktx(explicit):
    if explicit:
        return explicit
    patterns = [
        os.path.join(ROOT, "build", "*", "vcpkg_installed", "*", "tools", "ktx", "ktx*"),
        os.path.join(ROOT, "cmake-build-*", "vcpkg_installed", "*", "tools", "ktx", "ktx*"),
    ]
    for pattern in patterns:
        for candidate in sorted(glob.glob(pattern)):
            if os.path.basename(candidate) in ("ktx", "ktx.exe"):
                return candidate
    found = shutil.which("ktx")
    if found:
        return found
    sys.exit("ktx tool not found: build the project once (vcpkg installs it) or give --ktx")


def image_channels(path):
    """Number of channels of a PNG or JPEG, read from its header."""
    with open(path, "rb") as f:
        data = f.read(1 << 16)
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        color_type = data[25]
        return {0: 1, 2: 3, 3: 3, 4: 2, 6: 4}[color_type]
    if data.startswith(b"\xff\xd8"):
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            marker = data[i + 1]
            length = int.from_bytes(data[i + 2:i + 4], "big")
            if marker in (0xC0, 0xC1, 0xC2):  # start of frame: the component count is its 6th byte
                return data[i + 9]
            i += 2 + length
    raise ValueError(f"{path}: not a PNG or JPEG file")


def ktx_format(channels, srgb):
    base = {1: "R8G8B8", 2: "R8G8B8A8", 3: "R8G8B8", 4: "R8G8B8A8"}[channels]
    return base + ("_SRGB" if srgb else "_UNORM")


def encode(ktx, source, target, role, force):
    if not force and os.path.exists(target) and os.path.getmtime(target) >= os.path.getmtime(source):
        return False
    args = [ktx, "create", "--format", ktx_format(image_channels(source), role == "color")] + COMMON
    if role == "color":
        args += ["--assign-tf", "srgb"]
    else:
        args += ["--assign-tf", "linear"]
    if role == "normal":
        args += ["--normal-mode", "--normalize"]
    args += [source, target]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"ktx create failed for {source}:\n{result.stderr}")
    return True


def roles_of(gltf):
    """The role of each image index: 'color', 'normal' or 'data' (from the materials that use it)."""
    textures = gltf.get("textures", [])
    roles = {}

    def use(info, role):
        if not info or "index" not in info:
            return
        texture = textures[info["index"]]
        source = texture.get("source")
        if source is None:
            return
        previous = roles.get(source)
        if previous and previous != role:
            print(f"  warning: image {source} used as {previous} and {role}; encoded as {previous}")
            return
        roles[source] = role

    for material in gltf.get("materials", []):
        pbr = material.get("pbrMetallicRoughness", {})
        use(pbr.get("baseColorTexture"), "color")
        use(material.get("emissiveTexture"), "color")
        use(material.get("normalTexture"), "normal")
        use(pbr.get("metallicRoughnessTexture"), "data")
        use(material.get("occlusionTexture"), "data")
    return roles


def convert(ktx, gltf_path, force):
    with open(gltf_path, encoding="utf-8") as f:
        gltf = json.load(f)
    directory = os.path.dirname(gltf_path)
    images = gltf.setdefault("images", [])
    roles = roles_of(gltf)
    encoded = 0
    ktx_image_of = {}  # plain image index -> KTX2 image index
    for index, role in sorted(roles.items()):
        image = images[index]
        uri = image.get("uri")
        if not uri or uri.startswith("data:") or uri.endswith(".ktx2"):
            continue  # embedded images are left alone
        source = os.path.join(directory, uri)
        ktx_uri = os.path.splitext(uri)[0] + ".ktx2"
        if encode(ktx, source, os.path.join(directory, ktx_uri), role, force):
            encoded += 1
            print(f"  {role:6}  {ktx_uri}")
        # The KTX2 image: reused if the model already declares it.
        existing = [i for i, other in enumerate(images) if other.get("uri") == ktx_uri]
        if existing:
            ktx_image_of[index] = existing[0]
        else:
            images.append({"uri": ktx_uri, "mimeType": "image/ktx2"})
            ktx_image_of[index] = len(images) - 1

    for texture in gltf.get("textures", []):
        source = texture.get("source")
        if source in ktx_image_of:
            texture.setdefault("extensions", {})[EXTENSION] = {"source": ktx_image_of[source]}
    if ktx_image_of:
        used = gltf.setdefault("extensionsUsed", [])
        if EXTENSION not in used:
            used.append(EXTENSION)

    with open(gltf_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(gltf, f, indent=2, ensure_ascii=False)
        f.write("\n")
    return encoded


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("paths", nargs="*", help=".gltf files or directories (default: assets/models)")
    parser.add_argument("--ktx", help="the ktx tool of KTX-Software")
    parser.add_argument("--force", action="store_true", help="encode even the textures that are up to date")
    options = parser.parse_args()

    ktx = find_ktx(options.ktx)
    paths = options.paths or [os.path.join(ROOT, "assets", "models")]
    models = []
    for path in paths:
        if os.path.isdir(path):
            models += sorted(glob.glob(os.path.join(path, "**", "*.gltf"), recursive=True))
        else:
            models.append(path)
    total = 0
    for model in models:
        print(os.path.relpath(model, ROOT))
        total += convert(ktx, model, options.force)
    print(f"{total} texture(s) encoded, {len(models)} model(s) updated")


if __name__ == "__main__":
    main()
