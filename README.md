# agora-voice-client

A self-hosted macOS command-line client that joins an Agora channel and gives you
two-way voice with a remote AI agent. Single-operator tool: you run it on your own
machine with your own Agora credentials. Tokens are minted locally from your App
Certificate (see `docs/adr/0001-local-token-minting-self-hosted.md`) — never distribute
a build with your certificate, and never share the certificate.

## Build from source

```bash
./scripts/fetch-sdk.sh                 # downloads the pinned Agora SDK (~88 MB) into third_party/agora/
cmake -S . -B build && cmake --build build
./build/tests                          # unit tests (config + token)
```

## Run

```bash
export AGORA_APP_ID=<your app id>
export AGORA_APP_CERTIFICATE=<your app certificate>   # tokens minted locally
./build/agora-voice-client --channel <room> --uid <N>
```

- `--uid <N>` is the Client UID and **must match the uid the AI agent subscribes to**
  (see `docs/adr/0003-client-uid-is-a-coordinated-identifier.md`). `--uid 0` (the default)
  is for solo/echo testing only.
- `--token-ttl <seconds>` optional (default 3600); the client auto-renews before expiry.
- Instead of a certificate you may set `AGORA_TOKEN=<token>` to use a token directly.
- Press Ctrl-C to leave the channel and exit cleanly.

## Machine mode (`--json`)

Run with `--json` to drive the client from another program. It emits one JSON object
per line on **stdout** (human/debug logs and any SDK noise stay on **stderr**):

```bash
./agora-voice-client --json --channel <room> --uid <N> | jq .
```

Event types (every line also has a `ts`, Unix milliseconds):

| `type` | fields | meaning |
|---|---|---|
| `meta` | `protocol`, `client`, `version` | first line; protocol handshake |
| `ready` | `channel`, `uid` | joined; mic live |
| `peer_joined` / `peer_left` | `uid` | a remote participant (the AI agent) joined/left |
| `reconnecting` / `reconnected` | — | connection dropped / restored |
| `token_renewed` | — | token re-minted and renewed |
| `error` | `code`, `message` | non-fatal SDK error |
| `fatal` | `code`, `message` | unrecoverable; process exits non-zero next |
| `stopping` | `reason` (`signal`\|`fatal`) | established session shutting down |

**Terminal event guarantee.** Every `--json` run ends with exactly one terminal event
matching the exit code:
- pre-session failure (bad args, mint failure, join failure) → `fatal`, exit 1 or 2;
- established session ended by Ctrl-C/SIGTERM → `stopping` (`reason:"signal"`), exit 0;
- established session lost unrecoverably → `fatal` then `stopping` (`reason:"fatal"`),
  exit non-zero.

**Compatibility.** Within a `protocol` version, changes are additive —
**ignore unknown event types and unknown fields**. A breaking change bumps `protocol`. See
`docs/adr/0006-cli-integration-subprocess-jsonl.md`.

`--version` and `--help` print human text (not JSON) even with `--json`.

## First run: microphone permission

macOS gates the microphone. The **first** run must happen in a Terminal **at the Mac**
(a graphical login session) so you can click **Allow** on the microphone prompt. After
that one approval, headless/SSH runs work. A first run over pure SSH cannot grant mic
access. Because the binary is ad-hoc signed, each rebuild re-triggers the prompt
(see `docs/adr/0002-macos-mic-permission-signing.md`).

## Packaging

```bash
./scripts/package.sh   # -> dist/agora-voice-client-macos-arm64.tar.gz (binary + frameworks)
```

The archive bundles the Agora frameworks next to the binary (`@loader_path` rpath), so it
runs on another macOS arm64 machine without a separate SDK download. The microphone-permission
note above still applies on the new machine.

## Windows (x64)

**Prebuilt:** download `agora-voice-client-windows-x64.zip` from the latest
[release](https://github.com/plutoless/agora-voice-client/releases), unzip, and run
`agora-voice-client.exe` (the Agora DLLs ship beside it). It is unsigned, so SmartScreen
may warn → **More info → Run anyway**.

**Build from source** (Visual Studio 2022 + CMake; run from a Git Bash shell for the fetch step):
```bat
bash scripts/fetch-sdk.sh
cmake -S . -B build
cmake --build build --config Release
build\Release\tests.exe
```

**Run:**
```bat
set AGORA_APP_ID=<app id>
set AGORA_APP_CERTIFICATE=<certificate>
agora-voice-client.exe --channel <room> --uid <N>
```

- Enable **Settings → Privacy & security → Microphone → "Let desktop apps access your
  microphone"** (Windows gates mic access globally; there is no per-app prompt).
- Close with **Ctrl-C**, not the window — closing the console window can't leave the
  channel cleanly and may leave a brief ghost participant.
- `--json` (machine mode) and `--version` behave exactly as on macOS.

## Manual integration test

1. Have a second participant join the same channel: the AI agent, or the
   [Agora web demo](https://webdemo.agora.io/) with the same App ID + channel + matching uid setup.
2. Run the client; confirm `[avc] ready`, then `[avc] AI Agent joined uid=...`.
3. **Two-way audio:** speak — the other side hears you; the other side speaks — you hear it.
4. **Echo cancellation:** using speakers (not headphones), confirm the far end does NOT
   hear its own voice looped back (validates the built-in 3A — the core of the design).
5. **Clean exit:** Ctrl-C; confirm the participant disappears (no ghost in the channel).
