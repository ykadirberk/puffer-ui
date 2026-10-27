"""Portable guards for all-frame integrated/native comparison receipts."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

path = Path(__file__).resolve().parents[1] / "tools/compare_vulkan_integrated.py"
spec = importlib.util.spec_from_file_location("compare_integrated", path)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class IntegratedProtocolTests(unittest.TestCase):
    def test_all_six_three_way_orders_rotate(self):
        self.assertEqual(len({tuple(tool.side_order(i)) for i in range(6)}), 6)
        for i in range(12):
            self.assertEqual(set(tool.side_order(i)), {"before", "after_native", "after"})

    def test_every_actual_add_frame_is_checked(self):
        rows = [dict(count=5000 + sum(f >= add for add in (90, 120, 150))) for f in range(180)]
        tool.validate_counts(rows, "moving")
        rows[90]["count"] = 5000
        with self.assertRaises(ValueError):
            tool.validate_counts(rows, "moving")
        tool.validate_counts([dict(count=5000)] * 180, "idle")
        tool.validate_counts([dict(count=5000)] * 180, "local")

    def test_failed_process_keeps_its_partial_frames_and_diagnostics(self):
        temporary_root = Path(__file__).resolve().parents[1] / "out" / "test-tmp"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_root, prefix="integrated-protocol-") as directory:
            stem = Path(directory) / "failed"
            error = subprocess.CalledProcessError(7, ["probe"], "frame,count\n0,5000\n", "failure")
            with patch.object(tool, "run_native", side_effect=error):
                with self.assertRaises(subprocess.CalledProcessError):
                    tool.retain_native(["probe"], stem)
            self.assertEqual(stem.with_suffix(".csv").read_text(), "frame,count\n0,5000\n")
            self.assertEqual(stem.with_suffix(".log").read_text(), "failure")

    def test_maximum_retains_first_frame_even_when_percentile_does_not(self):
        samples = [100] + [1] * 179
        self.assertEqual(tool.percentile(samples, .99), 1)
        self.assertEqual(max(samples), 100)


if __name__ == "__main__":
    unittest.main()
