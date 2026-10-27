"""Paired native-always-draw vs integrated 5000 Spiral; retain every frame."""
from pathlib import Path
import argparse
import csv
import gzip
import hashlib
import io
import json
import shutil
import statistics
import subprocess
import sys
import time
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from compare_vulkan_program import native_command, run_native, summarize_run, window_cpu

def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction))]


def side_order(repeat):
    order = ["before", "after_native", "after"]
    order = order[repeat % 3:] + order[:repeat % 3]
    return list(reversed(order)) if repeat % 2 else order


def validate_counts(rows, mode):
    for frame, row in enumerate(rows):
        expected = 5000 + (sum(frame >= add for add in (90, 120, 150)) if mode == "moving" else 0)
        if int(row["count"]) != expected:
            raise ValueError(f"actual Add/count sequence changed at frame {frame}")


def retain_native(command, stem):
    try:
        result = run_native(command)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        for suffix, value in ((".csv", error.output), (".log", error.stderr)):
            if isinstance(value, bytes):
                value = value.decode("utf-8", errors="replace")
            stem.with_suffix(suffix).write_text(value or "", encoding="utf-8")
        raise
    stem.with_suffix(".csv").write_text(result[0], encoding="utf-8")
    stem.with_suffix(".log").write_text(result[1], encoding="utf-8")
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=6)
    parser.add_argument("--hz", type=int, default=0)
    parser.add_argument("--visible", action="store_true")
    parser.add_argument("--staged", action="store_true")
    args = parser.parse_args()
    if args.runs < 2 or args.runs > 16 or args.hz < 0 or args.hz > 1000:
        parser.error("2..16 paired runs and 0..1000 Hz required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binaries = {}
    binary_hashes = {}
    for side, original in (("before", args.baseline), ("after_native", args.candidate), ("after", args.candidate)):
        binary = output / (side + ".exe")
        shutil.copy2(original.resolve(), binary)
        binaries[side] = binary
        binary_hashes[side] = hashlib.sha256(binary.read_bytes()).hexdigest()
    modes = {"moving": [], "idle": ["--idle"], "local": ["--local-change"]}
    uploads = ("direct", "staged") if args.staged else ("direct",)
    receipt = {"protocol": "5000 actual Spiral; old native/current native/current integrated; all six 3-way orders rotate; every frame and outlier retained",
               "binaries": binary_hashes, "hz": args.hz, "visible_requested": args.visible,
               "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               "runs": [], "complete": False}
    frames = []
    for repeat in range(args.runs):
        for upload in uploads:
            for mode, workload in modes.items():
                for side in side_order(repeat):
                    stem = f"{mode}-{upload}-{repeat:02}-{side}"
                    flags = (["--sdf", "--vector-text", "--no-program-cache"] if side != "after" else ["--integrated"])
                    flags += workload
                    if upload == "staged": flags += ["--staged"]
                    if args.visible: flags += ["--visible"]
                    command = native_command(binaries[side], flags, args.hz)
                    print(stem, flush=True)
                    stdout, stderr, wall, cpu = retain_native(command, output / stem)
                    rows = list(csv.DictReader(io.StringIO(stdout)))
                    validate_counts(rows, mode)
                    summary = summarize_run(rows, retained=mode != "moving", side=side,
                                             mode=mode, upload=upload, repeat=repeat)
                    summary.update(cpu)
                    summary.update(window_cpu(stderr))
                    summary.update(command=command, wall_ms=wall)
                    receipt["runs"].append(summary)
                    for row in rows: frames.append(dict(side=side, mode=mode, upload=upload, repeat=repeat, **row))
                    (output / "measurement.json").write_text(json.dumps(receipt, indent=2) + "\n")
    fields = list(dict.fromkeys(key for row in frames for key in row))
    with gzip.open(output / "frames.csv.gz", "wt", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(frames)
    cohorts = []
    for mode in modes:
        for upload in uploads:
            for side in binaries:
                runs = [r for r in receipt["runs"] if r["mode"] == mode and r["upload"] == upload and r["side"] == side]
                cohort = [r for r in frames if r["mode"] == mode and r["upload"] == upload and r["side"] == side]
                times = [float(r["total_ms"]) for r in cohort]
                values = dict(mode=mode, upload=upload, side=side, frames=len(times),
                              mean_ms=statistics.mean(times), p95_ms=percentile(times, .95),
                              p99_ms=percentile(times, .99), max_ms=max(times))
                for field in ("process_cpu_ms", "process_cycles", "frame_window_cpu_ms", "frame_window_cycles",
                              "frame_window_main_cpu_ms", "frame_window_main_cycles"):
                    available = [r[field] for r in runs if field in r]
                    values[field] = statistics.mean(available) if available else None
                cohorts.append(values)
    receipt.update(complete=True, frames=len(frames), cohorts=cohorts,
                   frames_sha256=hashlib.sha256((output / "frames.csv.gz").read_bytes()).hexdigest())
    (output / "measurement.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"Retained {len(frames)} frames in {output}", flush=True)

if __name__ == "__main__": main()
