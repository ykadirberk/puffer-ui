"""Integrity of cold-frame/startup telemetry; no GPU or SDK required."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compare_vulkan_startup import log_number, parse_timing


class StartupTelemetry(unittest.TestCase):
    def output(self):
        return "frame,total_ms\n" + "".join(f"{f},{53 if f == 0 else 1}\n" for f in range(180))

    def test_cold_maximum_is_retained_in_all_statistics(self):
        rows, result = parse_timing(self.output(), "gpu_total_ms=90 gpu_samples=181 "
                                   "setup_ms=120 preparation_ms=8", startup=True)
        self.assertEqual(len(rows), 180)
        self.assertEqual(result["cold_ms"], 53)
        self.assertEqual(result["max_frame"], 0)
        self.assertEqual(result["max_ms"], 53)
        self.assertEqual(result["mean_ms"], 232 / 180)
        self.assertEqual(result["setup_frame0_ms"], 173)
        self.assertEqual(result["setup_first_two_ms"], 174)
        self.assertEqual(result["gpu_ms"], .5)

    def test_exact_log_key_does_not_match_a_suffix(self):
        self.assertEqual(log_number("gpu_prewarm_ms=8 prewarm_ms=7", "prewarm_ms"), 7)
        self.assertEqual(log_number("setup_plus_frame0_ms=140 setup_ms=120", "setup_ms"), 120)
        with self.assertRaises(ValueError):
            log_number("gpu_prewarm_ms=8", "prewarm_ms")
        with self.assertRaises(ValueError):
            log_number("setup_ms=120 setup_ms=121", "setup_ms")

    def test_missing_reordered_and_nonfinite_frames_are_rejected(self):
        for output in [self.output().replace("179,1\n", ""),
                       self.output().replace("1,1\n2,1", "2,1\n1,1"),
                       self.output().replace("0,53", "0,nan"),
                       self.output().replace("0,53", "0,-1")]:
            with self.subTest(output=output[:35]), self.assertRaises(ValueError):
                parse_timing(output, "", native=False)

    def test_null_control_requires_no_native_telemetry(self):
        _, result = parse_timing(self.output(), "", native=False)
        self.assertEqual(result["max_ms"], 53)
        self.assertNotIn("gpu_ms", result)


if __name__ == "__main__":
    unittest.main()
