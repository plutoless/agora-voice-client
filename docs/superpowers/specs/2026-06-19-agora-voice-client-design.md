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
- `--uid <n>` — optional, default `0` (Agora auto-assigns a uid).
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
   Wraps Agora's RtcToken builder. Bypassed when an explicit token is supplied.
3. **`voice_engine`** — a thin interface over Agora that owns the `IRtcEngine`
   lifecycle: create → `initialize` (audio profile; channel profile = communication;
   3A enabled) → `joinChannel` → `leaveChannel` → release. Because devices and 3A are
   the SDK's job, this unit stays small. The interface is what makes future platforms
   pluggable; the macOS implementation is the first concrete one.
4. **`event_handler`** — implements `IRtcEngineEventHandler`; logs the events that
   matter (join success, connection-state changes, remote user joined/left, warnings,
   errors) to **stderr**.
5. **`main`** — wires the units together, installs SIGINT/SIGTERM handlers, blocks
   until a signal arrives, then triggers a clean shutdown.

## Runtime Flow

```
parse config
  → resolve token (explicit | mint | none)
  → create + initialize engine (audio profile, communication mode, 3A on)
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
- All operational logs go to **stderr**, keeping stdout clean for possible future
  status piping.

## Build & Layout

- **CMake**, C++17.
- macOS target links `AgoraRtcKit.framework` from the Agora Voice/Video SDK download,
  with loader/rpath configured so the packaged binary finds the framework next to
  itself.
- Project layout:
  ```
  CMakeLists.txt
  src/
    main.cpp
    config.{h,cpp}
    token.{h,cpp}
    voice_engine.h            # interface
    voice_engine_agora.cpp    # macOS/Windows IRtcEngine implementation
    event_handler.{h,cpp}
  third_party/agora/          # SDK framework + headers (fetched, gitignored)
  scripts/                    # SDK download / package helpers
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

## Open Item to Verify at Planning Time

Confirm whether the high-level `IRtcEngine` API is used uniformly on macOS and Windows
(expected yes) and pin the exact SDK package/version and its 3A configuration calls.
This only affects how much per-platform code lives behind the `voice_engine` interface;
it does not change the overall design.

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
