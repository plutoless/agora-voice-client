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

## Manual integration test

1. Have a second participant join the same channel: the AI agent, or the
   [Agora web demo](https://webdemo.agora.io/) with the same App ID + channel + matching uid setup.
2. Run the client; confirm `[avc] ready`, then `[avc] AI Agent joined uid=...`.
3. **Two-way audio:** speak — the other side hears you; the other side speaks — you hear it.
4. **Echo cancellation:** using speakers (not headphones), confirm the far end does NOT
   hear its own voice looped back (validates the built-in 3A — the core of the design).
5. **Clean exit:** Ctrl-C; confirm the participant disappears (no ghost in the channel).
