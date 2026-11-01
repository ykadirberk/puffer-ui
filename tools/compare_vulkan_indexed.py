"""Compare native indexed-draw changes with all original tests and frames fixed.

Use frozen pui_vulkan_native_spiral_tests binaries and a pre-edit receipt with
tests, drafts and direct/staged reference hashes. No capture is overwritten.
"""
from pathlib import Path
import argparse
import csv
import json
import shutil
import statistics
import subprocess
import time

from compare_vulkan_native_optimizations import digest, environment, parse_run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--runs", default=6, type=int)
    args = parser.parse_args()
    if not 2 <= args.runs <= 20:
        parser.error("Use 2..20 alternating repetitions")
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    baseline = json.loads(args.baseline.read_text())
    protected = {**baseline["tests"], **baseline["drafts"]}

    def check_hashes(hashes):
        for name, expected in hashes.items():
            if digest(root / name) != expected:
                raise RuntimeError("Measured source/test/oracle/draft changed: " + name)

    check_hashes(protected)
    sources = {p.relative_to(root).as_posix(): digest(p)
               for directory in ["include", "src", "examples", "tests", "tools", "cmake"]
               for p in (root / directory).rglob("*")
               if p.is_file() and p.suffix in
               [".h", ".inl", ".cpp", ".vert", ".frag", ".glsl", ".py", ".ps1", ".cmake"]
               and "__pycache__" not in p.parts}
    for name in ["CMakeLists.txt", "CMakePresets.json", "src/shaders/vulkan/manifest.json"]:
        sources[name] = digest(root / name)
    for path in sorted((root / "assets/fonts").glob("*.ttf")):
        sources[path.relative_to(root).as_posix()] = digest(path)
    binaries = {}
    for side, original in [("before", args.before), ("after", args.after)]:
        binaries[side] = out / (side + original.suffix)
        shutil.copy2(original, binaries[side])
    if digest(binaries["before"]) != baseline["binary"]["sha256"]:
        raise RuntimeError("Before binary is not the frozen source baseline")
    reference = args.reference.resolve()
    reference_hashes = {f"frame-{f:03}.rgba": digest(reference / f"frame-{f:03}.rgba")
                        for f in range(180)}
    if reference_hashes != baseline["references"]["direct"]:
        raise RuntimeError("Reference is not the immutable full-frame baseline")
    child_environment = environment({})
    for name in ["PUFFERUI_VULKAN_VALIDATION", "VK_INSTANCE_LAYERS"]:
        child_environment.pop(name, None)
    runs, frames = [], []
    for repeat in range(args.runs):
        for mode in (["direct", "staged"] if repeat % 2 == 0 else ["staged", "direct"]):
            for side in (["before", "after"] if repeat % 2 == 0 else ["after", "before"]):
                stem = f"{mode}-{side}-{repeat}"
                command = [str(binaries[side]), "--bench",
                           *(["--staged"] if mode == "staged" else [])]
                start = time.perf_counter()
                with (out / (stem + ".csv")).open("w") as stdout, \
                        (out / (stem + ".log")).open("w") as stderr:
                    subprocess.run(command, cwd=root, env=child_environment, stdout=stdout,
                                   stderr=stderr, check=True, timeout=180)
                rows, summary = parse_run((out / (stem + ".csv")).read_text(),
                                          (out / (stem + ".log")).read_text())
                summary["peak_upload_capacity_bytes"] = summary.pop("peak_upload_bytes")
                summary["peak_frame_upload_bytes"] = max(int(r["upload_bytes"]) for r in rows)
                if any(int(r["draw_buffer_allocations"]) for r in rows):
                    raise RuntimeError("Unexpected allocation in the timed drawing path")
                runs.append(dict(mode=mode, side=side, repeat=repeat, command=command,
                                 process_ms=(time.perf_counter() - start) * 1000, **summary))
                frames.extend(dict(mode=mode, side=side, repeat=repeat, **r) for r in rows)
                print(stem, summary["wall"], flush=True)
    for mode in ["direct", "staged"]:
        for repeat in range(args.runs):
            pair = {side: [r for r in frames if r["mode"] == mode and
                           r["repeat"] == repeat and r["side"] == side]
                    for side in ["before", "after"]}
            if [r["upload_bytes"] for r in pair["before"]] != \
                    [r["upload_bytes"] for r in pair["after"]]:
                raise RuntimeError("Original geometry/upload workload changed")
    checks = []
    for mode in ["direct", "staged"]:
        for side, cache in [("before", False), ("after", False), ("after", True)]:
            command = [str(binaries[side]), "--reference", str(reference),
                       *(["--staged"] if mode == "staged" else []),
                       *(["--cache"] if cache else [])]
            with (out / f"{mode}-{side}-cache{int(cache)}.golden.log").open("w") as log:
                subprocess.run(command, cwd=root, env=child_environment, stdout=log,
                               stderr=subprocess.STDOUT, check=True, timeout=180)
            checks.append(dict(mode=mode, side=side, cache=cache, exact_frames=180,
                               all_timed_peaks_covered=True, command=command))
    check_hashes({**protected, **sources})
    if {name: digest(reference / name) for name in reference_hashes} != reference_hashes:
        raise RuntimeError("Reference changed during measurement")
    with (out / "frames.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(frames[0]))
        writer.writeheader()
        writer.writerows(frames)
    summary = {}
    for mode in ["direct", "staged"]:
        groups = {side: [r for r in runs if r["mode"] == mode and r["side"] == side]
                  for side in ["before", "after"]}
        summary[mode] = {side: dict(mean_ms=statistics.median(r["wall"]["mean"] for r in group),
                                   p99_ms=statistics.median(r["wall"]["p99"] for r in group),
                                   max_ms=max(r["wall"]["maximum"] for r in group),
                                   gpu_ms=statistics.median(r["gpu_ms"] for r in group),
                                   cold_ms=statistics.median(r["cold_ms"] for r in group))
                         for side, group in groups.items()}
        paired = {side: {r["repeat"]: r for r in group} for side, group in groups.items()}
        summary[mode]["paired_wall_percent"] = statistics.median(
            (paired["after"][i]["wall"]["mean"] / paired["before"][i]["wall"]["mean"] - 1) * 100
            for i in range(args.runs))
    receipt = dict(protocol="Actual 180-frame 5000->5003 Spiral; alternating; no timed readbacks; all cold/outliers retained",
                   foreground_certified=False, baseline_commit=baseline["commit"],
                   binaries={s: dict(path=str(p), sha256=digest(p)) for s, p in binaries.items()},
                   source_hashes=sources, unchanged_tests=baseline["tests"],
                   protected_drafts=baseline["drafts"], reference_hashes=reference_hashes,
                   runs=runs, exact_checks=checks, summary=summary,
                   exact_frames=sum(c["exact_frames"] for c in checks))
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
