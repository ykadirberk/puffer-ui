"""Retain every frame of balanced R0/SDF/Slug/compute comparisons.

Timing uses hidden WSI, no readback, VSync off, real 180-frame showcase input.
Run visual captures separately. Never discard startup frames or outliers.
"""
import argparse
import csv
import hashlib
import io
import json
import pathlib
import os
import re
import shutil
import platform
import statistics
import subprocess
import time


def schedule(names, repetitions):
    # Balanced Latin square: each mode occupies every ordinal once per cycle.
    n = len(names)
    base = [0] + [(i + 1) // 2 if i % 2 else n - i // 2 for i in range(1, n)]
    result = []
    for run in range(repetitions):
        order = [names[(index + run) % n] for index in base]
        result.extend((run, name) for name in order)
    return result


def parse_frames(output):
    lines = output.splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith("frame,count,"))
    rows = list(csv.DictReader(io.StringIO("\n".join(lines[start:]))))
    rows = [r for r in rows if r.get("frame", "").isdigit()]
    if [int(r["frame"]) for r in rows] != list(range(180)):
        raise ValueError("expected every frame 0..179 exactly once")
    if any(int(r.get("readbacks", 0)) for r in rows):
        raise ValueError("readback contaminates timed run")
    return rows


def summarize(values):
    ordered = sorted(values)
    return dict(mean=statistics.mean(values), median=statistics.median(values),
                p95=ordered[int((len(ordered) - 1) * .95)],
                p99=ordered[int((len(ordered) - 1) * .99)], max=max(values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=pathlib.Path, required=True)
    parser.add_argument("--candidate", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--tiles", type=int, choices=[1150, 5000], default=5000)
    args = parser.parse_args()
    if args.runs < 2:
        parser.error("at least two repetitions are required")
    args.output.mkdir(parents=True, exist_ok=False)
    frozen = args.output / "bin"
    frozen.mkdir()
    originals = [args.baseline.resolve(), args.candidate.resolve()]
    for label, original in zip(["baseline", "candidate"], originals):
        (frozen / label).mkdir()
        shutil.copy2(original, frozen / label / original.name)
    args.baseline = frozen / "baseline" / originals[0].name
    args.candidate = frozen / "candidate" / originals[1].name
    commands = {
        "r0": [str(args.baseline.resolve()), "--bench"],
        "aa": [str(args.baseline.resolve()), "--bench"],
        "reference": [str(args.candidate.resolve())],
        "sdf_slug": [str(args.candidate.resolve()), "--sdf", "--vector-text"],
        "compute_full": [str(args.candidate.resolve()), "--compute", "--vector-text"],
        "compute_cache": [str(args.candidate.resolve()), "--compute", "--vector-text", "--cache"],
        "adaptive": [str(args.candidate.resolve()), "--adaptive", "--vector-text"],
    }
    retained, runs, peak_union = [], [], set()
    identity = None
    for run, mode in schedule(list(commands), args.runs):
        command = commands[mode] + ["--tiles", str(args.tiles)]
        start = time.perf_counter()
        process = subprocess.run(command, capture_output=True, text=True)
        elapsed = time.perf_counter() - start
        stem = args.output / f"run{run:02}_{mode}"
        stem.with_suffix(".stdout").write_text(process.stdout, encoding="utf-8")
        stem.with_suffix(".stderr").write_text(process.stderr, encoding="utf-8")
        process.check_returncode()
        name = re.search(r"device=(.*?) (?:vendor=|driver=)", process.stderr)
        driver = re.search(r"driver=(\d+)", process.stderr)
        if not name or not driver:
            raise ValueError("missing actual device/driver receipt")
        current = (name.group(1), driver.group(1))
        if identity is not None and identity != current:
            raise ValueError("adapter/driver changed during comparison")
        identity = current
        rows = parse_frames(process.stdout)
        if any(int(r["count"]) != args.tiles + sum(int(r["frame"]) >= n for n in [90, 120, 150]) for r in rows):
            raise ValueError("input stream differs from frozen workload")
        peaks = sorted(rows, key=lambda r: float(r["total_ms"]), reverse=True)[:10]
        for row in peaks:
            peak_union.update(range(max(0, int(row["frame"]) - 2), min(180, int(row["frame"]) + 3)))
        retained.extend(dict(run=run, mode=mode, **r) for r in rows)
        runs.append(dict(run=run, mode=mode, process_seconds=elapsed,
                         frame=summarize([float(r["total_ms"]) for r in rows]),
                         add=summarize([float(r["total_ms"]) for r in rows[90:]])))
        print(f"run={run} mode={mode} seconds={elapsed:.3f}", flush=True)
    fields = list(dict.fromkeys(k for r in retained for k in r))
    with (args.output / "frames.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(retained)
    summary = {mode: dict(
        frame=summarize([float(r["total_ms"]) for r in retained if r["mode"] == mode]),
        add=summarize([float(r["total_ms"]) for r in retained if r["mode"] == mode and int(r["frame"]) >= 90]))
        for mode in commands}
    manifest = dict(tiles=args.tiles, frames=180, repetitions=args.runs, runs=runs,
                    summary=summary, peak_union=sorted(peak_union), commands=commands,
                    adapter=dict(name=identity[0], driver=identity[1]),
                    platform=platform.platform(),
                    environment={key: os.environ.get(key) for key in ["VK_DRIVER_FILES", "VK_ICD_FILENAMES", "VULKAN_SDK"]},
                    binaries={str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in [args.baseline, args.candidate]})
    (args.output / "report.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
