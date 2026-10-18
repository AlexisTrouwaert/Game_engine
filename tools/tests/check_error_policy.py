"""Checks the error policy of the main loop (milestone 9, part 2) on the slice.

    python tools/tests/check_error_policy.py [BUILD_DIR]

BUILD_DIR: a development build (default build/windows-release, build/macos-release on macOS).

1. `tranche --test-plantage frame`: the first frame fails to prepare its meshes, as if a GPU staging
   buffer could not be mapped. The frame is drawn without them, the game goes on, and ends cleanly
   (exit code 0); the log says what was skipped.
2. `tranche --test-plantage update`: the game's update throws at its 30th tick, as a bug would. No
   exception may cross a frame: it is a controlled crash, with a report in crash_reports/ naming
   the exception, and exit code 1.

The slice leaves the player's display settings aside for a crash test, as for a replay; it runs
in a 640 x 360 window, with MOTEUR_CRASH_SILENT set (no message box).
The reports the check made are deleted. Exit code 0 when both hold.
"""

import glob
import os
import platform
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
# The windows of the programs this script runs open on the second screen (the first stays the
# developer's); one screen: the system's choice (ApplicationConfig::window_display).
os.environ.setdefault("MOTEUR_WINDOW_DISPLAY", "1")
WINDOWS = platform.system() == "Windows"


def preferences():
    if WINDOWS:
        return os.path.join(os.environ["APPDATA"], "moteur", "tranche")
    return os.path.expanduser("~/Library/Application Support/moteur/tranche")


def run(build, kind, seconds):
    folder = os.path.join(build, "apps", "tranche")
    exe = os.path.join(folder, "tranche" + (".exe" if WINDOWS else ""))
    environment = dict(os.environ, MOTEUR_CRASH_SILENT="1")
    process = subprocess.run([exe, "--run-seconds", str(seconds), "--pixel-size", "640", "360", "--test-plantage", kind],
                             cwd=folder, env=environment, capture_output=True, text=True, encoding="utf-8",
                             errors="replace", timeout=120)
    return process.returncode, process.stdout + process.stderr


def reports():
    return set(glob.glob(os.path.join(preferences(), "crash_reports", "*")))


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    build = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "build", ("windows" if WINDOWS else "macos") + "-release")
    failures = []

    code, output = run(build, "frame", 3)
    if code != 0:
        failures.append(f"frame failure: exit code {code} (expected 0: the game goes on)")
    if "meshes skipped this frame" not in output:
        failures.append("frame failure: the log does not say the meshes were skipped")
    print(f"frame failure: exit code {code}")

    before = reports()
    start = time.time()
    code, output = run(build, "update", 10)
    made = sorted(reports() - before)
    texts = [p for p in made if p.endswith(".txt")]
    if code != 1:
        failures.append(f"exception in update: exit code {code} (expected 1: a controlled crash)")
    if not texts:
        failures.append("exception in update: no report written")
    else:
        with open(texts[0], encoding="utf-8", errors="replace") as file:
            if "uncaught exception in Game::update" not in file.read():
                failures.append(f"exception in update: the report does not name the exception ({texts[0]})")
    print(f"exception in update: exit code {code}, {len(made)} report file(s), {time.time() - start:.1f} s")
    for path in made:
        os.remove(path)

    for failure in failures:
        print("FAILED: " + failure)
    print("error policy: " + ("ok" if not failures else "FAILED"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
