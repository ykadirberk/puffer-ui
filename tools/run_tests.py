#!/usr/bin/env python3
"""Builds and runs every check CI runs. Works on Windows, Linux and macOS.

Usage:  python tools/run_tests.py                 # build + all suites (debug)
        python tools/run_tests.py --quick         # core + pump tests only
        python tools/run_tests.py --no-build      # run what is already built
        python tools/run_tests.py --release       # a release build instead of debug
        python tools/run_tests.py --format        # also clang-format --dry-run --Werror
        python tools/run_tests.py --skip-examples # skip the examples_selftest target

Steps: build, pui_core_tests, pui_pump_tests, pui_golden_tests, examples_selftest,
API reference drift and tutorial snippets (those two are PowerShell scripts: they run
when `pwsh` (or Windows PowerShell) is available and are skipped otherwise), and
clang-format with --format. Exits 0 only when no step failed.

Windows needs no Developer prompt: the MSVC environment is imported when cl.exe is
not on PATH. Presets: x64-debug / x64-release (Windows), linux-debug, macos-debug;
a release build on Linux/macOS configures out/build/<os>-release directly.
"""
import argparse
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
    ap.add_argument("--quick", action="store_true", help="core + pump tests only")
    ap.add_argument("--no-build", action="store_true", help="run what is already built")
    ap.add_argument("--release", action="store_true", help="release build instead of debug")
    ap.add_argument("--format", action="store_true", help="also run clang-format --dry-run")
    ap.add_argument("--skip-examples", action="store_true", help="skip examples_selftest")
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

    if not args.no_build:
        if not os.path.exists(os.path.join(build_dir, "CMakeCache.txt")):
            if manual_release:
                gen = ["-G", "Ninja"] if shutil.which("ninja") else []
                r.step("configure (release)", ["cmake", "-S", ROOT, "-B", build_dir, *gen,
                                               "-DCMAKE_BUILD_TYPE=Release"])
            else:
                r.step(f"configure ({preset})", ["cmake", "--preset", preset])
        targets = ["pui_core_tests", "pui_pump_tests"]
        if not args.quick:
            targets.append("pui_golden_tests")
        # examples_selftest builds *and runs* every example: its own step below
        r.step(f"build ({', '.join(targets)})",
               ["cmake", "--build", build_dir, "--target", *targets])

    def suite(exe, name):
        path = os.path.join(bin_dir, exe + EXE)
        if not os.path.exists(path):
            print(f"{path} is not built (drop --no-build?)")
            r.results.append((name, "FAIL", 0.0))
            return
        r.step(name, [path])

    suite("pui_core_tests", "core tests")
    suite("pui_pump_tests", "pump tests (SDL event routing)")
    if not args.quick:
        suite("pui_golden_tests", "golden-image tests")
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
