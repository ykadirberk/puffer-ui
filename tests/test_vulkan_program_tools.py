"""Portable regressions for the uncensored program timing protocol."""
import importlib.util
from pathlib import Path
import unittest


path = Path(__file__).resolve().parents[1] / "tools/compare_vulkan_program.py"
spec = importlib.util.spec_from_file_location("compare_program", path)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)
gpu_spec = importlib.util.spec_from_file_location("measure_gpu", path.with_name("measure_vulkan_gpu.py"))
gpu = importlib.util.module_from_spec(gpu_spec)
gpu_spec.loader.exec_module(gpu)


class ProgramProtocolTests(unittest.TestCase):
    def rows(self, retained=False):
        return [dict(frame=str(i), count="5000" if retained else "5003", readbacks="0",
                     total_ms="100" if i == 0 else "1", begin_ms="0.1", build_ms="0.7",
                     end_ms="0.2", slot_wait_ms="0", completed_gpu_ms="0.3",
                     gpu_samples="1", upload_bytes="64", metadata_bytes="128", program_hits="0")
                for i in range(180)]

    def test_first_frame_and_every_outlier_are_retained(self):
        result = tool.summarize_run(self.rows(), retained=False)
        self.assertEqual(result["max_frame"], 0)
        self.assertEqual(result["first_ms"], 100)
        self.assertAlmostEqual(result["mean_ms"], 279 / 180)
        self.assertIn(0, result["peak_frames"])
        self.assertEqual(result["add_ms"], [1, 1, 1])

    def test_incomplete_or_readback_stream_is_rejected(self):
        with self.assertRaises(ValueError):
            tool.summarize_run(self.rows()[:-1], retained=False)
        rows = self.rows()
        rows[90]["readbacks"] = "1"
        with self.assertRaises(ValueError):
            tool.summarize_run(rows, retained=False)

    def test_retained_protocol_has_no_additions(self):
        self.assertEqual(len(tool.modes_for(True)), 8)
        self.assertEqual(len(tool.modes_for(False)), 5)
        tool.summarize_run(self.rows(True), retained=True)
        with self.assertRaises(ValueError):
            tool.summarize_run(self.rows(True), retained=False)

    def test_pacing_changes_only_the_explicit_native_option(self):
        for retained in (False, True):
            for flags in tool.modes_for(retained).values():
                self.assertEqual(tool.native_command(Path("native.exe"), flags, 0),
                                 ["native.exe", *flags])
                self.assertEqual(tool.native_command(Path("native.exe"), flags, 60),
                                 ["native.exe", *flags, "--hz", "60"])
        for invalid in (-1, 1001):
            with self.assertRaises(ValueError):
                tool.native_command(Path("native.exe"), [], invalid)

    def test_gpu_percentages_keep_adapters_and_physical_engines_separate(self):
        samples = [dict(counter="engine", instance=f"pid_7_luid_{luid}_phys_0_eng_{engine}_engtype_3D",
                        status=0, value=value)
                   for luid, engine, value in [(1, 0, 80), (2, 0, 2), (1, 1, 3)]]
        result = gpu.summarize(samples)
        self.assertEqual(len(result), 3)
        self.assertEqual(sorted(v["mean"] for v in result.values()), [2, 3, 80])

    def test_gpu_invalid_status_is_retained_raw_and_excluded_from_aggregate(self):
        sample = dict(counter="dedicated", instance="pid_7_luid_1_phys_0", status=0, value=1024)
        invalid = dict(sample, status=0xc0000bba, value=9999)
        result = gpu.summarize([sample, invalid])
        self.assertEqual(result["dedicated/luid_1_phys_0"]["mean"], 1024)
        self.assertEqual(invalid["value"], 9999)

    def test_integrated_gpu_control_keeps_workload_and_pacing_identical(self):
        self.assertEqual(gpu.workloads(True), {'idle': ['--idle'], 'moving': [], 'local': ['--local-change']})
        self.assertEqual(gpu.painter_flags('before', True), ['--sdf', '--vector-text', '--no-program-cache'])
        self.assertEqual(gpu.painter_flags('after', True), ['--integrated'])
        self.assertEqual(gpu.painter_flags('before', True, True),
                         ['--compute', '--cache', '--vector-text', '--no-program-cache'])
        self.assertEqual(gpu.painter_flags('after', True, True), ['--integrated'])
        self.assertEqual(gpu.painter_flags('before'), [])
        self.assertEqual(gpu.painter_flags('after'), [])
        self.assertEqual(len(gpu.workloads()), 4)


if __name__ == "__main__":
    unittest.main()
