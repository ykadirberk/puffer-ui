"""Windows diagnostic only: bounded GPU stage queries and C++ allocation stacks.

The normal CMake Release/HarfBuzz/Vulkan dependencies must already be built.
An alternate implementation tree can be a frozen git archive of the baseline.
The public API and actual showcase fixture must be compatible with this tree.
Do not overlap this diagnostic with builds or acceptance timing measurements.
C malloc/calloc/realloc imports from this executable and its static libraries
are traced. Other DLLs, driver heap, aligned C allocation and frees are not traced.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

from run_tests import import_msvc_env


def validate_workload(program, heap_only, integrated, mode):
    if program and not heap_only and mode not in ("integrated_tiles", "integrated_tiles_staged"):
        raise ValueError("--program requires --heap-only unless fine tiles are explicitly forced: "
                         "discarded command queries never execute")
    if integrated and not program:
        raise ValueError("--integrated requires --program")


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--implementation-root", type=Path, default=root)
    parser.add_argument("--build-dir", type=Path, default=root / "out/build/x64-release")
    parser.add_argument("--heap-only", action="store_true",
                        help="do not instrument GPU queries (safe with discarded program tapes)")
    parser.add_argument("--startup", action="store_true",
                        help="also capture module heap allocations before the first frame")
    parser.add_argument("--program", action="store_true",
                        help="trace the actual settled/local whole-program workloads")
    parser.add_argument("--integrated", action="store_true",
                        help="trace only integrated program workloads")
    parser.add_argument("--mode", choices=["sdf", "compute", "cache", "idle", "local", "staged",
                                         "integrated", "integrated_idle", "integrated_local", "integrated_staged",
                                         "integrated_tiles", "integrated_tiles_staged"],
                        help="run only this diagnostic workload")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this stack/symbol collector requires Windows and MSVC")
    try:
        validate_workload(args.program, args.heap_only, args.integrated, args.mode)
    except ValueError as error:
        parser.error(str(error))
    output = args.output.resolve()
    implementation = args.implementation_root.resolve()
    build = args.build_dir.resolve()
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    hb = next(line.split("=", 1)[1] for line in cache.splitlines()
              if line.startswith("harfbuzz_SOURCE_DIR:STATIC="))
    sdk = Path(os.environ["VULKAN_SDK"])
    output.mkdir(parents=True, exist_ok=False)
    import_msvc_env()
    source = root / "tools/profile_vulkan_painter.cpp"
    # The showcase helper temporarily defines/undefines main itself. Rename
    # only the benchmark entry point in a new derived fixture, never edit the
    # original benchmark/tests or rely on a main macro surviving that include.
    benchmark = "bench_vulkan_program.cpp" if args.program else "bench_vulkan_painter.cpp"
    body = (root / "tests" / benchmark).read_text(encoding="utf-8")
    entry = "int main(int argc, char **argv)"
    if body.count(entry) != 1:
        raise ValueError("benchmark entry point changed; update this collector explicitly")
    body = body.replace(entry, "int original_benchmark_main(int argc, char **argv)")
    body = body.replace('#include "test_vulkan_compute_spiral.cpp"',
                        f'#include "{root.as_posix()}/tests/test_vulkan_compute_spiral.cpp"')
    body = body.replace('#define PUFFERUI_PROGRAM_HELPERS "test_vulkan_compute_spiral.cpp"',
                        f'#define PUFFERUI_PROGRAM_HELPERS "{root.as_posix()}/tests/test_vulkan_compute_spiral.cpp"')
    fixture = output / "benchmark.inl"
    fixture.write_text(body, encoding="utf-8")
    executable = output / "profile.exe"
    obj = output / "profile.obj"
    compiler = ["cl", "/nologo", "/std:c++20", "/EHsc", "/O2", "/Ob1", "/MD", "/DNDEBUG",
                "/D_CRT_SECURE_NO_WARNINGS", "/GL", "/Z7", "/DPUFFERUI_ENABLE_VULKAN",
                "/DPUFFERUI_ENABLE_SDL3", "/DPUFFERUI_ENABLE_HARFBUZZ",
                f'/DPUFFERUI_ASSET_DIR="{root.as_posix()}/assets"',
                f'/DPUI_PROFILE_BENCHMARK="{fixture.as_posix()}"']
    compiler += ["/I" + str(p) for p in (implementation / "include", root / "tests", root / "vendored/stb",
                 root / "vendored/SDL/include", build / "vendored/SDL/include-revision",
                 Path(hb) / "src", sdk / "Include")]
    compiler += ["/c", str(source), "/Fo" + str(obj)]
    if args.heap_only:
        compiler.append("/DPUI_PROFILE_HEAP_ONLY")
    libraries = [build / "vendored/SDL/SDL3-static.lib", sdk / "Lib/vulkan-1.lib",
                 build / "_deps/pufferui_harfbuzz-build/harfbuzz-gpu.lib",
                 build / "_deps/pufferui_harfbuzz-build/harfbuzz.lib"]
    libraries = list(map(str, libraries)) + [name + ".lib" for name in
        "dbghelp kernel32 user32 gdi32 winmm imm32 version uuid advapi32 setupapi shell32 hid "
        "dinput8 uiautomationcore comctl32 ole32 oleaut32 winspool comdlg32".split()]
    linker = ["link", "/nologo", "/LTCG", "/DEBUG:FULL", "/INCREMENTAL:NO", "/SUBSYSTEM:CONSOLE",
              "/OUT:" + str(executable), "/PDB:" + str(output / "profile.pdb"), str(obj), *libraries]
    for name, command in (("build", compiler), ("link", linker)):
        with (output / (name + ".log")).open("w") as log:
            subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
    runs = []
    modes = [("sdf", ["--sdf", "--vector-text"]),
             ("compute", ["--compute", "--vector-text"]),
             ("cache", ["--compute", "--vector-text", "--cache"])]
    if args.program:
        modes += [("idle", ["--idle", "--compute", "--vector-text", "--cache"]),
                  ("local", ["--local-change", "--compute", "--vector-text", "--cache"]),
                  ("staged", ["--compute", "--vector-text", "--cache", "--staged"])]
        modes += [("integrated", ["--integrated"]),
                  ("integrated_idle", ["--idle", "--integrated"]),
                  ("integrated_local", ["--local-change", "--integrated"]),
                  ("integrated_staged", ["--staged", "--integrated"]),
                  ("integrated_tiles", ["--integrated", "--no-program-cache"]),
                  ("integrated_tiles_staged", ["--integrated", "--no-program-cache", "--staged"])]
    if args.mode:
        modes = [(name, flags) for name, flags in modes if name == args.mode]
        if not modes:
            parser.error("that diagnostic mode requires --program")
    if args.integrated:
        modes = [(name, flags) for name, flags in modes if name.startswith("integrated")]
    for mode, flags in modes:
        directory = output / mode
        directory.mkdir()
        with (directory / "frames.csv").open("w") as frames, (directory / "run.log").open("w") as log:
            environment = dict(os.environ, PUI_PROFILE_STARTUP="1" if args.startup else "0")
            result = subprocess.run([str(executable), *flags], cwd=directory, stdout=frames,
                                    stderr=log, timeout=180, env=environment)
        runs.append(dict(mode=mode, arguments=flags, exit_code=result.returncode))
        if result.returncode:
            raise RuntimeError(f"diagnostic failed: {mode}; inspect {directory}")
    (output / "receipt.json").write_text(json.dumps(dict(
        protocol="diagnostic only; ordinary/aligned C++ and module C heap stacks; "
                 "not acceptance timings; actual 180-frame 5000-item showcase",
        heap_only=args.heap_only, startup_traced=args.startup, program_workloads=args.program,
        implementation_root=str(implementation), compiler=compiler, linker=linker,
        binary_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(), runs=runs), indent=2),
        encoding="utf-8")
    print(f"Diagnostic retained in {output}")


if __name__ == "__main__":
    main()
