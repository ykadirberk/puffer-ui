"""Exact frame coverage and CPU-thread attribution in the timing protocol."""
import unittest
import test_vulkan_program_tools as protocol

tool = protocol.tool


class FrameStreamTests(unittest.TestCase):
    def test_duplicate_or_reordered_frames_cannot_hide_an_outlier(self):
        for index, replacement in [(90, "89"), (0, "179"), (179, "178")]:
            rows = protocol.ProgramProtocolTests().rows()
            rows[index]["frame"] = replacement
            with self.assertRaises(ValueError):
                tool.summarize_run(rows, retained=False)

    def test_process_and_main_thread_counters_remain_separate(self):
        result = tool.window_cpu("frame_window_cpu_ms=812.500000 frame_window_cycles=4000000000\n"
                                 "frame_window_main_cpu_ms=625.000000 frame_window_main_cycles=3000000000\n")
        self.assertEqual(result["frame_window_cpu_ms"], 812.5)
        self.assertEqual(result["frame_window_main_cpu_ms"], 625)
        self.assertEqual(result["frame_window_cycles"] - result["frame_window_main_cycles"], 1000000000)

    def test_missing_main_thread_counter_is_unavailable(self):
        result = tool.window_cpu("frame_window_cpu_ms=812.500000 frame_window_cycles=4000000000\n")
        self.assertNotIn("frame_window_main_cpu_ms", result)
        self.assertNotIn("frame_window_main_cycles", result)
        self.assertEqual(tool.window_cpu("no Windows counters"), {})


if __name__ == "__main__":
    unittest.main()
