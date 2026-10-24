"""Alternating, uncensored 180-frame native painter comparison (no readback)."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=6)
    args = parser.parse_args()
    if args.runs < 2:
        parser.error("at least two repetitions are required")
    args.output.mkdir(parents=True, exist_ok=False)
    binaries = {}
    for side, source in (("before", args.baseline), ("after", args.candidate)):
        directory = args.output / side
        directory.mkdir()
        snapshot = directory / source.name
        shutil.copy2(source, snapshot)
        binaries[side] = snapshot.resolve()
    modes = {"reference": [], "sdf": ["--sdf", "--vector-text"],
             "compute": ["--compute", "--vector-text"],
             "cache": ["--compute", "--vector-text", "--cache"],
             "adaptive": ["--adaptive", "--vector-text"]}
    runs, frames, geometry = [], [], {}
    adapter = None
    for repeat in range(args.runs):
        for mode, flags in modes.items():
            for label in (["before", "after"] if repeat % 2 == 0 else ["after", "before"]):
                exe = binaries[label]
                start = time.perf_counter()
                result = subprocess.run([str(exe.resolve()), *flags], capture_output=True, text=True,
                                        timeout=180, check=True)
                process_ms = (time.perf_counter() - start) * 1000
                stem = args.output / f"{mode}-{label}-{repeat}"
                stem.with_suffix(".csv").write_text(result.stdout)
                stem.with_suffix(".log").write_text(result.stderr)
                rows = list(csv.DictReader(io.StringIO(result.stdout)))
                if len(rows) != 180 or int(rows[-1]["count"]) != 5003:
                    raise ValueError("incomplete 5000-item workload")
                if sum(int(row["readbacks"]) for row in rows):
                    raise ValueError("timed workload performed readback")
                match = re.search(r"device=(.*?) driver=(\d+) mode=\d+ cache=\d+ vector=\d+ hidden_wsi=1", result.stderr)
                if not match or (adapter is not None and adapter != match.groups()):
                    raise ValueError("adapter/driver or WSI protocol changed")
                adapter = match.groups()
                for row in rows:
                    key = (mode, int(row["frame"]))
                    value = tuple(int(row[k]) for k in ("count", "draws", "vertices", "indices"))
                    if key in geometry and geometry[key] != value:
                        raise ValueError(f"geometry changed at {key}")
                    geometry[key] = value
                times = [float(row["total_ms"]) for row in rows]
                run = dict(mode=mode, side=label, repeat=repeat, process_ms=process_ms,
                           first_ms=times[0], mean_ms=statistics.mean(times), max_ms=max(times),
                           max_frame=times.index(max(times)),
                           peak_frames=sorted(range(180), key=lambda i: times[i], reverse=True)[:10],
                           add_ms=[times[i] for i in (90, 120, 150)],
                           gpu_ms=sum(float(row["completed_gpu_ms"]) for row in rows),
                           gpu_samples=sum(int(row["gpu_samples"]) for row in rows),
                           upload_bytes=sum(int(row["upload_bytes"]) for row in rows),
                           metadata_peak=max(int(row["metadata_bytes"]) for row in rows),
                           adapter=result.stderr)
                runs.append(run)
                for row in rows:
                    frames.append(dict(mode=mode, side=label, repeat=repeat, **row))
                print(mode, label, repeat, f"mean={run['mean_ms']:.3f} max={run['max_ms']:.3f} "
                      f"GPU={run['gpu_ms']:.3f} process={process_ms:.1f}", flush=True)
    summary = {}
    for mode in modes:
        summary[mode] = {}
        for side in ("before", "after"):
            selected = [r for r in runs if r["mode"] == mode and r["side"] == side]
            summary[mode][side] = {key: statistics.median(r[key] for r in selected)
                                   for key in ("mean_ms", "max_ms", "first_ms", "process_ms",
                                               "gpu_ms", "gpu_samples", "upload_bytes", "metadata_peak")}
    receipt = {"schema": 1, "protocol": "hidden WSI; VSync disabled; all 180 frames; no readback; "
               "completed GPU samples are delayed submissions; cold setup retained in process_ms",
               "binaries": {label: {"path": str(exe.resolve()),
                                      "sha256": hashlib.sha256(exe.read_bytes()).hexdigest()}
                            for label, exe in binaries.items()},
               "summary": summary, "runs": runs}
    (args.output / "comparison.json").write_text(json.dumps(receipt, indent=2))
    with (args.output / "frames.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(frames[0]))
        writer.writeheader()
        writer.writerows(frames)


if __name__ == "__main__":
    main()
