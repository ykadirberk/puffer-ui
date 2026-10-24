import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location(
    "compare_vulkan_compute", pathlib.Path(__file__).resolve().parents[1] / "tools" / "compare_vulkan_compute.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ComputeMeasurementTests(unittest.TestCase):
    def test_balanced_schedule_retains_each_mode_each_run(self):
        names = ["r0", "aa", "reference", "sdf", "compute", "cache"]
        order = module.schedule(names, 6)
        for run in range(6):
            self.assertCountEqual([mode for number, mode in order if number == run], names)
        self.assertNotEqual(order[0][1], order[6][1])
        for position in range(6):
            self.assertCountEqual([order[run * 6 + position][1] for run in range(6)], names)

    def test_parser_preserves_outlier_and_rejects_missing_frame_or_readback(self):
        header = "frame,count,total_ms,readbacks\n"
        lines = [f"{f},5000,{1000 if f == 127 else 1},0" for f in range(180)]
        rows = module.parse_frames(header + "\n".join(lines))
        self.assertEqual(float(rows[127]["total_ms"]), 1000)
        with self.assertRaises(ValueError):
            module.parse_frames(header + "\n".join(lines[:-1]))
        lines[50] = "50,5000,1,1"
        with self.assertRaises(ValueError):
            module.parse_frames(header + "\n".join(lines))


if __name__ == "__main__":
    unittest.main()
