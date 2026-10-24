"""Matched baseline/candidate null and D3D11 controls; retain all 180 frames."""
import argparse
import csv
import hashlib
import json
import pathlib
import shutil
import subprocess
import time
from compare_vulkan_compute import schedule, parse_frames, summarize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=pathlib.Path, required=True)
    parser.add_argument("--candidate", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--runs", type=int, default=8)
    parser.add_argument("--tiles", type=int, choices=[1150, 5000], default=5000)
    args = parser.parse_args()
    if args.runs < 2:
        parser.error("at least two repetitions are required")
    args.output.mkdir(parents=True, exist_ok=False)
    commands, binaries = {}, {}
    for label, source in [("baseline", args.baseline), ("candidate", args.candidate)]:
        directory = args.output / label
        directory.mkdir()
        exe = directory / source.name
        shutil.copy2(source, exe)
        binaries[label] = hashlib.sha256(exe.read_bytes()).hexdigest()
        for backend in ["null", "direct3d11"]:
            commands[f"{backend}_{label}"] = [str(exe.resolve()), "--bench", backend, "--tiles", str(args.tiles)]
    rows, geometry, records = [], {}, []
    for run, mode in schedule(list(commands), args.runs):
        start = time.perf_counter()
        process = subprocess.run(commands[mode], capture_output=True, text=True)
        elapsed = time.perf_counter() - start
        (args.output / f"{run:02}_{mode}.stdout").write_text(process.stdout)
        (args.output / f"{run:02}_{mode}.stderr").write_text(process.stderr)
        process.check_returncode()
        frames = parse_frames(process.stdout)
        for row in frames:
            frame = int(row["frame"])
            shape = tuple(row[key] for key in ["count", "draws", "vertices", "indices"])
            key = (mode.split('_')[0], frame)
            if key in geometry and geometry[key] != shape:
                raise ValueError(f"geometry changed at {mode} frame {frame}")
            geometry[key] = shape
            rows.append(dict(run=run, mode=mode, **row))
        records.append(dict(run=run, mode=mode, process_seconds=elapsed))
        print(f"run={run} mode={mode} seconds={elapsed:.3f}", flush=True)
    with (args.output / "frames.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    summary = {mode: dict(
        frame=summarize([float(r["total_ms"]) for r in rows if r["mode"] == mode]),
        add=summarize([float(r["total_ms"]) for r in rows if r["mode"] == mode and int(r["frame"]) >= 90]))
        for mode in commands}
    (args.output / "report.json").write_text(json.dumps(dict(
        tiles=args.tiles, runs=records, summary=summary, binaries=binaries, commands=commands), indent=2))


if __name__ == "__main__":
    main()
