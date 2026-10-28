"""Retain alternating, readback-free native Spiral runs and exact all-frame checks.

Baseline is the native probe linked against the first renderer, with bitmap
text. Candidate runs both bitmap and HarfBuzz/Slug. Never compare their images
as if font representation were unchanged. No build runs during this protocol.
"""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import re
import shutil
import statistics
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def distribution(values):
    values = sorted(values)
    return dict(mean=statistics.mean(values), p50=values[(len(values) - 1) // 2],
                p95=values[int((len(values) - 1) * .95)],
                p99=values[int((len(values) - 1) * .99)], maximum=values[-1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bitmap-reference", type=Path, required=True)
    parser.add_argument("--text-reference", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=6)
    args = parser.parse_args()
    if args.runs < 2 or args.runs > 20:
        parser.error("Use 2..20 alternating repetitions")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    sources = {p.relative_to(root).as_posix(): digest(p)
               for directory in ["include", "src", "examples", "tests", "cmake", "tools"]
               for p in (root / directory).rglob("*")
               if p.is_file() and p.suffix in [".h", ".cpp", ".inl", ".glsl", ".vert", ".frag", ".py", ".cmake"]
               and "__pycache__" not in p.parts}
    for p in [root/"CMakeLists.txt", root/"CMakePresets.json", root/"src/shaders/vulkan/manifest.json",
              *sorted((root/"assets/fonts").glob("*.ttf"))]:
        sources[p.relative_to(root).as_posix()] = digest(p)
    binaries = {}
    for name, path in [("baseline", args.baseline), ("candidate", args.candidate)]:
        target = output / (name + path.suffix)
        shutil.copy2(path, target)
        binaries[name] = target
    cases = [("baseline-bitmap", "baseline", ["--bitmap"]),
             ("candidate-bitmap", "candidate", ["--bitmap"]),
             ("candidate-slug", "candidate", [])]
    with (output/"baseline-reference.log").open("w") as log:
        subprocess.run([str(binaries["baseline"]), "--bitmap", "--reference", str(args.bitmap_reference.resolve())],
                       cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
    runs = []
    all_rows = []
    for repeat in range(args.runs):
        for staged in [False, True]:
            for label, side, flags in (cases if repeat % 2 == 0 else list(reversed(cases))):
                stem = f"{label}-{'staged' if staged else 'direct'}-{repeat}"
                command = [str(binaries[side]), "--bench", *flags, *(["--staged"] if staged else [])]
                process_start = time.perf_counter()
                with (output / (stem + ".csv")).open("w") as stdout, (output / (stem + ".log")).open("w") as stderr:
                    subprocess.run(command, cwd=root, stdout=stdout, stderr=stderr, check=True, timeout=90)
                process_ms = (time.perf_counter()-process_start)*1000
                rows = list(csv.DictReader((output / (stem + ".csv")).read_text().splitlines()))
                if [int(r["frame"]) for r in rows] != list(range(180)):
                    raise RuntimeError("Incomplete frame stream: " + stem)
                for row in rows:
                    all_rows.append(dict(label=label, staged=int(staged), repeat=repeat, **row))
                wall = [float(r["total_ms"]) for r in rows]
                samples = sum(int(r["gpu_samples"]) for r in rows)
                final = re.search(r"gpu_total_ms=([\d.]+) gpu_samples=(\d+) frames=180", (output/(stem+".log")).read_text())
                if not final:
                    raise RuntimeError("Missing complete GPU interval: "+stem)
                runs.append(dict(label=label, staged=staged, repeat=repeat, frames=180, process_ms=process_ms,
                    wall=distribution(wall), cold_ms=wall[0], steady=distribution(wall[1:]),
                    gpu_ms=float(final[1])/180, gpu_samples=int(final[2]),
                    peak_upload_bytes=max(int(r["resident_bytes"]) for r in rows),
                    peak_frame=max(range(180), key=lambda f: wall[f]), command=command))
                print(stem, "mean", runs[-1]["wall"]["mean"], "peak", max(wall), flush=True)
    checks = []
    for staged in [False, True]:
        for cache in [False, True]:
            for bitmap in [False, True]:
                label = f"{'bitmap' if bitmap else 'slug'}-{'staged' if staged else 'direct'}-cache{int(cache)}"
                reference = args.bitmap_reference if bitmap else args.text_reference
                command = [str(binaries["candidate"]), "--reference", str(reference.resolve()),
                           *(["--bitmap"] if bitmap else []), *(["--staged"] if staged else []),
                           *(["--cache"] if cache else [])]
                with (output / (label + ".golden.log")).open("w") as log:
                    subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
                checks.append(dict(label=label, exact_frames=180, command=command,
                                   covers_timed_peaks=True))
    fields = list(dict.fromkeys(key for row in all_rows for key in row))
    with (output / "frames.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(all_rows)
    for relative, expected in sources.items():
        if digest(root / relative) != expected:
            raise RuntimeError("Measured source changed: " + relative)
    receipt = dict(protocol="180 actual 5000->5003 Spiral frames; alternating; offscreen; no readbacks in timing; all cold/outlier frames retained",
                   foreground_certified=False, binaries={k: dict(path=str(v), sha256=digest(v)) for k, v in binaries.items()},
                   source_hashes=sources, runs=runs, exact_checks=checks, exact_frames=sum(c["exact_frames"] for c in checks),
                   references={name: [dict(frame=f, sha256=digest(path / f"frame-{f:03}.rgba")) for f in range(180)]
                               for name, path in [("first-native-bitmap", args.bitmap_reference), ("native-Slug-before-optimization", args.text_reference)]})
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
