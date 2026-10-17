"""The measurement scenes of the engine (milestone 8, part 4), the same on Windows and macOS:

    python tools/measures/measure.py [BUILD_DIR] [--size W H] [--seconds S] [--output DIR]
                                     [--compare DIR] [--scenes fight,crowd,crossing,loading]

Each scene runs the sandbox in Release, without vsync, and writes its report (--report-json,
version 2):

- fight:    the slice, every creature (300) gathered around the hero who strikes without pause,
            30 fires and 32 lights (4 shadowed): the worst case of an ARPG fight;
- crowd:    800 animated characters at the game's zoom (--skinned 800);
- crossing: the hero walks across the whole slice map (culling, shadows, fog uncovering);
- loading:  title, loading, game, pause, title, ten times (load times, memory, leaks).

fight and crossing run a second time with --gpu-timing for the GPU time per pass (it slows the CPU
down: the CPU figures come from the first run). The reports land in --output (default
build/measures/<date>_<os>), with summary.json; --compare gives an older folder, and any CPU or GPU
figure more than 10 % worse is flagged (exit code 1). BUILD_DIR: build/windows-release or
build/macos-release. The budgets (moteur_doc/JALON_8_CONSOLIDATION.md, decisions) are checked on the
minimal machine (the Mac M3); elsewhere they are shown for information.
"""

import datetime
import json
import os
import platform
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WINDOWS = platform.system() == "Windows"

# Budgets of milestone 8 (minimal machine, crowded fight).
BUDGET_FRAME_P99_MS = 1000.0 / 60.0
BUDGET_MAIN_THREAD_MS = 8.0
BUDGET_PROCESS_MB = 2048.0
BUDGET_GPU_MB = 1536.0
BUDGET_ZONE_LOAD_MS = 5000.0


def run(exe, args, cwd):
    result = subprocess.run([exe] + args, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return result.returncode, result.stdout + result.stderr


def scenes(seconds, size):
    """Each scene: the sandbox's arguments, and whether it runs again for the GPU times."""
    window = ["--pixel-size", str(size[0]), str(size[1]), "--no-vsync"]
    return {
        "fight": (["--mesure", "combat", "--run-seconds", str(seconds)] + window, True),
        "crowd": (["--skinned", "800", "--run-seconds", str(seconds)] + window, False),
        "crossing": (["--mesure", "traversee", "--run-seconds", str(seconds * 2)] + window, True),
        "loading": (["--states", "--states-cycles", "10"] + window, False),
    }


def summary_of(report, gpu_report):
    zones = {z["name"]: z["mean"] for z in report.get("zones", [])}
    line = {
        "cpu_mean_ms": report["cpu_ms"]["mean"],
        "cpu_p99_ms": report["cpu_ms"]["p99"],
        "cpu_max_ms": report["cpu_ms"]["max"],
        "cpu_frames_over_33_ms": report["frames_over"]["cpu_33_3_ms"],
        "logic_ms": report["phases_ms"]["logic"],
        "record_ms": report["phases_ms"]["record"],
        "submit_ms": report["phases_ms"]["submit"],
        "process_peak_mb": report["memory_mib"]["process_peak"],
        "gpu_peak_mb": report["memory_mib"]["gpu_peak"],
        "draw_calls": report["per_frame"]["draw_calls"],
        "triangles": report["per_frame"]["triangles"],
        "meshes_prepare_ms": zones.get("meshes: prepare", 0.0),
    }
    if gpu_report and "gpu_ms" in gpu_report:
        line["gpu_total_ms"] = gpu_report["gpu_ms"]["total"]
        line["gpu_per_pass_ms"] = gpu_report["gpu_ms"]
    line.update({f"value_{k}": v for k, v in report.get("values", {}).items()})
    return line


def verdicts(name, line):
    """The budgets this scene can tell, each (text, held)."""
    checks = []
    if name in ("fight", "crowd", "crossing"):
        checks.append((f"CPU p99 {line['cpu_p99_ms']:.2f} ms <= {BUDGET_FRAME_P99_MS:.1f}", line["cpu_p99_ms"] <= BUDGET_FRAME_P99_MS))
        main = line["logic_ms"] + line["record_ms"] + line["submit_ms"]
        checks.append((f"main thread {main:.2f} ms <= {BUDGET_MAIN_THREAD_MS:.0f}", main <= BUDGET_MAIN_THREAD_MS))
        if "gpu_total_ms" in line:
            checks.append((f"GPU {line['gpu_total_ms']:.2f} ms <= {BUDGET_FRAME_P99_MS:.1f}", line["gpu_total_ms"] <= BUDGET_FRAME_P99_MS))
        checks.append((f"no frame over 33 ms ({line['cpu_frames_over_33_ms']})", line["cpu_frames_over_33_ms"] == 0))
    checks.append((f"process {line['process_peak_mb']:.0f} MB <= {BUDGET_PROCESS_MB:.0f}", line["process_peak_mb"] <= BUDGET_PROCESS_MB))
    checks.append((f"GPU {line['gpu_peak_mb']:.0f} MB <= {BUDGET_GPU_MB:.0f}", line["gpu_peak_mb"] <= BUDGET_GPU_MB))
    if name == "loading" and "value_slice_load_ms_max" in line:
        load = line["value_slice_load_ms_max"]
        checks.append((f"slice loading {load:.0f} ms <= {BUDGET_ZONE_LOAD_MS:.0f}", load <= BUDGET_ZONE_LOAD_MS))
        if "value_gpu_objects_after_cycle_1" in line:
            same = line["value_gpu_objects_after_cycle_1"] == line["value_gpu_objects_last_cycle"]
            checks.append(("GPU objects stable from one cycle to the next", same))
    return checks


def compare(current, older_folder):
    path = os.path.join(older_folder, "summary.json")
    if not os.path.exists(path):
        print(f"\nno summary.json in {older_folder}")
        return []
    with open(path, encoding="utf-8") as file:
        older = json.load(file)["scenes"]
    flagged = []
    for name, line in current.items():
        for key in ("cpu_mean_ms", "cpu_p99_ms", "gpu_total_ms", "meshes_prepare_ms"):
            if key in line and key in older.get(name, {}) and older[name][key] > 0.05:
                change = line[key] / older[name][key] - 1.0
                if change > 0.10:
                    flagged.append(f"{name} {key}: {older[name][key]:.3f} -> {line[key]:.3f} ms ({change:+.0%})")
    return flagged


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    args = sys.argv[1:]

    def option(name, count=1, default=None):
        if name in args:
            i = args.index(name)
            values = args[i + 1:i + 1 + count]
            del args[i:i + 1 + count]
            return values if count > 1 else values[0]
        return default

    size = [int(v) for v in option("--size", 2, ["1920", "1080"])]
    seconds = int(option("--seconds", 1, "30"))
    older = option("--compare")
    wanted = option("--scenes", 1, "fight,crowd,crossing,loading").split(",")
    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H%M")
    out = os.path.abspath(option("--output", 1, os.path.join(ROOT, "build", "measures", f"{stamp}_{platform.system().lower()}")))
    default = "build/windows-release" if WINDOWS else "build/macos-release"
    build = os.path.abspath(os.path.join(ROOT, args[0] if args else default))
    app_dir = os.path.join(build, "apps", "bac_a_sable")
    exe = os.path.join(app_dir, "bac_a_sable" + (".exe" if WINDOWS else ""))
    if not os.path.exists(exe):
        print(f"no program at {exe}: build the Release configuration first")
        return 2
    os.makedirs(out, exist_ok=True)

    lines = {}
    machine = None
    for name, (scene_args, with_gpu) in scenes(seconds, size).items():
        if name not in wanted:
            continue
        print(f"=== {name}", flush=True)
        report_path = os.path.join(out, f"{name}.json")
        code, text = run(exe, scene_args + ["--report-json", report_path], app_dir)
        if code != 0 or not os.path.exists(report_path):
            print(f"  FAILED (exit code {code}): " + " | ".join(text.splitlines()[-5:]))
            continue
        with open(report_path, encoding="utf-8") as file:
            report = json.load(file)
        machine = machine or {"engine": report["engine"], "gpu": report["gpu"], "platform": report["platform"],
                              "window_pixels": report["window_pixels"], "processor": platform.processor()}
        gpu_report = None
        if with_gpu:
            gpu_path = os.path.join(out, f"{name}_gpu.json")
            run(exe, scene_args + ["--gpu-timing", "--report-json", gpu_path], app_dir)
            if os.path.exists(gpu_path):
                with open(gpu_path, encoding="utf-8") as file:
                    gpu_report = json.load(file)
        line = summary_of(report, gpu_report)
        lines[name] = line
        print(f"  CPU mean {line['cpu_mean_ms']:.2f} ms, p99 {line['cpu_p99_ms']:.2f}, max {line['cpu_max_ms']:.2f}"
              + (f"; GPU {line['gpu_total_ms']:.2f} ms" if "gpu_total_ms" in line else "")
              + f"; process {line['process_peak_mb']:.0f} MB, GPU {line['gpu_peak_mb']:.0f} MB")
        for text_, held in verdicts(name, line):
            print(f"    {'ok      ' if held else 'EXCEEDED'} {text_}")

    with open(os.path.join(out, "summary.json"), "w", encoding="utf-8", newline="\n") as file:
        json.dump({"machine": machine, "size": size, "seconds": seconds, "scenes": lines}, file, indent=2,
                  ensure_ascii=False)
        file.write("\n")
    print(f"\nreports in {out}")
    if older:
        flagged = compare(lines, older)
        print("\nregressions of more than 10 %:" if flagged else "\nno regression of more than 10 %")
        for line in flagged:
            print("  " + line)
        return 1 if flagged else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
