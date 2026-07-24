---
name: fix-aetherkiri-game-compatibility
description: Diagnose, implement, validate, commit, and document end-to-end compatibility fixes for KiriKiri games running in AetherKiri. Use when a game fails to import or start, loses backgrounds or character layers, crashes after extended play or save/load, stalls in a native-plugin-dependent mode, renders a black frame, has path or script compatibility failures, or needs coordinated changes across the public AetherKiri repository and private AetherInternal package followed by local builds and verified-games updates.
---

# Fix AetherKiri Game Compatibility

Carry a game from a reproducible failure to a verified minimal fix. Preserve the public/private package boundary, validate the actual affected flow, and leave a reviewable commit chain.

## Establish scope and repository state

1. Identify the public AetherKiri worktree, its current branch and remotes, and the checked-out `packages/AetherInternal` revision.
2. Read repository instructions and inspect `git status` in both repositories before changing anything.
3. Preserve unrelated tracked changes and untracked local helpers. Never commit game packages, saves, logs, generated builds, proprietary SDK files, credentials, or local paths.
4. Pull or fetch the requested upstream state before reproduction when the user asks to test the latest code. Create a dedicated compatibility branch unless the user identifies an existing branch.
5. Record the exact original game title separately from localized folder names, translation labels, or test-directory aliases.

Do not treat a deliberately missing or modified save, an unrelated popup, or a stale process as the target failure. Follow the user's stated boundary.

## Reproduce with evidence

1. Launch through the normal AetherKiri game-selection interface unless the user explicitly requests direct auto-start.
2. Capture the newest KiriKiri/AetherKiri log and correlate it with the visible symptom. Use focused diagnostic environment variables only when existing evidence is insufficient.
3. Reduce the failure to the first unsupported boundary, for example:
   - path normalization or archive lookup;
   - TJS parsing, dispatch, member calls, or dictionary loading;
   - plugin registration or native module behavior;
   - layer creation, texture upload, compositing, or presentation;
   - save serialization, encoding, ownership, or lifetime;
   - input-method composition and committed-text delivery.
4. Distinguish startup success from gameplay success. A title page proves only the startup path; it does not verify save/load, prolonged play, galleries, movies, character rendering, or plugin-driven RPG scenes.
5. Prefer narrow tracing that exposes names, types, arguments, transitions, and failure codes. Avoid permanent high-volume logging unless it is independently useful and appropriately gated.

For an existing diagnostic ZIP, use `$unpack-investigate-artifacts` rather than recollecting evidence.

## Determine implementation ownership

Inspect the code that owns the missing behavior before editing:

- Put general engine behavior, public interfaces, diagnostics, portable fallbacks, registration hooks, and build integration in AetherKiri.
- Put non-public native-plugin compatibility implementations and private extension sources in AetherInternal.
- Keep game-specific filenames, constants, and script workarounds out of shared engine paths unless evidence proves they represent a reusable engine contract.
- Reuse existing implementations and ABI boundaries before adding a parallel subsystem.
- Do not alter `.github/workflows` merely to compensate for an incomplete local checkout or missing private package. Preserve a working remote build contract.

If both repositories must change, make the public side tolerate the package being absent when that is an established project requirement. Do not silently degrade a build that is expected to include Internal.

## Implement the minimum complete fix

1. Trace the failing call from script-visible behavior through plugin/engine boundaries to rendering or state output.
2. Implement every operation required by the smallest real game flow, including lifecycle and error behavior. A stub returning success is not a fix when downstream state remains absent.
3. Match existing data structures, ownership rules, encodings, and platform abstractions.
4. Keep Win32 window management out of portable plugin emulation when AetherKiri already owns the window and presentation path.
5. Route drawing through AetherKiri's existing renderer and texture/compositing facilities. Verify alpha, ordering, transforms, clipping, invalidation, resize, and frame presentation as applicable.
6. Preserve full Unicode input. Buffer IME pre-edit/composition state outside the KiriKiri layer and send only committed text; support Chinese, Japanese, and other composed input without leaking transient punctuation or candidates.
7. Add focused tests for stable public contracts where practical. Use a real authorized game flow for behavior that cannot be represented faithfully by a unit test.

Do not broaden the patch into unrelated cleanup.

## Coordinate public and private commits

When AetherInternal changes:

1. Build and inspect the Internal change in its own repository.
2. Commit and push the Internal implementation first.
3. Update the public repository's `packages/AetherInternal` gitlink to that reachable commit.
4. Commit public interfaces, loader behavior, tests, and the gitlink together only when they form one coherent compatibility step.
5. Confirm the public checkout can resolve the pinned private revision in the intended authenticated workflow.

Commit files in the repository that owns them. Use the user-specified author identity. Stage explicit paths, inspect the staged diff, and keep local helpers and generated artifacts out of every commit.

## Validate the repaired flow

Run validation in increasing scope:

1. Run focused tests or compile the changed targets.
2. Configure a fresh local build with AetherInternal enabled and confirm configuration reports the private package as enabled.
3. Build the complete requested application target, not only a library.
4. Confirm the expected Internal and public sources actually compiled or linked.
5. Start the normal application and import/select the authorized local game.
6. Reproduce the formerly failing boundary and continue beyond it.
7. Exercise the relevant regression surface: background and character rendering, repeated save/load after extended runtime, input, audio, scene changes, return-to-title behavior, or native-plugin-dependent RPG rendering.
8. Inspect logs for script errors, bridge failures, missing module registrations, crashes, deadlocks, and repeated warnings.

Do not claim success from compilation alone. For a render fix, require a presented non-black frame with the expected layers. For an RPG/native-plugin fix, enter the RPG scene and verify its map, character, HUD, and input rather than relying on story-mode startup.

When a remote workflow supplies private content, keep its existing interface intact and verify local behavior through the same package contract. Change workflows only when the workflow itself is the demonstrated failure and the user authorizes that scope.

## Document support and finish

After the user confirms the game works:

1. Update both `doc/verified_games.md` and `doc/verified_games.zh-CN.md`.
2. Use the official original game title. Do not publish the local folder name.
3. Record only platforms and build profiles actually tested, such as `Windows x64 debug app`.
4. Describe only flows that were exercised. Use smoke verification for startup/basic-input coverage and flow verification only for explicitly tested in-game flows.
5. Credit the verifier and keep machine-local game paths out of the documents.
6. Update both document dates and keep the English and Chinese rows equivalent.
7. Run whitespace checks, inspect the final diff, and confirm workflow files and unrelated files remain unchanged.
8. Commit the documentation on the compatibility branch with explicit file staging.

Report the branch, public and private commit IDs, build result, exact verified flow, support-list title/platform, and anything intentionally left uncommitted. Push only when the user requests it or the active task already includes publishing.
