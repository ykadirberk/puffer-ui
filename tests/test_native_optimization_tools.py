"""Reject incomplete/native GPU telemetry rather than trimming cold or peak frames."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("native_measure", Path(__file__).resolve().parents[1]/"tools/compare_vulkan_native_optimizations.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class NativeMeasurementTests(unittest.TestCase):
    def frame_stream(self):
        return "frame,total_ms,resident_bytes\n"+"".join(f"{i},{100 if i == 0 else 200 if i == 127 else 1},4096\n" for i in range(180))

    def test_retains_cold_peak_and_all_gpu_submissions(self):
        rows, result = MODULE.parse_run(self.frame_stream(), "gpu_total_ms=360 gpu_samples=207 frames=180\noptimization_stats hits=15 fills=2\n")
        self.assertEqual(len(rows), 180)
        self.assertEqual(result["cold_ms"], 100)
        self.assertEqual(result["peak_frame"], 127)
        self.assertEqual(result["wall"]["maximum"], 200)
        self.assertEqual(result["gpu_ms"], 2)  # denominator is logical frames, not 207 intervals
        self.assertEqual(result["optimization"], {"hits": 15, "fills": 2})

    def test_rejects_missing_duplicate_or_invalid_frames(self):
        final = "gpu_total_ms=360 gpu_samples=180 frames=180"
        for source in [self.frame_stream().replace("127,200,4096\n", ""),
                       self.frame_stream().replace("127,200,4096", "126,200,4096"),
                       self.frame_stream().replace("127,200,4096", "127,nan,4096")]:
            with self.assertRaises(ValueError):
                MODULE.parse_run(source, final)

    def test_rejects_missing_or_ambiguous_completed_gpu_total(self):
        for final in ["", "gpu_total_ms=1 gpu_samples=0 frames=180",
                      "gpu_total_ms=1 gpu_samples=180 frames=180\n"*2]:
            with self.assertRaises(ValueError):
                MODULE.parse_run(self.frame_stream(), final)

    def test_requires_complete_after_benchmark_telemetry(self):
        final = "gpu_total_ms=360 gpu_samples=180 frames=180\n"
        for counters in ["", "optimization_stats hits=1 fills=2\n"]:
            with self.assertRaises(ValueError):
                MODULE.parse_run(self.frame_stream(), final+counters, require_optimization=True)
        counters = ("optimization_stats hits=15 misses=1 fills=2 coverage_draws=1 "
                    "coverage_bytes=4096 evictions=0 image_bytes=8192\n")
        _, result = MODULE.parse_run(self.frame_stream(), final+counters, require_optimization=True)
        self.assertEqual(result["optimization"]["coverage_bytes"], 4096)


if __name__ == "__main__":
    unittest.main()
