"""Downloads the animated test characters into assets/models/characters/ (about 14 MB), not kept
in Git.

    python tools/models/fetch_test_characters.py

- KayKit (Kay Lousberg, https://kaylousberg.com), under CC0: the Knight of the "Adventurers"
  pack (the hero) and the Warrior and Minion of the "Skeletons" pack (the creatures), with a
  weapon of each pack in its own file (to attach to a hand, milestone 5 part 8), and each pack's
  LICENSE.txt. All share one 41-joint skeleton (same joint names, same order) and about 80 clips
  each (idle, walking, running, melee attacks, hits, deaths...), at 30 frames per second.
- Khronos glTF-Sample-Assets (https://github.com/KhronosGroup/glTF-Sample-Assets), small models
  made to test a glTF reader: SimpleSkin (CC0, 2 joints), InterpolationTest (CC0: STEP, LINEAR
  and CUBICSPLINE keys), RiggedFigure (CC-BY 4.0, Cesium) and Fox (CC0 model by PixelMannen,
  CC-BY 4.0 rig and animations by tomkranis, @AsoboStudio and @scurest). The CC-BY ones must be
  credited: they are, in assets/credits.json and the "À propos" window of the sandbox. Keep this
  list and that file in step.

Sources are pinned to a commit, and each download is checked against the SHA-256 of the file this
list was made with: a source that changed its file is reported rather than used. Files already
present with the right size are not downloaded again.

The KayKit textures are small palette images (1024 x 1024 PNG, about 15 KB), inside the .glb
files: they are not converted to KTX2 (tools/textures/convert_gltf_textures.py only handles
.gltf files with external images).
"""

import hashlib
import os
import sys
import urllib.request

USER_AGENT = {"User-Agent": "moteur-engine-test-assets/1.0"}

ADVENTURERS = ("https://raw.githubusercontent.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0/"
               "672074b73ba276876a19e8816ecdc5241817ab47/")
SKELETONS = ("https://raw.githubusercontent.com/KayKit-Game-Assets/KayKit-Character-Pack-Skeletons-1.0/"
             "15b62b9bad122f72926c10fb14d622c73819fa54/")
KHRONOS = ("https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/"
           "c6a6bd13ab2b3c685c7903d03561b8a9392f38b8/Models/")
A = ADVENTURERS + "addons/kaykit_character_pack_adventures/"
S = SKELETONS + "addons/kaykit_character_pack_skeletons/"

# (url, size, sha256, destination under assets/models/characters/)
FILES = [
    (ADVENTURERS + "LICENSE.txt", 891,
     "ae322141814056dda0deea7540d74c41d87aee1da319977cd1bd84ee5a923629", "kaykit_adventurers/LICENSE.txt"),
    (A + "Characters/gltf/Knight.glb", 3659532,
     "60428e3abc09ba83e595d256e3af8c5c976b46cdae599f0802fc82b4a3445168", "kaykit_adventurers/Knight.glb"),
    (A + "Assets/gltf/sword_1handed.gltf", 3080,
     "f88345d0c89d52710ddb00555964fe7a6f4024874d7ee902ee4c742c048a1958", "kaykit_adventurers/sword_1handed.gltf"),
    (A + "Assets/gltf/sword_1handed.bin", 13256,
     "780ebfd002cd181fd5b1626f5a6ae8dda0ff2df58c3ff1fd6e7584f53a969d8a", "kaykit_adventurers/sword_1handed.bin"),
    (A + "Assets/gltf/knight_texture.png", 14172,
     "5d250ccc5da020e6126bfa3839f83bd9a465a951ed223e4d13c08b1925e154d4", "kaykit_adventurers/knight_texture.png"),
    (SKELETONS + "LICENSE.txt", 914,
     "5d822abca4e08c5a91d329e5372b3dc605cba8d994f752cb0f7dfdb7a0a79954", "kaykit_skeletons/LICENSE.txt"),
    (S + "Characters/gltf/Skeleton_Warrior.glb", 4863620,
     "178b6fda810b814c250d8a2010c24dfd9b458b9006dd323353e620b7ff118bbe", "kaykit_skeletons/Skeleton_Warrior.glb"),
    (S + "Characters/gltf/Skeleton_Minion.glb", 4814296,
     "6ffc003f895bed0b074791e0e490846210a2e2f8fc7da300aba53cc185f95968", "kaykit_skeletons/Skeleton_Minion.glb"),
    (S + "Assets/gltf/Skeleton_Blade.gltf", 3047,
     "682a1df2eecb93dea35de628d6b93ca40d1b83586422356f3c1956741f2abf76", "kaykit_skeletons/Skeleton_Blade.gltf"),
    (S + "Assets/gltf/Skeleton_Blade.bin", 20056,
     "ccf4f6642130d11650400fa649669ccdb784280eef235fac3a12a197cbc7debe", "kaykit_skeletons/Skeleton_Blade.bin"),
    (S + "Assets/gltf/skeleton_texture.png", 17037,
     "15741a25c53e04fa9bf3beac3bc0de442359404b1ff9be863b892cb551ad3657", "kaykit_skeletons/skeleton_texture.png"),
    (KHRONOS + "SimpleSkin/glTF-Embedded/SimpleSkin.gltf", 3566,
     "7d0c3f48d0510d101269cb4fdd3ee035eaada0e04809a0899e0b4cfe8c38e68f", "khronos/SimpleSkin.gltf"),
    (KHRONOS + "InterpolationTest/glTF-Binary/InterpolationTest.glb", 7952,
     "a86eb331b4a083715e75fe19a1f747eac5692d5b9ff120f1eaa457c23ba72bca", "khronos/InterpolationTest.glb"),
    (KHRONOS + "RiggedFigure/glTF-Binary/RiggedFigure.glb", 50116,
     "d6be85417d3e256861ee733eea6916093a7af7c79c16366181fd8abcaeb38cf5", "khronos/RiggedFigure.glb"),
    (KHRONOS + "Fox/glTF-Binary/Fox.glb", 162852,
     "d97044e701822bac5a62696459b27d7b375aada5de8574ed4362edbba94771f7", "khronos/Fox.glb"),
]


def download(url, sha256):
    with urllib.request.urlopen(urllib.request.Request(url, headers=USER_AGENT)) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != sha256:
        raise RuntimeError(f"{url}: SHA-256 {digest}, expected {sha256} (the source changed its file)")
    return data


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    characters = os.path.normpath(os.path.join(here, "..", "..", "assets", "models", "characters"))
    total = 0
    failures = 0
    for url, size, sha256, destination in FILES:
        path = os.path.join(characters, destination)
        if os.path.exists(path) and os.path.getsize(path) == size:
            continue
        try:
            data = download(url, sha256)
        except Exception as e:  # keep going: the other sources may work
            print(f"error: {e}", file=sys.stderr)
            failures += 1
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        print(f"{len(data):>9}  {path}")
        total += len(data)
    print(f"{total} bytes downloaded into {characters}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
