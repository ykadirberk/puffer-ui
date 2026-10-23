#!/usr/bin/env python3
"""Builds and runs every check CI runs. Works on Windows, Linux and macOS.

Usage:  python tools/run_tests.py                 # build + all suites (debug)
        python tools/run_tests.py --quick         # core + pump + performance regressions
        python tools/run_tests.py --no-build      # run what is already built
        python tools/run_tests.py --release       # a release build instead of debug
        python tools/run_tests.py --format        # also clang-format --dry-run --Werror
        python tools/run_tests.py --skip-examples # skip the examples_selftest target

Steps: build, core/pump/performance regressions, golden and performance pixel
baselines, examples_selftest,
API reference drift and tutorial snippets (those two are PowerShell scripts: they run
when `pwsh` (or Windows PowerShell) is available and are skipped otherwise), and
clang-format with --format. Exits 0 only when no step failed.

Windows needs no Developer prompt: the MSVC environment is imported when cl.exe is
not on PATH. Presets: x64-debug / x64-release (Windows), linux-debug, macos-debug;
a release build on Linux/macOS configures out/build/<os>-release directly.
"""
import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IS_WINDOWS = platform.system() == "Windows"
EXE = ".exe" if IS_WINDOWS else ""


def import_msvc_env():
    """Merge vcvars64.bat's environment into ours (Windows, cl.exe not on PATH)."""
    if shutil.which("cl"):
        return
    roots = [os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)")]
    for base in filter(None, roots):
        vs = os.path.join(base, "Microsoft Visual Studio")
        for dirpath, _dirs, files in os.walk(vs):
            if "vcvars64.bat" in files:
                bat = os.path.join(dirpath, "vcvars64.bat")
                print(f"MSVC environment: {bat}")
                out = subprocess.run(f'"{bat}" >nul && set', shell=True, capture_output=True,
                                     text=True).stdout
                for line in out.splitlines():
                    key, sep, value = line.partition("=")
                    if sep:
                        os.environ[key] = value
                return
    sys.exit("cl.exe not found and no vcvars64.bat under Program Files; use a VS Developer shell.")


def powershell():
    for name in ("pwsh", "powershell"):
        if shutil.which(name):
            return name
    return None


class Runner:
    def __init__(self):
        self.results = []  # (name, status, seconds)

    def step(self, name, cmd=None, fn=None, cwd=ROOT):
        print(f"\n==== {name}", flush=True)
        start = time.time()
        try:
            code = fn() if fn else subprocess.call(cmd, cwd=cwd)
        except (OSError, RuntimeError) as err:
            print(err)
            code = 1
        status = "ok" if code == 0 else "FAIL"
        if status == "FAIL":
            print(f"FAILED: {name}")
        self.results.append((name, status, time.time() - start))
        return code == 0

    def skip(self, name, why):
        print(f"\n==== {name}\nskipped: {why}")
        self.results.append((name, "skip", 0.0))


def main():
    ap = argparse.ArgumentParser(description="Build and run every PufferUI check.")
    ap.add_argument("--quick", action="store_true", help="core + pump + performance + showcase regressions")
    ap.add_argument("--no-build", action="store_true", help="run what is already built")
    ap.add_argument("--release", action="store_true", help="release build instead of debug")
    ap.add_argument("--format", action="store_true", help="also run clang-format --dry-run")
    ap.add_argument("--skip-examples", action="store_true", help="skip examples_selftest")
    ap.add_argument("--vulkan", action="store_true", help="enable and verify the optional Vulkan backend (SDK + runtime required)")
    args = ap.parse_args()

    system = platform.system()
    if IS_WINDOWS:
        import_msvc_env()
        preset = "x64-release" if args.release else "x64-debug"
    elif system == "Darwin":
        preset = "macos-debug"
    else:
        preset = "linux-debug"
    config = "Release" if args.release else "Debug"
    # a release build off Windows has no preset: configure it directly
    manual_release = args.release and not IS_WINDOWS
    build_name = preset.split("-")[0] + "-release" if manual_release else preset
    build_dir = os.path.join(ROOT, "out", "build", build_name)
    bin_dir = os.path.join(build_dir, config)

    r = Runner()
    cache_path = os.path.join(build_dir, "CMakeCache.txt")
    def enabled(name):
        if not os.path.exists(cache_path):return False
        with open(cache_path,encoding="utf8") as cache:
            return any(line.strip()==name+":BOOL=ON" for line in cache)

    if not args.no_build:
        if not os.path.exists(cache_path):
            if manual_release:
                gen = ["-G", "Ninja"] if shutil.which("ninja") else []
                r.step("configure (release)", ["cmake", "-S", ROOT, "-B", build_dir, *gen,
                                               "-DCMAKE_BUILD_TYPE=Release"])
            else:
                r.step(f"configure ({preset})", ["cmake", "--preset", preset])
        if args.vulkan or enabled("PUFFERUI_WITH_VULKAN"):
            if not r.step("configure Vulkan registry", ["cmake", "-S", ROOT, "-B", build_dir,
                                                       "-DPUFFERUI_WITH_VULKAN=ON"]):
                return 1

    vulkan_cases = []
    if args.vulkan and not enabled("PUFFERUI_WITH_VULKAN"):
        print("Vulkan was requested but this build does not contain it.")
        return 1
    if enabled("PUFFERUI_WITH_VULKAN"):
        registry = os.path.join(build_dir, f"pufferui-vulkan-tests-{config}.json")
        try:
            with open(registry, encoding="utf8") as source:
                vulkan_cases = [case for case in json.load(source)
                                if not args.quick or not case["full_only"]]
        except (OSError, ValueError, KeyError) as err:
            print(f"Cannot read Vulkan test registry: {err}; rerun without --no-build.")
            return 1

    if not args.no_build:
        targets = ["pui_core_tests", "pui_pump_tests", "pui_perf_tests", "pui_memory_tests", "pui_preparation_tests", "pui_showcase_tests"]
        targets += list(dict.fromkeys(case["target"] for case in vulkan_cases))
        if not args.quick:
            targets.append("pui_golden_tests")
            targets.append("pui_perf_pixels")
            targets.append("pui_core_perf_pixels")
        # examples_selftest builds *and runs* every example: its own step below
        if not r.step(f"build ({', '.join(targets)})",
                      ["cmake", "--build", build_dir, "--target", *targets]):
            print("Build failed; existing binaries have not been validated against these sources.")
            return 1

    def suite(exe, name, extra=()):
        path = os.path.join(bin_dir, exe + EXE)
        if not os.path.exists(path):
            print(f"{path} is not built (drop --no-build?)")
            r.results.append((name, "FAIL", 0.0))
            return
        r.step(name, [path, *extra])

    suite("pui_core_tests", "core tests")
    suite("pui_perf_tests", "performance regressions")
    suite("pui_memory_tests", "memory performance regressions")
    suite("pui_preparation_tests", "geometry preparation regressions")
    suite("pui_showcase_tests", "showcase regressions")
    suite("pui_pump_tests", "pump tests (SDL event routing)")
    r.step("Python tool regressions", [sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*tools.py"])
    for case in vulkan_cases:
        title = "Vulkan " + " ".join([case["target"], *case["args"]])
        suite(case["target"], title, case["args"])
    if not args.quick:
        suite("pui_golden_tests", "golden-image tests")
        suite("pui_perf_pixels", "performance pixel baselines")
        suite("pui_core_perf_pixels", "core performance pixel baselines")
        r.step("1150-tile Spiral golden frames", [os.path.join(bin_dir, "pui_showcase_tests" + EXE), "--pixels"])
        r.step("5000-tile Spiral golden frames", [os.path.join(bin_dir, "pui_showcase_tests" + EXE), "--pixels", "--tiles", "5000"])
        r.step("5000-tile Spiral peak golden frames", [os.path.join(bin_dir, "pui_showcase_tests" + EXE), "--peak-pixels"])
        r.step("external consumer builds", [sys.executable, os.path.join(ROOT, "tools", "check_consumers.py")])
        if enabled("PUFFERUI_WITH_VULKAN") and enabled("PUFFERUI_WITH_SDL3"):
            r.step("installed Vulkan/core/SDL consumers", [sys.executable, os.path.join(ROOT, "tools", "check_vulkan_consumers.py"),
                   "--build", build_dir, "--config", "Release" if args.release else "Debug"])
        if not args.skip_examples:
            r.step("examples_selftest (every example, offscreen)",
                   ["cmake", "--build", build_dir, "--target", "examples_selftest"])
        ps = powershell()
        for name, script, extra in (("API reference drift", "gen_api.ps1", ["-Check"]),
                                    ("tutorial snippets", "check_tutorial.ps1", [])):
            if ps:
                cmd = [ps, "-NoProfile", "-File", os.path.join(ROOT, "tools", script), *extra]
                r.step(name, cmd)
            else:
                r.skip(name, "needs pwsh (PowerShell)")

    if args.format:
        if not shutil.which("clang-format"):
            r.skip("clang-format", "clang-format 22.1.3 not on PATH (pip install clang-format==22.1.3)")
        else:
            def fmt():
                files = subprocess.run(["git", "ls-files", "*.h", "*.cpp", "*.inl", ":!:vendored"],
                                       cwd=ROOT, capture_output=True, text=True).stdout.split()
                return subprocess.call(["clang-format", "--dry-run", "--Werror", *files], cwd=ROOT)
            r.step("clang-format", fn=fmt)

    print(f"\n================ summary ({preset}{' release' if manual_release else ''})")
    for name, status, secs in r.results:
        print(f"{status:<5}  {name}  ({secs:.1f}s)")
    failed = sum(1 for _n, s, _t in r.results if s == "FAIL")
    if failed:
        print(f"{failed} step(s) failed")
        return 1
    print("all steps passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
