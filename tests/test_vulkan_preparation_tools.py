"""The performance protocol must retain spikes and reject mismatched work."""
import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "compare_preparation", Path(__file__).resolve().parents[1] / "tools/compare_vulkan_preparation.py")
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)


class PreparationProtocolTests(unittest.TestCase):
    def invoke(self, corruption=None):
        temporary_root = Path(__file__).resolve().parents[1] / "build-vulkan-tool-tests"
        temporary_root.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_root, prefix="protocol-") as directory:
            root = Path(directory)
            for name in ("old.exe", "new.exe"):
                (root / name).write_bytes(name.encode())
            output = root / "results"
            args = ["compare", str(root / "old.exe"), str(root / "new.exe"),
                    "--output", str(output), "--runs", "2"]

            def fake_run(command, **kwargs):
                candidate = Path(command[0]).parent.name == "after"
                rows = [dict(frame=i, count=5000 + (i >= 90) + (i >= 120) + (i >= 150),
                             total_ms=274 if i == 0 else 500 if i == 127 else 1,
                             draws=10, vertices=40, indices=60, readbacks=0,
                             completed_gpu_ms=1, gpu_samples=1, upload_bytes=100, metadata_bytes=200)
                        for i in range(180)]
                driver = 8389024
                if candidate and corruption == "geometry":
                    rows[127]["vertices"] += 4
                if candidate and corruption == "readback":
                    rows[90]["readbacks"] = 1
                if candidate and corruption == "missing":
                    rows.pop()
                if candidate and corruption == "adapter":
                    driver += 1
                stream = io.StringIO()
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
                return subprocess.CompletedProcess(command, 0, stream.getvalue(),
                    f"device=Fixture driver={driver} mode=3 cache=1 vector=1 hidden_wsi=1\n")

            with patch.object(sys, "argv", args), patch.object(TOOL.subprocess, "run", fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                TOOL.main()
            result = json.loads((output / "comparison.json").read_text())
            with (output / "frames.csv").open() as stream:
                frames = list(csv.DictReader(stream))
            self.assertEqual(len(frames), 20 * 180)
            for run in result["runs"]:
                self.assertEqual(run["first_ms"], 274)
                self.assertEqual(run["max_ms"], 500)
                self.assertEqual(run["max_frame"], 127)
                self.assertEqual(run["gpu_samples"], 180)
            self.assertEqual((output / "before/old.exe").read_bytes(), b"old.exe")
            self.assertEqual((output / "after/new.exe").read_bytes(), b"new.exe")

    def test_all_frames_and_startup_spikes_are_retained(self):
        self.invoke()

    def test_changed_geometry_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "geometry"):
            self.invoke("geometry")

    def test_timed_readbacks_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "readback"):
            self.invoke("readback")

    def test_incomplete_workload_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "incomplete"):
            self.invoke("missing")

    def test_adapter_changes_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "adapter"):
            self.invoke("adapter")


if __name__ == "__main__":
    unittest.main()
