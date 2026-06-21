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

## Windows (x64)

Unzip `agora-voice-client-windows-x64.zip` and run `agora-voice-client.exe` (the Agora
DLLs ship beside it). Unsigned, so SmartScreen may warn → **More info → Run anyway**.

```bat
set AGORA_APP_ID=<your app id>
set AGORA_APP_CERTIFICATE=<your app certificate>
agora-voice-client.exe --channel <room> --uid <N>
```

Enable **Settings → Privacy & security → Microphone → "Let desktop apps access your
microphone"**. Close with **Ctrl-C** (not the window) for a clean leave. `--json` and
`--version` behave the same as macOS.
