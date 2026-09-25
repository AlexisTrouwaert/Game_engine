"""Downloads the test sounds and musics into assets/audio/ (about 11 MB), not kept in Git.

    python tools/audio/fetch_test_sounds.py

All are under CC0 (public domain, no attribution required); their authors are credited anyway, in
assets/credits.json and in the "À propos" window of the sandbox. Keep this list and that file in
step.

- Kenney (https://kenney.nl): "Impact Sounds" (footsteps on stone, metal impacts) and "Interface
  Sounds" (clicks), OGG Vorbis. Only a few files of each pack are kept, with the pack's License.txt.
- OpenGameArt (https://opengameart.org): two musics and an ambience (MP3, as published) and a
  burning fire (WAV), used as a sound placed in the world.

Each download is checked against the SHA-256 of the file this list was made with: a source that
changed its file is reported rather than used. Files already present are not downloaded again.
"""

import hashlib
import io
import os
import sys
import urllib.request
import zipfile

USER_AGENT = {"User-Agent": "moteur-engine-test-assets/1.0"}

# (url, sha256, {file in the zip: destination under assets/audio/}) for the zips,
# (url, sha256, destination) for single files.
PACKS = [
    ("https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip",
     "029d734af1582474edf3a694d1b0cebc97c1c152f2f39fa34d4c2bafc5de77f8",
     {**{f"Audio/footstep_concrete_00{i}.ogg": f"kenney_impact/footstep_concrete_00{i}.ogg" for i in range(5)},
      **{f"Audio/impactMetal_light_00{i}.ogg": f"kenney_impact/impactMetal_light_00{i}.ogg" for i in range(5)},
      "License.txt": "kenney_impact/License.txt"}),
    ("https://kenney.nl/media/pages/assets/interface-sounds/fa43c1dd4d-1677589452/kenney_interface-sounds.zip",
     "f2193d072726d6758a5f7871b2dcc54dcce0d5c35c6f0a62f92549b327c81232",
     {"Audio/click_001.ogg": "kenney_interface/click_001.ogg",
      "Audio/confirmation_001.ogg": "kenney_interface/confirmation_001.ogg",
      "Audio/back_001.ogg": "kenney_interface/back_001.ogg",
      "Audio/error_001.ogg": "kenney_interface/error_001.ogg",
      "License.txt": "kenney_interface/License.txt"}),
]
FILES = [
    ("https://opengameart.org/sites/default/files/the_field_of_dreams.mp3",
     "103a7032a49be7e8399c5cb771f7759eac9ac1a0d2bf227f41fff42ad8d78194", "music/the_field_of_dreams.mp3"),
    ("https://opengameart.org/sites/default/files/TownTheme.mp3",
     "2657861d5107d4a3c01ef81cb6a4d61ddd5e7a054b6da57e658373d79d0c3466", "music/town_theme.mp3"),
    ("https://opengameart.org/sites/default/files/Forgoten_tombs_1.mp3",
     "989b26d4c6ec9fb90da2c9c892a396920e1b2e14a426052574310e275ebd01d0", "ambience/forgotten_tombs.mp3"),
    ("https://opengameart.org/sites/default/files/fire-1.wav",
     "26702de8a195bcbafeae72034861d875a1b7917168a25106f4e1957ecdddf7e5", "effects/fire_1.wav"),
]


def download(url, sha256):
    with urllib.request.urlopen(urllib.request.Request(url, headers=USER_AGENT)) as response:
        data = response.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != sha256:
        raise RuntimeError(f"{url}: SHA-256 {digest}, expected {sha256} (the source changed its file)")
    return data


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    print(f"{len(data):>9}  {path}")
    return len(data)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    audio = os.path.normpath(os.path.join(here, "..", "..", "assets", "audio"))
    total = 0
    failures = 0
    for url, sha256, members in PACKS:
        if all(os.path.exists(os.path.join(audio, d)) for d in members.values()):
            continue
        try:
            archive = zipfile.ZipFile(io.BytesIO(download(url, sha256)))
        except Exception as e:  # keep going: the other sources may work
            print(f"error: {e}", file=sys.stderr)
            failures += 1
            continue
        for member, destination in members.items():
            total += write(os.path.join(audio, destination), archive.read(member))
    for url, sha256, destination in FILES:
        path = os.path.join(audio, destination)
        if os.path.exists(path):
            continue
        try:
            total += write(path, download(url, sha256))
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            failures += 1
    print(f"{total} bytes downloaded into {audio}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
