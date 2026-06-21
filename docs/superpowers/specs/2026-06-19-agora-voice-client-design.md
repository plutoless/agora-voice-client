# Agora Voice Client — Design Spec

**Date:** 2026-06-19
**Status:** Approved design, ready for implementation planning

## Purpose

A self-contained, downloadable command-line binary that turns any shell/PC into a
real-time voice endpoint in an Agora channel. The primary use case is **talking to a
remote AI agent instantly**: a user (or a setup skill) downloads the binary, runs it
with a channel name, and immediately has two-way voice with whatever AI participant is
in that channel.

Distribution model: the binary is fetched per-platform by a download skill, unpacked,
and run directly — no Python/Node/Electron runtime to install first. This is why the
client is a **native C++ executable** rather than a scripted/Electron client.

## Scope

In scope:
- Audio only (voice). No video, no recording, no screen share.
- Join one channel, go live on the real microphone and speakers, stay connected until
  the process is signalled to stop (Ctrl-C / SIGTERM).
- No interactive runtime controls (no mute/volume commands). "Connect and talk."
- First shipping target: **macOS arm64**.

Out of scope (now):
- Windows and Linux binaries (Windows is a near-free follow-on; Linux is a deferred,
  isolated decision — see Cross-Platform Strategy).
- Interactive command handling, multiple simultaneous channels, the AI/server side of
  the conversation (a separate participant, not this client).

## Key Architectural Decision: built-in device + 3A, NOT push/pull

Agora's value for a voice-AI client is its built-in audio device handling **plus 3A**
(AEC = acoustic echo cancellation, AGC = automatic gain control, ANS = automatic noise
suppression). AEC is essential here: without hardware-tuned echo cancellation the mic
captures the AI's voice played from the speakers and feeds it back, so the AI hears
itself.

Therefore the client uses Agora's **high-level `IRtcEngine` API with built-in
microphone/speaker capture and 3A**. We do **not** use the custom-audio (push/pull PCM)
path, and we do **not** manage devices ourselves (no miniaudio in the core). The SDK
owns capture, playback, and echo cancellation.

This built-in path is identical on macOS and Windows (same `IRtcEngine`, same 3A), so
those two platforms share one code path at full audio quality. Linux is the only
platform without a built-in-device client SDK and is handled separately (below).

## Configuration

Connection settings are supplied via environment variables (secrets) plus CLI flags
(per-run, non-secret).

Environment variables:
- `AGORA_APP_ID` — **required**.
- `AGORA_APP_CERTIFICATE` — optional. If set, the client mints its own token locally.
- `AGORA_TOKEN` — optional. Used directly if provided.

CLI flags:
- `--channel <name>` — **required**.
- `--uid <n>` — the **Client UID**. Defaults to `0` (auto-assign), but `0` only works
  for solo/echo testing. For talking to the AI Agent it must be set to the fixed uid the
  AI subscribes to (see ADR-0003). The resolved uid is a single value used for *both*
  token minting and channel join so they cannot drift.
- `--token-ttl <seconds>` — optional, default `3600`. Used only when minting locally.

Token resolution order:
1. If `AGORA_TOKEN` is set, use it directly.
2. Else if `AGORA_APP_CERTIFICATE` is set, mint a token locally from app id +
   certificate + channel + uid + ttl.
3. Else join with no token (only works if the Agora project is in testing / no-auth
   mode).

## Components

Each unit has one purpose and a clear boundary so it can be understood and tested
independently.

1. **`config`** — parses env vars + CLI flags, validates required fields, produces a
   `ClientConfig` value. No Agora dependency → unit-testable in isolation.
2. **`token`** — given app id, certificate, channel, uid, ttl, returns a token string.
   Wraps Agora's vendored **AccessToken2** builder (`RtcTokenBuilder2`, copied from the
   `AgoraIO/Tools` repo into `third_party/agora-token/`; not the legacy builder). Its
   OpenSSL HMAC-SHA256 dependency is replaced with a platform-native `hmac_sha256()`
   call — **CommonCrypto (`CCHmac`) on macOS** — so the token feature adds no external
   build dependency. The `hmac_sha256()` seam lets Windows (BCrypt) and Linux (OpenSSL)
   slot in later. Bypassed when an explicit `AGORA_TOKEN` is supplied.
3. **`voice_engine`** — a thin interface over Agora that owns the `IRtcEngine`
   lifecycle: create → `initialize` → set channel profile `LIVE_BROADCASTING` →
   `setClientRole(BROADCASTER)` → set a **speech-optimized mono audio profile** with an
   **AEC-preserving scenario** (echo cancellation must stay on — ADR-0002/3A is the whole
   point) → `joinChannel` → `leaveChannel` → release. Profile/role/audio config are fixed
   (no tuning flags in v1). See ADR-0004 for the profile + role choice. Because devices
   and 3A are the SDK's job, this unit stays small. The interface is what makes future platforms
   pluggable; the macOS implementation is the first concrete one.
4. **event handling** — on macOS this is an `AgoraRtcEngineDelegate` living inside
   `voice_engine_agora.mm` (ADR-0005), not a separate C++ file. It logs the events that
   matter (join success, connection-state changes, remote user joined/left, warnings,
   errors) to **stderr** and surfaces semantic C++ callbacks through the `voice_engine`
   interface. Token expiry is surfaced as a callback; `main` performs the **automatic
   local token renewal** (re-mint via the `token` unit, then `renewToken()`), keeping
   certificate handling out of the engine. This relies on the Client holding the App
   Certificate locally (ADR-0001).
5. **`main`** — wires the units together, installs SIGINT/SIGTERM handlers, blocks
   until a signal arrives, then triggers a clean shutdown.

## Runtime Flow

```
parse config
  → resolve token (explicit | mint | none)
  → create + initialize engine (LIVE_BROADCASTING, role BROADCASTER,
      speech-optimized mono audio profile, AEC-preserving scenario, 3A on)
  → register event handler
  → joinChannel
  → [microphone live, remote audio plays, events logged to stderr]
  → wait for SIGINT / SIGTERM
  → leaveChannel
  → release engine
  → exit 0
```

## Error Handling

- Missing `AGORA_APP_ID` or `--channel`: print usage to stderr and exit non-zero
  before touching the SDK.
- SDK init / join failures: log the Agora error code and its meaning, exit non-zero.
- Signal received during a call: perform a graceful `leaveChannel` + `release` so the
  call ends cleanly on Agora's side (no ghost participant left in the channel).
- Transient network drops: **rely on the SDK's built-in auto-reconnection** — no custom
  rejoin logic. Log the RECONNECTING → CONNECTED transitions to stderr.
- Unrecoverable connection failure (`CONNECTION_STATE_FAILED`, e.g. banned uid, invalid
  app id, SDK gave up): log the reason and **exit non-zero** rather than retrying forever.
- AI Agent presence does **not** affect lifecycle: log "AI Agent joined / left" for
  visibility, but the Client keeps running. Only a signal or unrecoverable failure ends it.
- All operational logs go to **stderr**, keeping stdout clean for possible future
  status piping.

## Build & Layout

- **CMake**, C++17.
- macOS target links `AgoraRtcKit.framework` from the Agora Voice/Video SDK download,
  with loader/rpath configured so the packaged binary finds the framework next to
  itself.
- The build embeds an `Info.plist` (with `NSMicrophoneUsageDescription`) into the Mach-O
  and **ad-hoc code-signs** the binary so macOS grants microphone access. No Apple
  Developer certificate is required now (see ADR-0002); Developer ID + notarization is a
  later upgrade. First run must be in a GUI Terminal at the Mac to approve the mic
  prompt; each rebuild re-prompts under ad-hoc signing.
- Project layout:
  ```
  CMakeLists.txt
  src/
    main.cpp
    config.{h,cpp}
    token.{h,cpp}
    voice_engine.h            # pure C++ interface + semantic callbacks
    voice_engine_agora.mm     # macOS Obj-C++ impl (AgoraRtcEngineKit + delegate)
  third_party/agora/          # SDK framework + headers (fetched, gitignored)
  third_party/agora-token/    # vendored AccessToken2 builder (committed)
  scripts/fetch-sdk.sh        # downloads pinned Voice SDK + verifies checksum
  scripts/                    # packaging helpers
  README.md                   # how to get the SDK, set env, run
  ```

## Distribution

Agora ships **dynamic** libraries, so a release artifact is the executable plus the
Agora runtime lib, packaged together with loader paths set:

- `agora-voice-client-macos-arm64.tar.gz` (executable + `AgoraRtcKit.framework`)
- (later) `agora-voice-client-windows-x64.zip` (executable + `.dll`s)
- (later) `agora-voice-client-linux-x64.tar.gz` — see Cross-Platform Strategy

The download skill selects the right archive for the host, unpacks it, and runs
`./agora-voice-client --channel <ai-room>`.

## Cross-Platform Strategy

- **macOS (now):** built-in device + 3A via `IRtcEngine`. First shipping target
  (arm64).
- **Windows (near-free follow-on):** same `IRtcEngine` built-in path and 3A; only the
  linked binary and packaging differ. Reuses `config`, `token`, `event_handler`, and
  the `voice_engine` interface unchanged.
- **Linux (deferred, isolated):** Agora has no built-in-device client SDK for Linux —
  only the push/pull Server Gateway SDK. Linux is therefore inherently the weaker
  platform for client-side 3A. When/if Linux is needed, it gets a **separate
  `voice_engine` implementation** (push/pull PCM + a device library such as miniaudio +
  whatever APM the Linux SDK offers) behind the same interface, so it never degrades
  the macOS/Windows path. This is a future decision, not part of the first version.

## Resolved at Planning Time

- **macOS API surface:** the supported entry point is the Objective-C `AgoraRtcEngineKit`,
  not the C++ `IRtcEngine` (which is only reachable by bridging in an `.mm` file). The
  macOS `voice_engine` is therefore Objective-C++ against `AgoraRtcEngineKit` (ADR-0005);
  the C++ interface is reserved for future Windows/Linux implementations.
- **Token builder:** `agora::tools::RtcTokenBuilder2::BuildTokenWithUid(...)` (AccessToken2),
  vendored, with HMAC routed to CommonCrypto.
- Still pin at implementation time: the exact 4.x Voice SDK package URL + checksum, and
  the precise `AgoraAudioScenario`/`AgoraAudioProfile` enum values that keep AEC enabled.

## Testing

- **Unit tests (no SDK / no network):**
  - `config`: parsing and validation (required fields, defaults, token resolution
    order).
  - `token`: output shape/format for known inputs.
- **Manual integration test (documented checklist in README):**
  - Run the client and join the same channel as a second participant (another client
    or the Agora web demo / the AI agent).
  - Confirm two-way audio and, critically, that echo cancellation works (the far end
    does not hear its own voice returned).
  - Confirm clean exit on Ctrl-C (no ghost participant remains).

  Fully automated audio testing is not worth the cost for this tool (YAGNI).

## Implementation Notes (post-build reconciliation)

Discovered while building; recorded so the design matches reality:

- **SDK package:** Agora no longer publishes a standalone *Voice* SDK zip on the public
  CDN — only the **FULL** Native SDK package (`Agora_Native_SDK_for_Mac_v<ver>_FULL.zip`,
  pinned to 4.4.0). It contains the same `AgoraRtcKit` framework, plus sibling frameworks
  (`aosl.framework`, `Agoraffmpeg.framework`, …) that `AgoraRtcKit` loads at runtime.
  `scripts/fetch-sdk.sh` extracts and ad-hoc-signs all of them.
- **Token builder is header-only:** the vendored AccessToken2 implementation lives inline
  in `utils.h` (no `AccessToken2.cpp`). The OpenSSL HMAC was swapped for CommonCrypto in
  `utils.h`; the patch is documented in `third_party/agora-token/PATCHES.md`. The builder
  also needs **zlib** (linked via `find_package(ZLIB)`).
- **Audio scenario:** `AgoraAudioScenarioMeeting` (set via `AgoraRtcEngineConfig.audioScenario`)
  keeps AEC/ANS/AGC on; the combined `setAudioProfile:scenario:` is deprecated.
- **Code signing:** the binary is ad-hoc signed with hardened runtime, the embedded
  Info.plist, and entitlements `com.apple.security.device.audio-input` **plus**
  `com.apple.security.cs.disable-library-validation` (required so the ad-hoc binary may
  load Agora's differently-signed dylibs). `scripts/package.sh` produces a relocatable
  archive via an `@loader_path` rpath and re-signs binary + frameworks.
