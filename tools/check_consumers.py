"""Build external consumers, both add_subdirectory and installed package paths."""
import argparse
import pathlib
import subprocess
import tempfile

from run_tests import ROOT, IS_WINDOWS, import_msvc_env


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdl-build", help="Existing complete build whose SDL package to install")
    args = parser.parse_args()
    if IS_WINDOWS:
        import_msvc_env()
    source = pathlib.Path(ROOT)
    fixture = source / "tests" / "consumer"

    def run(*cmd):
        subprocess.run([str(x) for x in cmd], check=True)

    generated = (source / "out" / "consumer-checks").resolve()
    generated.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="run-", dir=generated) as scratch:
        root = pathlib.Path(scratch).resolve()
        assert root.is_relative_to(generated)
        common = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
                  "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded"]
        library = root / "library"
        prefix = root / "install"
        run("cmake", "-S", source, "-B", library, *common,
            "-DPUFFERUI_WITH_SDL3=OFF", "-DPUFFERUI_BUILD_TESTS=OFF",
            "-DPUFFERUI_BUILD_EXAMPLES=OFF", f"-DCMAKE_INSTALL_PREFIX={prefix}")
        run("cmake", "--build", library)
        run("cmake", "--install", library)
        for kind, extra in (("in-tree", [f"-DPUFFERUI_SOURCE={source}"]),
                            ("installed", [f"-DCMAKE_PREFIX_PATH={prefix}"])):
            build = root / kind
            run("cmake", "-S", fixture, "-B", build, *common, *extra)
            run("cmake", "--build", build)
            run(build / ("consumer.exe" if IS_WINDOWS else "consumer"))
        if args.sdl_build:
            sdl_prefix = root / "sdl-install"
            run("cmake", "--build", pathlib.Path(args.sdl_build).resolve())
            run("cmake", "--install", pathlib.Path(args.sdl_build).resolve(), "--prefix", sdl_prefix)
            build = root / "sdl-consumer"
            run("cmake", "-S", fixture, "-B", build, *common,
                f"-DCMAKE_PREFIX_PATH={sdl_prefix}", "-DCONSUMER_SDL=ON")
            run("cmake", "--build", build)
            run(build / ("consumer.exe" if IS_WINDOWS else "consumer"))
    print("all consumer checks passed")


if __name__ == "__main__":
    main()
