"""Fuzzing of the engine's file readers (milestone 8, part 8): each program of tests/fuzz runs for a
while on a corpus seeded with the project's own files; whatever crashes it is kept.

    python tools/fuzz/fuzzer.py [--seconds S] [--targets map,effect,...] [--parallel] [BUILD_DIR]

BUILD_DIR: the fuzzing build (cmake --preset windows-fuzz / macos-fuzz; default build/<os>-fuzz).
Corpus and findings land in build/fuzz/<target>/ (corpus/ grows from run to run). A finding
(crash-..., timeout-..., oom-...) must become a case of tests/test_robustness.cpp once fixed.
A reader killed before libFuzzer could keep its input (a stack overflow) is reported too.
Exit code 1 if anything was found.
"""

import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "ci"))
import verify as verifier  # noqa: E402  (the same environment as the builds: MSVC, the sanitizers' runtime)

# Each reader and the files its corpus starts from.
SEEDS = {
    "map": ["assets/maps/*.json"],
    "effect": ["assets/effects/*.json"],
    "animation": ["assets/animations/*.json"],
    "save": ["tests/data/*.sav", "build/fuzz_seeds/*.sav"],
    "ktx2": ["assets/models/polyhaven/**/*.ktx2"],
    "gltf": ["assets/models/*.glb", "assets/models/characters/kaykit_skeletons/Skeleton_Minion.glb"],
    "table": ["build/fuzz_seeds/*.mdat"],
    "bindings": ["assets/input/*.json"],
    "image": ["assets/particles/*.png"],
    "archive": ["build/fuzz_seeds/*.pak"],
    "model": [".imported/models/*.mmdl", ".imported/models/characters/kaykit_skeletons/Skeleton_Minion.glb.mmdl"],
    "registry": ["build/fuzz_seeds/*.mreg", "assets/models/*.meta"],
    "document": ["build/fuzz_seeds/*.json", "assets/effects/*.json"],
    "prefab": ["../ARPG/assets/models/props/*.prefab.json"],
}


def seed_files():
    """A save, compiled tables and an archive, made by the release build for the corpus."""
    folder = os.path.join(ROOT, "build", "fuzz_seeds")
    os.makedirs(folder, exist_ok=True)
    release = os.path.join(ROOT, "build", f"{verifier.os_name()}-release")
    exe = os.path.join(release, "apps", "bac_a_sable", "bac_a_sable" + verifier.EXE)
    if os.path.exists(exe) and not glob.glob(os.path.join(folder, "*.mdat")):
        subprocess.run([exe, "--compile-data", folder + os.sep], cwd=os.path.dirname(exe), capture_output=True)
    cook = os.path.join(release, "tools", "cook", "moteur_cook" + verifier.EXE)
    if os.path.exists(cook) and not glob.glob(os.path.join(folder, "*.pak")):
        out = os.path.join(folder, "cook")
        subprocess.run([cook, "--sources", os.path.join(ROOT, "examples", "jeu_minimal", "assets"), "--manifest",
                        os.path.join(ROOT, "examples", "jeu_minimal", "assets", "livraison.json"), "--output", out],
                       capture_output=True)
        for pak in glob.glob(os.path.join(out, "archives", "*.pak")):
            shutil.copy(pak, folder)
        # The frozen registry and the documents in their shipped form (CBOR).
        for name in ["asset_registry.mreg"] + [os.path.relpath(p, os.path.join(out, "assets"))
                                               for p in glob.glob(os.path.join(out, "assets", "**", "*.json"), recursive=True)]:
            source = os.path.join(out, "assets", name)
            if os.path.exists(source):
                shutil.copy(source, os.path.join(folder, os.path.basename(name)))


def main():
    args = sys.argv[1:]
    seconds = 120
    parallel = "--parallel" in args
    args = [a for a in args if a != "--parallel"]
    if "--seconds" in args:
        seconds = int(args[args.index("--seconds") + 1])
        del args[args.index("--seconds"):args.index("--seconds") + 2]
    wanted = list(SEEDS)
    if "--targets" in args:
        wanted = args[args.index("--targets") + 1].split(",")
        del args[args.index("--targets"):args.index("--targets") + 2]
    unknown = [name for name in wanted if name not in SEEDS]
    if unknown:
        print(f"unknown target(s): {', '.join(unknown)} (known: {', '.join(SEEDS)})")
        return 2
    build = os.path.abspath(args[0]) if args else os.path.join(ROOT, "build", f"{verifier.os_name()}-fuzz")
    environment = verifier.prepare_environment()
    seed_files()

    def run(name):
        """One reader's run: (line to print, finding or None)."""
        exe = os.path.join(build, "tests", "fuzz", f"fuzz_{name}" + verifier.EXE)
        if not os.path.exists(exe):
            return f"{name}: no program ({exe})", None
        work = os.path.join(ROOT, "build", "fuzz", name)
        corpus = os.path.join(work, "corpus")
        os.makedirs(corpus, exist_ok=True)
        for pattern in SEEDS[name]:
            for path in glob.glob(os.path.join(ROOT, pattern), recursive=True):
                if os.path.getsize(path) < 4 * 1024 * 1024:
                    shutil.copy(path, corpus)
        before = set(os.listdir(work))
        result = subprocess.run([exe, corpus, f"-max_total_time={seconds}", "-timeout=10", "-rss_limit_mb=4096",
                                 "-max_len=1048576", f"-artifact_prefix={work}{os.sep}", "-print_final_stats=1"],
                                cwd=work, env=environment, capture_output=True, text=True, encoding="utf-8", errors="replace")
        new = sorted(f for f in set(os.listdir(work)) - before if f.split("-")[0] in ("crash", "timeout", "oom", "leak", "slow"))
        runs = next((line.split(":")[-1].strip() for line in result.stderr.splitlines() if "stat::number_of_executed_units" in line), "?")
        corpus_size = len(os.listdir(corpus))
        # A reader killed before libFuzzer could keep its input (a stack overflow, mostly) leaves no
        # file and no final statistics: a finding all the same, the input is lost but the log names it.
        if not new and (result.returncode != 0 or runs == "?"):
            new = [f"abnormal end (exit code {result.returncode}, no file kept)"]
        line = (f"{name:<11} {runs:>11} runs  corpus {corpus_size:>5}  "
                + (f"FOUND: {', '.join(new)}" if new else "nothing"))
        return line, ((name, new, result.stderr[-3000:]) if new else None)

    # --parallel: every reader at once, one process each (a long run of several hours fits in one
    # night); otherwise one after the other.
    findings = []
    if parallel:
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(wanted)) as pool:
            results = list(pool.map(run, wanted))
    else:
        results = []
        for name in wanted:
            results.append(run(name))
            print(results[-1][0], flush=True)
    for line, finding in results:
        if parallel:
            print(line, flush=True)
        if finding:
            findings.append(finding)
    for name, files, log in findings:
        print(f"\n=== {name}: {files}\n{log}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
