"""Fresh Vulkan configurations include HarfBuzz/Slug without changing core defaults."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class VulkanDependencyDefaults(unittest.TestCase):
    def configure(self, vulkan, harfbuzz=None):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix="pui-default-") as directory:
            path = Path(directory)
            # Execute the option and FetchContent route with a local dependency
            # stub; option tests must not download or compile HarfBuzz.
            dependency = path / "harfbuzz"
            dependency.mkdir()
            (dependency / "CMakeLists.txt").write_text(
                "set_property(GLOBAL PROPERTY dependency_requested TRUE)\n", encoding="utf-8")
            source = f'''cmake_minimum_required(VERSION 3.21)
project(DefaultDependency NONE)
set(PUFFERUI_WITH_VULKAN {"ON" if vulkan else "OFF"})
set(FETCHCONTENT_SOURCE_DIR_PUFFERUI_HARFBUZZ "${{CMAKE_SOURCE_DIR}}/harfbuzz" CACHE PATH "" FORCE)
include("{root.as_posix()}/cmake/PufferUIHarfBuzz.cmake")
get_property(requested GLOBAL PROPERTY dependency_requested)
file(WRITE "${{CMAKE_BINARY_DIR}}/result.txt" "${{PUFFERUI_WITH_HARFBUZZ}};${{requested}}")
'''
            (path / "CMakeLists.txt").write_text(source, encoding="utf-8")
            command = ["cmake", "-S", str(path), "-B", str(path / "build")]
            if shutil.which("ninja"):
                command[1:1] = ["-G", "Ninja"]
            if harfbuzz is not None:
                command.append("-DPUFFERUI_WITH_HARFBUZZ=" + ("ON" if harfbuzz else "OFF"))
            result = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return (path / "build/result.txt").read_text().split(";")

    def test_fresh_vulkan_enables_slug_dependency(self):
        self.assertEqual(self.configure(True), ["ON", "TRUE"])

    def test_core_default_remains_independent(self):
        self.assertEqual(self.configure(False), ["OFF", ""])

    def test_explicit_opt_out_is_preserved(self):
        self.assertEqual(self.configure(True, False), ["OFF", ""])

    def test_explicit_opt_in_is_preserved(self):
        self.assertEqual(self.configure(False, True), ["ON", "TRUE"])


if __name__ == "__main__":
    unittest.main()
