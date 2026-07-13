from __future__ import annotations

import argparse
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
    @staticmethod
    def android_args(device: str | None = None) -> argparse.Namespace:
        return argparse.Namespace(device=device, adb="adb")

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

    def test_android_preflight_rejects_missing_device_before_build(self) -> None:
        completed = subprocess.CompletedProcess(["adb"], 0, stdout=b"List of devices attached\n\n")
        with mock.patch.object(diagnose, "run", return_value=completed):
            with self.assertRaisesRegex(diagnose.DiagnoseError, "No Android device detected"):
                diagnose.android_preflight(self.android_args())

    def test_android_execute_stops_before_build_without_device(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            args = argparse.Namespace(
                platform="android", profile="baseline", build_install=False, reuse_build=False,
                device=None, duration=0.0, session="missing-device",
                output=str(Path(temp) / "bundle"), include_screenshot=False,
            )
            completed = subprocess.CompletedProcess(
                ["adb"], 0, stdout=b"List of devices attached\n\n")
            with mock.patch.object(diagnose, "run", return_value=completed), \
                    mock.patch.object(diagnose, "build") as build_mock:
                with self.assertRaisesRegex(diagnose.DiagnoseError, "No Android device detected"):
                    diagnose.execute(args)
            build_mock.assert_not_called()
            self.assertFalse(Path(args.output).exists())

    def test_android_preflight_explains_unauthorized_device(self) -> None:
        completed = subprocess.CompletedProcess(
            ["adb"], 0, stdout=b"List of devices attached\nABC123\tunauthorized\n")
        with mock.patch.object(diagnose, "run", return_value=completed):
            with self.assertRaisesRegex(diagnose.DiagnoseError, "unauthorized"):
                diagnose.android_preflight(self.android_args())

    def test_android_preflight_requires_serial_for_multiple_devices(self) -> None:
        completed = subprocess.CompletedProcess(
            ["adb"], 0,
            stdout=b"List of devices attached\nABC123\tdevice\nEMULATOR\tdevice\n")
        with mock.patch.object(diagnose, "run", return_value=completed):
            with self.assertRaisesRegex(diagnose.DiagnoseError, "Multiple Android devices"):
                diagnose.android_preflight(self.android_args())

    def test_android_preflight_selects_only_ready_device(self) -> None:
        completed = subprocess.CompletedProcess(
            ["adb"], 0, stdout=b"List of devices attached\nABC123\tdevice\n")
        args = self.android_args()
        with mock.patch.object(diagnose, "run", return_value=completed):
            diagnose.android_preflight(args)
        self.assertEqual(args.device, "ABC123")
        self.assertEqual(
            diagnose.adb_command(args, "shell", "pidof", diagnose.ANDROID_PACKAGE)[:3],
            [args.adb, "-s", "ABC123"],
        )

    def test_android_preflight_accepts_wireless_device_with_mdns_descriptor(self) -> None:
        completed = subprocess.CompletedProcess(
            ["adb"], 0,
            stdout=(
                b"List of devices attached\n"
                b"adb-c693383b-1quekS (2)._adb-tls-connect._tcp device "
                b"product:fixture model:fixture transport_id:3\n"
            ),
        )
        args = self.android_args()
        with mock.patch.object(diagnose, "run", return_value=completed):
            diagnose.android_preflight(args)
        self.assertEqual(
            args.device,
            "adb-c693383b-1quekS (2)._adb-tls-connect._tcp",
        )

    def test_android_preflight_reports_wireless_device_real_state(self) -> None:
        completed = subprocess.CompletedProcess(
            ["adb"], 0,
            stdout=(
                b"List of devices attached\n"
                b"adb-c693383b-1quekS (2)._adb-tls-connect._tcp offline\n"
            ),
        )
        with mock.patch.object(diagnose, "run", return_value=completed):
            with self.assertRaisesRegex(diagnose.DiagnoseError, "offline"):
                diagnose.android_preflight(self.android_args())

    def test_adb_resolution_prefers_android_sdk_over_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            sdk = Path(temp)
            adb = sdk / "platform-tools/adb"
            adb.parent.mkdir()
            adb.touch()
            with mock.patch.dict(
                diagnose.os.environ,
                {"ANDROID_SDK_ROOT": str(sdk), "ANDROID_HOME": ""},
                clear=False,
            ), mock.patch.object(diagnose.shutil, "which", return_value="/path/adb"):
                self.assertEqual(diagnose.resolve_adb_executable(), str(adb))

    def test_android_transport_waits_for_wireless_rediscovery(self) -> None:
        missing = subprocess.CompletedProcess(
            ["adb"], 0, stdout=b"List of devices attached\n\n")
        ready = subprocess.CompletedProcess(
            ["adb"], 0,
            stdout=(
                b"List of devices attached\n"
                b"WIRELESS (2)._adb-tls-connect._tcp device\n"
            ),
        )
        wireless_serial = "WIRELESS (2)._adb-tls-connect._tcp"
        args = self.android_args(wireless_serial)
        with mock.patch.object(diagnose, "run", side_effect=[missing, missing, ready]), \
                mock.patch.object(diagnose.time, "sleep"):
            diagnose.wait_for_android_transport(args, timeout=1.0)

    def test_android_install_retries_only_after_transport_failure(self) -> None:
        disconnected = subprocess.CompletedProcess(
            ["adb"], 1, stdout=b"adb: device 'WIRELESS' not found\n")
        installed = subprocess.CompletedProcess(["adb"], 0, stdout=b"Success\n")
        args = self.android_args("WIRELESS")
        with mock.patch.object(diagnose, "run", side_effect=[disconnected, installed]) as run_mock, \
                mock.patch.object(diagnose, "wait_for_android_transport") as wait_mock:
            diagnose.android_install(args, Path("app.apk"))
        wait_mock.assert_called_once_with(args)
        self.assertEqual(run_mock.call_count, 2)

    def test_android_install_does_not_retry_apk_failure(self) -> None:
        rejected = subprocess.CompletedProcess(
            ["adb"], 1, stdout=b"Failure [INSTALL_FAILED_UPDATE_INCOMPATIBLE]\n")
        args = self.android_args("WIRELESS")
        with mock.patch.object(diagnose, "run", return_value=rejected), \
                mock.patch.object(diagnose, "wait_for_android_transport") as wait_mock:
            with self.assertRaisesRegex(diagnose.DiagnoseError, "INSTALL_FAILED"):
                diagnose.android_install(args, Path("app.apk"))
        wait_mock.assert_not_called()

    def test_android_request_uses_push_instead_of_wireless_stdin(self) -> None:
        args = argparse.Namespace(
            device="WIRELESS (2)._adb-tls-connect._tcp", adb="adb",
            session="fixture-session", profile="baseline",
        )
        completed = subprocess.CompletedProcess(["adb"], 0, stdout=b"")
        with tempfile.TemporaryDirectory() as temp, \
                mock.patch.object(diagnose, "run", return_value=completed) as run_mock:
            bundle = Path(temp)
            (bundle / "platform").mkdir()
            diagnose.write_android_diagnostic_request(args, bundle)
            commands = [call.args[0] for call in run_mock.call_args_list]
            self.assertIn("mkdir", commands[0])
            self.assertEqual(commands[1][3], "push")
            self.assertEqual(
                commands[1][-1],
                diagnose.ANDROID_DIAGNOSTIC_ROOT + "/diagnostic-request.json",
            )
            self.assertNotIn("exec-out", [part for command in commands for part in command])
            self.assertNotIn("run-as", [part for command in commands for part in command])
            self.assertFalse((bundle / "platform/diagnostic-request.json").exists())

    def test_capture_existing_install_is_the_default(self) -> None:
        parsed = diagnose.parser().parse_args(["run", "android", "--duration", "0"])
        self.assertFalse(parsed.build_install)
        self.assertFalse(parsed.reuse_build)

    def test_execute_only_builds_when_explicitly_requested(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            def make_args(name: str, build_install: bool) -> argparse.Namespace:
                return argparse.Namespace(
                    platform="android", profile="baseline", build_install=build_install,
                    reuse_build=False, device="fixture", duration=0.0,
                    session=name, output=str(Path(temp) / name), include_screenshot=False,
                )

            completed = subprocess.CompletedProcess(["fixture"], 0, stdout=b"")
            with mock.patch.dict(diagnose.PREFLIGHT, {"android": lambda _args: None}), \
                    mock.patch.dict(diagnose.PREPARE, {"android": lambda _args, _bundle, _env: {}}), \
                    mock.patch.dict(diagnose.COLLECT, {"android": lambda _args, _bundle, _state: None}), \
                    mock.patch.object(diagnose, "run", return_value=completed), \
                    mock.patch.object(diagnose, "build") as build_mock, \
                    mock.patch.object(diagnose.time, "sleep"):
                diagnose.execute(make_args("attach", False))
                build_mock.assert_not_called()
                diagnose.execute(make_args("fresh", True))
                build_mock.assert_called_once_with("android")

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
