"""Alternating actual-showcase timings, including Windows process CPU and cycles.

Readback belongs to the separate golden protocol. Every timed frame and process
is retained; kernel/user CPU includes startup, driver workers and shutdown.
"""
import argparse
import csv
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time


def process_cpu(process):
    if os.name != "nt":
        return {}
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    get_times = kernel.GetProcessTimes
    get_times.argtypes = [wintypes.HANDLE, *([ctypes.POINTER(wintypes.FILETIME)] * 4)]
    get_times.restype = wintypes.BOOL
    cycle_time = kernel.QueryProcessCycleTime
    cycle_time.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_ulonglong)]
    cycle_time.restype = wintypes.BOOL
    times = [wintypes.FILETIME() for _ in range(4)]
    cycles = ctypes.c_ulonglong()
    if not get_times(int(process._handle), *(ctypes.byref(t) for t in times)):
        raise ctypes.WinError(ctypes.get_last_error())
    if not cycle_time(int(process._handle), ctypes.byref(cycles)):
        raise ctypes.WinError(ctypes.get_last_error())
    ms = lambda t: ((t.dwHighDateTime << 32) | t.dwLowDateTime) / 10000
    return dict(process_kernel_ms=ms(times[2]), process_user_ms=ms(times[3]),
                process_cpu_ms=ms(times[2]) + ms(times[3]), process_cycles=cycles.value)


def run_native(command):
    start = time.perf_counter()
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True) as process:
        try:
            stdout, stderr = process.communicate(timeout=180)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
        cpu = process_cpu(process)
        if process.returncode:
            raise subprocess.CalledProcessError(process.returncode, command, stdout, stderr)
    return stdout, stderr, (time.perf_counter() - start) * 1000, cpu


def summarize_run(rows, *, retained, **metadata):
    count = 5000 if retained else 5003
    if (len(rows) != 180 or int(rows[-1]["count"]) != count or
            any(int(row["frame"]) != index for index, row in enumerate(rows))):
        raise ValueError("incomplete actual 5000-item workload")
    if any(int(row["readbacks"]) for row in rows):
        raise ValueError("timed workload performed readback")
    times = [float(row["total_ms"]) for row in rows]
    return dict(**metadata, first_ms=times[0], mean_ms=statistics.mean(times),
                max_ms=max(times), max_frame=times.index(max(times)),
                peak_frames=sorted(range(180), key=lambda i: times[i], reverse=True)[:10],
                add_ms=[times[i] for i in (90, 120, 150)],
                begin_ms=statistics.mean(float(r["begin_ms"]) for r in rows),
                build_ms=statistics.mean(float(r["build_ms"]) for r in rows),
                end_ms=statistics.mean(float(r["end_ms"]) for r in rows),
                wait_ms=sum(float(r["slot_wait_ms"]) for r in rows),
                gpu_ms=sum(float(r["completed_gpu_ms"]) for r in rows),
                gpu_samples=sum(int(r["gpu_samples"]) for r in rows),
                upload_bytes=sum(int(r["upload_bytes"]) for r in rows),
                metadata_peak=max(int(r["metadata_bytes"]) for r in rows),
                program_hits=sum(int(r["program_hits"]) for r in rows))


def window_cpu(stderr):
    """Absent thread counters remain unavailable; never turn them into zero."""
    result = {}
    for prefix in ("frame_window", "frame_window_main"):
        match = re.search(rf"{prefix}_cpu_ms=([\d.]+) {prefix}_cycles=(\d+)", stderr)
        if match:
            result[prefix + "_cpu_ms"] = float(match[1])
            result[prefix + "_cycles"] = int(match[2])
    return result


def modes_for(retained):
    if not retained:
        return {"reference": [], "sdf": ["--sdf", "--vector-text"],
                "compute": ["--compute", "--vector-text"],
                "cache": ["--compute", "--vector-text", "--cache"],
                "adaptive": ["--adaptive", "--vector-text"]}
    modes = {}
    for name, workload in [("idle", "--idle"), ("local", "--local-change")]:
        for mode, flags in {"sdf": ["--sdf"], "compute": ["--compute"],
                            "cache": ["--compute", "--cache"],
                            "staged": ["--compute", "--cache", "--staged"]}.items():
            modes[name + "-" + mode] = [workload, "--vector-text", *flags]
    return modes


def native_command(binary, flags, hz):
    if not 0 <= hz <= 1000:
        raise ValueError("pacing must be 0 (uncapped) or 1..1000 Hz")
    return [str(binary), *flags, *(["--hz", str(hz)] if hz else [])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=6)
    parser.add_argument("--retained", action="store_true")
    parser.add_argument("--hz", type=int, default=0, help="same host frame pacing on both sides")
    args = parser.parse_args()
    if args.runs < 2:
        parser.error("at least two alternating repetitions are required")
    if not 0 <= args.hz <= 1000:
        parser.error("--hz must be 0 (uncapped) or 1..1000")
    args.output.mkdir(parents=True, exist_ok=False)
    binaries = {}
    for side, source in [("before", args.baseline), ("after", args.candidate)]:
        directory = args.output / side
        directory.mkdir()
        target = directory / source.name
        shutil.copy2(source, target)
        binaries[side] = target.resolve()
    runs, frames, geometry, adapter = [], [], {}, None
    modes = modes_for(args.retained)
    for repeat in range(args.runs):
        for mode, flags in modes.items():
            for side in (["before", "after"] if repeat % 2 == 0 else ["after", "before"]):
                command = native_command(binaries[side], flags, args.hz)
                stdout, stderr, elapsed, cpu = run_native(command)
                stem = args.output / f"{mode}-{side}-{repeat}"
                stem.with_suffix(".csv").write_text(stdout)
                stem.with_suffix(".log").write_text(stderr)
                rows = list(csv.DictReader(io.StringIO(stdout)))
                match = re.search(r"device=(.*?) driver=(\d+) mode=\d+ cache=\d+ vector=\d+ hidden_wsi=1", stderr)
                if not match or (adapter is not None and adapter != match.groups()):
                    raise ValueError("adapter/driver or WSI protocol changed")
                adapter = match.groups()
                cpu.update(window_cpu(stderr))
                for row in rows:
                    key = mode, int(row["frame"])
                    value = tuple(int(row[k]) for k in ("count", "draws", "vertices", "indices"))
                    if key in geometry and geometry[key] != value:
                        raise ValueError(f"semantic geometry changed at {key}")
                    geometry[key] = value
                run = summarize_run(rows, retained=args.retained, mode=mode, side=side,
                                    repeat=repeat, process_ms=elapsed, command=command,
                                    adapter=stderr, **cpu)
                runs.append(run)
                frames.extend(dict(mode=mode, side=side, repeat=repeat, **r) for r in rows)
                print(mode, side, repeat, f"mean={run['mean_ms']:.4f} GPU={run['gpu_ms']:.2f} "
                      f"CPU={cpu.get('process_cpu_ms', 0):.2f} cycles={cpu.get('process_cycles', 0)}", flush=True)
    metrics = ["mean_ms", "max_ms", "first_ms", "process_ms", "begin_ms", "build_ms", "end_ms",
               "gpu_ms", "gpu_samples", "upload_bytes", "metadata_peak", "program_hits"]
    metrics.extend(k for k in ("process_cpu_ms", "process_user_ms", "process_kernel_ms", "process_cycles",
                              "frame_window_cpu_ms", "frame_window_cycles",
                              "frame_window_main_cpu_ms", "frame_window_main_cycles")
                   if k in runs[0])
    summary = {mode: {side: {k: statistics.median(r[k] for r in runs
                                               if r["mode"] == mode and r["side"] == side)
                             for k in metrics} for side in ("before", "after")} for mode in modes}
    receipt = dict(schema=1, workload="static-local" if args.retained else "animated-add",
                   pacing_hz=args.hz,
                   protocol="hidden native WSI; VSync off; all 180 frames; no readback; "
                   "delayed GPU submissions; Windows CPU/cycles include startup and shutdown",
                   binaries={k: dict(path=str(v), sha256=hashlib.sha256(v.read_bytes()).hexdigest())
                             for k, v in binaries.items()}, summary=summary, runs=runs)
    (args.output / "comparison.json").write_text(json.dumps(receipt, indent=2))
    with (args.output / "frames.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(frames[0]))
        writer.writeheader()
        writer.writerows(frames)


if __name__ == "__main__":
    main()
