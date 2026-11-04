"""Qualify native placement/index changes with all simple/Spiral/SVG frames.

Timing uses the unchanged benchmark executables without readbacks. The separate
simple-path executable reuses that benchmark for immutable full-frame captures.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import compare_vulkan_batches as batches


def parse_simple_golden(stdout, stderr):
    lines = stdout.strip().splitlines()
    if not lines or not re.fullmatch(
            r"180 exact simple Slug frames passed \((direct|staged), capture=0\)", lines[-1]):
        raise RuntimeError("missing complete simple-path golden certificate")
    rows, summary = batches.parse("\n".join(lines[:-1]) + "\n", stderr)
    if summary.get("readbacks") != 180:
        raise RuntimeError("simple-path qualification must read every frame")
    for key in ("draw_cpp_allocations", "draw_buffer_allocations"):
        if summary[key]["maximum"] != 0:
            raise RuntimeError("simple-path qualification allocated while drawing")
    return rows, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--spiral-references", type=Path, required=True)
    parser.add_argument("--simple-references", type=Path, required=True)
    parser.add_argument("--vector-references", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pairs", type=int, default=8)
    args = parser.parse_args()
    if args.pairs < 2:
        parser.error("at least two alternating pairs are required")
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    for key in ("PUFFERUI_VULKAN_VALIDATION", "VK_INSTANCE_LAYERS"):
        env.pop(key, None)
    command = [sys.executable, str(root / "tools/compare_vulkan_batches.py"),
               "--before", str(args.before.resolve()), "--after", str(args.after.resolve()),
               "--spiral-references", str(args.spiral_references.resolve()),
               "--vector-references", str(args.vector_references.resolve()),
               "--output", str(output / "batches"), "--pairs", str(args.pairs)]
    subprocess.run(command, cwd=root, env=env, check=True)
    receipt = json.loads((output / "batches/receipt.json").read_text())
    references = batches.unpack_references(args.simple_references.resolve(),
                                           output / "simple-references")
    receipt["simple_reference_hashes"] = {
        p.name: batches.digest(p) for p in sorted(references.glob("frame-*.rgba"))}
    binaries = {}
    for label, directory in (("before", args.before), ("after", args.after)):
        source = directory.resolve() / ("pui_vulkan_vector_cpu_tests" +
                                         (".exe" if os.name == "nt" else ""))
        destination = output / ("simple-" + label + source.suffix)
        shutil.copy2(source, destination)
        binaries[label] = destination
        receipt["binaries"]["simple/" + label] = batches.digest(destination)
    for mode in ("direct", "staged"):
        paired = {}
        for label in ("before", "after"):
            name = "simple-" + mode + "-" + label
            command = [str(binaries[label]), *(["--staged"] if mode == "staged" else []),
                       "--reference", str(references)]
            print(name, flush=True)
            result = subprocess.run(command, cwd=root, env=env, capture_output=True,
                                    text=True, timeout=300)
            (output / (name + ".log")).write_text(result.stderr, encoding="utf-8")
            (output / (name + ".csv")).write_text(result.stdout, encoding="utf-8")
            if result.returncode:
                raise RuntimeError(name + " failed")
            rows, summary = parse_simple_golden(result.stdout, result.stderr)
            paired[label] = rows
            receipt["goldens"].append({"name": name, "frames": 180,
                                       "reference": str(references), "summary": summary})
        fields = ("draws", "upload_bytes", "draw_cpp_allocations", "draw_buffer_allocations")
        if ([tuple(r[k] for k in fields) for r in paired["before"]] !=
                [tuple(r[k] for k in fields) for r in paired["after"]]):
            raise RuntimeError("simple-path golden stream counters differ")
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(f"{len(receipt['runs']) * 180} timed frames; "
          f"{sum(g['frames'] for g in receipt['goldens'])} exact full-image comparisons", flush=True)


if __name__ == "__main__":
    main()
