# Release CI (macOS universal) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On a `v*` tag push, GitHub Actions builds a universal (arm64+x86_64) macOS `agora-voice-client`, runs the unit tests as a gate, packages a relocatable ad-hoc-signed archive, and publishes it as a downloadable GitHub Release asset.

**Architecture:** A single tag-triggered workflow on a pinned `macos-15` runner reuses the existing `scripts/fetch-sdk.sh` and `scripts/package.sh`. Supporting changes: `package.sh` gains an architecture label argument; the binary gains a tag-stamped `--version` flag; a committed `release-notes.md` carries the Gatekeeper-bypass + mic instructions.

**Tech Stack:** GitHub Actions, `gh` CLI, CMake (`CMAKE_OSX_ARCHITECTURES`, compile define), bash, `lipo`, `codesign`, `xattr`.

**Spec:** `docs/superpowers/specs/2026-06-20-release-ci-design.md`. Builds on ADR-0002 (ad-hoc signing / mic permission).

**Context for the implementer:** This work lives on branch `feat/agora-voice-client` (which contains the whole client + its build scripts; not yet merged to master). The Agora SDK is fetched locally already (`third_party/agora/AgoraRtcKit.framework` exists); if missing, run `./scripts/fetch-sdk.sh`. The app builds via `cmake -S . -B build && cmake --build build` producing `build/agora-voice-client`. Existing tests: `./build/tests` (15 cases).

---

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `CMakeLists.txt` | Modify | Add `AVC_VERSION` cache var → `AVC_VERSION_STR` compile define on the app target |
| `src/main.cpp` | Modify | Handle `--version`/`--help` before config parsing; print stamped version |
| `scripts/package.sh` | Modify | Accept arch-label arg (default `arm64`); name dir/archive by it |
| `release-notes.md` | Create | GitHub Release body: Gatekeeper bypass + first-run mic + version note |
| `.github/workflows/release.yml` | Create | Tag-triggered build → test → package → verify → publish |

---

## Task 1: Tag-stamped `--version` flag

**Files:**
- Modify: `CMakeLists.txt` (app target only)
- Modify: `src/main.cpp`

- [ ] **Step 1: Add the version define to the app target in `CMakeLists.txt`**

After the `agora-voice-client` target's `target_include_directories(...)` block (around line 41), add:

```cmake
set(AVC_VERSION "dev" CACHE STRING "Version string stamped into the binary")
target_compile_definitions(agora-voice-client PRIVATE AVC_VERSION_STR="${AVC_VERSION}")
```

Do NOT add this to the `tests` target (tests don't include `main.cpp`).

- [ ] **Step 2: Handle `--version`/`--help` early in `src/main.cpp`**

Add this fallback near the top of `src/main.cpp`, after the existing `#include` lines:

```cpp
#ifndef AVC_VERSION_STR
#define AVC_VERSION_STR "dev"
#endif
```

Then, inside `main`, as the very first statements (before the `std::map<std::string, std::string> env;` line), add:

```cpp
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--version") {
      std::cout << "agora-voice-client " << AVC_VERSION_STR << "\n";
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "usage: agora-voice-client --channel <name> [--uid <n>] "
                   "[--token-ttl <seconds>] [--version]\n";
      return 0;
    }
  }
```

(`<iostream>` and `<string>` are already included in main.cpp.)

- [ ] **Step 3: Build with a stamped version and verify**

Run:
```bash
test -d third_party/agora/AgoraRtcKit.framework || ./scripts/fetch-sdk.sh
cmake -S . -B build -DAVC_VERSION="test-1.2.3" && cmake --build build
./build/agora-voice-client --version
```
Expected: prints `agora-voice-client test-1.2.3` and exits 0.

- [ ] **Step 4: Verify `--help` and that normal usage still works**

Run:
```bash
./build/agora-voice-client --help ; echo "help_exit=$?"
./build/agora-voice-client ; echo "noargs_exit=$?"
./build/tests
```
Expected: `--help` prints usage and `help_exit=0`; no-args still prints the "AGORA_APP_ID is required" usage with `noargs_exit=2`; tests show `Status: SUCCESS!` (15 cases).

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/main.cpp
git commit -m "feat: tag-stamped --version flag"
```

---

## Task 2: `package.sh` architecture label argument

**Files:**
- Modify: `scripts/package.sh`

- [ ] **Step 1: Read the current script**

Run: `cat scripts/package.sh`. Note every place the literal `agora-voice-client-macos-arm64` appears (the staging dir `OUT` and the `tar` archive name).

- [ ] **Step 2: Parameterize the architecture label**

Edit `scripts/package.sh`:
1. Near the top (after `ROOT=...`), introduce the label variable:
   ```bash
   LABEL="${1:-arm64}"
   ```
2. Change the staging dir definition to use it:
   ```bash
   OUT="$ROOT/dist/agora-voice-client-macos-$LABEL"
   ```
3. Change the final `tar` + echo to use the label:
   ```bash
   ( cd "$ROOT/dist" && tar -czf "agora-voice-client-macos-$LABEL.tar.gz" "agora-voice-client-macos-$LABEL" )
   echo "Wrote $ROOT/dist/agora-voice-client-macos-$LABEL.tar.gz"
   ```
Leave everything else (the framework copy loop, `install_name_tool -add_rpath @loader_path`, the per-framework re-sign loop, the binary re-sign) exactly as-is.

- [ ] **Step 3: Verify default label still works**

Run:
```bash
cmake --build build && ./scripts/package.sh
test -f dist/agora-voice-client-macos-arm64.tar.gz && echo "DEFAULT_OK"
```
Expected: `DEFAULT_OK` (default arg → `arm64`, same as before).

- [ ] **Step 4: Verify a custom label**

Run:
```bash
./scripts/package.sh universal
test -d dist/agora-voice-client-macos-universal && test -f dist/agora-voice-client-macos-universal.tar.gz && echo "LABEL_OK"
( cd dist/agora-voice-client-macos-universal && ./agora-voice-client --version )
```
Expected: `LABEL_OK`, and the packaged binary prints its version (proving the relocatable copy still runs). NOTE: at this point `build/` is still arm64-only, so the `universal` label is just a name here — the real universal build is verified in Task 5.

- [ ] **Step 5: Commit**

```bash
git add scripts/package.sh
git commit -m "build: package.sh accepts an architecture label argument"
```

---

## Task 3: Release notes template

**Files:**
- Create: `release-notes.md`

- [ ] **Step 1: Create `release-notes.md`** (repo root)

````markdown
# agora-voice-client (macOS)

Universal build — Apple Silicon **and** Intel. Ad-hoc signed, **not notarized**.

## Install & run

```bash
tar -xzf agora-voice-client-macos-universal.tar.gz
# Clear the Gatekeeper quarantine once (required for un-notarized downloads):
xattr -dr com.apple.quarantine agora-voice-client-macos-universal
cd agora-voice-client-macos-universal

export AGORA_APP_ID=<your app id>
export AGORA_APP_CERTIFICATE=<your app certificate>
./agora-voice-client --channel <room> --uid <N>
```

- **First run must be at the Mac** (a graphical login session) so you can approve the
  microphone prompt. SSH/headless runs work after that one approval.
- `--uid <N>` must match the uid the AI agent subscribes to. `--uid 0` is solo testing only.
- `./agora-voice-client --version` prints this build's version.

See the repository README for full usage and the manual integration checklist.
````

- [ ] **Step 2: Commit**

```bash
git add release-notes.md
git commit -m "docs: GitHub Release notes template (gatekeeper bypass + mic)"
```

---

## Task 4: Release workflow

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: Create `.github/workflows/release.yml`**

```yaml
name: release

on:
  push:
    tags:
      - 'v*'

permissions:
  contents: write

jobs:
  macos:
    runs-on: macos-15
    steps:
      - uses: actions/checkout@v4

      - name: Fetch Agora SDK
        run: ./scripts/fetch-sdk.sh

      - name: Configure (universal, version-stamped)
        run: >
          cmake -S . -B build
          -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
          -DAVC_VERSION="${GITHUB_REF_NAME}"

      - name: Build
        run: cmake --build build

      - name: Test (gate)
        run: ./build/tests

      - name: Package
        run: ./scripts/package.sh universal

      - name: Verify universal binary
        run: |
          archs=$(lipo -archs dist/agora-voice-client-macos-universal/agora-voice-client)
          echo "archs: $archs"
          echo "$archs" | grep -q x86_64
          echo "$archs" | grep -q arm64

      - name: Publish release (create or update asset)
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release create "$GITHUB_REF_NAME" dist/*.tar.gz \
            --title "$GITHUB_REF_NAME" --notes-file release-notes.md \
          || gh release upload "$GITHUB_REF_NAME" dist/*.tar.gz --clobber
```

- [ ] **Step 2: Validate the YAML syntax locally**

Run:
```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml')); print('YAML OK')"
```
Expected: `YAML OK`. (If `pyyaml` is unavailable, `python3 -c "import yaml"` will error — then instead just re-read the file and confirm indentation by eye; the structure must match exactly the block above.)

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: tag-triggered macOS universal release workflow"
```

---

## Task 5: Local pre-flight + quarantine acceptance test (verification gate)

This task runs the exact universal pipeline locally so CI is not the first attempt, and proves the documented Gatekeeper bypass works. No code changes; no commit.

**Files:** none (verification only).

- [ ] **Step 1: Clean universal build + test gate + package**

Run:
```bash
rm -rf build dist
test -d third_party/agora/AgoraRtcKit.framework || ./scripts/fetch-sdk.sh
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DAVC_VERSION="v0.0.0-preflight"
cmake --build build
./build/tests
./scripts/package.sh universal
```
Expected: build succeeds; `./build/tests` → `Status: SUCCESS!`; package writes `dist/agora-voice-client-macos-universal.tar.gz`.

- [ ] **Step 2: Assert the binary is genuinely universal and version-stamped**

Run:
```bash
lipo -archs dist/agora-voice-client-macos-universal/agora-voice-client
./dist/agora-voice-client-macos-universal/agora-voice-client --version
```
Expected: `lipo` prints `x86_64 arm64` (order may vary, both present); `--version` prints `agora-voice-client v0.0.0-preflight`.

- [ ] **Step 3: Quarantine-bypass acceptance test**

Simulate a downloaded (quarantined) copy, confirm the bypass clears it:
```bash
rm -rf /tmp/avc-q && cp -R dist/agora-voice-client-macos-universal /tmp/avc-q
xattr -w com.apple.quarantine "0081;00000000;manual;" /tmp/avc-q/agora-voice-client
echo "quarantine set:"; xattr -p com.apple.quarantine /tmp/avc-q/agora-voice-client
xattr -dr com.apple.quarantine /tmp/avc-q
( cd /tmp/avc-q && ./agora-voice-client --version ) && echo "RUNS_AFTER_CLEAR"
rm -rf /tmp/avc-q
```
Expected: the quarantine attribute prints, then after `xattr -dr` the binary runs and prints its version followed by `RUNS_AFTER_CLEAR`. This confirms clearing quarantine is sufficient for the ad-hoc + hardened-runtime binary (ADR-0002). (Note: whether the *pre-clear* run is blocked depends on the macOS version and GUI session; the load-bearing assertion is that it RUNS after clearing.)

- [ ] **Step 4: Record the real-release procedure (no action, just confirm understanding)**

The workflow only fires when `.github/workflows/release.yml` exists on the tagged commit. So to cut the first real release:
```bash
# After this branch is merged to master (or on a commit that includes the workflow):
git tag v0.1.0
git push origin v0.1.0
# Then watch: Actions tab → "release" run → confirm the GitHub Release has
# agora-voice-client-macos-universal.tar.gz attached.
```
This end-to-end CI check is the final manual verification and happens at actual release time.

---

## Self-Review

**Spec coverage:**
- Tag `v*` trigger + GitHub Release asset → Task 4. ✓
- Universal (arm64+x86_64) build → Task 4 (configure) + Task 5 (verify). ✓
- Test gate → Task 4 (`Test (gate)` step). ✓
- `package.sh` arch label → Task 2. ✓
- `lipo` both-slices assertion → Task 4 (Verify step) + Task 5. ✓
- `gh` CLI + `contents: write` + create-or-clobber idempotency → Task 4. ✓
- Pinned `macos-15` → Task 4. ✓
- `--version` tag-stamped, handled before config → Task 1. ✓
- Gatekeeper bypass documented (`release-notes.md`) → Task 3. ✓
- Quarantine-bypass acceptance test → Task 5. ✓
- Local pre-flight → Task 5. ✓
- Tagged-commit caveat → Task 5 Step 4. ✓
- Out of scope (notarization, Windows/Linux, per-commit CI) → not present. ✓

**Placeholder scan:** No TBD/TODO; every file's full content or exact edit is given. The only "read the existing file" instruction (Task 2 Step 1) is paired with the precise substitutions to make.

**Type/name consistency:** `AVC_VERSION` (CMake cache var) → `AVC_VERSION_STR` (compile define, used in main.cpp with `#ifndef` fallback) — consistent across Task 1 steps. Archive name `agora-voice-client-macos-universal.tar.gz` consistent across Tasks 2–5 and `release-notes.md`. `$GITHUB_REF_NAME` used consistently in the workflow. `--version` output format identical in Task 1 and verified in Tasks 4/5.
