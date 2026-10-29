"""Alternate native showcase binaries, retain all frames, compare immutable RGBA.

Uses pui_vulkan_native_opt3_bench (the unchanged actual showcase workload).
Factory-only environment controls belong to this benchmark, never the example.
"""
from pathlib import Path
import argparse
import csv
import hashlib
import io
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import time

CONTROLS = ("PUFFERUI_TEST_COVERAGE_BYTES", "PUFFERUI_TEST_LEGACY_VERTICES")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def distribution(values):
    values = sorted(values)
    return dict(mean=statistics.mean(values), p50=values[(len(values)-1)//2],
                p95=values[int((len(values)-1)*.95)],
                p99=values[int((len(values)-1)*.99)], maximum=values[-1])


def parse_run(stdout, stderr, *, require_optimization=False):
    rows = list(csv.DictReader(io.StringIO(stdout)))
    if [int(row["frame"]) for row in rows] != list(range(180)):
        raise ValueError("Expected every frame 0..179 exactly once")
    values = [float(row["total_ms"]) for row in rows]
    if any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("Invalid frame timing")
    final = re.findall(r"gpu_total_ms=([\d.]+) gpu_samples=(\d+) frames=180", stderr)
    if len(final) != 1:
        raise ValueError("Missing unambiguous completed GPU total")
    gpu_total, intervals = float(final[0][0]), int(final[0][1])
    if not math.isfinite(gpu_total) or gpu_total < 0 or not intervals:
        raise ValueError("Invalid completed GPU telemetry")
    telemetry = re.findall(r"optimization_stats ([^\n]+)", stderr)
    if len(telemetry) > 1:
        raise ValueError("Timed workload must use exactly one device")
    counters = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", telemetry[0])} if telemetry else {}
    required = {"hits", "misses", "fills", "coverage_draws", "coverage_bytes", "evictions", "image_bytes"}
    if require_optimization and not required.issubset(counters):
        raise ValueError("Expected the native optimization benchmark and its complete telemetry")
    return rows, dict(wall=distribution(values), cold_ms=values[0],
                      steady=distribution(values[1:]), gpu_ms=gpu_total/180,
                      gpu_intervals=intervals, peak_frame=max(range(180), key=values.__getitem__),
                      peak_upload_bytes=max(int(row["resident_bytes"]) for row in rows),
                      optimization=counters)


def environment(controls):
    result = os.environ.copy()
    for name in CONTROLS:
        result.pop(name, None)
    result.update(controls)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--spiral-reference", type=Path, required=True)
    parser.add_argument("--baseline-receipt", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=6)
    args = parser.parse_args()
    if not 2 <= args.runs <= 20:
        parser.error("Use 2..20 alternating repetitions")
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    baseline = json.loads(args.baseline_receipt.read_text())
    existing = baseline["existing_tests"]
    for name, expected in existing.items():
        if digest(root/name) != expected:
            raise RuntimeError("Existing test/oracle changed: "+name)
    sources = {path.relative_to(root).as_posix(): digest(path)
               for directory in ["include", "src", "examples", "tests", "cmake", "tools"]
               for path in (root/directory).rglob("*")
               if path.is_file() and path.suffix in [".h", ".cpp", ".inl", ".vert", ".frag", ".glsl", ".py", ".ps1", ".cmake"]
               and "__pycache__" not in path.parts}
    for path in [root/"CMakeLists.txt", root/"CMakePresets.json", root/".gitattributes",
                 root/"src/shaders/vulkan/manifest.json", *sorted((root/"assets/fonts").glob("*.ttf"))]:
        sources[path.relative_to(root).as_posix()] = digest(path)
    binaries = {}
    for name, source in [("before", args.before), ("after", args.after)]:
        target = out/(name+source.suffix)
        shutil.copy2(source, target)
        binaries[name] = target
    references = {"spiral": args.spiral_reference.resolve()}
    reference_hashes = {}
    for scenario, reference in references.items():
        reference_hashes[scenario] = {f"frame-{frame:03}.rgba": digest(reference/f"frame-{frame:03}.rgba") for frame in range(180)}
        with (out/(scenario+"-before-reference.log")).open("w") as log:
            subprocess.run([str(binaries["before"]), "--reference", str(reference)], cwd=root,
                           env=environment({}), stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=180)
    cases = [("before", "before", {}), ("after", "after", {}),
             ("after-no-coverage", "after", {"PUFFERUI_TEST_COVERAGE_BYTES": "0"}),
             ("after-legacy-no-coverage", "after", {"PUFFERUI_TEST_COVERAGE_BYTES": "0", "PUFFERUI_TEST_LEGACY_VERTICES": "1"})]
    runs, all_rows = [], []
    for repeat in range(args.runs):
        for scenario in references:
            for staged in [False, True]:
                for label, side, controls in (cases if repeat % 2 == 0 else list(reversed(cases))):
                    stem = f"{scenario}-{label}-{'staged' if staged else 'direct'}-{repeat}"
                    command = [str(binaries[side]), "--bench", *(["--staged"] if staged else [])]
                    started = time.perf_counter()
                    with (out/(stem+".csv")).open("w") as stdout, (out/(stem+".log")).open("w") as stderr:
                        subprocess.run(command, cwd=root, env=environment(controls),
                                       stdout=stdout, stderr=stderr, check=True, timeout=180)
                    elapsed = (time.perf_counter()-started)*1000
                    rows, summary = parse_run((out/(stem+".csv")).read_text(), (out/(stem+".log")).read_text(),
                                              require_optimization=side == "after")
                    runs.append(dict(scenario=scenario, label=label, staged=staged, repeat=repeat,
                                     command=command, controls=controls, process_ms=elapsed, **summary))
                    all_rows.extend(dict(scenario=scenario, label=label, staged=int(staged), repeat=repeat, **row) for row in rows)
                    print(stem, "mean", summary["wall"]["mean"], "max", summary["wall"]["maximum"], flush=True)
    checks = []
    for scenario, reference in references.items():
        for staged in [False, True]:
            for cache in [False, True]:
                for label, side, controls in cases[1:]:
                    stem = f"{scenario}-{label}-staged{int(staged)}-cache{int(cache)}"
                    command = [str(binaries[side]), "--reference", str(reference),
                               *(["--staged"] if staged else []), *(["--cache"] if cache else [])]
                    with (out/(stem+".golden.log")).open("w") as log:
                        subprocess.run(command, cwd=root, env=environment(controls),
                                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
                    checks.append(dict(scenario=scenario, label=label, staged=staged, cache=cache,
                                       exact_frames=180, all_timed_peaks_covered=True, command=command))
    fields = list(dict.fromkeys(key for row in all_rows for key in row))
    with (out/"frames.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(all_rows)
    for name, expected in sources.items():
        if digest(root/name) != expected:
            raise RuntimeError("Measured source changed: "+name)
    for scenario, hashes in reference_hashes.items():
        for name, expected in hashes.items():
            if digest(references[scenario]/name) != expected:
                raise RuntimeError("Immutable reference changed")
    receipt = dict(protocol="Actual 180-frame 5000->5003 Spiral; alternating; offscreen; no timed readbacks; cold/outliers retained",
                   foreground_certified=False, baseline_commit=baseline["commit"],
                   binaries={key: dict(path=str(path), sha256=digest(path)) for key, path in binaries.items()},
                   source_hashes=sources, existing_tests=existing, reference_hashes=reference_hashes,
                   runs=runs, exact_checks=checks, exact_frames=sum(row["exact_frames"] for row in checks))
    (out/"receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")


if __name__ == "__main__":
    main()
