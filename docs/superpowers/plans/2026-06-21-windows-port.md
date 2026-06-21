# Windows Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and run `agora-voice-client` on Windows x64 — a new C++ `IRtcEngine` engine, BCrypt HMAC, cross-platform CMake, OS-aware SDK fetch, push-CI matrix, and a Windows release artifact — reusing the portable core behind the `VoiceEngine` interface.

**Architecture:** Split platform-specific bits (HMAC, engine, a stdout-mode tweak) behind existing interfaces; guard the build by platform in CMake. macOS stays green and is verified locally; the Windows engine is pure C++ `IRtcEngine` (syntax-checked locally against the bundled headers); BCrypt + linking + the real Windows build are verified on `windows-latest` CI (the only Windows build loop).

**Tech Stack:** C++17, CMake (FetchContent), GitHub Actions (matrix), MSVC, CNG BCrypt, Agora C++ `IRtcEngine` SDK, doctest.

**Spec:** `docs/superpowers/specs/2026-06-21-windows-port-design.md`. Decisions: ADRs 0001–0006.

**CRITICAL execution note:** This machine (macOS) cannot compile/run Windows. Tasks 1–4 are locally verifiable (macOS build/tests + a local `-fsyntax-only` check of the Windows engine). Tasks 5–6 are verified by **pushing the branch and watching the CI run** — expect a fix-forward loop on the first Windows build (blind code). The branch must be pushed for CI tasks.

**Reference for the Windows engine API:** the macOS framework we already fetched bundles the **same cross-platform C++ headers** at `third_party/agora/AgoraRtcKit.framework/Headers/` (`IAgoraRtcEngine.h`, `AgoraBase.h`). Read them for exact signatures/enums.

---

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `src/hmac_apple.cpp` | Create (move) | CommonCrypto HMAC-SHA256 (macOS) |
| `src/hmac_windows.cpp` | Create | BCrypt HMAC-SHA256 (Windows) |
| `src/token.cpp` | Modify | remove hmac impl + CommonCrypto include (keeps `build_rtc_token`) |
| `src/voice_engine_agora_win.cpp` | Create | Windows engine over C++ `IRtcEngine` |
| `src/main.cpp` | Modify | `#ifdef _WIN32` set stdout binary mode |
| `scripts/fetch-sdk.sh` | Modify | OS-aware; Windows downloads + normalizes to `include/ lib/ bin/` |
| `scripts/package-windows.ps1` | Create | zip exe + DLLs |
| `release-notes.md` | Modify | cover macOS + Windows |
| `CMakeLists.txt` | Modify | platform hmac/engine selection, zlib (system/FetchContent), WIN32 link/copy |
| `.github/workflows/ci.yml` | Create | push/PR build+test matrix (macos-15 + windows-latest), SDK cache |
| `.github/workflows/release.yml` | Modify | add `windows` job → `agora-voice-client-windows-x64.zip` |
| `README.md` | Modify | Windows usage section |

---

## Task 1: Split HMAC into platform files

**Files:** Create `src/hmac_apple.cpp`, `src/hmac_windows.cpp`; modify `src/token.cpp`, `CMakeLists.txt`.

- [ ] **Step 1: Create `src/hmac_apple.cpp`** (the existing CommonCrypto impl, moved verbatim)

```cpp
#include "token.h"
#include <CommonCrypto/CommonHMAC.h>

namespace avc {
std::vector<unsigned char> hmac_sha256(const std::vector<unsigned char>& key,
                                       const std::vector<unsigned char>& message) {
  std::vector<unsigned char> out(CC_SHA256_DIGEST_LENGTH);
  CCHmac(kCCHmacAlgSHA256,
         key.empty() ? nullptr : key.data(), key.size(),
         message.empty() ? nullptr : message.data(), message.size(),
         out.data());
  return out;
}
}  // namespace avc
```

- [ ] **Step 2: Create `src/hmac_windows.cpp`** (BCrypt; will only compile on Windows/CI)

```cpp
#include "token.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdexcept>
#include <string>

namespace avc {
namespace {
void check(NTSTATUS s, const char* what) {
  if (s != 0) throw std::runtime_error(std::string("BCrypt ") + what + " failed");
}
}  // namespace

std::vector<unsigned char> hmac_sha256(const std::vector<unsigned char>& key,
                                       const std::vector<unsigned char>& message) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  check(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG), "open");
  BCRYPT_HASH_HANDLE hash = nullptr;
  unsigned char dummy = 0;
  NTSTATUS s = BCryptCreateHash(
      alg, &hash, nullptr, 0,
      key.empty() ? &dummy : const_cast<PUCHAR>(key.data()),
      static_cast<ULONG>(key.size()), 0);
  if (s != 0) { BCryptCloseAlgorithmProvider(alg, 0); check(s, "createhash"); }
  if (!message.empty()) {
    s = BCryptHashData(hash, const_cast<PUCHAR>(message.data()),
                       static_cast<ULONG>(message.size()), 0);
    if (s != 0) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
                  check(s, "hashdata"); }
  }
  std::vector<unsigned char> out(32);
  s = BCryptFinishHash(hash, out.data(), static_cast<ULONG>(out.size()), 0);
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(alg, 0);
  check(s, "finishhash");
  return out;
}
}  // namespace avc
```

- [ ] **Step 3: Remove the HMAC impl from `src/token.cpp`**

Open `src/token.cpp`. Delete the `#include <CommonCrypto/CommonHMAC.h>` line and the entire `hmac_sha256(...)` function definition (it now lives in `hmac_apple.cpp`). Leave `build_rtc_token` and everything else untouched — it calls `avc::hmac_sha256` indirectly via the vendored `utils.h`, which is satisfied at link time by the platform hmac file.

- [ ] **Step 4: Select the platform hmac source in `CMakeLists.txt`**

Near the top (after the `set(CMAKE_CXX_…)` lines), add:
```cmake
if(APPLE)
  set(AVC_HMAC_SRC src/hmac_apple.cpp)
elseif(WIN32)
  set(AVC_HMAC_SRC src/hmac_windows.cpp)
endif()
```
Then add `${AVC_HMAC_SRC}` to the `tests` target source list AND the `agora-voice-client` target source list (both currently compile `src/token.cpp`; add the hmac source right after it in each).

- [ ] **Step 5: Build + test locally (macOS)**

Run:
```bash
test -d third_party/agora/AgoraRtcKit.framework || ./scripts/fetch-sdk.sh
cmake -S . -B build && cmake --build build && ./build/tests
```
Expected: builds; `./build/tests` → `Status: SUCCESS!` (22 cases). The HMAC/RFC-4231 test still passes — proving the macOS hmac still works after the move.

- [ ] **Step 6: Commit**

```bash
git add src/hmac_apple.cpp src/hmac_windows.cpp src/token.cpp CMakeLists.txt
git commit -m "refactor: split hmac_sha256 into platform files (CommonCrypto + BCrypt)"
```

---

## Task 2: OS-aware SDK fetch

**Files:** Modify `scripts/fetch-sdk.sh`.

- [ ] **Step 1: Read the current script** — `cat scripts/fetch-sdk.sh`. Note the macOS download + framework extraction and the pinned `SDK_VERSION`.

- [ ] **Step 2: Make it OS-aware**

Edit `scripts/fetch-sdk.sh` so it branches on OS. Keep the existing macOS path. Add a Windows path that downloads the Windows FULL zip and normalizes artifacts into `third_party/agora/{include,lib,bin}/`. Structure (adapt to the existing variable names in the file):

```bash
OS="$(uname -s)"
case "$OS" in
  Darwin)
    SDK_URL="https://download.agora.io/sdk/release/Agora_Native_SDK_for_Mac_v${SDK_VERSION}_FULL.zip"
    SDK_SHA256="540a46d3b301232275b4fa4ed38782680797789d042b4f8cda43bdcd2b7da027"
    # ... existing macOS extraction (frameworks) unchanged ...
    ;;
  MINGW*|MSYS*|CYGWIN*)
    SDK_URL="https://download.agora.io/sdk/release/Agora_Native_SDK_for_Windows_v${SDK_VERSION}_FULL.zip"
    SDK_SHA256="PUT_WINDOWS_SHA256_HERE"   # first CI run prints it; pin it then
    echo "Downloading Windows SDK $SDK_VERSION ..."
    curl -fSL -o "$TMP/sdk.zip" "$SDK_URL"
    if [ "$SDK_SHA256" != "PUT_WINDOWS_SHA256_HERE" ]; then
      echo "$SDK_SHA256  $TMP/sdk.zip" | sha256sum -c -
    else
      echo "WARNING: Windows checksum not pinned. Computed:"; sha256sum "$TMP/sdk.zip"
    fi
    unzip -q "$TMP/sdk.zip" -d "$TMP/x"
    DEST="$ROOT/third_party/agora"
    rm -rf "$DEST"; mkdir -p "$DEST/include" "$DEST/lib" "$DEST/bin"
    # Normalize: find the C++ headers, the import lib, and the runtime DLLs (x64).
    inc="$(dirname "$(find "$TMP/x" -name 'IAgoraRtcEngine.h' | head -n1)")"
    cp -R "$inc"/. "$DEST/include/"
    find "$TMP/x" -path '*x86_64*' -name '*.lib' -exec cp {} "$DEST/lib/" \;
    find "$TMP/x" -path '*x86_64*' -name '*.dll' -exec cp {} "$DEST/bin/" \;
    echo "Windows SDK normalized into $DEST/{include,lib,bin}:"; ls "$DEST/lib" "$DEST/bin"
    ;;
esac
```
Notes: the Windows zip's internal folder names (`x86_64` vs `x64`, `sdk/...`) vary — the `find` patterns above are robust to nesting; if the first CI run finds nothing, widen the `-path` glob (drop `x86_64`) and re-pin. `sha256sum` exists in the runner's git-bash. The macOS branch keeps using `shasum -a 256`.

- [ ] **Step 3: Verify the macOS path still works locally**

```bash
./scripts/fetch-sdk.sh && ls third_party/agora/AgoraRtcKit.framework >/dev/null && echo "MAC_FETCH_OK"
```
Expected: `MAC_FETCH_OK` (macOS unaffected). The Windows branch is exercised on CI in Task 5.

- [ ] **Step 4: Commit**

```bash
git add scripts/fetch-sdk.sh
git commit -m "build: OS-aware fetch-sdk (Windows SDK download + normalize)"
```

---

## Task 3: Windows engine (`voice_engine_agora_win.cpp`) — local syntax-check

**Files:** Create `src/voice_engine_agora_win.cpp`.

- [ ] **Step 1: Read the bundled C++ headers for exact symbols**

```bash
H=third_party/agora/AgoraRtcKit.framework/Headers
grep -n "onJoinChannelSuccess\|onUserJoined\|onUserOffline\|onConnectionStateChanged\|onTokenPrivilegeWillExpire\|onError" "$H/AgoraBase.h" | head
grep -n "joinChannel(\|renewToken\|setClientRole\|setChannelProfile\|setAudioProfile\|enableAudio\|virtual int initialize\|release(" "$H/IAgoraRtcEngine.h" | head -30
grep -n "struct ChannelMediaOptions\|publishMicrophoneTrack\|autoSubscribeAudio\|clientRoleType\|channelProfile" "$H/IAgoraRtcEngine.h" | head
grep -n "CONNECTION_STATE_RECONNECTING\|CONNECTION_STATE_CONNECTED\|CONNECTION_STATE_FAILED\|CLIENT_ROLE_BROADCASTER\|CHANNEL_PROFILE_LIVE_BROADCASTING\|AUDIO_PROFILE_SPEECH_STANDARD\|AUDIO_SCENARIO_MEETING\|typedef.*uid_t" "$H/AgoraBase.h" | head -20
```
Use the exact names you find; the template below matches Agora 4.x but adjust to the headers.

- [ ] **Step 2: Create `src/voice_engine_agora_win.cpp`**

```cpp
#include "IAgoraRtcEngine.h"
#include "voice_engine.h"

using namespace agora;
using namespace agora::rtc;

namespace {

class AvcHandler : public IRtcEngineEventHandler {
 public:
  avc::VoiceEngineCallbacks cb;
  bool reconnecting = false;

  void onJoinChannelSuccess(const char* channel, uid_t uid, int elapsed) override {
    if (cb.on_joined) cb.on_joined();
  }
  void onUserJoined(uid_t uid, int elapsed) override {
    if (cb.on_user_joined) cb.on_user_joined(static_cast<std::uint32_t>(uid));
  }
  void onUserOffline(uid_t uid, USER_OFFLINE_REASON_TYPE reason) override {
    if (cb.on_user_left) cb.on_user_left(static_cast<std::uint32_t>(uid));
  }
  void onConnectionStateChanged(CONNECTION_STATE_TYPE state,
                                CONNECTION_CHANGED_REASON_TYPE reason) override {
    if (state == CONNECTION_STATE_RECONNECTING) {
      reconnecting = true;
      if (cb.on_reconnecting) cb.on_reconnecting();
    } else if (state == CONNECTION_STATE_CONNECTED) {
      if (reconnecting) { reconnecting = false; if (cb.on_reconnected) cb.on_reconnected(); }
    } else if (state == CONNECTION_STATE_FAILED) {
      if (cb.on_fatal) cb.on_fatal(static_cast<int>(reason));
    }
  }
  void onTokenPrivilegeWillExpire(const char* token) override {
    if (cb.on_token_will_expire) cb.on_token_will_expire();
  }
  void onError(int err, const char* msg) override {
    if (cb.on_error) cb.on_error(err);
  }
};

class AgoraWinVoiceEngine : public avc::VoiceEngine {
 public:
  ~AgoraWinVoiceEngine() override { stop(); }

  bool start(const avc::ClientConfig& cfg, const std::string& token,
             const avc::VoiceEngineCallbacks& cb) override {
    if (engine_) return false;
    handler_.cb = cb;
    engine_ = createAgoraRtcEngine();
    if (!engine_) return false;

    RtcEngineContext ctx;
    ctx.appId = cfg.app_id.c_str();
    ctx.eventHandler = &handler_;
    ctx.channelProfile = CHANNEL_PROFILE_LIVE_BROADCASTING;
    ctx.audioScenario = AUDIO_SCENARIO_MEETING;   // keeps AEC on (ADR-0004)
    if (engine_->initialize(ctx) != 0) return false;

    engine_->setChannelProfile(CHANNEL_PROFILE_LIVE_BROADCASTING);
    engine_->setClientRole(CLIENT_ROLE_BROADCASTER);
    engine_->setAudioProfile(AUDIO_PROFILE_SPEECH_STANDARD);
    engine_->enableAudio();

    ChannelMediaOptions options;
    options.channelProfile = CHANNEL_PROFILE_LIVE_BROADCASTING;
    options.clientRoleType = CLIENT_ROLE_BROADCASTER;
    options.publishMicrophoneTrack = true;
    options.autoSubscribeAudio = true;

    int rc = engine_->joinChannel(token.empty() ? nullptr : token.c_str(),
                                  cfg.channel.c_str(), cfg.uid, options);
    return rc == 0;
  }

  void renew_token(const std::string& token) override {
    if (engine_) engine_->renewToken(token.c_str());
  }

  void stop() override {
    if (engine_) {
      engine_->leaveChannel();
      engine_->release(true);  // sync teardown; drains callbacks
      engine_ = nullptr;
    }
  }

 private:
  IRtcEngine* engine_ = nullptr;
  AvcHandler handler_;
};

}  // namespace

namespace avc {
std::unique_ptr<VoiceEngine> make_voice_engine() {
  return std::make_unique<AgoraWinVoiceEngine>();
}
}  // namespace avc
```

- [ ] **Step 3: Syntax-check locally against the bundled C++ headers**

Run:
```bash
clang++ -std=c++17 -fsyntax-only \
  -I third_party/agora/AgoraRtcKit.framework/Headers -I src \
  src/voice_engine_agora_win.cpp && echo "SYNTAX OK"
```
Expected: `SYNTAX OK`. Fix any symbol/enum/signature error by matching the real header declarations (Step 1) until it parses. This validates the engine's C++ before any Windows build. (It will NOT link here — that's CI. `ChannelMediaOptions` fields are `Optional<T>` in 4.x; assigning `true`/enums works via the implicit `Optional` constructor — if syntax-check complains, wrap as `options.publishMicrophoneTrack = true;` is correct, but confirm the field types in the header.)

- [ ] **Step 4: Confirm macOS build is unaffected**

`cmake --build build && ./build/tests` → still SUCCESS. (`voice_engine_agora_win.cpp` is not in the macOS target, so it isn't compiled here — only syntax-checked.)

- [ ] **Step 5: Commit**

```bash
git add src/voice_engine_agora_win.cpp
git commit -m "feat: Windows voice engine over C++ IRtcEngine (syntax-checked)"
```

---

## Task 4: Cross-platform CMake + Windows stdout binary mode

**Files:** Modify `CMakeLists.txt`, `src/main.cpp`.

- [ ] **Step 1: Add Windows stdout binary mode to `src/main.cpp`**

At the top of `src/main.cpp`, after the existing includes, add:
```cpp
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
```
As the very first statement inside `main` (before the arg loop), add:
```cpp
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);  // keep JSONL lines LF-only on Windows
#endif
```

- [ ] **Step 2: Make `CMakeLists.txt` cross-platform**

Replace the zlib line and the app-target section. (a) zlib provisioning:
```cmake
if(WIN32)
  include(FetchContent)
  FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1)
  FetchContent_MakeAvailable(zlib)
  set(AVC_ZLIB zlibstatic)          # confirm target name on first CI run
else()
  find_package(ZLIB REQUIRED)
  set(AVC_ZLIB ZLIB::ZLIB)
endif()
```
Change both targets' `target_link_libraries(... ZLIB::ZLIB)` to `... ${AVC_ZLIB}`. On Windows the `zlibstatic` target's include dir is propagated by FetchContent; if `<zlib.h>` isn't found, add `target_include_directories(<target> PRIVATE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})`.

(b) Guard the app target by platform. Wrap the entire existing `agora-voice-client` block in `if(APPLE) … endif()` and add a WIN32 branch:
```cmake
set(AVC_VERSION "dev" CACHE STRING "Version string stamped into the binary")

if(APPLE)
  # ---- existing macOS app target, unchanged, but source list gains ${AVC_HMAC_SRC} ----
  add_executable(agora-voice-client
    src/main.cpp src/config.cpp src/token.cpp src/event_reporter.cpp
    ${AVC_HMAC_SRC} src/voice_engine_agora.mm)
  # ... keep the existing include dirs, -fobjc-arc -F, sectcreate Info.plist,
  #     frameworks, ${AVC_ZLIB}, BUILD_RPATH/INSTALL_RPATH, codesign POST_BUILD ...
  target_compile_definitions(agora-voice-client PRIVATE AVC_VERSION_STR="${AVC_VERSION}")

elseif(WIN32)
  add_executable(agora-voice-client
    src/main.cpp src/config.cpp src/token.cpp src/event_reporter.cpp
    ${AVC_HMAC_SRC} src/voice_engine_agora_win.cpp)
  target_include_directories(agora-voice-client PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/agora-token
    ${CMAKE_SOURCE_DIR}/third_party/agora/include)
  target_compile_definitions(agora-voice-client PRIVATE AVC_VERSION_STR="${AVC_VERSION}")
  file(GLOB AGORA_WIN_LIBS ${CMAKE_SOURCE_DIR}/third_party/agora/lib/*.lib)
  target_link_libraries(agora-voice-client PRIVATE ${AVC_ZLIB} bcrypt ${AGORA_WIN_LIBS})
  # Copy the Agora DLLs next to the .exe so it runs from its own dir.
  add_custom_command(TARGET agora-voice-client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/third_party/agora/bin $<TARGET_FILE_DIR:agora-voice-client>)
endif()
```
Use `AGORA_WIN_LIBS` (note name) consistently. If linking reports the import lib isn't `AgoraRtcKit.lib`, the `file(GLOB …/*.lib)` picks up whatever the SDK ships — adjust if multiple libs conflict.

- [ ] **Step 3: Verify macOS still builds + tests (the refactor must not regress mac)**

```bash
rm -rf build && cmake -S . -B build && cmake --build build && ./build/tests
./build/agora-voice-client --version
```
Expected: SUCCESS (22 cases); `--version` prints `agora-voice-client dev`. (The `_setmode` block is `#ifdef _WIN32`, inert on macOS.)

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/main.cpp
git commit -m "build: cross-platform CMake (zlib FetchContent, WIN32 target) + stdout binary on Windows"
```

---

## Task 5: Push CI matrix (the Windows build loop)

**Files:** Create `.github/workflows/ci.yml`. This task is where all blind Windows code gets really verified. Expect iteration.

- [ ] **Step 1: Create `.github/workflows/ci.yml`**

```yaml
name: ci

on:
  push:
  pull_request:

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [macos-15, windows-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4

      - name: Cache Agora SDK
        uses: actions/cache@v4
        with:
          path: third_party/agora
          key: ${{ runner.os }}-agora-4.4.0

      - name: Fetch Agora SDK
        shell: bash
        run: ./scripts/fetch-sdk.sh

      - name: Configure
        run: cmake -S . -B build

      - name: Build
        run: cmake --build build

      - name: Test
        shell: bash
        run: |
          if [ -f build/tests ]; then ./build/tests; else ./build/Debug/tests.exe; fi
```
Notes: `shell: bash` is available on `windows-latest` (git-bash). The test binary path differs (MSVC multi-config puts it under `build/Debug/`); the conditional handles both. If the Windows configure needs a generator/arch, add `-A x64` to the configure step.

- [ ] **Step 2: Push and watch the run; fix-forward to green**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: push/PR build+test matrix (macOS + Windows)"
# push the branch (set <branch> to the current feature branch)
git push -u origin "$(git branch --show-current)"
gh run watch "$(gh run list --workflow=ci.yml -L1 --json databaseId -q '.[0].databaseId')" --exit-status
```
Expected: **macOS leg green immediately**; **Windows leg likely red on the first run** — iterate. Read failures with `gh run view --log-failed`. Likely Windows fixes, in order:
  1. **SDK fetch**: the `find` globs in `fetch-sdk.sh` didn't match the zip layout → widen them; pin the Windows sha256 (printed in the log).
  2. **zlib target**: `zlibstatic` wrong name → check FetchContent output (could be `zlib`); fix `AVC_ZLIB`. Add zlib include dirs if `<zlib.h>` not found.
  3. **Agora headers/lib**: include path or `.lib` name mismatch → adjust `third_party/agora/include` / the `file(GLOB)`.
  4. **Engine API**: an enum/signature differs in the Windows headers vs the macOS-bundled ones → fix per the compiler error.
  5. **Generator/arch**: add `-A x64` to configure if MSVC defaults wrong; test path `build/Debug/tests.exe`.
Commit each fix and push; re-watch until **both legs are green**. The Windows `tests.exe` passing proves BCrypt HMAC (RFC 4231), config, and event_reporter all work on Windows.

- [ ] **Step 3: Confirm both legs green**

`gh run list --workflow=ci.yml -L1` shows the latest run **success**. Done when macOS + Windows both pass.

---

## Task 6: Windows release artifact

**Files:** Create `scripts/package-windows.ps1`; modify `release-notes.md`, `.github/workflows/release.yml`.

- [ ] **Step 1: Create `scripts/package-windows.ps1`**

```powershell
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root "build\Release\agora-voice-client.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $root "build\agora-voice-client.exe" }
$out  = Join-Path $root "dist\agora-voice-client-windows-x64"
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item $exe $out
Copy-Item (Join-Path $root "third_party\agora\bin\*.dll") $out
$zip = Join-Path $root "dist\agora-voice-client-windows-x64.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $out -DestinationPath $zip
Write-Host "Wrote $zip"
```

- [ ] **Step 2: Broaden `release-notes.md` to cover Windows**

Append to `release-notes.md` (keep the macOS section):
````markdown

## Windows (x64)

Unzip `agora-voice-client-windows-x64.zip` and run `agora-voice-client.exe`. The Agora DLLs
ship beside the exe. Unsigned, so SmartScreen may warn → **More info → Run anyway**.

```bat
set AGORA_APP_ID=<your app id>
set AGORA_APP_CERTIFICATE=<your app certificate>
agora-voice-client.exe --channel <room> --uid <N>
```
Enable **Settings → Privacy & security → Microphone → "Let desktop apps access your
microphone"**. Close with **Ctrl-C** (not the window) for a clean leave. `--json` works the
same as macOS.
````

- [ ] **Step 3: Add a `windows` job to `.github/workflows/release.yml`**

Add this job alongside the existing `macos` job (same `permissions: contents: write` at top already applies):
```yaml
  windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Fetch Agora SDK
        shell: bash
        run: ./scripts/fetch-sdk.sh
      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAVC_VERSION="${GITHUB_REF_NAME}"
        shell: bash
      - name: Build
        run: cmake --build build --config Release
      - name: Test (gate)
        shell: bash
        run: ./build/Release/tests.exe
      - name: Package
        shell: pwsh
        run: ./scripts/package-windows.ps1
      - name: Publish release (create or update asset)
        shell: bash
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh release create "$GITHUB_REF_NAME" dist/*.zip \
            --title "$GITHUB_REF_NAME" --notes-file release-notes.md \
          || gh release upload "$GITHUB_REF_NAME" dist/*.zip --clobber
```
(The `macos` job's existing publish step uses the same create-or-clobber pattern; both racing is fine.)

- [ ] **Step 4: Commit + push; verify via CI**

```bash
git add scripts/package-windows.ps1 release-notes.md .github/workflows/release.yml
git commit -m "release: Windows x64 artifact job + notes"
git push
gh run watch "$(gh run list --workflow=ci.yml -L1 --json databaseId -q '.[0].databaseId')" --exit-status
```
Expected: ci.yml still green (the release.yml change doesn't affect ci.yml, but confirm nothing else broke). The release job itself is exercised when a `v*` tag is pushed (after merge), at actual release time — note that as the final manual check.

---

## Task 7: README Windows section

**Files:** Modify `README.md`.

- [ ] **Step 1: Add a "Windows" subsection** after the macOS build/run content:

````markdown
## Windows (x64)

Prebuilt: download `agora-voice-client-windows-x64.zip` from the latest release, unzip,
and run `agora-voice-client.exe` (Agora DLLs ship beside it). SmartScreen may warn →
**More info → Run anyway**.

Build from source (needs Visual Studio 2022 + CMake):
```bat
bash scripts/fetch-sdk.sh
cmake -S . -B build -A x64
cmake --build build --config Release
build\Release\tests.exe
```

Run:
```bat
set AGORA_APP_ID=<app id>
set AGORA_APP_CERTIFICATE=<certificate>
agora-voice-client.exe --channel <room> --uid <N>
```
Enable **Settings → Privacy & security → Microphone → "Let desktop apps access your
microphone"**. Close with **Ctrl-C**, not the window (the window close can't leave the
channel cleanly and may leave a brief ghost participant). `--json` and `--version` behave
exactly as on macOS.
````

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: Windows build/run/usage section"
```

---

## Self-Review

**Spec coverage:**
- hmac split (CommonCrypto + BCrypt), CMake selects per platform → Task 1. ✓
- OS-aware SDK fetch (Windows package, normalized layout) → Task 2. ✓
- Windows engine over C++ `IRtcEngine` + local syntax-check → Task 3. ✓
- Cross-platform CMake guards, FetchContent zlib, DLL copy → Task 4. ✓
- stdout binary mode on Windows (JSONL LF) → Task 4. ✓
- Push CI matrix (macos-15 + windows-latest) + SDK cache → Task 5. ✓
- Release Windows job + create-or-clobber + broadened notes → Task 6. ✓
- Packaging (`package-windows.ps1`, DLLs beside exe, no signing) → Task 6. ✓
- README Windows usage (mic privacy, SmartScreen, Ctrl-C) → Task 7. ✓
- Verification: local mac build/tests (1,2,4), local engine syntax-check (3), CI both legs (5), live audio manual (README). ✓
- macOS behavior unchanged beyond hmac move + CMake guards → Tasks 1,4 verify mac green. ✓

**Placeholder scan:** No vague TODOs. The deliberately-deferred values are the Windows SDK sha256 (printed on first CI run, then pinned) and the exact SDK lib/zlib-target/enum names — each paired with the precise command to discover and the fix-forward checklist in Task 5. These are genuine external unknowns under a CI-only build, not hand-waving.

**Type/name consistency:** `avc::hmac_sha256` signature identical in `hmac_apple.cpp`/`hmac_windows.cpp` and the `token.h` declaration. `VoiceEngineCallbacks` members used in `voice_engine_agora_win.cpp` match `voice_engine.h` (on_joined/on_user_joined/on_user_left/on_reconnecting/on_reconnected/on_error/on_token_will_expire/on_fatal). `make_voice_engine()` defined once per platform impl, same signature. `AVC_VERSION`→`AVC_VERSION_STR` consistent with the existing macOS flow. `AVC_HMAC_SRC`/`AVC_ZLIB`/`AGORA_WIN_LIBS` CMake vars used consistently.
