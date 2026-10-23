"""Driver selection tests require neither Vulkan nor a GPU."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("launcher", Path(__file__).resolve().parents[1]/"tools/run_vulkan_showcase.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)
RX = dict(device="test discrete", type=2)
UMA = dict(device="test integrated", type=1)


def scratch():
    root = Path(__file__).resolve().parents[1]/"out/vulkan-tools-tests"
    root.mkdir(parents=True, exist_ok=True)
    temporary = tempfile.TemporaryDirectory(dir=root)
    assert Path(temporary.name).resolve().is_relative_to(root.resolve())
    return temporary


class LauncherTests(unittest.TestCase):
    def test_default_discrete_needs_no_override(self):
        environment = {"KEEP": "yes"}
        with patch.object(launcher, "probe", return_value=RX), patch.object(launcher, "missing_driver_entries") as missing:
            selected, info = launcher.select_driver("app", environment)
        self.assertEqual(selected, environment)
        self.assertIsNot(selected, environment)
        self.assertEqual(info, RX)
        missing.assert_not_called()

    def test_registered_family_fallback_checks_actual_device(self):
        with scratch() as directory:
            store = Path(directory)/"FileRepository"
            stale = store/"vendor.inf_amd64_missing"/"driver.json"
            good = store/"vendor.inf_amd64_good"/"driver.json"
            bad = store/"vendor.inf_amd64_bad"/"driver.json"
            for path in [good, bad]:
                path.parent.mkdir(parents=True)
                path.write_text("{}")
            environment = {"KEEP": "yes"}
            def fake_probe(executable, env):
                if env.get("VK_DRIVER_FILES") == str(good):
                    return RX
                return UMA
            with patch.object(launcher, "missing_driver_entries", return_value=[(RX["device"], stale)]), patch.object(launcher, "probe", side_effect=fake_probe):
                selected, info = launcher.select_driver("app", environment, RX["device"])
            self.assertEqual(selected["VK_DRIVER_FILES"], str(good))
            self.assertEqual(info, RX)
            self.assertEqual(environment, {"KEEP": "yes"})
            self.assertEqual(set(launcher.candidates(stale)), {good, bad})

    def test_explicit_environment_is_not_replaced(self):
        with patch.object(launcher, "probe", return_value=UMA), patch.object(launcher, "missing_driver_entries") as missing:
            with self.assertRaisesRegex(RuntimeError, "Explicit driver selection"):
                launcher.select_driver("app", {"VK_ICD_FILENAMES": "chosen.json"}, RX["device"])
            missing.assert_not_called()

    def test_missing_explicit_manifest_fails_before_probe(self):
        with scratch() as directory, patch.object(launcher, "probe") as probe:
            with self.assertRaisesRegex(RuntimeError, "does not exist"):
                launcher.select_driver("app", {}, manifest=Path(directory)/"absent.json")
            probe.assert_not_called()

    def test_no_unrelated_package_search_or_silent_integrated_fallback(self):
        stale = Path("arbitrary/vendor_amd64_missing/driver.json")
        self.assertEqual(launcher.candidates(stale), [])
        with patch.object(launcher, "missing_driver_entries", return_value=[(RX["device"], stale)]), patch.object(launcher, "probe", return_value=UMA):
            with self.assertRaisesRegex(RuntimeError, "No verified driver"):
                launcher.select_driver("app", {}, RX["device"])

    def test_probe_handles_failure_and_timeout(self):
        with patch.object(launcher.subprocess, "run", side_effect=launcher.subprocess.TimeoutExpired("app", 20)):
            self.assertIsNone(launcher.probe("app", {}))
        with patch.object(launcher.subprocess, "run") as run:
            run.return_value.returncode = 1
            self.assertIsNone(launcher.probe("app", {}))

    def test_forwarding_uses_argument_list_and_propagates_exit(self):
        with scratch() as directory:
            executable = Path(directory)/"application with spaces.exe"
            executable.touch()
            with patch.object(launcher, "select_driver", return_value=({"KEEP": "yes"}, RX)), patch.object(launcher.subprocess, "run") as run:
                run.return_value.returncode = 7
                self.assertEqual(launcher.main(["--exe", str(executable), "--", "--screenshot", "path with spaces.bmp"]), 7)
                run.assert_called_once_with([str(executable.resolve()), "--screenshot", "path with spaces.bmp"], env={"KEEP": "yes"})


if __name__ == "__main__":
    unittest.main()
