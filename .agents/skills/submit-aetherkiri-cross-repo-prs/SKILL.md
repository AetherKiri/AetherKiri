---
name: submit-aetherkiri-cross-repo-prs
description: Prepare, validate, push, open, monitor, and merge paired pull requests across the public AetherKiri repository and private AetherInternal package without publishing private source or unrelated history. Use when a change spans public engine hooks and private plugin implementations, when a public gitlink must pin an unmerged Internal commit for trusted CI, when GitHub Actions must build the combined product, or when reviewing cross-repository PR security, workflow failures, merge order, and cleanup.
---

# Submit AetherKiri Cross-Repo PRs

Create minimal, reviewable public and private PRs while preserving the AetherInternal confidentiality boundary. Treat successful compilation and confidentiality as separate requirements.

## Establish the trust boundary

Confirm all of the following before pushing:

- AetherKiri is public and `packages/AetherInternal` is a private git submodule.
- Private implementation, tests that disclose algorithms, reverse-engineering notes, and proprietary assets remain in AetherInternal.
- Public changes contain only generic interfaces, portable fallback behavior, package integration, non-sensitive tests, documentation, and the gitlink.
- The public branch is created inside `AetherKiri/AetherKiri` when combined CI requires repository secrets. Fork and Dependabot PRs do not receive `AETHERSECRET` and must build public fallbacks.
- The workflow uses `pull_request`, not `pull_request_target`, to execute PR code.

Do not equate a private checkout with zero disclosure. A public gitlink reveals the private commit SHA. Public Actions logs can reveal private paths, symbol names, warnings, or compiler error excerpts. Uploaded application artifacts contain compiled Internal code and can be reverse engineered. Never claim absolute secrecy when public CI builds private code.

## Audit public workflow exposure

Before relying on combined CI, inspect `.github/workflows` on the PR base and branch:

1. Confirm trusted same-repository PRs enable Internal and supply only the read-only deploy key required for submodule checkout.
2. Confirm untrusted/fork PRs disable recursive private submodules and select public fallbacks.
3. Keep `persist-credentials: false` and repository permissions at `contents: read` unless a reviewed job requires more.
4. Confirm no step prints secrets, SSH configuration, private diffs, source contents, environment dumps, or generated compile databases.
5. Review every artifact upload. Public app artifacts include private compiled behavior even when source stays private. Upload them only when that distribution is intentional.
6. Keep caches from untrusted PRs read-only and never cache credentials or private source trees into a public artifact.

If private source confidentiality is stricter than public build-log exposure allows, run the full build in AetherInternal and report a status to public instead of compiling Internal in public Actions.

## Create clean branches

Use independent worktrees based on the latest remote default branches. Do not open a PR from a long-lived branch containing removed, reverted, or previously private history.

1. Fetch both remotes and inspect open PRs.
2. Create the Internal branch from the latest `AetherInternal/origin/main`.
3. Create the public branch from the latest `AetherKiri/origin/main`.
4. Reapply only the commits required for the current fix. Preserve author identity.
5. Scan the public diff for game files, local paths, private class names, algorithms, proprietary SDK references, secrets, and unrelated historical commits.
6. Leave local launchers, logs, saves, builds, and game packages untracked.

Use `$fix-aetherkiri-game-compatibility` to determine public/private implementation ownership and validate the game behavior before submission.

## Submit Internal first

1. Commit the private implementation in AetherInternal.
2. Rebase or merge the latest private `origin/main` before publishing.
3. Push the private branch.
4. Obtain the full commit ID from Git; never type or extrapolate it from a short prefix:

```bash
git rev-parse HEAD
git ls-remote origin refs/heads/BRANCH
```

Require both commands to report the same 40-character SHA.

5. Open the Internal PR against `main` and record its URL.
6. Keep the remote branch until the public PR has merged or its gitlink points to an Internal `main` commit.

An Internal commit does not need to be merged for trusted public CI to test it. It must be reachable from an advertised private remote ref and readable by the workflow deploy key.

## Prepare the public PR

1. Add only generic engine hooks and public behavior required by the private extension.
2. Pin `packages/AetherInternal` to the verified full Internal SHA. Prefer checking out the submodule commit and staging it; if using `git update-index --cacheinfo`, paste the output of `git rev-parse`, then verify:

```bash
git ls-tree HEAD packages/AetherInternal
git ls-remote INTERNAL_URL refs/heads/INTERNAL_BRANCH
```

The two SHAs must match exactly.

3. Do not copy private implementation into a public fallback merely to make CI pass.
4. Add public tests and compatibility documentation without private fixtures or local paths.
5. Run `git diff --check`, inspect every public file, and confirm `.github/workflows` is unchanged unless workflow behavior is explicitly part of the reviewed fix.
6. Push the same-repository public branch and open the PR against `main`.
7. Link the Internal PR from the public PR and the public PR from the Internal PR. Describe contracts and behavior, not private implementation details.

## Monitor combined CI

Watch the new run for the exact public head SHA. Ignore superseded runs.

1. Confirm `Checkout Repository` succeeds.
2. Confirm `Verify AetherInternal package` succeeds and reports the expected gitlink.
3. Confirm configuration explicitly enables AetherInternal.
4. Confirm requested platform builds and focused tests run.
5. Review public logs before sharing them. Move detailed private compiler diagnostics to the private PR when they disclose implementation.
6. Check uploaded artifacts against the intended distribution policy.

An immediate `not our ref` failure means the gitlink is wrong, the commit was not pushed, the branch was deleted, or the deploy key cannot read it. Compare full SHAs before changing workflows.

If public `main` advances and the PR becomes conflicting, merge or rebase the latest base. Resolve a submodule conflict only after proving ancestry:

```bash
git -C INTERNAL_REPO merge-base --is-ancestor BASE_INTERNAL_SHA PR_INTERNAL_SHA
```

Retain the PR Internal SHA when it descends from the new base pin. Re-run diff and workflow checks after resolving.

## Merge safely

Prefer this order:

1. Merge the Internal PR.
2. Determine the commit now reachable from Internal `main`.
3. If Internal was squash-merged or rebased, update the public gitlink to the new resulting SHA and rerun CI.
4. Merge the public PR only after its gitlink is permanently reachable.
5. Delete feature branches only after both default branches and CI no longer depend on them.

Public may technically merge while pinning an unmerged private branch commit, and `main` workflow can build it while that ref remains available. Do not use this as the normal merge order because deleting or rewriting the private branch would break every fresh checkout of public `main`.

## Report completion

Report both PR URLs, exact head SHAs, public gitlink SHA, mergeability, CI status, intended artifact exposure, and merge order. State explicitly that private source is not present in the public diff, while acknowledging any public build-log and binary-artifact exposure.
