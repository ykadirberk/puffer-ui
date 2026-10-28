"""Paired production/fine INTEGRATED workloads; retain every frame and outlier."""
from pathlib import Path
import argparse
import csv
import gzip
import hashlib
import io
import json
import shutil
import statistics

from compare_vulkan_integrated import percentile, retain_native, validate_counts
from compare_vulkan_program import native_command, summarize_run, window_cpu


def workload_flags(painter, mode, upload):
    if painter not in ("production", "fine") or mode not in ("moving", "idle", "local"):
        raise ValueError("unknown painter/workload")
    if upload not in ("direct", "staged"):
        raise ValueError("unknown upload configuration")
    flags = ["--integrated"]
    if painter == "fine":
        flags.append("--no-program-cache")
    if mode != "moving":
        flags.append("--idle" if mode == "idle" else "--local-change")
    if upload == "staged":
        flags.append("--staged")
    return flags


def checked_summary(rows, mode, **metadata):
    validate_counts(rows, mode)
    return summarize_run(rows, retained=mode != "moving", mode=mode, **metadata)


def cohort_summary(rows):
    if not rows:
        raise ValueError("empty cohort")
    times = [float(row["total_ms"]) for row in rows]
    return dict(frames=len(rows), mean_ms=statistics.mean(times),
                p95_ms=percentile(times, .95), p99_ms=percentile(times, .99),
                max_ms=max(times), first_ms=times[0],
                peak_frame=int(rows[times.index(max(times))]["frame"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--runs", type=int, default=6)
    parser.add_argument("--hz", type=int, default=0)
    args = parser.parse_args()
    if not 2 <= args.runs <= 16 or not 0 <= args.hz <= 1000:
        parser.error("2..16 paired runs and 0..1000 Hz required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binaries = {}
    hashes = {}
    for side, original in (("before", args.baseline), ("after", args.candidate)):
        binary = output / (side + ".exe")
        shutil.copy2(original.resolve(), binary)
        binaries[side] = binary
        hashes[side] = hashlib.sha256(binary.read_bytes()).hexdigest()
    receipt = dict(protocol="5000 actual Spiral, 180 frames, three real Adds; "
                            "identical production/fine flags before/after; no readback; "
                            "all startup/peak frames retained; hidden WSI is not foreground certification",
                   binaries=hashes, runs=[], complete=False, hz=args.hz,
                   script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    frames = []
    for repeat in range(args.runs):
        for painter in ("production", "fine"):
            for upload in ("direct", "staged"):
                for mode in ("moving", "idle", "local"):
                    order = ("before", "after") if repeat % 2 == 0 else ("after", "before")
                    for side in order:
                        stem = f"{painter}-{upload}-{mode}-{repeat:02}-{side}"
                        command = native_command(binaries[side], workload_flags(painter, mode, upload), args.hz)
                        print(stem, flush=True)
                        stdout, stderr, wall, cpu = retain_native(command, output / stem)
                        rows = list(csv.DictReader(io.StringIO(stdout)))
                        summary = checked_summary(rows, mode, side=side, painter=painter,
                                                  upload=upload, repeat=repeat)
                        summary.update(cpu)
                        summary.update(window_cpu(stderr))
                        summary.update(command=command, wall_ms=wall)
                        receipt["runs"].append(summary)
                        frames.extend(dict(side=side, painter=painter, mode=mode, upload=upload,
                                           repeat=repeat, **row) for row in rows)
                        (output / "measurement.json").write_text(json.dumps(receipt, indent=2) + "\n")
    fields = list(dict.fromkeys(key for row in frames for key in row))
    with gzip.open(output / "frames.csv.gz", "wt", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(frames)
    cohorts = []
    for painter in ("production", "fine"):
        for upload in ("direct", "staged"):
            for mode in ("moving", "idle", "local"):
                for side in binaries:
                    selected = [r for r in frames if (r["painter"], r["upload"], r["mode"], r["side"]) ==
                                (painter, upload, mode, side)]
                    value = dict(painter=painter, upload=upload, mode=mode, side=side,
                                 **cohort_summary(selected))
                    runs = [r for r in receipt["runs"] if (r["painter"], r["upload"], r["mode"], r["side"]) ==
                            (painter, upload, mode, side)]
                    for field in ("process_cpu_ms", "process_cycles", "frame_window_cpu_ms",
                                  "frame_window_cycles", "frame_window_main_cpu_ms", "frame_window_main_cycles"):
                        values = [r[field] for r in runs if field in r]
                        value[field] = statistics.mean(values) if values else None
                    value["gpu_ms_per_frame"] = sum(r["gpu_ms"] for r in runs) / len(selected)
                    value["gpu_samples"] = sum(r["gpu_samples"] for r in runs)
                    cohorts.append(value)
    receipt.update(complete=True, frames=len(frames), cohorts=cohorts,
                   frames_sha256=hashlib.sha256((output / "frames.csv.gz").read_bytes()).hexdigest())
    (output / "measurement.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"Retained {len(frames)} frames: {output}", flush=True)


if __name__ == "__main__":
    main()
