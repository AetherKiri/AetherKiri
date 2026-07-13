from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("aetherkiri_diagnose", ROOT / "tools/diagnose.py")
assert SPEC is not None and SPEC.loader is not None
diagnose = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(diagnose)


class DiagnoseTests(unittest.TestCase):
    def test_platform_adapter_matrix_is_complete(self) -> None:
        expected = {"android", "ios", "ios-simulator", "macos", "web"}
        self.assertEqual(set(diagnose.PREPARE), expected)
        self.assertEqual(set(diagnose.COLLECT), expected)

    def test_profiles_centralize_low_and_high_overhead_flags(self) -> None:
        baseline = diagnose.diagnostic_env("baseline", "session")
        full = diagnose.diagnostic_env("full", "session")
        self.assertEqual(baseline["AETHERKIRI_FRAME_SPIKE_MS"], "20")
        self.assertNotIn("AETHERKIRI_INPUT_TRACE", baseline)
        self.assertEqual(full["AETHERKIRI_INPUT_TRACE"], "1")
        self.assertEqual(full["AETHERKIRI_DIAGNOSTIC_SESSION"], "session")

    def test_host_marker_is_added_when_ui_marker_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            bundle = Path(temp)
            app_event = {"sequence": 7, "monotonic_us": 123456}
            diagnose.add_host_marker_if_needed(bundle, "session", [app_event])
            events = diagnose.load_events(bundle)
            self.assertEqual(events[0]["event"], "issue_marker")
            self.assertEqual(events[0]["monotonic_us"], 123457)
            self.assertTrue(events[0]["fields"]["ui_marker_missing"])
            self.assertEqual(events[0]["fields"]["timestamp_basis"], "last_app_event")

    def test_summary_reports_marker_window_without_claiming_cause(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            bundle = Path(temp)
            events = [
                {
                    "schema": 1, "sequence": 1, "monotonic_us": 10_000_000,
                    "event": "host_frame_spike", "level": "warning",
                    "layer": "godot", "subsystem": "render", "duration_us": 31000,
                    "queue_dropped": 0, "fields": {},
                },
                {
                    "schema": 1, "sequence": 2, "monotonic_us": 11_000_000,
                    "event": "issue_marker", "level": "info",
                    "layer": "godot", "subsystem": "lifecycle", "duration_us": 0,
                    "queue_dropped": 0, "fields": {"label": "user_issue"},
                },
            ]
            (bundle / "events.jsonl").write_text(
                "\n".join(json.dumps(item) for item in events) + "\n", encoding="utf-8")
            diagnose.write_summary(bundle, "session", "android", "baseline")
            summary = (bundle / "summary.md").read_text(encoding="utf-8")
            self.assertIn("Nearby warning/slow events: 1", summary)
            self.assertIn("First observed anomaly boundary", summary)
            self.assertIn("--profile render", summary)
            self.assertIn("Temporal proximity alone is not treated as a confirmed cause", summary)

    def test_app_metadata_is_merged_and_events_are_sorted(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            bundle = Path(temp)
            (bundle / "metadata.json").write_text(
                json.dumps({"profile": "render", "git_revision": "abc"}), encoding="utf-8")
            (bundle / "metadata.app.json").write_text(json.dumps({
                "model": "fixture-device", "renderer": "fixture-renderer",
                "profile": "render", "category_mask": 261,
                "slow_frame_threshold_ms": 20,
            }), encoding="utf-8")
            (bundle / "events.jsonl").write_text(
                json.dumps({"monotonic_us": 20, "sequence": 2, "layer": "godot"}) + "\n" +
                json.dumps({"monotonic_us": 10, "sequence": 1, "layer": "engine"}) + "\n",
                encoding="utf-8",
            )
            diagnose.merge_app_metadata(bundle)
            diagnose.normalize_events(bundle)
            metadata = json.loads((bundle / "metadata.json").read_text(encoding="utf-8"))
            events = diagnose.load_events(bundle)
            self.assertEqual(metadata["device"], "fixture-device")
            self.assertEqual(metadata["renderer_backend"], "fixture-renderer")
            self.assertEqual([event["monotonic_us"] for event in events], [10, 20])

    def test_command_failure_captures_output(self) -> None:
        completed = subprocess.CompletedProcess(["fake"], 7, stdout=b"device disconnected")
        with mock.patch.object(diagnose.subprocess, "run", return_value=completed):
            with self.assertRaises(diagnose.DiagnoseError) as raised:
                diagnose.run(["fake"])
        self.assertIn("device disconnected", str(raised.exception))

    def test_missing_app_session_is_a_valid_partial_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            bundle = Path(temp)
            (bundle / "app-data").mkdir()
            diagnose.consolidate_app_files(bundle, "missing")
            self.assertFalse((bundle / "events.jsonl").exists())


if __name__ == "__main__":
    unittest.main()
