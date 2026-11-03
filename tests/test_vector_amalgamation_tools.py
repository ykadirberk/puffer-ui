"""The single-header distribution must contain nested private SVG/path slices."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VectorAmalgamation(unittest.TestCase):
    @unittest.skipUnless(shutil.which("pwsh"), "amalgamation requires PowerShell")
    def test_nested_namespace_and_class_slices_are_expanded_in_place(self):
        generated = ROOT / "out/amalgamation-checks"
        generated.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="run-", dir=generated) as scratch:
            self.assertTrue(Path(scratch).resolve().is_relative_to(generated.resolve()))
            output = Path(scratch) / "pufferui_single.h"
            subprocess.run([shutil.which("pwsh"), "-NoProfile", "-File", str(ROOT / "tools/amalgamate.ps1"),
                            "-Out", str(output)], check=True, capture_output=True)
            text = output.read_text(encoding="utf-8")
            self.assertFalse(re.search(r'^\s*#include "[^"\n]+\.inl"', text, re.M))
            for source in ["vector_svg_path", "vector_svg_xml", "vector_svg_namespaces", "vector_svg_style",
                           "vector_svg_geometry", "vector_svg_scene", "vector_svg_gradients", "vector_svg_clips",
                           "renderer_vector_boolean", "renderer_vector_boundary"]:
                lines = (ROOT / f"include/pufferui/impl/{source}.inl").read_text().splitlines()[4:]
                definition = next(line for line in lines if line.strip() and not line.lstrip().startswith(("//", "#")))
                self.assertIn(definition, text, source)


if __name__ == "__main__":
    unittest.main()
