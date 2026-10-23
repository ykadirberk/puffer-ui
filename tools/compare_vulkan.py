"""Retain alternating Vulkan upload/cache/A-A Spiral runs and optional SDL controls.

Run only after builds, tests and captures finish. Every frame/outlier is retained.
Different Vulkan and D3D11 adapters must not be treated as a matched GPU comparison.
"""
import argparse
import csv
import datetime
import hashlib
import json
import os
import pathlib
import platform
import shutil
import statistics
import subprocess
import time


def percentile(values, q):
    values = sorted(values)
    return values[min(len(values) - 1, int((len(values) - 1) * q))]


def summarize(runs):
    return dict(
        add_mean_ms=statistics.mean(r["add_mean_ms"] for r in runs),
        median_add_max_ms=statistics.median(r["add_max_ms"] for r in runs),
        absolute_max_ms=max(float(r["worst"]["total_ms"]) for r in runs),
        all_mean_ms=statistics.mean(r["all_mean_ms"] for r in runs),
        first_frame_median_ms=statistics.median(r["first_frame_ms"] for r in runs),
        warm_p95_median_ms=statistics.median(r["p95_ms"] for r in runs),
        warm_p99_median_ms=statistics.median(r["p99_ms"] for r in runs))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", default="out/build/x64-release/Release")
    parser.add_argument("--output", required=True)
    parser.add_argument("--runs", type=int, default=6)
    parser.add_argument("--references", action="store_true", help="also run null and native SDL D3D11 controls")
    parser.add_argument("--interaction", action="store_true", help="also measure the offscreen caret/scroll/atlas/resize sequence")
    parser.add_argument("--require-device", help="reject a Spiral run whose queried Vulkan device name differs")
    args = parser.parse_args()
    if args.runs < 2:
        parser.error("at least two alternating repetitions are required")
    if args.references and platform.system() != "Windows":
        parser.error("the D3D11 reference control requires Windows")
    source = pathlib.Path(__file__).resolve().parent.parent
    output = pathlib.Path(args.output).resolve()
    if (output / "results.json").exists():
        parser.error("choose a new output directory; prior measurements are preserved")
    output.mkdir(parents=True, exist_ok=True)
    binaries = output / "binaries"
    binaries.mkdir(exist_ok=True)
    suffix = ".exe" if platform.system() == "Windows" else ""
    vulkan_exe = f"pui_vulkan_spiral_tests{suffix}"
    sdl_exe = f"pui_showcase_tests{suffix}"
    names = [vulkan_exe]
    if args.references:
        names.append(sdl_exe)
    if args.interaction:
        names.append(f"pui_vulkan_interaction_tests{suffix}")
    for name in names:
        shutil.copy2(pathlib.Path(args.bin_dir) / name, binaries / name)
    files = list((source / "include/pufferui/impl").glob("*.inl"))
    files += [source / "include/pufferui/pufferui.h", source / "tests/test_vulkan_spiral.cpp"]
    files += [source / "examples/pui_showcase.cpp", source / "examples/example_common.h",
              source / "examples/tutorial/glass.h"]
    files += [p for p in (source / "src/shaders/vulkan").glob("*") if p.is_file()] + [pathlib.Path(__file__).resolve()]
    if args.interaction:
        files.append(source / "tests/test_vulkan_interaction.cpp")
    files += list((source / "assets/fonts").glob("*.ttf")) + [binaries / name for name in names]
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    modes = [("reference", []), ("cache", ["--cache"]), ("staged", ["--staged"]), ("aa", [])]
    if args.references:
        modes += [("null", []), ("direct3d11", [])]
    records, raw, geometry = [], [], {}
    for tiles in [1150, 5000]:
        for run in range(1, args.runs + 1):
            for name, flags in modes if run % 2 else reversed(modes):
                stem = f"{tiles}-{name}-{run}"
                if name in ["null", "direct3d11"]:
                    cmd = [str(binaries / sdl_exe), "--bench", name, "--tiles", str(tiles)]
                else:
                    cmd = [str(binaries / vulkan_exe), "--bench", "--tiles", str(tiles), *flags]
                start = time.monotonic()
                with (output / f"{stem}.csv").open("w") as f, (output / f"{stem}.log").open("w") as log:
                    result = subprocess.run(cmd, stdout=f, stderr=log)
                process_seconds = time.monotonic() - start
                if result.returncode:
                    raise RuntimeError(f"{stem}: exit {result.returncode}; inspect retained log")
                device_log = (output / f"{stem}.log").read_text()
                if args.require_device and name not in ["null", "direct3d11"]:
                    if f"device={args.require_device} vendor=" not in device_log:
                        raise RuntimeError(f"{stem}: required Vulkan device was not used; inspect retained log")
                rows = list(csv.DictReader((output / f"{stem}.csv").open()))
                assert len(rows) == 180, stem
                for i, row in enumerate(rows):
                    assert int(row["frame"]) == i
                    key = (tiles, "sdl" if name in ["null", "direct3d11"] else "vulkan", i)
                    shape = tuple(row[k] for k in ["count", "draws", "vertices", "indices"])
                    assert key not in geometry or geometry[key] == shape, (stem, i)
                    geometry[key] = shape
                    raw.append(dict(tiles=tiles, variant=name, run=run, **row))
                assert int(rows[-1]["count"]) == tiles + 3
                adding = [float(r["total_ms"]) for r in rows[90:]]
                warm = [float(r["total_ms"]) for r in rows[60:]]
                worst = max(rows, key=lambda r: float(r["total_ms"]))
                records.append(dict(tiles=tiles, variant=name, run=run,
                    process_seconds=process_seconds, first_frame_ms=float(rows[0]["total_ms"]),
                    all_mean_ms=statistics.mean(float(r["total_ms"]) for r in rows),
                    add_mean_ms=statistics.mean(adding), add_max_ms=max(adding),
                    p95_ms=percentile(warm, .95), p99_ms=percentile(warm, .99), worst=worst,
                    device_log=device_log))
                print(stem, "Add", round(records[-1]["add_mean_ms"], 4), "max", worst["total_ms"], flush=True)
    summary = {}
    for tiles in [1150, 5000]:
        summary[str(tiles)] = {}
        for name, flags in modes:
            runs = [r for r in records if r["tiles"] == tiles and r["variant"] == name]
            summary[str(tiles)][name] = summarize(runs)
    if args.interaction:
        interaction = []
        variants = modes[:4]
        for run in range(1, args.runs + 1):
            for name, flags in variants if run % 2 else reversed(variants):
                stem = f"interaction-{name}-{run}"
                start = time.monotonic()
                with (output / f"{stem}.csv").open("w") as f, (output / f"{stem}.log").open("w") as log:
                    result = subprocess.run([str(binaries / f"pui_vulkan_interaction_tests{suffix}"), "--bench", *flags], stdout=f, stderr=log)
                if result.returncode:
                    raise RuntimeError(f"{stem}: exit {result.returncode}; inspect retained log")
                elapsed = time.monotonic() - start
                rows = list(csv.DictReader((output / f"{stem}.csv").open()))
                assert len(rows) == 180
                for i, row in enumerate(rows):
                    assert int(row["frame"]) == i
                    key = ("interaction", i)
                    shape = tuple(row[k] for k in ["draws", "vertices", "indices"])
                    assert key not in geometry or geometry[key] == shape, (stem, i)
                    geometry[key] = shape
                    raw.append(dict(tiles=0, variant=name, run=run, **row))
                values = [float(r["total_ms"]) for r in rows]
                interaction.append(dict(variant=name, run=run, process_seconds=elapsed,
                    all_mean_ms=statistics.mean(values), absolute_max_ms=max(values),
                    p95_ms=percentile(values,.95), p99_ms=percentile(values,.99),
                    resize_frame_ms=values[100],
                    static_caret_mean_ms=statistics.mean(values[3:20]),
                    atlas_mean_ms=statistics.mean(values[20:24]),
                    scroll_mean_ms=statistics.mean(values[40:55]),
                    blur_mean_ms=statistics.mean(values[60:70]),
                    dpi_mean_ms=statistics.mean(values[80:100])))
                print(stem, "mean", round(statistics.mean(values),4), "max", round(max(values),4), flush=True)
        summary["interaction"] = {name: {key:statistics.mean(r[key] for r in interaction if r["variant"]==name)
            for key in ["all_mean_ms","static_caret_mean_ms","atlas_mean_ms","scroll_mean_ms","blur_mean_ms","dpi_mean_ms","resize_frame_ms"]}
            for name, flags in variants}
    else:
        interaction = []
    columns = list(dict.fromkeys(k for r in raw for k in r))
    with (output / "frames.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        writer.writerows(raw)
    assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() == h for p, h in hashes.items())
    manifest = dict(date=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        platform=platform.platform(), processor=platform.processor(),
        required_vulkan_device=args.require_device,
        runtime_environment={k:os.environ.get(k) for k in ["VULKAN_SDK","VK_DRIVER_FILES","VK_ICD_FILENAMES","VK_INSTANCE_LAYERS","SDL_VIDEODRIVER"]},
        head=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip(),
        runs=records, interaction_runs=interaction, summary=summary, frames=len(raw), sha256=hashes,
        frame_csv_sha256=hashlib.sha256((output / "frames.csv").read_bytes()).hexdigest(),
        protocol="1360x860 hidden window, vsync off, 64-size Spiral; 180 frames; dt=1/60; Adds90/120/150; all frames retained",
        interaction_protocol="offscreen 320x240 ->352x256 at100; dt=1/30; atlas20-23; scroll40-42; blur60-69; DPI1.5 at80-99; gamma1.2 at140; recreation inside frame; setup in process elapsed",
        limits=["GPU timestamps are delayed completed submissions, not aligned CPU frames.",
                "Process elapsed includes startup/shutdown; frame 0 includes geometry preparation.",
                "Vulkan and D3D11 device selection may differ; no same-GPU speed ratio is implied.",
                "No foreground FPS, thermal or power certification. No system settings changed."])
    (output / "results.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == "__main__":
    main()
