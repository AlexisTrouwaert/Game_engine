"""The reference checks of the engine (milestone 7, part 11), in one command:

    python tools/tests/check_references.py [BUILD_DIR] [--record] [--quick]

- the reference captures (2D demo, 3D demo, its replay, the playable slice twice), compared with
  tests/data/references.json for this platform;
- the exactness test of the saves: the slice saved after tick 120 (and 250), then loaded, goes on
  exactly as the slice never interrupted (the three captures A, B and C must be the same);
- the map editor's self-test (edits, undo, objects, saving, a file changed elsewhere, "Essayer").

BUILD_DIR: the build to test (default: build/windows-release, or build/macos-release on a Mac).
--record writes the hashes found as this platform's references (after checking the change is
wanted). --quick skips the second save point and the editor. The player's key bindings of the 3D
demo are moved aside while it runs (they change the help line of the captures). Exit code 0 when
everything matches.
"""

import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REFERENCES = os.path.join(ROOT, "tests", "data", "references.json")


def platform_key():
    return {"Windows": "windows", "Darwin": "macos"}.get(platform.system(), platform.system().lower())


def preferences_directory(application):
    """Where SDL_GetPrefPath("moteur", application) points."""
    if platform.system() == "Windows":
        return os.path.join(os.environ.get("APPDATA", ""), "moteur", application)
    if platform.system() == "Darwin":
        return os.path.expanduser(os.path.join("~/Library/Application Support/moteur", application))
    return os.path.expanduser(os.path.join("~/.local/share/moteur", application))


def digest(path):
    if not os.path.exists(path):
        return "absent"
    with open(path, "rb") as file:
        return hashlib.sha256(file.read()).hexdigest()[:12]


def run(exe, args, cwd):
    result = subprocess.run([exe] + args, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return result.returncode, result.stdout + result.stderr


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    record = "--record" in sys.argv
    quick = "--quick" in sys.argv
    default = "build/windows-release" if platform.system() == "Windows" else "build/macos-release"
    build = os.path.abspath(os.path.join(ROOT, args[0] if args else default))
    app_dir = os.path.join(build, "apps", "bac_a_sable")
    suffix = ".exe" if platform.system() == "Windows" else ""
    exe = os.path.join(app_dir, "bac_a_sable" + suffix)
    editor = os.path.join(app_dir, "editeur" + suffix)
    if not os.path.exists(exe):
        print(f"no program at {exe}: build it first")
        return 2

    with open(REFERENCES, encoding="utf-8") as file:
        references = json.load(file)
    expected = references.get(platform_key(), {})
    out = tempfile.mkdtemp(prefix="moteur_references_")
    replay_slice = os.path.join(ROOT, "tests", "data", "slice_replay.json")
    replay_demo = os.path.join(ROOT, "tests", "data", "demo3d_replay.json")

    bindings = os.path.join(preferences_directory("bac_a_sable"), "demo3d_bindings.json")
    aside = bindings + ".aside"
    if os.path.exists(bindings):
        shutil.move(bindings, aside)
    found, failures = {}, []
    try:
        captures = {
            "2d": ["--demo", "--seed", "42", "--freeze-after", "30", "--no-input", "--run-seconds", "3"],
            "demo3d": ["--demo3d", "--seed", "42", "--freeze-after", "60", "--no-input", "--run-seconds", "3"],
            "demo3d_replay": ["--demo3d", "--seed", "42", "--freeze-after", "60", "--run-seconds", "3",
                              "--replay-input", replay_demo],
            "slice": ["--states", "--replay-input", replay_slice, "--freeze-after", "340", "--run-seconds", "9",
                      "--pixel-size", "1280", "720"],
        }
        for name, extra in captures.items():
            runs = 2 if name == "slice" else 1
            for k in range(runs):
                path = os.path.join(out, f"{name}_{k}.png")
                run(exe, extra + ["--capture", path], app_dir)
                hash_ = digest(path)
                print(f"{name:<16} {hash_}" + (f" (run {k + 1})" if runs > 1 else ""))
                if k == 0:
                    found[name] = hash_
                elif hash_ != found[name]:
                    failures.append(f"{name}: two runs differ ({found[name]}, {hash_})")
            if name in expected and found[name] != expected[name]:
                failures.append(f"{name}: {found[name]}, reference {expected[name]}")
            elif name not in expected:
                print(f"  (no reference for {platform_key()}: --record to keep {found[name]})")

        # Exactness: uninterrupted (A), saved (B) and loaded (C) give the same capture.
        common = ["--seed", "42", "--replay-input", replay_slice, "--freeze-after", "400", "--run-seconds", "12",
                  "--pixel-size", "1280", "720", "+set", "effects.enabled", "0"]
        a = os.path.join(out, "exact_a.png")
        run(exe, ["--slice"] + common + ["--capture", a], app_dir)
        for tick in [120] if quick else [120, 250]:
            save = os.path.join(out, f"exact_{tick}.sav")
            b = os.path.join(out, f"exact_b_{tick}.png")
            c = os.path.join(out, f"exact_c_{tick}.png")
            run(exe, ["--slice"] + common + ["--save-at", str(tick), save, "--capture", b], app_dir)
            run(exe, ["--load", save, "--replay-start", str(tick)] + common + ["--capture", c], app_dir)
            hashes = (digest(a), digest(b), digest(c))
            same = len(set(hashes)) == 1 and hashes[0] != "absent"
            print(f"exactness @{tick:<5} A {hashes[0]}  B {hashes[1]}  C {hashes[2]}  {'ok' if same else 'DIFFERENT'}")
            if not same:
                failures.append(f"exactness after tick {tick}: {hashes}")
        found["exactness"] = digest(a)
        if "exactness" in expected and found["exactness"] != expected["exactness"]:
            failures.append(f"exactness capture: {found['exactness']}, reference {expected['exactness']}")

        if not quick and os.path.exists(editor):
            code, text = run(editor, ["tranche", "--self-test", "--run-seconds", "60"], app_dir)
            passed = code == 0 and "editor self-test: ok" in text
            print(f"editor self-test {'ok' if passed else 'FAILED'}")
            if not passed:
                failures.append("editor self-test: " + "; ".join(l for l in text.splitlines() if "FAILED" in l)[:400])
    finally:
        if os.path.exists(aside):
            shutil.move(aside, bindings)

    if record:
        references[platform_key()] = found
        with open(REFERENCES, "w", encoding="utf-8", newline="\n") as file:
            json.dump(references, file, indent=2, ensure_ascii=False)
            file.write("\n")
        print(f"references of {platform_key()} written to {REFERENCES}")
    if failures:
        print("\nFAILED:")
        for failure in failures:
            print("  " + failure)
        return 1
    print("\nall references match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
