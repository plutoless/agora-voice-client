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

## Workflow (`.github/workflows/release.yml`, runner `macos-latest`)

1. **Checkout** the tagged commit.
2. **Fetch SDK:** `./scripts/fetch-sdk.sh` — downloads the pinned Agora SDK from the
   public CDN (no login). The extracted `macos-arm64_x86_64` framework slice is already
   universal, so it supports both architectures.
3. **Configure universal:** `cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.
4. **Build:** `cmake --build build`.
5. **Test gate:** `./build/tests` — if unit tests fail, the job fails and no Release is
   published. (Tests build universal but execute on the runner's native arch.)
6. **Package:** `./scripts/package.sh universal` → `dist/agora-voice-client-macos-universal.tar.gz`.
7. **Publish:** create the GitHub Release for the tag and upload the tarball, with
   auto-generated notes plus the Gatekeeper-bypass instructions. Uses the built-in
   `GITHUB_TOKEN` (needs `contents: write` permission).

## Supporting changes

- **`scripts/package.sh`:** accept an optional architecture label argument, defaulting to
  `arm64`. The label is used for the staging dir and archive name
  (`agora-voice-client-macos-<label>`). All other behavior is unchanged: copy the binary
  + all `*.framework` from `third_party/agora/`, ad-hoc re-sign each framework, add an
  `@loader_path` rpath to the binary, re-sign the binary, and tar.
- **CMake:** no change required. The universal flag is passed by the workflow;
  `CMAKE_OSX_ARCHITECTURES` produces a `lipo` fat binary that ad-hoc signs normally.
- **Documentation (Release notes body + README):** state that the build is **ad-hoc
  signed and not notarized**, so after download the user must clear quarantine once:
  `xattr -dr com.apple.quarantine agora-voice-client-macos-universal` (or right-click →
  Open). The existing first-run microphone-permission note (must be approved at the Mac,
  ADR-0002) also applies on the tester's machine.

## Verification

- **Local pre-flight** (so CI is not the first universal attempt): run the exact
  universal configure + build + `./build/tests` + `./scripts/package.sh universal`, and
  confirm `lipo -archs dist/agora-voice-client-macos-universal/agora-voice-client` reports
  `x86_64 arm64`, and the packaged binary runs from its own dir (usage, exit 2).
- **CI end-to-end** can only be fully verified by pushing a real `v*` tag and watching the
  Actions run produce the Release asset. This is the final manual check.

## Constraints / notes

- The repo is public, so the Release asset is publicly downloadable — acceptable; no
  secrets are in the build (App ID/Certificate are runtime env only, never committed).
- The Agora SDK download (~88 MB) happens on every release build; acceptable for a
  tag-triggered workflow. Caching could be added later if needed.
