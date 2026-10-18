"""The engine's verification, the same steps a hosted CI will run (milestone 8, part 2), in one command:

    python tools/ci/verify.py [--quick] [--configs debug,release] [--no-references] [--no-msl]
                              [--no-example] [--no-analysis] [--no-asan] [--no-tsan] [--no-docs] [--no-package]
                              [--keep-going]

For each configuration (default: debug and release; --quick: release only), on this OS:

1. configure with the CMake preset of the repository (<os>-<config>), warnings as errors;
2. build everything;
3. run the unit tests (ctest);
4. check the data tables (--check-data) and compile them into a temporary folder (--compile-data);

then, once:

5. examples/jeu_minimal configured, built and run from the outside of the engine, with its own
   presets, as a game with the engine in a git submodule (skipped by --no-example);
6. Windows: export the MSL shaders and fail if they differ from the committed ones (a shader changed
   without exporting its MSL breaks the Mac build; skipped by --no-msl);
7. the reference captures, the exactness test of the saves and the editor's self-test
   (tools/tests/check_references.py on the release build), then the error policy of the main loop
   (tools/tests/check_error_policy.py: a GPU failure inside a frame, an exception in the game's
   update); skipped by --no-references;
8. the static analysis of the project's code (tools/ci/static_analysis.py, clang-tidy with
   .clang-tidy, on the release build): any warning fails (skipped by --no-analysis);
9. the unit tests built with AddressSanitizer (preset <os>-asan) and run (skipped by --no-asan); on
   macOS, also with ThreadSanitizer (preset macos-tsan: the frame's jobs; skipped by --no-tsan);
10. the API reference: Doxygen on the public headers, any warning fails (tools/docs/check_docs.py;
    skipped by --no-docs);
11. the shipping configuration (preset <os>-shipping) built with the slice's package (installer and
    portable archive on Windows, .app and .dmg on macOS); on Windows the package is installed for
    the current user, played on the slice's replay (reference capture), uninstalled
    (tools/package/check_package.py). Skipped by --no-package and by --quick.

The script prepares its environment itself: on Windows, the Visual Studio developer environment
(found with vswhere) and CMake / Ninja (CLion's, the ones that share the build folders, else the
PATH's); everywhere, vcpkg from MOTEUR_VCPKG_ROOT, CLion's vcpkg (~/.vcpkg-clion/vcpkg) or
VCPKG_ROOT, in that order (Visual Studio's developer shell sets VCPKG_ROOT to its own copy, which
cannot build this project). On Windows every command runs in a hidden console of its own, so the
script gives the same builds from a terminal, an IDE or a program without a console (see
own_console). Other scripts import it for that environment (prepare_environment(), command()).

Exit code 0 when every step passed. --keep-going goes on after a failure (to see them all).
"""

import glob
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WINDOWS = platform.system() == "Windows"
MACOS = platform.system() == "Darwin"
EXE = ".exe" if WINDOWS else ""


def os_name():
    return "windows" if WINDOWS else "macos" if MACOS else "linux"


# --- environment ---------------------------------------------------------------------------------

def visual_studio_environment():
    """The environment of a Visual Studio x64 developer prompt, as a dict."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        raise SystemExit("vswhere.exe not found: install Visual Studio 2022 with the C++ desktop workload")
    path = subprocess.run([vswhere, "-latest", "-products", "*", "-requires",
                           "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                          capture_output=True, text=True).stdout.strip()
    vcvars = os.path.join(path, "VC", "Auxiliary", "Build", "vcvars64.bat")
    if not os.path.exists(vcvars):
        raise SystemExit(f"vcvars64.bat not found under {path}")
    output = subprocess.run(f'cmd /s /c ""{vcvars}" >nul && set"', capture_output=True, text=True,
                            encoding="mbcs", errors="replace", shell=True).stdout
    environment = {}
    for line in output.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            environment[key] = value
    return environment


def find_tool(name, environment):
    """The folder to put first on the PATH for `name` (CMake, Ninja), or None to keep the PATH's.

    CLion's own CMake and Ninja come first: the presets' build folders (build/<preset>) are shared
    with CLion, and a folder must always be configured and built by the same CMake and Ninja. The
    Visual Studio environment puts older ones on the PATH (CMake 3.31, Ninja 1.12): Ninja 1.12 does
    not read the log of Ninja 1.13 and rewrites it, after which everything is built again."""
    roots = glob.glob(r"C:\Program Files\JetBrains\CLion*\bin") if WINDOWS else \
        glob.glob("/Applications/CLion*.app/Contents/bin")
    for root in sorted(roots, reverse=True):
        for sub in (["cmake", "win", "x64", "bin"], ["ninja", "win", "x64"]) if WINDOWS else \
                (["cmake", "mac", "bin"], ["ninja", "mac"]):
            folder = os.path.join(root, *sub)
            if os.path.exists(os.path.join(folder, name + EXE)):
                return folder
    if shutil.which(name, path=environment.get("PATH") or environment.get("Path")):
        return None
    raise SystemExit(f"{name} not found (CLion's, or on the PATH)")


def vcpkg_root():
    for candidate in (os.environ.get("MOTEUR_VCPKG_ROOT"), os.path.expanduser("~/.vcpkg-clion/vcpkg"),
                      os.environ.get("VCPKG_ROOT")):
        if candidate and os.path.exists(os.path.join(candidate, "scripts", "buildsystems", "vcpkg.cmake")):
            return candidate
    raise SystemExit("vcpkg not found: set MOTEUR_VCPKG_ROOT")


def own_console():
    """Windows: what makes a command run in a console of its own, hidden (keyword arguments of Popen).

    A French MSVC writes "Remarque : inclusion du fichier :" with a no-break space whose byte depends
    on where its output goes: the console's code page (the system's OEM page, 850 here, in a terminal
    or CLion), or UTF-8 when the program has no console at all (the script started by an IDE, a
    scheduler or an agent). CMake records the prefix it saw when it configured; built under the
    other one, Ninja no longer recognises the lines and silently records no header dependencies
    (stale objects after a header change). Every command of the script, configuration included,
    therefore gets a console of its own: always the system's code page, whoever started the script,
    the same as in a terminal and in CLion, and without touching the caller's console (chcp did)."""
    if not WINDOWS:
        return {}
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0  # SW_HIDE
    return {"creationflags": subprocess.CREATE_NEW_CONSOLE, "startupinfo": startup}


def prepare_environment():
    environment = visual_studio_environment() if WINDOWS else dict(os.environ)
    key = next((k for k in environment if k.upper() == "PATH"), "PATH")
    for tool in ("cmake", "ninja"):
        folder = find_tool(tool, {"PATH": environment.get(key, "")})
        if folder:
            environment[key] = folder + os.pathsep + environment.get(key, "")
    environment["VCPKG_ROOT"] = vcpkg_root()
    # The programs the checks run open their windows on the second screen (the first one stays the
    # developer's); a machine with one screen falls back to it (ApplicationConfig::window_display).
    environment.setdefault("MOTEUR_WINDOW_DISPLAY", "1")
    return environment


# --- steps ---------------------------------------------------------------------------------------

class Steps:
    def __init__(self, keep_going):
        self.results = []
        self.keep_going = keep_going

    def run(self, name, function):
        print(f"\n=== {name}", flush=True)
        start = time.perf_counter()
        try:
            ok, detail = function()
        except Exception as error:  # a step that crashes is a failed step, with its reason
            ok, detail = False, f"{type(error).__name__}: {error}"
        seconds = time.perf_counter() - start
        self.results.append((name, ok, detail, seconds))
        print(f"--- {name}: {'ok' if ok else 'FAILED'} ({seconds:.0f} s){' - ' + detail if detail else ''}", flush=True)
        if not ok and not self.keep_going:
            raise StopIteration
        return ok

    def summary(self):
        print("\nSummary")
        for name, ok, detail, seconds in self.results:
            print(f"  {'ok    ' if ok else 'FAILED'} {name:<44} {seconds:6.0f} s" + (f"  {detail}" if not ok and detail else ""))
        return all(ok for _, ok, _, _ in self.results)


def command(args, environment, cwd=ROOT, tail=40):
    """Runs a command, its output shown live; returns (exit code == 0, last lines on failure)."""
    path = next((v for k, v in environment.items() if k.upper() == "PATH"), None)
    executable = shutil.which(args[0], path=path) or args[0]  # Windows looks in the caller's PATH otherwise
    process = subprocess.Popen([executable] + args[1:], cwd=cwd, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding="utf-8", errors="replace", **own_console())
    lines = []
    for line in process.stdout:
        sys.stdout.write(line)
        lines.append(line.rstrip())
    process.wait()
    if process.returncode == 0:
        return True, ""
    return False, f"exit code {process.returncode}: " + " | ".join(lines[-tail:])[-600:]


def program(build, name):
    return os.path.join(build, "apps", "bac_a_sable", name + EXE)


def configuration_steps(steps, environment, config):
    preset = f"{os_name()}-{config}"
    build = os.path.join(ROOT, "build", preset)
    steps.run(f"{preset}: configure", lambda: command(
        ["cmake", "--preset", preset, "-DMOTEUR_WARNINGS_AS_ERRORS=ON"], environment))
    steps.run(f"{preset}: build", lambda: command(["cmake", "--build", "--preset", preset], environment))
    if WINDOWS:
        steps.run(f"{preset}: header dependencies", lambda: header_dependencies_step(environment, build))
    steps.run(f"{preset}: unit tests", lambda: command(
        ["ctest", "--test-dir", build, "--output-on-failure"], environment))
    sandbox = program(build, "bac_a_sable")
    steps.run(f"{preset}: --check-data", lambda: command([sandbox, "--check-data"], environment,
                                                         cwd=os.path.dirname(sandbox)))
    compiled = tempfile.mkdtemp(prefix="moteur_mdat_")
    steps.run(f"{preset}: --compile-data", lambda: command([sandbox, "--compile-data", compiled], environment,
                                                           cwd=os.path.dirname(sandbox)))
    return build


def build_ninja(environment, build):
    """The Ninja that builds this folder (CMAKE_MAKE_PROGRAM of its cache): any other one could
    rewrite its log in another version's format."""
    cache = os.path.join(build, "CMakeCache.txt")
    if os.path.exists(cache):
        with open(cache, encoding="utf-8", errors="replace") as file:
            for line in file:
                if line.startswith("CMAKE_MAKE_PROGRAM:"):
                    return line.split("=", 1)[1].strip()
    path = next((v for k, v in environment.items() if k.upper() == "PATH"), None)
    return shutil.which("ninja", path=path)


def objects_without_dependencies(environment, build):
    output = subprocess.run([build_ninja(environment, build), "-C", build, "-t", "deps"], capture_output=True, text=True,
                            encoding="utf-8", errors="replace").stdout
    return [line.split(":")[0] for line in output.splitlines()
            if "#deps 0" in line and ".cpp.obj" in line and "build_info.cpp" not in line]


def header_dependencies_step(environment, build):
    """Every object compiled from a C++ source of the project must have its headers recorded.

    The builds run in a console of their own (see own_console), so the prefix MSVC writes matches the
    one CMake recorded. Objects left without dependencies all the same (a build of that folder made
    elsewhere, under another code page) are deleted and built again, up to three times; the step
    fails only if some still have none: then the folder was configured under another code page than
    the system's, and must be configured again from scratch (delete it)."""
    repaired = 0
    for _ in range(3):
        missing = objects_without_dependencies(environment, build)
        if not missing:
            return True, (f"{repaired} object(s) compiled again to get their dependencies back" if repaired else "")
        for obj in missing:
            target = os.path.join(build, obj)
            if os.path.exists(target):
                os.remove(target)
        repaired += len(missing)
        ok, detail = command(["cmake", "--build", build], environment)
        if not ok:
            return False, detail
    missing = objects_without_dependencies(environment, build)
    if missing:
        return False, (f"{len(missing)} object(s) without header dependencies: the folder was configured under "
                       f"another code page than the system's; delete it and run again ({', '.join(missing[:5])})")
    return True, f"{repaired} object(s) compiled again to get their dependencies back"


def example_steps(steps, environment, config):
    """examples/jeu_minimal built from the outside, as a game consumes the engine (part 3)."""
    folder = os.path.join(ROOT, "examples", "jeu_minimal")
    preset = f"{os_name()}-{config}"
    build = os.path.join(folder, "build", preset)
    steps.run(f"external example {preset}: configure", lambda: command(
        ["cmake", "--preset", preset, "-DMOTEUR_WARNINGS_AS_ERRORS=ON"], environment, cwd=folder))
    steps.run(f"external example {preset}: build", lambda: command(
        ["cmake", "--build", build], environment, cwd=folder))
    exe = os.path.join(build, "jeu_minimal" + EXE)
    steps.run(f"external example {preset}: run 2 s", lambda: command(
        [exe, "--run-seconds", "2"], environment, cwd=build))


def asan_steps(steps, environment):
    """The unit tests under AddressSanitizer (milestone 8, part 8): their own build, warnings as errors."""
    preset = f"{os_name()}-asan"
    build = os.path.join(ROOT, "build", preset)
    steps.run(f"{preset}: configure", lambda: command(
        ["cmake", "--preset", preset, "-DMOTEUR_WARNINGS_AS_ERRORS=ON"], environment))
    steps.run(f"{preset}: build the tests", lambda: command(
        ["cmake", "--build", build, "--target", "moteur_tests"], environment))
    if WINDOWS:
        steps.run(f"{preset}: header dependencies", lambda: header_dependencies_step(environment, build))
    steps.run(f"{preset}: unit tests", lambda: command(
        ["ctest", "--test-dir", build, "--output-on-failure"], environment))


def tsan_steps(steps, environment):
    """The unit tests under ThreadSanitizer (milestone 9, part 9: the frame's work over the cores).
    Clang only: macOS (MSVC has no ThreadSanitizer)."""
    preset = "macos-tsan"
    build = os.path.join(ROOT, "build", preset)
    steps.run(f"{preset}: configure", lambda: command(
        ["cmake", "--preset", preset, "-DMOTEUR_WARNINGS_AS_ERRORS=ON"], environment))
    steps.run(f"{preset}: build the tests", lambda: command(
        ["cmake", "--build", build, "--target", "moteur_tests"], environment))
    steps.run(f"{preset}: unit tests", lambda: command(
        ["ctest", "--test-dir", build, "--output-on-failure"], environment))


def package_steps(steps, environment):
    """The shipping configuration and the slice's package, checked as a player gets it."""
    preset = f"{os_name()}-shipping"
    build = os.path.join(ROOT, "build", preset)
    steps.run(f"{preset}: configure", lambda: command(
        ["cmake", "--preset", preset, "-DMOTEUR_WARNINGS_AS_ERRORS=ON"], environment))
    steps.run(f"{preset}: build the slice's package", lambda: command(
        ["cmake", "--build", build, "--target", "tranche_package"], environment))
    if WINDOWS:
        script = os.path.join(ROOT, "tools", "package", "check_package.py")
        steps.run(f"{preset}: install, play, uninstall", lambda: command([sys.executable, script, build], environment))


def tree_digest(folder):
    digests = {}
    for path in sorted(glob.glob(os.path.join(folder, "*"))):
        with open(path, "rb") as file:
            digests[os.path.basename(path)] = hashlib.sha256(file.read()).hexdigest()
    return digests


def msl_step(environment, build):
    folder = os.path.join(ROOT, "shaders", "generated", "msl")
    before = tree_digest(folder)
    ok, detail = command(["cmake", "--build", build, "--target", "export_msl_shaders"], environment)
    if not ok:
        return ok, detail
    after = tree_digest(folder)
    changed = sorted(name for name in set(before) | set(after) if before.get(name) != after.get(name))
    if changed:
        return False, "MSL differs from the committed one (now regenerated: commit it): " + ", ".join(changed)
    return True, ""


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    arguments = sys.argv[1:]
    quick = "--quick" in arguments
    configs = ["release"] if quick else ["debug", "release"]
    if "--configs" in arguments:
        configs = arguments[arguments.index("--configs") + 1].split(",")
    steps = Steps("--keep-going" in arguments)
    environment = prepare_environment()
    print(f"vcpkg: {environment['VCPKG_ROOT']}")
    try:
        builds = {config: configuration_steps(steps, environment, config) for config in configs}
        reference_build = builds.get("release")
        if "--no-example" not in arguments:
            example_steps(steps, environment, configs[-1])
        if WINDOWS and "--no-msl" not in arguments and reference_build:
            steps.run("exported MSL up to date", lambda: msl_step(environment, reference_build))
        if reference_build and "--no-references" not in arguments:
            script = os.path.join(ROOT, "tools", "tests", "check_references.py")
            args = [sys.executable, script, reference_build] + (["--quick"] if quick else [])
            steps.run("reference captures, exactness, editor", lambda: command(args, environment))
        if reference_build and "--no-references" not in arguments:
            script = os.path.join(ROOT, "tools", "tests", "check_error_policy.py")
            steps.run("error policy (frame failure, exception in update)",
                      lambda: command([sys.executable, script, reference_build], environment))
        if reference_build and "--no-analysis" not in arguments:
            script = os.path.join(ROOT, "tools", "ci", "static_analysis.py")
            steps.run("static analysis (clang-tidy)", lambda: command([sys.executable, script, reference_build], environment))
        if "--no-asan" not in arguments:
            asan_steps(steps, environment)
        if not WINDOWS and "--no-tsan" not in arguments:
            tsan_steps(steps, environment)
        if "--no-docs" not in arguments:
            script = os.path.join(ROOT, "tools", "docs", "check_docs.py")
            steps.run("API reference (Doxygen)", lambda: command([sys.executable, script], environment))
        if not quick and "--no-package" not in arguments:
            package_steps(steps, environment)
    except StopIteration:
        pass
    return 0 if steps.summary() else 1


if __name__ == "__main__":
    sys.exit(main())
