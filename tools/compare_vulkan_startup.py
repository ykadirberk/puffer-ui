"""Retain startup, unchanged late-hint Spiral and null-renderer comparisons.

Both directories contain native/startup/showcase-test executables. Build the
startup baseline from frozen old sources with PUFFERUI_STARTUP_BASELINE; that
macro only reproduces the old missing bootstrap callback. No golden is written.
"""
from pathlib import Path
import argparse
import csv
import io
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import time

from compare_vulkan_native_optimizations import digest, environment


def log_number(log, name):
    matches = re.findall(r"(?:^|\s)" + re.escape(name) + r"=([\d.]+)", log)
    if len(matches) != 1:
        raise ValueError("Expected exactly one log field: " + name)
    value = float(matches[0])
    if not math.isfinite(value) or value < 0:
        raise ValueError("Invalid log field: " + name)
    return value


def parse_timing(stdout, stderr, *, startup=False, native=True):
    rows = list(csv.DictReader(io.StringIO(stdout)))
    if [int(row["frame"]) for row in rows] != list(range(180)):
        raise ValueError("Expected all 180 ordered frames")
    values = [float(row["total_ms"]) for row in rows]
    if any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("Invalid frame timing")
    summary = dict(mean_ms=statistics.mean(values), p99_ms=sorted(values)[177],
                   cold_ms=values[0], frame1_ms=values[1], max_ms=max(values),
                   max_frame=values.index(max(values)))
    if native:
        summary["gpu_ms"] = log_number(stderr, "gpu_total_ms") / 180
        if not log_number(stderr, "gpu_samples"):
            raise ValueError("Missing completed GPU intervals")
    if startup:
        setup = log_number(stderr, "setup_ms")
        summary.update(setup_ms=setup, preparation_ms=log_number(stderr, "preparation_ms"),
                       setup_frame0_ms=setup + values[0],
                       setup_first_two_ms=setup + values[0] + values[1])
    return rows, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--runs", type=int, default=8)
    args = parser.parse_args()
    if not 2 <= args.runs <= 20:
        parser.error("Use 2..20 repetitions")
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    baseline = json.loads(args.baseline.read_text())
    protected = {**{p: h for p, h in baseline["tracked"].items()
                    if p.startswith(("tests/", "assets/fonts/", "src/shaders/"))},
                 **baseline["drafts"]}

    def verify(hashes):
        for name, expected in hashes.items():
            if digest(root / name) != expected:
                raise RuntimeError("Protected input changed: " + name)

    verify(protected)
    frozen = {p: h for p, h in baseline["tracked"].items()
              if p.startswith(("include/", "examples/"))}
    for name, expected in frozen.items():
        if digest(args.before / name) != expected:
            raise RuntimeError("Frozen before source changed: " + name)
    build = json.loads((args.before / "startup-build.json").read_text())
    if build["source_sha256"] != digest(root / "tests/test_vulkan_startup_spiral.cpp"):
        raise RuntimeError("Startup before/after workloads differ")
    source_hashes = {p.relative_to(root).as_posix(): digest(p)
                     for directory in ["include", "src", "examples", "tests", "tools"]
                     for p in (root / directory).rglob("*")
                     if p.is_file() and p.suffix in [".h", ".inl", ".cpp", ".py", ".ps1"]}
    binaries = {}
    for side, directory in [("before", args.before), ("after", args.after)]:
        target = out / side
        target.mkdir()
        binaries[side] = {}
        for name in ["pui_vulkan_startup_spiral_tests", "pui_vulkan_native_spiral_tests", "pui_showcase_tests"]:
            source = directory / (name + (".exe" if os.name == "nt" else ""))
            destination = target / source.name
            shutil.copy2(source, destination)
            binaries[side][name] = destination
    for name in ["pui_vulkan_native_spiral_tests", "pui_showcase_tests"]:
        if digest(binaries["before"][name]) != baseline["binaries"][name]:
            raise RuntimeError("Wrong frozen before binary: " + name)
    if digest(binaries["before"]["pui_vulkan_startup_spiral_tests"]) != build["binary_sha256"]:
        raise RuntimeError("Wrong frozen-source startup binary")
    reference = args.reference.resolve()
    references = {f"frame-{f:03d}.rgba": digest(reference / f"frame-{f:03d}.rgba") for f in range(180)}
    if references != baseline["references"]["direct"]:
        raise RuntimeError("Wrong immutable reference")
    env = environment({})
    for name in ["PUFFERUI_VULKAN_VALIDATION", "VK_INSTANCE_LAYERS"]:
        env.pop(name, None)
    runs, frames = [], []
    scenarios = ["startup", "late", "preparation-control"]

    def command(scenario, mode, side):
        if scenario == "null":
            return [str(binaries[side]["pui_showcase_tests"]), "--bench", "null", "--tiles", "5000"]
        name = "pui_vulkan_native_spiral_tests" if scenario == "late" else "pui_vulkan_startup_spiral_tests"
        binary_side = "after" if scenario == "preparation-control" else side
        return [str(binaries[binary_side][name]), "--bench",
                *(["--legacy"] if scenario == "preparation-control" and side == "before" else []),
                *(["--staged"] if mode == "staged" else [])]

    def timed(scenario, mode, side, repeat):
        stem = f"{scenario}-{mode}-{side}-{repeat}"
        cmd = command(scenario, mode, side)
        start = time.perf_counter()
        with (out / (stem + ".csv")).open("w") as stdout, (out / (stem + ".log")).open("w") as stderr:
            subprocess.run(cmd, cwd=root, env=env, stdout=stdout, stderr=stderr, check=True, timeout=180)
        elapsed = (time.perf_counter() - start) * 1000
        rows, summary = parse_timing((out / (stem + ".csv")).read_text(),
                                    (out / (stem + ".log")).read_text(),
                                    startup=scenario in ["startup", "preparation-control"],
                                    native=scenario != "null")
        if scenario in ["startup", "preparation-control"]:
            log = (out / (stem + ".log")).read_text()
            summary["adapter"] = {key: int(log_number(log, key)) for key in ["vendor", "device", "driver"]}
            if log_number(log, "prepared") != (side == "after"):
                raise RuntimeError("Wrong preparation mode: " + stem)
        runs.append(dict(scenario=scenario, mode=mode, side=side, repeat=repeat,
                         command=cmd, process_ms=elapsed, **summary))
        frames.extend(dict(scenario=scenario, mode=mode, side=side, repeat=repeat, **row) for row in rows)
        (out / "runs.json").write_text(json.dumps(runs, indent=2) + "\n")
        print(stem, f"mean={summary['mean_ms']:.4f} cold={summary['cold_ms']:.4f} "
                    f"max={summary['max_ms']:.4f}@{summary['max_frame']}", flush=True)

    for repeat in range(args.runs):
        order = scenarios[repeat % 3:] + scenarios[:repeat % 3]
        for mode in (["direct", "staged"] if repeat % 2 == 0 else ["staged", "direct"]):
            for scenario in order:
                for side in (["before", "after"] if repeat % 2 == 0 else ["after", "before"]):
                    timed(scenario, mode, side, repeat)
        for side in (["before", "after"] if repeat % 2 == 0 else ["after", "before"]):
            timed("null", "null", side, repeat)
    summary = {}
    adapters = {tuple(x["adapter"].values()) for x in runs if "adapter" in x}
    if len(adapters) != 1:
        raise RuntimeError("Measured adapters/drivers differ")
    for scenario in [*scenarios, "null"]:
        summary[scenario] = {}
        for mode in (["null"] if scenario == "null" else ["direct", "staged"]):
            groups = {side: [x for x in runs if x["scenario"] == scenario and x["mode"] == mode
                             and x["side"] == side] for side in ["before", "after"]}
            result = {}
            for side, data in groups.items():
                result[side] = {key: statistics.median(x[key] for x in data)
                                for key in data[0] if key.endswith("_ms") and key != "max_ms"}
                result[side]["absolute_max_ms"] = max(x["max_ms"] for x in data)
                result[side]["maxima"] = [{key: x[key] for key in ["repeat", "max_ms", "max_frame"]} for x in data]
                result[side]["same_frames_ms"] = {str(f): statistics.median(
                    float(x["total_ms"]) for x in frames if x["scenario"] == scenario and
                    x["mode"] == mode and x["side"] == side and int(x["frame"]) == f)
                    for f in [0, 1, 126, 127, 128, 157, 158]}
            changes = [(b["mean_ms"] / a["mean_ms"] - 1) * 100
                       for a, b in zip(groups["before"], groups["after"])]
            result.update(paired_mean_percent=statistics.median(changes),
                          paired_changes_percent=changes, improved_pairs=sum(x < 0 for x in changes))
            summary[scenario][mode] = result
            for repeat in range(args.runs):
                pair = {side: [x for x in frames if x["scenario"] == scenario and x["mode"] == mode
                               and x["side"] == side and x["repeat"] == repeat] for side in ["before", "after"]}
                keys = set(pair["before"][0]) & set(pair["after"][0]) & {
                    "vertices", "indices", "draws", "actual_draws", "submissions", "upload_bytes", "glyph_upload_bytes"}
                for key in keys:
                    if [x[key] for x in pair["before"]] != [x[key] for x in pair["after"]]:
                        raise RuntimeError("Workload changed: " + repr((scenario, mode, repeat, key)))
    checks = []
    for mode in ["direct", "staged"]:
        for side in ["before", "after"]:
            for kind in ["pui_vulkan_startup_spiral_tests", "pui_vulkan_native_spiral_tests"]:
                for cache in ([False, True] if side == "after" else [False]):
                    cmd = [str(binaries[side][kind]), "--reference", str(reference),
                           *(["--staged"] if mode == "staged" else []), *(["--cache"] if cache else [])]
                    log = out / f"golden-{mode}-{side}-{kind}-cache{int(cache)}.log"
                    with log.open("w") as stream:
                        subprocess.run(cmd, cwd=root, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                       check=True, timeout=180)
                    checks.append(dict(mode=mode, side=side, kind=kind, cache=cache, exact_frames=180, command=cmd))
                    print("golden", mode, side, kind, cache, flush=True)
        cmd = [str(binaries["after"]["pui_vulkan_startup_spiral_tests"]), "--legacy",
               "--reference", str(reference), *(["--staged"] if mode == "staged" else [])]
        with (out / f"golden-{mode}-after-startup-legacy.log").open("w") as stream:
            subprocess.run(cmd, cwd=root, env=env, stdout=stream, stderr=subprocess.STDOUT,
                           check=True, timeout=180)
        checks.append(dict(mode=mode, side="after", kind="startup-legacy", cache=False,
                           exact_frames=180, command=cmd))
    verify({**protected, **source_hashes})
    if references != {name: digest(reference / name) for name in references}:
        raise RuntimeError("Immutable references changed")
    columns = ["scenario", "mode", "side", "repeat", "frame"]
    columns += sorted(set().union(*(set(x) for x in frames)) - set(columns))
    with (out / "frames.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        writer.writerows(frames)
    receipt = dict(protocol=dict(frames=180, tiles=5000, final_tiles=5003, adds=[90, 120, 150],
                                 width=1360, height=860, size=64, dt="1/60"),
                   summary=summary, runs=runs, exact_checks=checks, timed_frames=len(frames),
                   binary_hashes={s: {n: digest(p) for n, p in b.items()} for s, b in binaries.items()},
                   source_hashes=source_hashes, protected_hashes=protected, reference_hashes=references)
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Retained", len(frames), "timed frames and", len(checks) * 180, "exact RGBA comparisons")


if __name__ == "__main__":
    main()
