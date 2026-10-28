"""The paired Vulkan gate must reject invalid workloads and retain cold/outlier frames."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from compare_vulkan_locality import checked_summary, cohort_summary, workload_flags
from profile_vulkan_painter import validate_workload


class LocalityGateTests(unittest.TestCase):
    def test_discardable_programs_cannot_create_gpu_timestamp_queries(self):
        for mode in (None, "integrated", "integrated_idle", "integrated_local", "integrated_staged"):
            with self.assertRaisesRegex(ValueError, "discarded command queries"):
                validate_workload(True, False, False, mode)
            validate_workload(True, True, False, mode)
        for mode in ("integrated_tiles", "integrated_tiles_staged"):
            validate_workload(True, False, True, mode)
        with self.assertRaisesRegex(ValueError, "requires --program"):
            validate_workload(False, True, True, None)
        validate_workload(False, False, False, "cache")

    def rows(self):
        return [dict(frame=str(frame), count=str(5000 + sum(frame >= add for add in (90, 120, 150))),
                     total_ms="100" if frame == 90 else "20" if frame == 0 else "1",
                     readbacks="0", begin_ms="0", build_ms="1", end_ms="0", slot_wait_ms="0",
                     completed_gpu_ms="0.25", gpu_samples="1", upload_bytes="0",
                     metadata_bytes="0", program_hits="0") for frame in range(180)]

    def test_production_and_fine_keep_the_same_integrated_renderer(self):
        for upload in ("direct", "staged"):
            for mode in ("moving", "idle", "local"):
                production = workload_flags("production", mode, upload)
                fine = workload_flags("fine", mode, upload)
                self.assertEqual([flag for flag in fine if flag != "--no-program-cache"], production)
                self.assertEqual(production[0], "--integrated")
        with self.assertRaises(ValueError):
            workload_flags("unknown", "moving", "direct")

    def test_all_frames_cold_frame_and_add_spike_are_retained(self):
        rows = self.rows()
        summary = checked_summary(rows, "moving")
        self.assertEqual(summary["first_ms"], 20)
        self.assertEqual((summary["max_frame"], summary["max_ms"]), (90, 100))
        self.assertEqual(summary["add_ms"], [100, 1, 1])
        self.assertEqual(summary["gpu_samples"], 180)
        cohort = cohort_summary(rows * 2)
        self.assertEqual((cohort["frames"], cohort["peak_frame"], cohort["max_ms"]), (360, 90, 100))

    def test_incomplete_wrong_add_and_readback_workloads_fail(self):
        cases = [self.rows()[:-1], self.rows(), self.rows(), self.rows()]
        cases[1][120]["count"] = "5001"
        cases[2][33]["readbacks"] = "1"
        cases[3][42]["frame"] = "43"
        for rows in cases:
            with self.assertRaises(ValueError):
                checked_summary(rows, "moving")


if __name__ == "__main__":
    unittest.main()
