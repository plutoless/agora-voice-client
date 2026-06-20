# Release CI — Design Spec

**Date:** 2026-06-20
**Status:** Approved design, ready for implementation planning

## Purpose

Produce a publicly downloadable macOS build of `agora-voice-client` so it can be handed
to testers (and the Operator's other machines) without building from source. The build
is created by CI on a version tag and published as a GitHub Release asset.

## Scope

In scope:
- A GitHub Actions workflow that, on a `v*` tag push, builds a **universal**
  (arm64 + x86_64) macOS binary, runs the unit tests as a gate, packages a relocatable
  archive, and attaches it to a GitHub Release.
- A small change to `scripts/package.sh` to label the archive by architecture.
- A tag-stamped `--version` flag (CMake `AVC_VERSION` define + `main` handling) so test
  builds are identifiable.
- Documentation of the macOS Gatekeeper bypass for the un-notarized download.

Out of scope (YAGNI):
- Notarization / Apple Developer ID signing (no certificate yet — ADR-0002). The build
  stays ad-hoc signed.
- Windows/Linux build jobs.
- Per-commit / per-PR CI.

## Trigger and output

- **Trigger:** push of a tag matching `v*` (e.g. `v0.1.0`).
- **Output:** a GitHub Release for that tag with the asset
  `agora-voice-client-macos-universal.tar.gz` (binary + bundled Agora frameworks),
  downloadable at `…/releases/download/<tag>/agora-voice-client-macos-universal.tar.gz`.

## Workflow (`.github/workflows/release.yml`, runner pinned to `macos-15`)

The runner is pinned to **`macos-15`** (not `macos-latest`) for reproducible release
builds; bumping it is a deliberate, reviewed change. The job declares
`permissions: contents: write` so the default `GITHUB_TOKEN` may create a Release.

1. **Checkout** the tagged commit.
2. **Fetch SDK:** `./scripts/fetch-sdk.sh` — downloads the pinned Agora SDK from the
   public CDN (no login). The extracted `macos-arm64_x86_64` framework slice is already
   universal, so it supports both architectures.
3. **Configure universal:** `cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DAVC_VERSION="${GITHUB_REF_NAME}"`
   (the tag is stamped into the binary's `--version`).
4. **Build:** `cmake --build build`.
5. **Test gate:** `./build/tests` — if unit tests fail, the job fails and no Release is
   published. (Tests build universal but execute on the runner's native arm64 slice; the
   x86_64 slice is not run — acceptable since the tested code is arch-agnostic.)
6. **Package + verify universal:** `./scripts/package.sh universal` →
   `dist/agora-voice-client-macos-universal.tar.gz`. Then assert the binary is actually
   fat: `lipo -archs dist/agora-voice-client-macos-universal/agora-voice-client` must
   report both `x86_64` and `arm64` (catches a silently single-slice build); fail the job
   otherwise.
7. **Publish (idempotent):** create the Release if absent, else re-upload the asset:
   ```bash
   gh release create "$GITHUB_REF_NAME" dist/*.tar.gz --title "$GITHUB_REF_NAME" \
     --notes-file release-notes.md \
     || gh release upload "$GITHUB_REF_NAME" dist/*.tar.gz --clobber
   ```
   Uses the preinstalled `gh` CLI authed by `GITHUB_TOKEN` (no third-party action).
   `release-notes.md` is a committed template carrying the Gatekeeper-bypass and
   first-run-mic instructions.

## Supporting changes

- **`scripts/package.sh`:** accept an optional architecture label argument, defaulting to
  `arm64`. The label is used for the staging dir and archive name
  (`agora-voice-client-macos-<label>`). All other behavior is unchanged: copy the binary
  + all `*.framework` from `third_party/agora/`, ad-hoc re-sign each framework, add an
  `@loader_path` rpath to the binary, re-sign the binary, and tar.
- **CMake:** add an `AVC_VERSION` cache variable (default `"dev"`) compiled in as a
  define (e.g. `AVC_VERSION_STR`). The universal-arch flag is passed by the workflow;
  `CMAKE_OSX_ARCHITECTURES` produces a `lipo` fat binary that ad-hoc signs normally.
- **`--version` flag (`src/main.cpp`):** before building the env map / calling
  `parse_config`, scan argv for `--version` (and `--help`); on `--version`, print
  `AVC_VERSION_STR` and exit 0. Local builds report `dev`; CI builds report the tag.
  This must short-circuit *before* config validation so `--version` works with no env/flags.
- **Documentation (Release notes body + README):** state that the build is **ad-hoc
  signed and not notarized**, so after download the user must clear quarantine once:
  `xattr -dr com.apple.quarantine agora-voice-client-macos-universal` (or right-click →
  Open). The existing first-run microphone-permission note (must be approved at the Mac,
  ADR-0002) also applies on the tester's machine.

## Verification

- **Local pre-flight** (so CI is not the first universal attempt): run the exact
  universal configure + build + `./build/tests` + `./scripts/package.sh universal`, and
  confirm `lipo -archs dist/agora-voice-client-macos-universal/agora-voice-client` reports
  `x86_64 arm64`, the packaged binary runs from its own dir (usage, exit 2), and
  `--version` prints the value of `-DAVC_VERSION`.
- **Quarantine-bypass acceptance test** (don't ship the download on faith): simulate a
  fresh download by setting quarantine on a copy
  (`xattr -w com.apple.quarantine "0081;0;test;" <binary>`), confirm it is blocked, then
  `xattr -dr com.apple.quarantine <dir>` and confirm it runs. This proves the documented
  bypass is sufficient for the ad-hoc + hardened-runtime binary (builds on ADR-0002).
- **CI end-to-end** can only be fully verified by pushing a real `v*` tag and watching the
  Actions run produce the Release asset. This is the final manual check.

## Constraints / notes

- The repo is public, so the Release asset is publicly downloadable — acceptable; no
  secrets are in the build (App ID/Certificate are runtime env only, never committed).
- The Agora SDK download (~88 MB) happens on every release build; acceptable for a
  tag-triggered workflow. Caching could be added later if needed.
- **The workflow only triggers if `.github/workflows/release.yml` exists on the tagged
  commit.** Since the client + this CI currently live on `feat/agora-voice-client` (PR #1,
  not yet merged), the first usable tag must point at a commit that includes the workflow
  — i.e. merge to `master` first, then tag, or tag a commit on the branch that contains it.
- Windows support is a separate, later effort (a client port using the C++ `IRtcEngine`
  API + BCrypt HMAC, then a Windows CI job). Out of scope for this spec.
