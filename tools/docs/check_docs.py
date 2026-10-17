"""Runs Doxygen on the public headers and prints its warnings (milestone 8, part 9).

    python tools/docs/check_docs.py [HEADER...] [--output DIR]

Without headers: every warning, exit code 1 if there is any (what the moteur_docs target and the
verifier require). With headers (names like renderer.hpp): only the warnings about them, to work on
a few files at a time. DIR (default build/docs_check): where the reference and warnings.txt go; give
each concurrent run its own.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def doxygen():
    found = shutil.which("doxygen")
    for candidate in (found, r"C:\Program Files\doxygen\bin\doxygen.exe", "/opt/homebrew/bin/doxygen", "/usr/local/bin/doxygen"):
        if candidate and os.path.exists(candidate):
            return candidate
    raise SystemExit("doxygen not found (https://www.doxygen.nl, or winget install DimitriVanHeesch.Doxygen)")


def version():
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
        for line in f:
            if line.startswith("project(moteur VERSION"):
                return line.split()[2]
    return "0"


def main():
    args = sys.argv[1:]
    output = os.path.join(ROOT, "build", "docs_check")
    if "--output" in args:
        output = os.path.abspath(args[args.index("--output") + 1])
        del args[args.index("--output"):args.index("--output") + 2]
    os.makedirs(output, exist_ok=True)
    root = ROOT.replace("\\", "/")
    with open(os.path.join(ROOT, "tools", "docs", "Doxyfile.in"), encoding="utf-8") as f:
        config = f.read()
    config = (config.replace("@PROJECT_VERSION@", version()).replace("@MOTEUR_ROOT@", root)
              .replace("@MOTEUR_DOCS_OUTPUT@", output.replace("\\", "/")).replace("@MOTEUR_DOCS_WARN_AS_ERROR@", "NO"))
    doxyfile = os.path.join(output, "Doxyfile")
    with open(doxyfile, "w", encoding="utf-8") as f:
        f.write(config)
    result = subprocess.run([doxygen(), doxyfile], capture_output=True, text=True, encoding="utf-8", errors="replace")
    with open(os.path.join(output, "warnings.txt"), encoding="utf-8", errors="replace") as f:
        warnings = [line.rstrip() for line in f if line.strip()]
    warnings += [line for line in result.stderr.splitlines() if line.strip()]
    args = [os.path.basename(a) for a in args]  # names or paths
    if args:
        warnings = [w for w in warnings if any(f"/include/moteur/{name}:" in w.replace("\\", "/") for name in args)]
    for warning in warnings:
        print(warning.replace(root + "/src/moteur/include/", ""))
    print(f"{len(warnings)} warning(s)" + (f" for {', '.join(args)}" if args else ""))
    return 1 if warnings else 0


if __name__ == "__main__":
    sys.exit(main())
