"""Downloads the 3D test models into assets/models/polyhaven/ (about 11 MB) and the test
environment into assets/environments/ (about 1.6 MB), neither kept in Git.

    python tools/models/fetch_test_models.py

All come from Poly Haven (https://polyhaven.com), under CC0: public domain, no attribution
required. Their authors are credited anyway, in assets/credits.json and in the "À propos" window of
the sandbox. Keep this list and that file in step.

The environment is an equirectangular HDR image (the sandbox loads every .hdr of
assets/environments/), used to compare the engine's lighting with Blender's under the same light.

The model textures are then encoded to KTX2 (tools/textures/convert_gltf_textures.py), which the
engine loads instead of the JPEG images. That step needs the ktx tool, which vcpkg builds with the
project: build once before running this script, or run the conversion later. Converting a model
rewrites its .gltf, which is therefore downloaded again (a few kilobytes) and converted again on
the next run.

The 1K glTF version of each model, and the 1K .hdr of each environment, is fetched through the official API (api.polyhaven.com), which
asks clients to identify themselves with a User-Agent. Each file's size is checked against the one
the API announces. Files already present with the right size are not downloaded again.
"""

import json
import os
import subprocess
import sys
import urllib.request

MODELS = ["wine_barrel_01", "Lantern_01", "antique_estoc", "boulder_01"]
ENVIRONMENTS = ["studio_small_09"]
RESOLUTION = "1k"
USER_AGENT = {"User-Agent": "moteur-engine-test-assets/1.0"}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=USER_AGENT)) as response:
        return response.read()


def fetch(path, url, size):
    """Downloads url to path unless it is already there with the right size; returns the bytes written."""
    if os.path.exists(path) and os.path.getsize(path) == size:
        return 0
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = get(url)
    if len(data) != size:
        raise RuntimeError(f"{url}: got {len(data)} bytes, expected {size}")
    with open(path, "wb") as f:
        f.write(data)
    print(f"{len(data):>9}  {path}")
    return len(data)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    assets = os.path.normpath(os.path.join(here, "..", "..", "assets"))
    total = 0
    for model in MODELS:
        files = json.loads(get(f"https://api.polyhaven.com/files/{model}"))["gltf"][RESOLUTION]["gltf"]
        # The .gltf itself, then what it refers to, at the relative paths it uses.
        targets = [(os.path.basename(files["url"]), files["url"], files["size"])]
        targets += [(path, info["url"], info["size"]) for path, info in files.get("include", {}).items()]
        for relative, url, size in targets:
            total += fetch(os.path.join(assets, "models", "polyhaven", model, relative), url, size)
    for environment in ENVIRONMENTS:
        file = json.loads(get(f"https://api.polyhaven.com/files/{environment}"))["hdri"][RESOLUTION]["hdr"]
        total += fetch(os.path.join(assets, "environments", os.path.basename(file["url"])), file["url"], file["size"])
    print(f"{total} bytes downloaded into {assets}")

    converter = os.path.join(here, "..", "textures", "convert_gltf_textures.py")
    result = subprocess.run([sys.executable, converter, os.path.join(assets, "models", "polyhaven")])
    if result.returncode != 0:
        print("The textures were not converted to KTX2: the models will use their JPEG images.")


if __name__ == "__main__":
    main()
