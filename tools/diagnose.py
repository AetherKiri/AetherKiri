#!/usr/bin/env python3
"""Run one bounded AetherKiri diagnostic reproduction and build a shareable bundle."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tarfile
import time
from typing import Any, Iterable
import urllib.error
import urllib.request
import uuid
import webbrowser


ROOT = Path(__file__).resolve().parents[1]
OUT_ROOT = ROOT / "out" / "diagnostics"
ANDROID_PACKAGE = "org.github.krkr2.aetherkiri"
ANDROID_DIAGNOSTIC_ROOT = "/storage/emulated/0/Documents/AetherKiri/Diagnostics"
IOS_BUNDLE = "com.liuyu.aetherkiri.kr37s"
MAC_APP_NAME = "AetherKiri"
PROFILES = (
    "baseline", "input", "render", "storage", "script", "audio", "video",
    "plugin", "system", "full",
)
POST_MARKER_GRACE_SECONDS = 5.25
ANDROID_RECONNECT_TIMEOUT_SECONDS = 30.0
ADB_DEVICE_STATES = frozenset({
    "device", "offline", "unauthorized", "bootloader", "recovery", "sideload",
})
ADB_TRANSPORT_FAILURES = (
    "no devices/emulators found", "device offline",
    "transport error", "connection reset", "connection closed",
)

PROFILE_ENV: dict[str, dict[str, str]] = {
    "baseline": {
        "AETHERKIRI_DIAGNOSTICS": "1",
        "AETHERKIRI_FRAME_SPIKE_MS": "20",
        "AETHERKIRI_ENGINE_TICK_SPIKE_MS": "20",
    },
    "input": {"AETHERKIRI_INPUT_TRACE": "1"},
    "render": {
        "AETHERKIRI_VERBOSE_RENDER_LOG": "1",
        "AETHERKIRI_GODOT_RENDER_STATS": "1",
        "AETHERKIRI_GODOT_GPU_TRACE_FALLBACK": "1",
    },
    "storage": {"AETHERKIRI_STORAGE_TRACE": "1", "AETHERKIRI_IMAGE_LOAD_TRACE": "1"},
    "script": {"AETHERKIRI_APP_TRACE": "1", "AETHERKIRI_TJS_VM_TRACE": "1"},
    "audio": {"AETHERKIRI_AUDIO_TRACE": "1", "AETHERKIRI_TJS_AUDIO_TRACE": "1"},
    "video": {"AETHERKIRI_VIDEO_TRACE": "1"},
    "plugin": {"AETHERKIRI_PLUGIN_TRACE": "1", "AETHERKIRI_TRACE_MISSING_MEMBERS": "1"},
    "system": {},
    "full": {
        "AETHERKIRI_INPUT_TRACE": "1",
        "AETHERKIRI_VERBOSE_RENDER_LOG": "1",
        "AETHERKIRI_GODOT_RENDER_STATS": "1",
        "AETHERKIRI_GODOT_GPU_TRACE_FALLBACK": "1",
        "AETHERKIRI_STORAGE_TRACE": "1",
        "AETHERKIRI_IMAGE_LOAD_TRACE": "1",
        "AETHERKIRI_APP_TRACE": "1",
        "AETHERKIRI_TJS_VM_TRACE": "1",
        "AETHERKIRI_AUDIO_TRACE": "1",
        "AETHERKIRI_VIDEO_TRACE": "1",
        "AETHERKIRI_PLUGIN_TRACE": "1",
        "AETHERKIRI_TRACE_MISSING_MEMBERS": "1",
        "AETHERKIRI_TRACE_LOG": "1",
    },
}


class DiagnoseError(RuntimeError):
    pass


def command_text(command: Iterable[str]) -> str:
    return " ".join(str(part) for part in command)


def run(command: list[str], *, env: dict[str, str] | None = None,
        cwd: Path = ROOT, check: bool = True, input_bytes: bytes | None = None,
        output_path: Path | None = None) -> subprocess.CompletedProcess[bytes]:
    print(f"[diagnose] $ {command_text(command)}", flush=True)
    result = subprocess.run(
        command, cwd=cwd, env=env, input=input_bytes,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_bytes(result.stdout)
    if check and result.returncode != 0:
        tail = result.stdout.decode("utf-8", errors="replace")[-4000:]
        raise DiagnoseError(f"Command failed ({result.returncode}): {command_text(command)}\n{tail}")
    return result


def output(command: list[str], *, check: bool = True) -> str:
    return run(command, check=check).stdout.decode("utf-8", errors="replace").strip()


def git_revision() -> str:
    return output(["git", "rev-parse", "--short=12", "HEAD"], check=False) or "unknown"


def diagnostic_env(profile: str, session: str) -> dict[str, str]:
    env = dict(os.environ)
    env.update(PROFILE_ENV["baseline"])
    if profile != "baseline":
        env.update(PROFILE_ENV.get(profile, {}))
    env["AETHERKIRI_DIAGNOSTIC_PROFILE"] = profile
    env["AETHERKIRI_DIAGNOSTIC_SESSION"] = session
    return env


def terminate(process: subprocess.Popen[bytes] | None, timeout: float = 5.0) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def wait_for_reproduction(duration: float | None) -> None:
    if duration is not None:
        print(f"[diagnose] Capturing for {duration:.1f}s. Reproduce and tap 标记问题.", flush=True)
        time.sleep(max(0.0, duration))
        return
    if not sys.stdin.isatty():
        raise DiagnoseError("Interactive input is unavailable; pass --duration SECONDS")
    input("[diagnose] Reproduce the issue, tap 标记问题, then press Enter here to collect… ")


def build(platform: str) -> None:
    if platform == "ios-simulator":
        run(["./build.sh", "ios", "debug", "--simulator"])
    else:
        run(["./build.sh", platform, "debug"])


def resolve_adb_executable() -> str:
    executable = "adb.exe" if os.name == "nt" else "adb"
    sdk_roots = [os.environ.get("ANDROID_SDK_ROOT"), os.environ.get("ANDROID_HOME")]
    if sys.platform == "darwin":
        sdk_roots.append(str(Path.home() / "Library/Android/sdk"))
    else:
        sdk_roots.append(str(Path.home() / "Android/Sdk"))
    for root in sdk_roots:
        if not root:
            continue
        candidate = Path(root).expanduser() / "platform-tools" / executable
        if candidate.is_file():
            return str(candidate)
    discovered = shutil.which(executable)
    if discovered:
        return discovered
    raise DiagnoseError(
        "ADB was not found. Set ANDROID_SDK_ROOT/ANDROID_HOME or add platform-tools to PATH."
    )


def adb_command(args: argparse.Namespace, *parts: str) -> list[str]:
    command = [getattr(args, "adb", None) or resolve_adb_executable()]
    if args.device:
        command += ["-s", args.device]
    return command + list(parts)


def parse_adb_devices(listing: str) -> dict[str, str]:
    """Return serial -> state, preserving spaces in Bonjour-renamed mDNS serials."""
    devices: dict[str, str] = {}
    saw_header = False
    for line in listing.splitlines():
        if line.strip().startswith("List of devices attached"):
            saw_header = True
            continue
        if not saw_header:
            continue
        columns = line.strip().split()
        if len(columns) < 2:
            continue
        state_index = next(
            (index for index, column in enumerate(columns[1:], 1)
             if column in ADB_DEVICE_STATES),
            None,
        )
        if state_index is None:
            devices[columns[0]] = columns[1]
            continue
        serial = " ".join(columns[:state_index])
        devices[serial] = columns[state_index]
    return devices


def android_preflight(args: argparse.Namespace) -> None:
    args.adb = resolve_adb_executable()
    run([args.adb, "start-server"], check=False)
    listing = output([args.adb, "devices"], check=False)
    devices = parse_adb_devices(listing)
    if args.device:
        state = devices.get(args.device)
        if state is None:
            raise DiagnoseError(
                f"Android device {args.device!r} was not found. Check `adb devices` or omit --device."
            )
        if state != "device":
            raise DiagnoseError(
                f"Android device {args.device!r} is {state}. Unlock it and accept the USB debugging prompt."
            )
        return
    ready = [serial for serial, state in devices.items() if state == "device"]
    if len(ready) == 1:
        args.device = ready[0]
        return
    if len(ready) > 1:
        raise DiagnoseError(
            "Multiple Android devices are connected. Pass --device SERIAL from `adb devices`."
        )
    if devices:
        states = ", ".join(f"{serial} ({state})" for serial, state in devices.items())
        raise DiagnoseError(
            f"No ready Android device: {states}. Unlock the device and accept USB debugging."
        )
    raise DiagnoseError(
        "No Android device detected. Connect a device or start an emulator, enable USB debugging, "
        "then verify it appears in `adb devices`."
    )


def wait_for_android_transport(args: argparse.Namespace,
                               timeout: float = ANDROID_RECONNECT_TIMEOUT_SECONDS) -> None:
    deadline = time.monotonic() + timeout
    last_state = "not found"
    print(
        f"[diagnose] Waiting for Android device {args.device}…",
        flush=True,
    )
    run([args.adb, "start-server"], check=False)
    while time.monotonic() < deadline:
        devices = parse_adb_devices(output([args.adb, "devices"], check=False))
        last_state = devices.get(args.device, "not found")
        if last_state == "device":
            return
        time.sleep(0.5)
    raise DiagnoseError(
        f"Android device {args.device!r} did not reconnect "
        f"(last state: {last_state}). Keep Wireless debugging enabled, or reconnect it with adb."
    )


def android_install(args: argparse.Namespace, apk: Path) -> None:
    command = adb_command(args, "install", "-r", str(apk))
    result = run(command, check=False)
    if result.returncode == 0:
        return
    detail = result.stdout.decode("utf-8", errors="replace")
    lowered = detail.lower()
    missing_transport = "adb: device '" in lowered and "' not found" in lowered
    if not missing_transport and not any(fragment in lowered for fragment in ADB_TRANSPORT_FAILURES):
        raise DiagnoseError(f"Command failed ({result.returncode}): {command_text(command)}\n{detail[-4000:]}")
    print("[diagnose] Android transport dropped during install; reconnecting once…", flush=True)
    wait_for_android_transport(args)
    run(command)


def write_android_diagnostic_request(args: argparse.Namespace, bundle: Path) -> None:
    request = json.dumps({"session": args.session, "profile": args.profile}).encode()
    local_path = bundle / "platform/diagnostic-request.json"
    remote_path = f"{ANDROID_DIAGNOSTIC_ROOT}/diagnostic-request.json"
    local_path.write_bytes(request)
    try:
        run(adb_command(args, "shell", "mkdir", "-p", ANDROID_DIAGNOSTIC_ROOT))
        run(adb_command(args, "push", str(local_path), remote_path))
    finally:
        local_path.unlink(missing_ok=True)


def wait_for_pid(args: argparse.Namespace, package: str, timeout: float = 20.0) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pid = output(adb_command(args, "shell", "pidof", package), check=False).split()
        if pid:
            return pid[0]
        time.sleep(0.5)
    raise DiagnoseError(f"Timed out waiting for Android process {package}")


def wait_android_session(args: argparse.Namespace, session: str, timeout: float = 45.0) -> None:
    deadline = time.monotonic() + timeout
    path = f"{ANDROID_DIAGNOSTIC_ROOT}/{session}/metadata.json"
    while time.monotonic() < deadline:
        result = run(adb_command(args, "shell", "test", "-f", path), check=False)
        if result.returncode == 0:
            return
        time.sleep(0.5)
    raise DiagnoseError(
        "Android app launched but its public Documents diagnostic session did not become ready. "
        "If the installed app predates public diagnostics, run once with --build-install."
    )


def android_prepare(args: argparse.Namespace, bundle: Path, env: dict[str, str]) -> dict[str, Any]:
    del env
    wait_for_android_transport(args)
    if getattr(args, "build_install", False):
        apk = ROOT / "out/godot/android/debug/AetherKiri-debug.apk"
        if not apk.exists():
            raise DiagnoseError(f"Android APK not found: {apk}")
        android_install(args, apk)
    write_android_diagnostic_request(args, bundle)
    run(adb_command(args, "shell", "am", "force-stop", ANDROID_PACKAGE), check=False)
    run(adb_command(args, "logcat", "-c"), check=False)
    run(adb_command(args, "shell", "monkey", "-p", ANDROID_PACKAGE,
                    "-c", "android.intent.category.LAUNCHER", "1"))
    pid = wait_for_pid(args, ANDROID_PACKAGE)
    wait_android_session(args, args.session)
    console_file = (bundle / "platform/logcat-live.txt").open("wb")
    console = subprocess.Popen(
        adb_command(args, "logcat", "--pid", pid, "-v", "threadtime"),
        cwd=ROOT, stdout=console_file, stderr=subprocess.STDOUT,
    )
    trace = None
    if args.profile in {"system", "full"}:
        trace = subprocess.Popen(
            adb_command(args, "shell", "perfetto", "-o",
                        "/data/misc/perfetto-traces/aetherkiri-diagnostic.pftrace",
                        "-t", "30s", "sched", "freq", "idle", "am", "wm", "gfx", "view"),
            cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    return {"pid": pid, "console": console, "console_file": console_file, "trace": trace}


def safe_extract_tar(data: bytes, destination: Path) -> None:
    archive_path = destination / "app-data.tar"
    archive_path.write_bytes(data)
    try:
        with tarfile.open(archive_path) as archive:
            archive.extractall(destination / "app-data", filter="data")
    except (tarfile.TarError, TypeError) as error:
        raise DiagnoseError(f"Unable to extract app diagnostic archive: {error}") from error


def android_collect(args: argparse.Namespace, bundle: Path, state: dict[str, Any]) -> None:
    if not state.get("pid"):
        (bundle / "platform/collection-skipped.txt").write_text(
            "Android application did not start; device evidence collection was skipped.\n",
            encoding="utf-8",
        )
        return
    terminate(state.get("trace"), 2)
    terminate(state.get("console"), 2)
    if state.get("console_file") is not None:
        state["console_file"].close()
    platform = bundle / "platform"
    pid = str(state.get("pid", ""))
    run(adb_command(args, "logcat", "-d", "--pid", pid, "*:W"),
        check=False, output_path=platform / "logcat-warning-error.txt")
    run(adb_command(args, "logcat", "-b", "crash", "-d"),
        check=False, output_path=platform / "logcat-crash.txt")
    run(adb_command(args, "shell", "dumpsys", "meminfo", ANDROID_PACKAGE),
        check=False, output_path=platform / "meminfo.txt")
    run(adb_command(args, "shell", "dumpsys", "gfxinfo", ANDROID_PACKAGE, "framestats"),
        check=False, output_path=platform / "gfxinfo-framestats.txt")
    if args.include_screenshot:
        run(adb_command(args, "exec-out", "screencap", "-p"), check=False,
            output_path=platform / "screenshot.png")
    tar_result = run(
        adb_command(args, "exec-out", "tar", "-C", ANDROID_DIAGNOSTIC_ROOT,
                    "-cf", "-", args.session), check=False,
    )
    if tar_result.returncode == 0 and tar_result.stdout:
        safe_extract_tar(tar_result.stdout, bundle)
    else:
        (platform / "app-data-error.txt").write_bytes(tar_result.stdout)
    if args.profile in {"system", "full"}:
        run(adb_command(args, "pull", "/data/misc/perfetto-traces/aetherkiri-diagnostic.pftrace",
                        str(platform / "aetherkiri-diagnostic.pftrace")), check=False)


def find_ios_project() -> Path:
    project = ROOT / "out/godot/ios/debug/AetherKiri.xcodeproj"
    if not project.exists():
        raise DiagnoseError(f"iOS project not found: {project}")
    return project


def build_ios_app(device: str, simulator: bool) -> Path:
    project = find_ios_project()
    derived = ROOT / "out/diagnostics/.ios-derived"
    command = [
        "xcodebuild", "-project", str(project), "-scheme", "AetherKiri",
        "-configuration", "Debug", "-derivedDataPath", str(derived),
    ]
    if simulator:
        command += ["-sdk", "iphonesimulator", "-destination", "generic/platform=iOS Simulator"]
    else:
        command += ["-destination", f"id={device}", "-allowProvisioningUpdates"]
    command.append("build")
    run(command)
    apps = sorted(derived.glob("Build/Products/Debug-*/*.app"), key=lambda p: p.stat().st_mtime)
    if not apps:
        raise DiagnoseError("xcodebuild completed but no AetherKiri.app was found")
    return apps[-1]


def ios_prepare(args: argparse.Namespace, bundle: Path, env: dict[str, str]) -> dict[str, Any]:
    if not args.device:
        raise DiagnoseError("iOS device collection requires --device <UDID-or-name>")
    if getattr(args, "build_install", False):
        app = build_ios_app(args.device, simulator=False)
        run(["xcrun", "devicectl", "device", "install", "app", "--device", args.device, str(app)])
    console_file = (bundle / "platform/devicectl-console.txt").open("wb")
    launch_env = {key: value for key, value in env.items() if key.startswith("AETHERKIRI_")}
    console = subprocess.Popen(
        ["xcrun", "devicectl", "device", "process", "launch", "--device", args.device,
         "--terminate-existing", "--console", "--environment-variables",
         json.dumps(launch_env), IOS_BUNDLE],
        cwd=ROOT, stdout=console_file, stderr=subprocess.STDOUT,
    )
    time.sleep(2)
    return {"console": console, "console_file": console_file}


def ios_collect(args: argparse.Namespace, bundle: Path, state: dict[str, Any]) -> None:
    platform = bundle / "platform"
    if args.include_screenshot:
        run(["xcrun", "devicectl", "device", "screenshot", "--device", args.device,
             str(platform / "screenshot.png")], check=False,
            output_path=platform / "screenshot-command.txt")
    terminate(state.get("console"), 3)
    if state.get("console_file") is not None:
        state["console_file"].close()
    run(["xcrun", "devicectl", "device", "copy", "from", "--device", args.device,
         "--source", ".", "--destination", str(bundle / "app-data"),
         "--domain-type", "appDataContainer", "--domain-identifier", IOS_BUNDLE], check=False,
        output_path=platform / "app-data-copy.txt")
    run(["xcrun", "devicectl", "device", "copy", "from", "--device", args.device,
         "--source", ".", "--destination", str(platform / "crash-logs"),
         "--domain-type", "systemCrashLogs"], check=False,
        output_path=platform / "crash-copy.txt")


def ios_simulator_prepare(args: argparse.Namespace, bundle: Path,
                          env: dict[str, str]) -> dict[str, Any]:
    run(["xcrun", "simctl", "terminate", "booted", IOS_BUNDLE], check=False)
    if getattr(args, "build_install", False):
        app = build_ios_app("booted", simulator=True)
        run(["xcrun", "simctl", "install", "booted", str(app)])
    console_file = (bundle / "platform/simctl-console.txt").open("wb")
    sim_env = dict(env)
    for key, value in list(env.items()):
        if key.startswith("AETHERKIRI_"):
            sim_env[f"SIMCTL_CHILD_{key}"] = value
    console = subprocess.Popen(
        ["xcrun", "simctl", "launch", "--console-pty", "booted", IOS_BUNDLE],
        cwd=ROOT, env=sim_env, stdout=console_file, stderr=subprocess.STDOUT,
    )
    time.sleep(2)
    return {"console": console, "console_file": console_file}


def ios_simulator_collect(args: argparse.Namespace, bundle: Path,
                          state: dict[str, Any]) -> None:
    if args.include_screenshot:
        run(["xcrun", "simctl", "io", "booted", "screenshot",
             str(bundle / "platform/screenshot.png")], check=False)
    terminate(state.get("console"), 3)
    if state.get("console_file") is not None:
        state["console_file"].close()
    container = output(["xcrun", "simctl", "get_app_container", "booted", IOS_BUNDLE, "data"], check=False)
    if container and Path(container).exists():
        candidates = [path for path in Path(container).rglob(args.session) if path.is_dir()]
        if candidates:
            shutil.copytree(candidates[0], bundle / "app-data/diagnostics" / args.session,
                            dirs_exist_ok=True)


def macos_prepare(args: argparse.Namespace, bundle: Path, env: dict[str, str]) -> dict[str, Any]:
    executable = ROOT / "out/godot/macos/debug/AetherKiri.app/Contents/MacOS/AetherKiri"
    if not executable.exists():
        raise DiagnoseError(f"macOS executable not found: {executable}")
    existing = output(["pgrep", "-x", MAC_APP_NAME], check=False)
    if existing:
        print("[diagnose] Terminating an existing AetherKiri process before capture.", flush=True)
        run(["pkill", "-x", MAC_APP_NAME], check=False)
        time.sleep(1)
    console_file = (bundle / "platform/console.txt").open("wb")
    process = subprocess.Popen(
        [str(executable)], cwd=ROOT, env=env,
        stdout=console_file, stderr=subprocess.STDOUT,
    )
    session_metadata = (Path.home() /
        "Library/Application Support/Godot/app_userdata/AetherKiri/diagnostics" /
        args.session / "metadata.json")
    deadline = time.monotonic() + 45.0
    while time.monotonic() < deadline:
        if session_metadata.exists():
            break
        if process.poll() is not None:
            console_file.flush()
            raise DiagnoseError(
                f"macOS app exited before diagnostics became ready; see {console_file.name}")
        time.sleep(0.25)
    else:
        terminate(process)
        raise DiagnoseError("macOS app launched but the diagnostic session did not become ready")
    return {"process": process, "console_file": console_file, "started": dt.datetime.now()}


def macos_collect(args: argparse.Namespace, bundle: Path, state: dict[str, Any]) -> None:
    if args.include_screenshot:
        run(["screencapture", "-x", str(bundle / "platform/screenshot.png")], check=False)
    terminate(state.get("process"), 5)
    if state.get("console_file") is not None:
        state["console_file"].close()
    user_dir = Path.home() / "Library/Application Support/Godot/app_userdata/AetherKiri"
    app_session = user_dir / "diagnostics" / args.session
    if app_session.exists():
        shutil.copytree(app_session, bundle / "app-data/diagnostics" / args.session,
                        dirs_exist_ok=True)
    current_log = user_dir / "logs/godot.log"
    if current_log.exists():
        shutil.copy2(current_log, bundle / "platform/godot.log")
    started: dt.datetime = state.get("started", dt.datetime.now())
    seconds = max(30, int((dt.datetime.now() - started).total_seconds()) + 10)
    run(["log", "show", "--last", f"{seconds}s", "--style", "compact",
         "--predicate", 'process == "AetherKiri"'], check=False,
        output_path=bundle / "platform/unified-log.txt")


def wait_http(url: str, timeout: float = 20.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1):
                return
        except (urllib.error.URLError, TimeoutError):
            time.sleep(0.25)
    raise DiagnoseError(f"Timed out waiting for {url}")


def free_local_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.bind(("127.0.0.1", 0))
        return int(server.getsockname()[1])


def wait_web_session(url: str, session: str, timeout: float = 45.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(f"{url}/__aetherkiri/client-logs", timeout=2) as response:
                payload = json.load(response)
            for entry in payload.get("logs", []):
                message = str(entry.get("message", ""))
                if f'\"session\":\"{session}\"' in message and "diagnostic_session_started" in message:
                    return
        except (urllib.error.URLError, json.JSONDecodeError):
            pass
        time.sleep(0.5)
    raise DiagnoseError("Web app loaded but the diagnostic session did not become ready")


def web_prepare(args: argparse.Namespace, bundle: Path, env: dict[str, str]) -> dict[str, Any]:
    del bundle
    server_log = (args.bundle / "platform/vite.txt").open("wb")
    port = free_local_port()
    web_env = dict(env)
    web_env["PORT"] = str(port)
    server = subprocess.Popen(
        ["npm", "run", "web:dev:debug"], cwd=ROOT, env=web_env,
        stdout=server_log, stderr=subprocess.STDOUT,
    )
    url = f"http://127.0.0.1:{port}"
    wait_http(url)
    session_url = f"{url}/?diagnostic_session={args.session}&diagnostic_profile={args.profile}"
    webbrowser.open(session_url)
    wait_web_session(url, args.session)
    return {"server": server, "server_log": server_log, "url": url}


def web_collect(args: argparse.Namespace, bundle: Path, state: dict[str, Any]) -> None:
    url = f"{state['url']}/__aetherkiri/client-logs?clear=1"
    try:
        with urllib.request.urlopen(url, timeout=5) as response:
            payload = json.load(response)
        logs = payload.get("logs", [])
        (bundle / "platform/browser-client-logs.json").write_text(
            json.dumps(logs, ensure_ascii=False, indent=2), encoding="utf-8")
        diagnostic_lines = []
        for entry in logs:
            message = str(entry.get("message", ""))
            if message.startswith("{\"schema\":1,"):
                diagnostic_lines.append(message)
        if diagnostic_lines:
            (bundle / "events.web.jsonl").write_text("\n".join(diagnostic_lines) + "\n", encoding="utf-8")
    except (urllib.error.URLError, json.JSONDecodeError) as error:
        (bundle / "platform/web-collection-error.txt").write_text(str(error), encoding="utf-8")
    terminate(state.get("server"), 3)
    if state.get("server_log") is not None:
        state["server_log"].close()


PREPARE = {
    "android": android_prepare,
    "ios": ios_prepare,
    "ios-simulator": ios_simulator_prepare,
    "macos": macos_prepare,
    "web": web_prepare,
}
COLLECT = {
    "android": android_collect,
    "ios": ios_collect,
    "ios-simulator": ios_simulator_collect,
    "macos": macos_collect,
    "web": web_collect,
}
PREFLIGHT = {"android": android_preflight}


def find_session_directory(bundle: Path, session: str) -> Path | None:
    candidates = [path for path in bundle.rglob(session) if path.is_dir()]
    candidates.sort(key=lambda path: len(path.parts))
    return candidates[0] if candidates else None


def consolidate_app_files(bundle: Path, session: str) -> None:
    source = find_session_directory(bundle / "app-data", session)
    if source is None:
        return
    metadata = source / "metadata.json"
    if metadata.exists():
        shutil.copy2(metadata, bundle / "metadata.app.json")
    event_sources = [source / "events.previous.jsonl", source / "events.jsonl"]
    with (bundle / "events.jsonl").open("wb") as destination:
        for path in event_sources:
            if path.exists():
                destination.write(path.read_bytes())
    incidents = source / "incidents"
    if incidents.exists():
        shutil.copytree(incidents, bundle / "incidents", dirs_exist_ok=True)


def merge_app_metadata(bundle: Path) -> None:
    tool_path = bundle / "metadata.json"
    app_path = bundle / "metadata.app.json"
    if not tool_path.exists() or not app_path.exists():
        return
    try:
        tool = json.loads(tool_path.read_text(encoding="utf-8"))
        app = json.loads(app_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return
    tool["device"] = app.get("model", "unknown")
    tool["renderer_backend"] = app.get("renderer", "unknown")
    tool["diagnostic_config"] = {
        "profile": app.get("profile", tool.get("profile")),
        "category_mask": app.get("category_mask"),
        "slow_frame_threshold_ms": app.get("slow_frame_threshold_ms"),
    }
    tool["app"] = app
    tool_path.write_text(json.dumps(tool, ensure_ascii=False, indent=2), encoding="utf-8")


def load_events(bundle: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for path in (bundle / "events.jsonl", bundle / "events.web.jsonl"):
        if not path.exists():
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                events.append(value)
    events.sort(key=lambda event: int(event.get("monotonic_us", 0)))
    return events


def add_host_marker_if_needed(bundle: Path, session: str, events: list[dict[str, Any]]) -> None:
    if any(event.get("event") == "issue_marker" for event in events):
        return
    sequence = max((int(event.get("sequence", 0)) for event in events), default=0) + 1
    raw_host_us = time.monotonic_ns() // 1000
    last_app_us = max((int(event.get("monotonic_us", 0)) for event in events), default=0)
    marker_us = last_app_us + 1 if last_app_us > 0 else raw_host_us
    marker = {
        "schema": 1, "session": session, "sequence": sequence,
        "monotonic_us": marker_us,
        "platform": sys.platform, "layer": "host", "subsystem": "lifecycle",
        "level": "warning", "event": "issue_marker", "duration_us": 0,
        "queue_dropped": 0,
        "fields": {
            "label": "host_finish", "ui_marker_missing": True,
            "host_monotonic_us": raw_host_us,
            "timestamp_basis": "last_app_event" if last_app_us > 0 else "host_clock",
        },
    }
    path = bundle / "events.jsonl"
    with path.open("a", encoding="utf-8") as file:
        file.write(json.dumps(marker, ensure_ascii=False, separators=(",", ":")) + "\n")


def normalize_events(bundle: Path) -> None:
    events = load_events(bundle)
    unique: list[dict[str, Any]] = []
    fingerprints: set[str] = set()
    for event in events:
        fingerprint = json.dumps(event, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        if fingerprint in fingerprints:
            continue
        fingerprints.add(fingerprint)
        unique.append(event)
    unique.sort(key=lambda event: (
        int(event.get("monotonic_us", 0)), int(event.get("sequence", 0)),
        str(event.get("layer", "")),
    ))
    path = bundle / "events.jsonl"
    path.write_text(
        "".join(json.dumps(event, ensure_ascii=False, separators=(",", ":")) + "\n"
                for event in unique),
        encoding="utf-8",
    )
    web_path = bundle / "events.web.jsonl"
    if web_path.exists():
        web_path.unlink()


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    return values[min(len(values) - 1, int((len(values) - 1) * fraction))]


def write_summary(bundle: Path, session: str, platform: str, profile: str) -> None:
    events = load_events(bundle)
    markers = [event for event in events if event.get("event") == "issue_marker"]
    warnings = [event for event in events
                if event.get("level") in {"warning", "error", "fatal"}
                and event.get("event") != "issue_marker"]
    spikes = [event for event in events if "spike" in str(event.get("event", ""))]
    anomalies = sorted(warnings + spikes, key=lambda event: int(event.get("monotonic_us", 0)))
    summaries = [event for event in events if event.get("event") == "frame_summary"]
    dropped = max((int(event.get("queue_dropped", 0)) for event in events), default=0)
    p95_values = [float(event.get("fields", {}).get("p95_ms", 0)) for event in summaries]
    max_values = [float(event.get("fields", {}).get("max_ms", 0)) for event in summaries]
    lines = [
        "# AetherKiri Diagnostic Summary", "",
        f"- Session: `{session}`", f"- Platform: `{platform}`", f"- Profile: `{profile}`",
        f"- Structured events: {len(events)}", f"- Issue markers: {len(markers)}",
        f"- Warning/error events: {len(warnings)}", f"- Slow events: {len(spikes)}",
        f"- Queue drops reported: {dropped}",
    ]
    if summaries:
        lines += [
            f"- Frame-summary P95 median: {percentile(p95_values, 0.5):.2f} ms",
            f"- Worst summarized frame: {max(max_values, default=0):.2f} ms",
        ]
    lines += ["", "## First observed anomaly boundary", ""]
    if anomalies:
        first = anomalies[0]
        lines.append(
            f"`{first.get('layer')}/{first.get('subsystem')}` first reported "
            f"`{first.get('event')}` at `{first.get('monotonic_us', 0)}`. "
            "Evidence before this boundary did not confirm a cause."
        )
    else:
        lines.append("No warning, error, or bounded slow event was observed in the structured stream.")
    lines += ["", "## Marker windows", ""]
    if not markers:
        lines.append("No marker was captured. Platform evidence is still included.")
    for index, marker in enumerate(markers, 1):
        timestamp = int(marker.get("monotonic_us", 0))
        nearby = []
        nearby_keys: set[tuple[Any, ...]] = set()
        for event in warnings + spikes:
            event_time = int(event.get("monotonic_us", 0))
            if not timestamp - 10_000_000 <= event_time <= timestamp + 5_000_000:
                continue
            key = (event.get("session"), event.get("sequence"), event_time,
                   event.get("layer"), event.get("event"))
            if key in nearby_keys:
                continue
            nearby_keys.add(key)
            nearby.append(event)
        fields = marker.get("fields", {})
        lines.append(f"### Marker {index}: `{fields.get('label', 'issue')}`")
        lines.append("")
        lines.append(f"- Timestamp: `{timestamp}`")
        lines.append(f"- Nearby warning/slow events: {len(nearby)}")
        for event in nearby[:12]:
            lines.append(
                f"- `{event.get('layer')}/{event.get('subsystem')}` "
                f"`{event.get('event')}` duration={event.get('duration_us', 0)}us"
            )
        lines.append("")
    lines += [
        "## Interpretation boundary", "",
        "This report aligns evidence to the reproduction window. Temporal proximity alone is not treated as a confirmed cause.",
    ]
    recommendation = "Keep the current profile and reproduce a narrower window."
    if dropped > 0:
        recommendation = "Re-run a shorter capture with the same profile before increasing detail; the bounded queue overflowed."
    elif profile == "baseline" and spikes:
        recommendation = "Re-run with `--profile render` to expand the slow rendering boundary."
    elif profile == "baseline":
        recommendation = "Re-run with `--profile system` if the symptom appears outside the app tick."
    lines += ["", "## Suggested next profile", "", recommendation]
    (bundle / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_tool_metadata(args: argparse.Namespace, bundle: Path) -> None:
    metadata = {
        "schema": 1,
        "session": args.session,
        "created_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "platform": args.platform,
        "profile": args.profile,
        "git_revision": git_revision(),
        "build_type": "debug" if getattr(args, "build_install", False) else "installed",
        "capture_mode": "build-install" if getattr(args, "build_install", False) else "existing-install",
        "raw_logs": True,
        "screenshots_requested": bool(args.include_screenshot),
        "tool": "tools/diagnose.py",
    }
    (bundle / "metadata.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")


def execute(args: argparse.Namespace) -> Path:
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    args.session = args.session or f"{timestamp}-{args.platform}-{uuid.uuid4().hex[:8]}"
    args.bundle = Path(args.output).resolve() if args.output else OUT_ROOT / args.session
    if args.platform in PREFLIGHT:
        PREFLIGHT[args.platform](args)
    bundle: Path = args.bundle
    (bundle / "platform").mkdir(parents=True, exist_ok=False)
    write_tool_metadata(args, bundle)
    env = diagnostic_env(args.profile, args.session)
    state: dict[str, Any] = {}
    error: Exception | None = None
    try:
        if getattr(args, "build_install", False) and not args.reuse_build:
            build("ios" if args.platform == "ios" else args.platform)
        state = PREPARE[args.platform](args, bundle, env)
        wait_for_reproduction(args.duration)
        print("[diagnose] Preserving the 5-second post-marker window…", flush=True)
        time.sleep(POST_MARKER_GRACE_SECONDS)
    except (DiagnoseError, KeyboardInterrupt) as caught:
        error = caught
    finally:
        try:
            COLLECT[args.platform](args, bundle, state)
        except Exception as collect_error:  # preserve partial evidence
            (bundle / "platform/collection-error.txt").write_text(str(collect_error), encoding="utf-8")
            if error is None:
                error = collect_error
    consolidate_app_files(bundle, args.session)
    merge_app_metadata(bundle)
    events = load_events(bundle)
    add_host_marker_if_needed(bundle, args.session, events)
    normalize_events(bundle)
    write_summary(bundle, args.session, args.platform, args.profile)
    archive = shutil.make_archive(str(bundle), "zip", bundle)
    print(f"[diagnose] Bundle: {bundle}")
    print(f"[diagnose] Archive: {archive}")
    if error is not None:
        raise DiagnoseError(f"Diagnostic run completed with partial evidence: {error}")
    return bundle


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    subcommands = root.add_subparsers(dest="command", required=True)
    run_parser = subcommands.add_parser(
        "run", help="capture an existing installation and package one reproduction")
    run_parser.add_argument("platform", choices=tuple(PREPARE))
    run_parser.add_argument("--profile", choices=PROFILES, default="baseline")
    run_parser.add_argument(
        "--build-install", action="store_true",
        help="explicitly build and install before capture (off by default)",
    )
    run_parser.add_argument(
        "--reuse-build", action="store_true",
        help="with --build-install, install the existing artifact without rebuilding",
    )
    run_parser.add_argument("--device", help="Android serial or iOS device identifier/name")
    run_parser.add_argument("--duration", type=float, help="non-interactive capture duration in seconds")
    run_parser.add_argument("--session", help="explicit diagnostic session id")
    run_parser.add_argument("--output", help="explicit uncompressed bundle directory")
    run_parser.add_argument("--include-screenshot", action="store_true",
                            help="reserve screenshot inclusion in metadata; capture remains opt-in")
    return root


def main() -> int:
    args = parser().parse_args()
    try:
        execute(args)
    except DiagnoseError as error:
        print(f"[diagnose] ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
