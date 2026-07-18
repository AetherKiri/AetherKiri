---
name: unpack-investigate-artifacts
description: Safely unpack and investigate existing archives through evidence-first static analysis. Use when Codex is given a diagnostic ZIP or directory produced by a collection tool such as AetherKiri tools/diagnose.py, or a TAR, APK, IPA, .app bundle, crash archive, or opaque package, and needs to validate integrity, align events and markers, compare layers, or identify the first unsupported boundary without building, installing, launching, reproducing, or collecting from a device.
---

# Unpack and Investigate Artifacts

Begin with the artifact the user supplied. Do not build, install, launch, reproduce, or run any collection command. In particular, never invoke `tools/diagnose.py` from this skill; it consumes the package that script already produced.

## Recognize an AetherKiri diagnostic bundle

Treat an existing `out/diagnostics/<timestamp>-<platform>-<session>.zip` or its matching directory as the primary input for this workflow. If the user says “the diagnostic package” without a path, locate the newest completed ZIP under `out/diagnostics/`; do not run `tools/diagnose.py` to create one.

After safe extraction, verify the bundle contract before interpreting it:

- root `metadata.json` identifies the requested session, platform, profile, Git revision, and capture mode;
- app metadata may have been merged into root metadata, while raw app files can remain under `app-data/`;
- root `events.jsonl` is the normalized cross-layer stream; check JSON validity, session IDs, sequence continuity, clock basis, and `queue_dropped` before sorting or filtering it;
- `incidents/marker-*-pre.jsonl` and `marker-*-post.jsonl` preserve the marker windows when the UI marker was accepted;
- `incidents/marker-*-state.json` captures the bounded performance, input, memory, and plugin context sampled by that one-click marker;
- `attachments/state-snapshot-*.json` contains explicit in-app state snapshots and `attachments/screenshot-*.png` contains only user-requested screenshots;
- `diagnostic_self_check` events report which app-side facilities were reachable, but do not prove that unrelated runtime paths were healthy;
- `platform/` contains raw host/platform evidence and collection errors or omissions;
- `summary.md` is a generated lead, not source evidence. Re-check every material statement against the structured events and raw platform files.

An absent UI marker is not automatically a failed bundle. Check for a host marker with `ui_marker_missing=true`, then state which app-side boundary is unavailable. Treat a typed marker label, state snapshot, screenshot, and self-check as separate evidence with independent timestamps.

## Preserve provenance

1. Record the source path, byte size, modification time, and SHA-256 before extraction.
2. Identify the format from magic and structure, not only the extension.
3. List archive members before extracting. Flag encrypted entries, absolute paths, traversal components, links, devices, surprising compression ratios, and duplicate names.
4. Extract into a new disposable directory. Never overwrite the source or extract over a working tree.

For ZIP-compatible packages and TAR archives, prefer:

```bash
python3 scripts/safe_unpack.py ARTIFACT OUTPUT_DIR --manifest OUTPUT_DIR.manifest.json
```

The script rejects traversal, links, special files, excessive member counts, and expanded-size limits. For unsupported formats, list with the narrowest available format-aware tool before choosing an extractor with equivalent protections.

## Build an inventory

Use `rg --files` first, then group files by role rather than dumping every path:

- metadata and manifests;
- structured events, markers, and incident windows;
- explicit state snapshots, screenshots, self-checks, and in-app export artifacts;
- platform logs, crash reports, traces, and thread dumps;
- binaries, architectures, symbols, libraries, plugins, and signatures;
- assets, scripts, configuration, databases, and caches;
- missing, truncated, duplicated, or unexpectedly empty evidence.

Keep secrets out of output. Report the presence and location of credentials, tokens, provisioning data, or personal content without printing their values.

## Route the investigation

- Diagnostic bundle: validate metadata, session identity, sequence continuity, clock basis, queue drops, marker pairing, and pre/post windows before interpreting symptoms.
- Android package or evidence: inspect manifest, ABI libraries, resources/assets, mapping or symbols, PID-scoped logcat, crash buffers, ANR/thread evidence, and device/build identity.
- Apple package or evidence: inspect `Info.plist`, architectures, frameworks, entitlements, dSYM/UUID matches, crash logs, device/build identity, and app Documents evidence.
- Web package or evidence: inspect build metadata, source maps, browser console/network evidence, workers, storage, and deployment headers.
- Generic resource or game archive: inspect indexes, nested containers, encodings, scripts, plugin boundaries, and ownership/lifetime transitions.

Read only the relevant variant. Do not infer runtime behavior solely from package layout.

## Establish the evidence chain

1. Translate the reported symptom or typed marker label into one or more falsifiable boundaries.
2. Anchor the timeline on explicit UI/host markers, snapshot timestamps, crash timestamps, sequence numbers, or monotonic clocks. State when clocks cannot be aligned.
3. Find the deepest confirmed successful boundary and the first missing, failed, or contradictory boundary.
4. Compare both sides of asynchronous or ownership transitions: queued/completed, created/destroyed, written/flushed, encoded/decoded, loaded/presented.
5. Separate facts, inferences, and unknowns. Temporal proximity is not causation.
6. Prefer a narrow next-evidence request over a broad request for every log.

For performance evidence, distinguish total duration from self-time and note whether tracing or logging could perturb timing. For rendering or missing UI, separate creation, enqueue, execution, upload, presentation, and input hit-testing.

## Report findings

Return a compact investigation report containing:

1. artifact identity and extraction integrity;
2. relevant inventory and exact evidence locations;
3. confirmed timeline and boundaries;
4. strongest supported finding;
5. competing explanations that remain possible;
6. missing evidence and the smallest useful next step.

Do not claim a root cause when the artifact only establishes correlation or the failure lies beyond the captured boundary.
