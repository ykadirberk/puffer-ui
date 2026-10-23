"""Verify installed Vulkan and SDK-independent core/SDL consumer boundaries."""
import argparse
import pathlib
import subprocess
import tempfile

from run_tests import ROOT, IS_WINDOWS, import_msvc_env


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", required=True, help="Complete Vulkan-enabled build to install")
    parser.add_argument("--config", choices=["Debug", "Release"], default="Debug")
    args = parser.parse_args()
    if IS_WINDOWS:
        import_msvc_env()
    source = pathlib.Path(ROOT)
    generated = source / "out" / "vulkan-consumer-checks"
    generated.mkdir(parents=True, exist_ok=True)

    def run(*cmd):
        subprocess.run([str(x) for x in cmd], check=True)

    with tempfile.TemporaryDirectory(prefix="run-", dir=generated) as scratch:
        root = pathlib.Path(scratch).resolve()
        assert root.is_relative_to(generated.resolve())
        prefix = root / "install"
        run("cmake", "--install", pathlib.Path(args.build).resolve(), "--prefix", prefix)
        common = ["-G", "Ninja", f"-DCMAKE_BUILD_TYPE={args.config}",
                  "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded", f"-DCMAKE_PREFIX_PATH={prefix}"]
        for kind in ["core", "sdl", "vulkan"]:
            fixture = source / "tests" / ("consumer_vulkan" if kind == "vulkan" else "consumer")
            build = root / kind
            extra = [] if kind == "vulkan" else ["-DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=TRUE"]
            if kind == "sdl":
                extra.append("-DCONSUMER_SDL=ON")
            run("cmake", "-S", fixture, "-B", build, *common, *extra)
            run("cmake", "--build", build)
            run(build / ("consumer.exe" if IS_WINDOWS else "consumer"))
    print("all installed Vulkan/core/SDL consumer checks passed")


if __name__ == "__main__":
    main()
