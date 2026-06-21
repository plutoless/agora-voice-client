# Windows Port — Design Spec

**Date:** 2026-06-21
**Status:** Approved design, ready for implementation planning

## Purpose

Make `agora-voice-client` build and run on Windows (x64), reusing the existing portable
core (`config`, `token`, `event_reporter`, `main`) behind the `VoiceEngine` interface. The
macOS engine is Objective-C++ over `AgoraRtcEngineKit` (ADR-0005); Windows gets a new C++
implementation over the `IRtcEngine` API, plus a BCrypt-backed HMAC and cross-platform
build/CI/packaging.

## Guiding constraint

The development machine is macOS and cannot compile or run Windows binaries (Agora's
Windows SDK is MSVC-built). Therefore **GitHub Actions `windows-latest` is the only build
and test loop**. The portable unit tests (`config`, `token`, `event_reporter`) run on the
Windows runner and are the automated proof that the shared core + BCrypt HMAC work on
Windows. The Windows engine compiles and links on CI but **live microphone/audio can only
be verified on a real Windows machine** (manual, by the Operator). This is why the port and
its CI are delivered together (CI is how we compile it).

## Scope

In scope:
- A cross-platform refactor so the shared code builds on both macOS and Windows.
- A new Windows engine implementation (`voice_engine_agora_win.cpp`) over C++ `IRtcEngine`.
- BCrypt (CNG) HMAC for token minting on Windows.
- OS-aware SDK fetch (Windows FULL package).
- A push/PR CI workflow (macOS + Windows matrix) — the dev loop.
- A Windows job in the release workflow producing `agora-voice-client-windows-x64.zip`.
- Windows packaging + usage docs.

Out of scope (YAGNI): Authenticode signing/notarization, Linux, ARM64-Windows, any change
to macOS runtime behavior beyond the hmac file move + CMake guards.

## Cross-platform refactor (shared code)

- **Split `hmac_sha256` into platform files.** Move the CommonCrypto implementation out of
  `token.cpp` into `src/hmac_apple.cpp`; add `src/hmac_windows.cpp` using CNG **BCrypt**
  (`BCryptOpenAlgorithmProvider(BCRYPT_SHA256_ALGORITHM, …, BCRYPT_ALG_HANDLE_HMAC_FLAG)`
  → `BCryptCreateHash` with the key → `BCryptHashData` → `BCryptFinishHash`). `token.h`
  keeps the `hmac_sha256` declaration; `token.cpp`/`build_rtc_token` stay portable. The
  vendored AccessToken2 (`utils.h`) keeps calling `avc::hmac_sha256` unchanged. CMake
  compiles exactly one platform hmac source.
- **CMake guards.** Wrap the current macOS-only app-target configuration in `if(APPLE)`
  (framework `-F`/`-framework AgoraRtcKit`, `-fobjc-arc`, `voice_engine_agora.mm`,
  Info.plist `sectcreate`, ad-hoc `codesign`, `@loader_path` rpath). Add an `if(WIN32)`
  branch: compile `voice_engine_agora_win.cpp`, link `AgoraRtcKit.lib` (from the Windows
  SDK) and `bcrypt.lib`, copy the Agora `.dll`s next to the `.exe` as a post-build step,
  no codesign (Windows resolves DLLs from the executable's directory — no rpath needed).
- **zlib on Windows.** The vendored token builder needs zlib; `windows-latest` has none by
  default. Provide it via CMake **FetchContent** (a pinned zlib built from source —
  reproducible, no runner package manager). macOS keeps using its system zlib.

## Windows engine — `src/voice_engine_agora_win.cpp`

Implements `avc::VoiceEngine` using the C++ `IRtcEngine` API (`agora::rtc`):
`createAgoraRtcEngine()` → `initialize(RtcEngineContext{appId, eventHandler, audioScenario})`
→ `setChannelProfile(CHANNEL_PROFILE_LIVE_BROADCASTING)` →
`setClientRole(CLIENT_ROLE_BROADCASTER)` → speech-optimized audio profile + an
AEC-preserving **Meeting** scenario → `enableAudio()` → `joinChannel(token, channel, uid,
ChannelMediaOptions{publishMicrophoneTrack=true, autoSubscribeAudio=true,
clientRoleType=BROADCASTER, channelProfile=LIVE_BROADCASTING})`. `renew_token` →
`renewToken`; `stop` → `leaveChannel` + `release`.

An `IRtcEngineEventHandler` subclass maps events to `VoiceEngineCallbacks`:
`onJoinChannelSuccess`→`on_joined`, `onUserJoined`→`on_user_joined`,
`onUserOffline`→`on_user_left`, `onConnectionStateChanged` →
reconnecting/reconnected/`on_fatal` (same Reconnecting→Connected tracking as macOS),
`onError`→`on_error`, `onTokenPrivilegeWillExpire`→`on_token_will_expire`. Behavior matches
ADR-0004 (broadcaster publishes mic; AEC stays on). Exact enum spellings
(`CHANNEL_PROFILE_TYPE`, `CLIENT_ROLE_TYPE`, `AUDIO_SCENARIO_TYPE`,
`CONNECTION_STATE_TYPE`) are confirmed against the Windows SDK headers in the plan.
Callbacks fire on an SDK thread, as on macOS — the reporter's mutex already covers this.

## JSONL on Windows

Windows opens stdout in text mode, translating `\n`→`\r\n`, which would corrupt the
`\n`-delimited JSONL contract. At startup the binary sets stdout to **binary mode** on
Windows (`_setmode(_fileno(stdout), _O_BINARY)`), so event lines remain LF-only. Guarded by
`#ifdef _WIN32`; no effect on macOS.

## SDK fetch — `scripts/fetch-sdk.sh` becomes OS-aware

Detects the OS (`uname` → `Darwin` vs `MINGW*/MSYS*` on the runner's git-bash). For Windows
it downloads the pinned `Agora_Native_SDK_for_Windows_v4.4.0_FULL.zip` (confirmed present on
the public CDN; sha256-pinned), and extracts the C++ headers + the x64 `.dll`/`.lib` into
`third_party/agora/`. macOS behavior is unchanged (frameworks). The exact Windows zip
internal layout (include/ and lib paths) is confirmed when writing the plan.

## CI

- **New `.github/workflows/ci.yml`** (triggers: `push`, `pull_request`): a matrix of
  `macos-15` and `windows-latest`. Steps: checkout → `fetch-sdk.sh` (OS-aware) → CMake
  configure → build → run unit tests (`./build/tests` / `build\…\tests.exe`). This is the
  iteration loop and also guards macOS against cross-platform regressions in shared code.
- **Extend `release.yml`** with a `windows` job (alongside `macos`): fetch SDK → configure
  (Release) → build → test gate → package → upload `agora-voice-client-windows-x64.zip` to
  the same Release. Both jobs use the create-or-clobber idempotency
  (`gh release create … || gh release upload … --clobber`) so the two jobs racing to create
  the release is tolerated (one creates, the other uploads).

## Packaging & docs

- **`scripts/package-windows.ps1`** (PowerShell): stage `agora-voice-client.exe` + the
  required Agora `.dll`s into `dist/agora-voice-client-windows-x64/` and zip it. No code
  signing. Windows finds the DLLs beside the exe automatically.
- **README / release notes:** Windows usage — unzip; enable
  **Settings → Privacy → Microphone → "Allow desktop apps to access your microphone"**
  (Windows gates mic access by a global setting; unpackaged console apps get no per-app
  prompt, unlike macOS TCC); and on first launch SmartScreen may warn → **"More info → Run
  anyway"** (the Windows analog of the macOS quarantine note). `--uid` coordination and the
  `--json` contract are identical to macOS.

## Testing / verification

- **Automated (CI, both platforms):** the unit suite — `config`, `token` (incl. HMAC vs
  RFC 4231, now exercising BCrypt on Windows and CommonCrypto on macOS), `event_reporter`.
  Green on the `windows-latest` matrix leg proves the portable core + BCrypt path.
- **Compile/link (CI Windows):** the full `agora-voice-client.exe` builds and links against
  the Windows SDK — proves the engine + CMake + SDK fetch are correct.
- **Manual (Operator's Windows machine):** live two-way audio with the AI agent, echo
  cancellation, `--json` event stream, clean exit — the same manual checklist as macOS,
  plus confirming the mic privacy setting and SmartScreen bypass. CI cannot test live audio.

## Open items to finalize in the plan (not blockers)

- zlib provisioning on Windows (FetchContent pinned version).
- Exact Windows SDK zip layout (header/lib/dll paths) and the exact C++ enum constant
  names, confirmed against the downloaded package.
- Whether the push CI should also run on macOS for every push (yes, per the matrix) — keep
  an eye on Actions minutes; acceptable for this repo.
