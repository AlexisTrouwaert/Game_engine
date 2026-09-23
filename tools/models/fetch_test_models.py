"""Downloads the 3D test models into assets/models/polyhaven/ (about 11 MB, not kept in Git).

    python tools/models/fetch_test_models.py

All come from Poly Haven (https://polyhaven.com), under CC0: public domain, no attribution
required. Their authors are credited anyway, in assets/credits.json and in the "À propos" window of
the sandbox. Keep this list and that file in step.

The 1K glTF version of each model is fetched through the official API (api.polyhaven.com), which
asks clients to identify themselves with a User-Agent. Each file's size is checked against the one
the API announces. Files already present with the right size are not downloaded again.
"""

import json
import os
import urllib.request

MODELS = ["wine_barrel_01", "Lantern_01", "antique_estoc", "boulder_01"]
RESOLUTION = "1k"
USER_AGENT = {"User-Agent": "moteur-engine-test-assets/1.0"}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=USER_AGENT)) as response:
        return response.read()


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    base = os.path.normpath(os.path.join(here, "..", "..", "assets", "models", "polyhaven"))
    total = 0
    for model in MODELS:
        files = json.loads(get(f"https://api.polyhaven.com/files/{model}"))["gltf"][RESOLUTION]["gltf"]
        # The .gltf itself, then what it refers to, at the relative paths it uses.
        targets = [(os.path.basename(files["url"]), files["url"], files["size"])]
        targets += [(path, info["url"], info["size"]) for path, info in files.get("include", {}).items()]
        for relative, url, size in targets:
            path = os.path.join(base, model, relative)
            if os.path.exists(path) and os.path.getsize(path) == size:
                continue
            os.makedirs(os.path.dirname(path), exist_ok=True)
            data = get(url)
            if len(data) != size:
                raise RuntimeError(f"{url}: got {len(data)} bytes, expected {size}")
            with open(path, "wb") as f:
                f.write(data)
            total += len(data)
            print(f"{len(data):>9}  {os.path.relpath(path, base)}")
    print(f"{total} bytes downloaded into {base}")


if __name__ == "__main__":
    main()
