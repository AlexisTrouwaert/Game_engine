"""Static analysis of the engine with clang-tidy (milestone 8, part 8): the checks of .clang-tidy, on
every source of the engine (src/moteur), with the compile commands of a build.

    python tools/ci/static_analysis.py [BUILD_DIR] [--files PATTERN] [--all]

BUILD_DIR: a build with compile_commands.json (default build/<os>-release). --all also analyses
the slice and the tools (apps/). Prints the warnings grouped by check; exit code 1 if there is any.
clang-tidy: on the PATH, or the one Visual Studio installs (VC/Tools/Llvm).
"""

import concurrent.futures
import glob
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
import verify as verifier  # noqa: E402

WARNING = re.compile(r"^(.*?):(\d+):(\d+): (warning|error): (.*) \[([^\]]+)\]$")


def clang_tidy():
    found = shutil.which("clang-tidy")
    if found:
        return found
    for candidate in glob.glob(r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-tidy.exe"):
        return candidate
    raise SystemExit("clang-tidy introuvable")


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    args = sys.argv[1:]
    everything = "--all" in args
    args = [a for a in args if a != "--all"]
    pattern = None
    if "--files" in args:
        pattern = args[args.index("--files") + 1]
        del args[args.index("--files"):args.index("--files") + 2]
    build = os.path.abspath(args[0]) if args else os.path.join(ROOT, "build", f"{verifier.os_name()}-release")
    with open(os.path.join(build, "compile_commands.json"), encoding="utf-8") as file:
        commands = json.load(file)
    folders = [os.path.join(ROOT, "src", "moteur")] + ([os.path.join(ROOT, "apps")] if everything else [])
    sources = sorted({os.path.normpath(c["file"]) for c in commands
                      if c["file"].endswith(".cpp") and any(os.path.normpath(c["file"]).startswith(f) for f in folders)
                      and (pattern is None or re.search(pattern, c["file"]))})
    tidy = clang_tidy()
    environment = verifier.prepare_environment()

    def analyse(source):
        result = subprocess.run([tidy, "-p", build, "--quiet", source], capture_output=True, text=True,
                                encoding="utf-8", errors="replace", env=environment)
        return source, result.stdout

    # Only the project's code is judged: what clang-tidy says of the libraries' headers (vcpkg_installed:
    # ozz's SSE code written for MSVC, which clang does not compile; cgltf, stb) is counted apart.
    own = [os.path.join(ROOT, "src"), os.path.join(ROOT, "apps")]
    foreign = 0
    warnings = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for source, output in pool.map(analyse, sources):
            for line in output.splitlines():
                match = WARNING.match(line.strip())
                if match:
                    path, row, _, _, message, check = match.groups()
                    absolute = os.path.normpath(os.path.join(build, path)) if not os.path.isabs(path) else os.path.normpath(path)
                    if not any(absolute.lower().startswith(folder.lower()) for folder in own):
                        foreign += 1
                        continue
                    key = (absolute, int(row), check)
                    warnings[key] = message
    by_check = {}
    for (path, row, check), message in sorted(warnings.items()):
        by_check.setdefault(check, []).append(f"{os.path.relpath(path, ROOT)}:{row}: {message}")
    for check, lines in sorted(by_check.items(), key=lambda item: -len(item[1])):
        print(f"\n{check} ({len(lines)})")
        for line in lines:
            print("  " + line)
    print(f"\n{len(sources)} file(s) analysed, {len(warnings)} warning(s) in the project's code "
          f"({foreign} in the libraries' headers, not counted)")
    return 1 if warnings else 0


if __name__ == "__main__":
    sys.exit(main())
