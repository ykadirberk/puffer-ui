"""Complete immutable simple-path fixtures and all-frame qualification guards."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("vector_cpu_compare", ROOT / "tools/compare_vulkan_vector_cpu.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class VectorCpuReferenceTests(unittest.TestCase):
    def sample(self, frames=180, capture=0):
        header = "frame,total_ms,draw_cpp_allocations,draws,upload_bytes,draw_buffer_allocations\n"
        data = "".join(f"{i},0.5,0,1,640000,0\n" for i in range(frames))
        return header + data + f"180 exact simple Slug frames passed (direct, capture={capture})\n"

    def test_new_fixture_contains_all_original_full_frames(self):
        archive = ROOT / "tests/golden/vector_cpu/frames.zip"
        with tempfile.TemporaryDirectory() as directory:
            output = module.batches.unpack_references(archive, Path(directory) / "frames")
            self.assertEqual(len(list(output.glob("frame-*.rgba"))), 180)
            self.assertTrue(all(p.stat().st_size == 512 * 512 * 4 for p in output.glob("*.rgba")))
            # Rotation/currentColor must produce a changing scene, not 180
            # copies of a static or empty frame. Pixel quantization permits repeats.
            self.assertGreater(len({module.batches.digest(p) for p in output.glob("*.rgba")}), 120)
            with self.assertRaises(FileExistsError):
                module.batches.unpack_references(archive, output)

    def test_all_frame_parser_rejects_incomplete_and_capture_output(self):
        rows, summary = module.parse_simple_golden(self.sample(), "readbacks=180")
        self.assertEqual([int(r["frame"]) for r in rows], list(range(180)))
        self.assertEqual(summary["readbacks"], 180)
        for text in [self.sample(179), self.sample(capture=1), self.sample().rsplit("\n", 2)[0]]:
            with self.assertRaises(RuntimeError):
                module.parse_simple_golden(text, "readbacks=180")

    def test_all_frame_parser_requires_readbacks_and_no_draw_allocations(self):
        for diagnostics in ["", "readbacks=0", "readbacks=179"]:
            with self.assertRaises(RuntimeError):
                module.parse_simple_golden(self.sample(), diagnostics)
        text = self.sample().replace("90,0.5,0,1", "90,0.5,1,1")
        with self.assertRaises(RuntimeError):
            module.parse_simple_golden(text, "readbacks=180")


if __name__ == "__main__":
    unittest.main()
