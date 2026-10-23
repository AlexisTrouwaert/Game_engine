"""Checks a game's Windows package the way a player gets it (milestone 8, parts 10 and 12).

    python tools/package/check_package.py [BUILD_DIR] [--game NAME] [--keep]
        [--replay FILE --expect HASH] [--freeze-after TICK] [--run-seconds S] [--pixel-size W H]

BUILD_DIR: the shipping build (default build/windows-shipping) after `--target <game>_package`.
--game: the game's target (default tranche): its installer is packages/<game>-*-windows-x64-setup.exe.

1. The program: no Visual C++ runtime DLL among its dependencies (static CRT), its version
   information filled in.
2. The installer, run silently for the current user only (no administrator rights) into a
   temporary folder: the files are where they should be, the Start menu shortcut and the uninstall
   entry exist. Installed a second time over itself (an update).
3. The installed game, run:
   - with --replay FILE --expect HASH: on that recorded input (--replay-input), its capture
     (--capture, after --freeze-after TICK, --run-seconds S, --pixel-size W H) must have the hash
     HASH (the first 12 hex digits of its SHA-256, as in tests/data/references.json): same image
     from the cooked, packed assets as from the source folder of the development build;
   - without a replay: for --run-seconds S (default 3), it must exit cleanly (exit code 0).
   For the slice (--game tranche) the defaults are its own: tests/data/slice_replay.json, the
   reference capture "slice" of tests/data/references.json, frozen at tick 340, 9 seconds,
   1280 x 720. Each option given replaces its default.
4. The uninstaller: the folder, the shortcut and the uninstall entry are gone; the player's folder
   (saves, settings) is still there.
5. The portable archive: it holds the same files as the installation.
6. What the archives hold (milestone 9, part 4): no glTF model and no JSON document; the models are
   prepared (.mmdl) and the documents kept under their names are CBOR.

The player's own settings and key bindings are moved aside while the game runs (they change the
capture) and put back. Exit code 0 when everything holds, 2 for a wrong command line or no
installer. --keep leaves the installation in place.
"""

import argparse

import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys
import struct
import tempfile
import time
import zipfile
from compression import zstd

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
# The windows of the programs this script runs open on the second screen (the first stays the
# developer's); one screen: the system's choice (ApplicationConfig::window_display).
os.environ.setdefault("MOTEUR_WINDOW_DISPLAY", "1")
sys.path.insert(0, os.path.join(ROOT, "tools", "ci"))
import verify as verifier  # noqa: E402  (the Visual Studio environment, for dumpbin)

FORBIDDEN_DLLS = ("vcruntime", "msvcp", "api-ms-win-crt", "ucrtbase", "concrt")


def digest(path):
    if not os.path.exists(path):
        return "absent"
    with open(path, "rb") as file:
        return hashlib.sha256(file.read()).hexdigest()[:12]


def archive_entries(path):
    """The entries of a .pak (moteur_doc/FORMATS.md, 7): (path, the first bytes it gives back)."""
    with open(path, "rb") as file:
        data = file.read()
    if data[:4] != b"MPAK":
        raise ValueError(f"{path}: not an archive of the engine")
    count, index_size = struct.unpack_from("<II", data, 8)
    position, entries = 24, []
    for _ in range(count):
        (length,) = struct.unpack_from("<H", data, position)
        name = data[position + 2:position + 2 + length].decode("utf-8")
        offset, stored, _size = struct.unpack_from("<QQQ", data, position + 2 + length)
        compression = data[position + 26 + length]
        raw = data[offset:offset + stored]
        entries.append((name, zstd.decompress(raw)[:16] if compression == 1 else raw[:16]))
        position += 31 + length
    return entries


def installed_files(folder):
    return sorted(os.path.relpath(p, folder).replace("\\", "/")
                  for p in glob.glob(os.path.join(folder, "**", "*"), recursive=True)
                  if os.path.isfile(p) and not os.path.basename(p).startswith("unins"))


def uninstall_key_exists(app_name):
    import winreg
    path = r"Software\Microsoft\Windows\CurrentVersion\Uninstall"
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
        i = 0
        while True:
            try:
                sub = winreg.EnumKey(key, i)
            except OSError:
                return False
            i += 1
            if not sub.endswith("_is1"):
                continue
            with winreg.OpenKey(key, sub) as entry:
                try:
                    if winreg.QueryValueEx(entry, "DisplayName")[0] == app_name:
                        return True
                except OSError:
                    pass


def parse_arguments():
    """The command line, with the slice's run as the defaults of --game tranche."""
    parser = argparse.ArgumentParser(description="Checks a game's Windows package the way a player gets it.")
    parser.add_argument("build", nargs="?", default=os.path.join(ROOT, "build", "windows-shipping"),
                        help="the shipping build (default build/windows-shipping)")
    parser.add_argument("--game", default="tranche", help="the game's target (default tranche)")
    parser.add_argument("--keep", action="store_true", help="leave the installation in place")
    parser.add_argument("--replay", help="recorded input the installed game plays (--replay-input)")
    parser.add_argument("--expect", help="hash of the capture on that replay (12 hex digits of its SHA-256)")
    parser.add_argument("--freeze-after", type=int, help="tick after which the capture's scene is frozen")
    parser.add_argument("--run-seconds", type=float, help="how long the installed game runs")
    parser.add_argument("--pixel-size", type=int, nargs=2, metavar=("W", "H"), help="the window's size in pixels")
    args = parser.parse_args()
    if args.game == "tranche":  # the slice: its reference replay and capture (tools/tests/check_references.py)
        if args.replay is None:
            args.replay = os.path.join(ROOT, "tests", "data", "slice_replay.json")
            if args.expect is None:
                with open(os.path.join(ROOT, "tests", "data", "references.json"), encoding="utf-8") as file:
                    args.expect = json.load(file).get("windows", {}).get("slice")
        args.freeze_after = 340 if args.freeze_after is None else args.freeze_after
        args.run_seconds = 9 if args.run_seconds is None else args.run_seconds
        args.pixel_size = args.pixel_size or [1280, 720]
    if args.replay is not None and not args.expect:
        parser.error("--replay needs --expect HASH (the capture the replay must give)")
    if args.expect and args.replay is None:
        parser.error("--expect needs --replay FILE")
    if args.run_seconds is None:
        args.run_seconds = 3  # no replay: a few seconds, then a clean exit
    return args


def main():
    args = parse_arguments()
    keep = args.keep
    game = args.game
    build = os.path.abspath(args.build)
    packages = os.path.join(build, "packages")
    setups = sorted(glob.glob(os.path.join(packages, f"{game}-*-windows-x64-setup.exe")), key=os.path.getmtime)
    if not setups:
        print(f"no installer in {packages}: build the target {game}_package of the shipping configuration")
        return 2
    setup = setups[-1]
    stem = os.path.basename(setup)[:-len("-setup.exe")]
    portable = os.path.join(packages, stem + ".zip")
    exe_name = game + ".exe"
    failures = []

    def check(condition, what):
        print(f"  {'ok' if condition else 'FAILED'}  {what}")
        if not condition:
            failures.append(what)

    print(f"package {stem}")
    # 1. The program.
    program = os.path.join(packages, "symbols", stem, exe_name)
    environment = verifier.prepare_environment()
    dumpbin = shutil.which("dumpbin", path=environment.get("PATH") or environment.get("Path")) or "dumpbin"
    result = subprocess.run([dumpbin, "/dependents", program], env=environment, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    dlls = [line.strip().lower() for line in result.stdout.splitlines() if line.strip().lower().endswith(".dll")]
    check(result.returncode == 0 and dlls, f"dependencies read ({', '.join(dlls)})")
    check(not [d for d in dlls if d.startswith(FORBIDDEN_DLLS)], "no Visual C++ runtime DLL (static CRT)")
    check(os.path.exists(os.path.join(packages, "symbols", stem, game + ".pdb")), "symbols kept (PDB)")

    # 2. The installer, for the current user only, twice (the second time: an update over the first).
    target = tempfile.mkdtemp(prefix="moteur_install_")
    folder = os.path.join(target, "jeu installé")  # an accented path with a space, like many players'
    preferences = os.path.join(os.environ["APPDATA"], "moteur", game)
    aside = []
    for name in ("display.json", "demo3d_bindings.json"):
        path = os.path.join(preferences, name)
        if os.path.exists(path):
            shutil.move(path, path + ".aside")
            aside.append(path)
    try:
        for attempt in (1, 2):
            log = os.path.join(target, f"install_{attempt}.log")
            code = subprocess.run([setup, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
                                   f"/DIR={folder}", f"/LOG={log}"]).returncode
            check(code == 0, f"installation {attempt} (exit code {code})")
        files = installed_files(folder)
        check(exe_name in files, "program installed")
        check(any(f.startswith("archives/") and f.endswith(".pak") for f in files), "archives installed")
        check(any(f.startswith("licenses/") for f in files), "license texts installed")
        # 6. Only prepared formats in the archives.
        packed = [entry for f in files if f.startswith("archives/") and f.endswith(".pak")
                  for entry in archive_entries(os.path.join(folder, f))]
        models = [name for name, _ in packed if name.lower().endswith((".gltf", ".glb"))]
        documents = [name for name, head in packed if name.lower().endswith(".json") and not (head and 0x80 <= head[0] <= 0xBF)]
        check(not models, f"no glTF model in the archives {models[:3]}")
        check(not documents, f"no JSON document in the archives (CBOR) {documents[:3]}")
        check(not any(f.startswith("assets/") or f.startswith("shaders/") for f in files),
              "no loose assets or shaders (everything is packed)")
        check(os.path.exists(os.path.join(folder, "unins000.exe")), "uninstaller present")
        programs = os.path.join(os.environ["APPDATA"], "Microsoft", "Windows", "Start Menu", "Programs")
        shortcuts = glob.glob(os.path.join(programs, "*.lnk"))
        app_names = [os.path.splitext(os.path.basename(s))[0] for s in shortcuts]
        version_info = subprocess.run(["powershell", "-NoProfile", "-Command",
                                       f"(Get-Item -LiteralPath '{os.path.join(folder, exe_name)}').VersionInfo.ProductName"],
                                      capture_output=True, text=True, encoding="utf-8", errors="replace").stdout.strip()
        check(bool(version_info), f"version information ({version_info})")
        check(version_info in app_names, "Start menu shortcut")
        check(uninstall_key_exists(version_info), "uninstall entry (Settings > Apps)")

        # 3. The installed game: on its replay (the capture compared), or a few seconds and a clean exit.
        command = [os.path.join(folder, exe_name), "--run-seconds", f"{args.run_seconds:g}"]
        if args.pixel_size:
            command += ["--pixel-size", str(args.pixel_size[0]), str(args.pixel_size[1])]
        capture = os.path.join(target, "capture.png")
        if args.replay is not None:
            # In French, the language of the reference capture, whatever the machine's.
            command += ["--language", "fr", "--replay-input", os.path.abspath(args.replay), "--capture", capture]
        if args.freeze_after is not None:
            command += ["--freeze-after", str(args.freeze_after)]
        run = subprocess.run(command, cwd=target, capture_output=True, text=True, encoding="utf-8", errors="replace")
        check(run.returncode == 0, f"installed game ran (exit code {run.returncode})")
        good = run.returncode == 0
        if args.replay is not None:
            found = digest(capture)
            check(found == args.expect, f"capture of the installed game {found}, expected {args.expect}")
            good = good and found == args.expect
        if not good:
            print("\n".join(run.stdout.splitlines()[-15:]))

        # 5. The portable archive: the same files.
        with zipfile.ZipFile(portable) as archive:
            names = sorted(n.split("/", 1)[1] for n in archive.namelist() if not n.endswith("/") and "/" in n)
        check(names == files, f"portable archive holds the installed files ({len(names)})")

        # 4. The uninstaller.
        os.makedirs(preferences, exist_ok=True)
        marker = os.path.join(preferences, "check_package_marker.txt")
        with open(marker, "w", encoding="utf-8") as file:
            file.write("a player's file: must survive the uninstallation\n")
        if not keep:
            code = subprocess.run([os.path.join(folder, "unins000.exe"), "/VERYSILENT", "/SUPPRESSMSGBOXES",
                                   "/NORESTART"]).returncode
            check(code == 0, f"uninstallation (exit code {code})")
            for _ in range(60):  # the uninstaller finishes from a copy of itself
                if not os.path.exists(folder):
                    break
                time.sleep(0.5)
            check(not os.path.exists(folder), "installation folder removed")
            check(not os.path.exists(os.path.join(programs, version_info + ".lnk")), "Start menu shortcut removed")
            check(not uninstall_key_exists(version_info), "uninstall entry removed")
            check(os.path.exists(marker), "the player's folder kept")
        os.remove(marker)
    finally:
        for path in aside:
            shutil.move(path + ".aside", path)
        if not keep:
            shutil.rmtree(target, ignore_errors=True)
        else:
            print(f"installation kept in {folder}")

    print("\nFAILED:\n  " + "\n  ".join(failures) if failures else "\npackage ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
