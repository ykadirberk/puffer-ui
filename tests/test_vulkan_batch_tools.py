"""Immutable full-frame fixture packaging and safe reference extraction."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("compare_batches", ROOT / "tools/compare_vulkan_batches.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BatchReferenceTests(unittest.TestCase):
    def test_immutable_reference_archive_is_complete(self):
        archive = ROOT / "tests/golden/vector_batch/frames.zip"
        with tempfile.TemporaryDirectory() as directory:
            output = module.unpack_references(archive, Path(directory) / "frames")
            self.assertEqual(len(list(output.glob("frame-*.rgba"))), 180)
            with self.assertRaises(FileExistsError):
                module.unpack_references(archive, output)

    def test_rejects_unexpected_paths_before_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "bad.zip"
            with zipfile.ZipFile(archive, "w") as output:
                output.writestr("../escape.rgba", "no")
            target = Path(directory) / "target"
            with self.assertRaises(RuntimeError):
                module.unpack_references(archive, target)
            self.assertFalse(target.exists())
            self.assertFalse((Path(directory).parent / "escape.rgba").exists())

    def test_parser_preserves_cold_and_peaks_and_rejects_incomplete_runs(self):
        csv = "frame,total_ms\n" + "".join(f"{i},{53 if i == 0 else i / 100}\n" for i in range(180))
        rows, summary = module.parse(csv, "gpu_total_ms=180.0 gpu_samples=180 setup_ms=2.0")
        self.assertEqual(len(rows), 180)
        self.assertEqual(summary["cold_ms"], 53)
        self.assertEqual(summary["top10"][0], (0, 53))
        self.assertEqual(summary["wall"]["maximum"], 53)
        with self.assertRaises(RuntimeError):
            module.parse(csv.rsplit("\n", 2)[0] + "\n", "")
        with self.assertRaises(RuntimeError):
            module.parse(csv.replace("0,53", "0,nan"), "")


if __name__ == "__main__":
    unittest.main()
