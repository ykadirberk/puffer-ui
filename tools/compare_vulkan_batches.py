"""Alternate frozen native vector/Spiral binaries; retain every frame and peak.

References are immutable full RGBA frames. Timing never performs readbacks.
The separate pui_vulkan_batch_tests capture executable must be compiled with
the same harness and frozen baseline implementation to establish SVG references.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import time
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def distribution(values):
    values = sorted(values)
    return {
        "mean": statistics.mean(values),
        "median": statistics.median(values),
        "p95": values[int((len(values) - 1) * .95)],
        "p99": values[int((len(values) - 1) * .99)],
        "maximum": values[-1],
    }


def unpack_references(archive, output):
    """Validate the fixed fixture before extracting; never accept arbitrary paths."""
    frames = {f"frame-{frame:03}.rgba" for frame in range(180)}
    with zipfile.ZipFile(archive) as source:
        names = source.namelist()
        if len(names) != 181 or set(names) != frames | {"receipt.json"}:
            raise RuntimeError("invalid vector reference archive members")
        receipt = json.loads(source.read("receipt.json"))
        hashes = receipt["frames"]["direct"]
        if set(hashes) != frames or hashes != receipt["frames"]["staged"]:
            raise RuntimeError("inconsistent direct/staged reference hashes")
        output.mkdir(parents=True, exist_ok=False)
        for name in sorted(frames):
            info = source.getinfo(name)
            if info.file_size != 512 * 512 * 4:
                raise RuntimeError("invalid vector reference frame size")
            data = source.read(info)
            if hashlib.sha256(data).hexdigest() != hashes[name]:
                raise RuntimeError("vector reference hash mismatch")
            (output / name).write_bytes(data)
    return output


def parse(stdout, stderr):
    import re
    rows = list(csv.DictReader(io.StringIO(stdout)))
    if [int(row["frame"]) for row in rows] != list(range(180)):
        raise RuntimeError("incomplete 180-frame measurement")
    times = [float(row["total_ms"]) for row in rows]
    if any(not math.isfinite(value) or value < 0 for value in times):
        raise RuntimeError("invalid timing")
    summary = {"wall": distribution(times), "cold_ms": times[0],
               "top10": sorted(enumerate(times), key=lambda value: value[1], reverse=True)[:10]}
    for key in ("gpu_total_ms", "gpu_samples", "setup_ms", "image_bytes", "path_residency_bytes", "readbacks"):
        match = re.search(r"\b" + key + r"=([\d.]+)", stderr)
        if match:
            summary[key] = float(match[1])
    for key in ("draw_cpp_allocations", "draw_buffer_allocations", "draws", "upload_bytes",
                "submissions", "submit_ms", "slot_wait_ms", "total_upload_bytes", "resident_bytes"):
        if key in rows[0]:
            summary[key] = distribution([float(row[key]) for row in rows])
    return rows, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--spiral-references", type=Path, required=True,
                        help="parent of spiral-direct and spiral-staged")
    parser.add_argument("--vector-references", type=Path, required=True,
                        help="parent of direct/staged SVG captures, or the committed frames.zip")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pairs", type=int, default=8)
    args = parser.parse_args()
    if args.pairs < 2:
        parser.error("at least two alternating pairs are required")
    root = Path(__file__).resolve().parent.parent
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    for key in ("PUFFERUI_VULKAN_VALIDATION", "VK_INSTANCE_LAYERS"):
        env.pop(key, None)
    names = ["pui_vulkan_vector_data_tests", "pui_vulkan_startup_spiral_tests",
             "pui_vulkan_native_spiral_tests", "pui_showcase_tests", "pui_vulkan_batch_tests"]
    bins = {}
    for label, directory in (("before", args.before), ("after", args.after)):
        target = out / label
        target.mkdir()
        for name in names:
            source = directory.resolve() / (name + (".exe" if os.name == "nt" else ""))
            destination = target / source.name
            shutil.copy2(source, destination)
            bins[label, name] = destination
    receipt = {"start_time": time.time(), "pairs": args.pairs, "runs": [], "goldens": [],
               "binaries": {f"{label}/{name}": digest(path) for (label, name), path in bins.items()},
               "reference_hashes": {}}
    vector_reference = (unpack_references(args.vector_references, out / "vector-references")
                        if args.vector_references.is_file() else None)

    def run(name, binary, flags):
        command = [str(binary), *map(str, flags)]
        print(name, flush=True)
        result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True, timeout=300)
        (out / (name + ".csv")).write_text(result.stdout, encoding="utf-8")
        (out / (name + ".log")).write_text(result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{name} failed: {result.returncode}")
        return result

    def golden(name, label, binary, reference, flags):
        paths = sorted(reference.glob("frame-*.rgba"))
        if len(paths) != 180:
            raise RuntimeError(f"{reference}: expected 180 immutable frames")
        hashes = {path.name: digest(path) for path in paths}
        receipt["reference_hashes"][str(reference)] = hashes
        result = run(name, bins[label, binary], [*flags, "--reference", reference])
        if "180" not in result.stdout + result.stderr:
            raise RuntimeError("golden runner did not certify 180 frames")
        receipt["goldens"].append({"name": name, "frames": 180, "reference": str(reference)})

    cases = []
    for mode in ("direct", "staged"):
        flags = ["--staged"] if mode == "staged" else []
        for cache in (False, True):
            case = mode + ("-cache" if cache else "-plain")
            selected = flags + (["--cache"] if cache else [])
            reference = args.spiral_references.resolve() / ("spiral-" + mode)
            for label in ("before", "after"):
                golden("golden-spiral-" + case + "-" + label, label,
                       "pui_vulkan_startup_spiral_tests", reference, selected)
            golden("golden-vector-" + case, "after", "pui_vulkan_batch_tests",
                   vector_reference or args.vector_references.resolve() / mode, selected)
            cases.append(("spiral-" + case, "pui_vulkan_startup_spiral_tests", ["--bench", *selected]))
        for label in ("before", "after"):
            golden("golden-late-" + mode + "-" + label, label, "pui_vulkan_native_spiral_tests",
                   args.spiral_references.resolve() / ("spiral-" + mode), flags)
        for complex_scene in (False, True):
            selected = ["--bench", *flags] + (["--complex"] if complex_scene else [])
            cases.append(("vector-" + mode + ("-complex" if complex_scene else "-simple"),
                          "pui_vulkan_vector_data_tests", selected))
    cases.append(("null", "pui_showcase_tests", ["--bench", "null", "--tiles", "5000"]))
    for case, binary, flags in cases:
        for pair in range(args.pairs):
            pair_rows = {}
            for label in (("before", "after") if pair % 2 == 0 else ("after", "before")):
                name = f"{case}-{pair:02}-{label}"
                result = run(name, bins[label, binary], flags)
                rows, summary = parse(result.stdout, result.stderr)
                pair_rows[label] = rows
                if case.startswith("vector"):
                    for key in ("draw_cpp_allocations", "draw_buffer_allocations"):
                        if summary[key]["maximum"] != 0:
                            raise RuntimeError(f"{name} allocated on the draw path")
                    if summary["readbacks"] != 0:
                        raise RuntimeError("timing performed a readback")
                receipt["runs"].append({"name": name, "case": case, "revision": label,
                                        "pair": pair, "command": [binary, *flags], "summary": summary})
            fields = (["vertices", "indices", "draws", "upload_bytes", "glyph_upload_bytes"]
                      if case.startswith("spiral") else ["count", "draws", "vertices", "indices"]
                      if case == "null" else ["upload_bytes"])
            if ([tuple(row[key] for key in fields) for row in pair_rows["before"]] !=
                    [tuple(row[key] for key in fields) for row in pair_rows["after"]]):
                raise RuntimeError(f"{case}: unchanged stream counters differ")
        (out / "receipt-progress.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    receipt["end_time"] = time.time()
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(f"{len(receipt['runs'])} runs / {len(receipt['runs']) * 180} timed frames; "
          f"{sum(item['frames'] for item in receipt['goldens'])} exact golden frames", flush=True)


if __name__ == "__main__":
    main()
