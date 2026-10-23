"""Launch the native showcase; repair stale driver discovery only in its child environment.

No registry writes, driver installation, or persistent environment changes.
Explicit VK_DRIVER_FILES/VK_ICD_FILENAMES settings are respected.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def missing_driver_entries():
    if sys.platform != "win32":
        return []
    import winreg
    entries = []
    key = r"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as display:
        index = 0
        while True:
            try:
                name = winreg.EnumKey(display, index)
            except OSError:
                break
            index += 1
            try:
                with winreg.OpenKey(display, name) as adapter:
                    paths = winreg.QueryValueEx(adapter, "VulkanDriverName")[0]
                    description = winreg.QueryValueEx(adapter, "DriverDesc")[0]
                for value in [paths] if isinstance(paths, str) else paths:
                    path = Path(os.path.expandvars(value))
                    if not path.is_file():
                        entries.append((description, path))
            except OSError:
                continue
    return entries


def candidates(stale):
    # Stay within the registered DriverStore package family. Do not search
    # arbitrary DLLs or add unrelated vendor drivers to the process.
    folder = stale.parent
    if folder.parent.name.lower() != "filerepository" or "_amd64_" not in folder.name:
        return []
    family = folder.name.split("_amd64_", 1)[0] + "_amd64_*"
    paths = [p for p in folder.parent.glob(family + "/" + stale.name) if p.is_file()]
    return sorted(paths, key=lambda p: p.stat().st_mtime_ns, reverse=True)


def probe(executable, environment):
    try:
        result = subprocess.run([str(executable), "--vulkan-device-info"],
                                env=environment, capture_output=True, text=True, timeout=20)
        if result.returncode:
            return None
        fields = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
        if not fields.get("device") or "type" not in fields:
            return None
        fields["type"] = int(fields["type"])
        return fields
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return None


def select_driver(executable, environment, required_device=None, manifest=None):
    child = dict(environment)
    explicit = manifest is not None or any(child.get(k) for k in ["VK_DRIVER_FILES", "VK_ICD_FILENAMES"])
    if manifest is not None:
        manifest = Path(manifest).resolve()
        if not manifest.is_file():
            raise RuntimeError(f"Driver manifest does not exist: {manifest}")
        child["VK_DRIVER_FILES"] = str(manifest)
    current = probe(executable, child)
    matches = lambda info: info and (required_device is None or info["device"] == required_device)
    if explicit:
        if not matches(current):
            raise RuntimeError("Explicit driver selection failed or did not expose the requested device.")
        return child, current
    if matches(current) and current["type"] == 2:  # Vulkan DISCRETE_GPU
        return child, current
    try:
        missing = missing_driver_entries()
    except OSError:
        missing = []
    for description, stale in missing:
        if required_device is not None and description != required_device:
            continue
        for path in candidates(stale):
            attempt = dict(child, VK_DRIVER_FILES=str(path))
            found = probe(executable, attempt)
            if matches(found) and found["device"] == description and found["type"] == 2:
                return attempt, found
    if matches(current) and not missing:
        return child, current
    raise RuntimeError("No verified driver for the requested/preferred discrete GPU was found. "
                       "Repair the GPU driver's Vulkan registration or pass --driver-manifest.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT/"out/build/x64-release/Release/pui_showcase_vulkan.exe")
    parser.add_argument("--device", help="require this exact queried Vulkan device name")
    parser.add_argument("--driver-manifest", type=Path, help="explicit driver manifest for this child process")
    parser.add_argument("--dry-run", action="store_true", help="verify and print selection without opening a window")
    parser.add_argument("showcase_args", nargs=argparse.REMAINDER, help="arguments after -- go to the showcase")
    args = parser.parse_args(argv)
    executable = args.exe.resolve()
    if not executable.is_file():
        parser.error(f"Missing {executable}; build target pui_showcase_vulkan first.")
    try:
        environment, info = select_driver(executable, os.environ, args.device, args.driver_manifest)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Vulkan device: {info['device']}", flush=True)
    if environment.get("VK_DRIVER_FILES"):
        print(f"Child-process driver: {environment['VK_DRIVER_FILES']}", flush=True)
    if args.dry_run:
        return 0
    forwarded = args.showcase_args
    if forwarded[:1] == ["--"]:
        forwarded = forwarded[1:]
    return subprocess.run([str(executable), *forwarded], env=environment).returncode


if __name__ == "__main__":
    sys.exit(main())
