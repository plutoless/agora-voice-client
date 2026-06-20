# JSONL Event Stream (CLI-consumable subprocess) — Design Spec

**Date:** 2026-06-20
**Status:** Approved design, ready for implementation planning

## Purpose

Make the `agora-voice-client` binary a well-behaved **subprocess** that any CLI or
orchestrator can drive, so a parent tool gains a "join a channel and talk to the AI agent
from the shell" capability (the equivalent of LiveKit's `lk room join`). The binary stays
a standalone tool; we add a stable machine-readable event contract on top.

## Integration model and rationale

Chosen model: **subprocess + JSONL events** (the parent spawns the binary and reads
structured events from stdout), over the alternative of an embeddable C-ABI library.

Why: the binary must be consumable by **any** CLI regardless of language. A library
forces the consumer into a compatible language or CGO/FFI plus, on macOS, an Objective-C
bridge — and would put us on the hook to maintain per-language bindings (a platform-vendor
cost; this is how LiveKit does it, but they ship hand-written native SDKs per language).
The subprocess contract is language-agnostic, process-isolated, low-maintenance, and
trivially testable. It also makes our tool *more* machine-friendly than `lk` itself, whose
`room join` has no JSON event stream. (Revisit a library only if this ever becomes a
multi-language SDK or a consumer needs in-process raw-audio access.)

## The contract

- A new `--json` flag switches the binary into machine mode.
- **stdout** carries **JSONL**: exactly one JSON object per line, one per lifecycle event.
  This is the machine channel the parent reads.
- **stderr** remains human/debug text (including any Agora SDK noise); a parent ignores
  or captures it separately.
- Default mode (no `--json`) is unchanged: human-readable `[avc]` text, stdout stays clean.
- Lifecycle and exit codes are unchanged: SIGINT/SIGTERM → clean leave; exit 0 normal,
  1 start-failure, 2 usage error, fatal-code on unrecoverable connection failure.
- `--version`/`--help` continue to short-circuit before anything else (added previously).

## Event schema

Every line is `{"type": <string>, "ts": <unix_ms>, ...fields}`. Lifecycle-only set:

| type | fields | when |
|---|---|---|
| `ready` | `channel` (string), `uid` (number) | joined successfully; mic live |
| `peer_joined` | `uid` (number) | a remote participant (the AI agent) joined |
| `peer_left` | `uid` (number) | a remote participant left |
| `reconnecting` | — | connection dropped; SDK auto-retrying |
| `reconnected` | — | connection restored |
| `token_renewed` | — | token re-minted locally and renewed on the engine |
| `error` | `code` (number), `message` (string) | non-fatal SDK error/warning |
| `fatal` | `code` (number), `message` (string) | unrecoverable; process exits non-zero next |
| `stopping` | `reason` (`"signal"` \| `"fatal"`) | clean shutdown beginning |

`ts` is Unix milliseconds. The only user-supplied string field is `channel`, which is
JSON-escaped.

## Architecture

Keeps the engine as pure transport and concentrates all user-facing output in `main` via
a testable reporter.

- **`event_reporter` (new, `src/event_reporter.{h,cpp}`, pure C++):** an `EventReporter`
  interface with one method per event (`ready`, `peer_joined`, `peer_left`,
  `reconnecting`, `reconnected`, `token_renewed`, `error`, `fatal`, `stopping`). Two
  implementations:
  - `JsonEventReporter` — writes JSONL to an injected `std::ostream&`.
  - `HumanEventReporter` — writes `[avc] …` text to an injected `std::ostream&`.
  Both take the stream by reference so they are unit-testable. Includes a small
  hand-rolled JSON string-escaper (no JSON library dependency).
- **`voice_engine.h` (modify):** add the semantic callbacks the events need but that we
  don't yet surface — `on_reconnecting`, `on_reconnected`, `on_error(int code)`.
  (`on_joined`, `on_user_joined`, `on_user_left`, `on_token_will_expire`, `on_fatal`
  already exist.) No Agora types leak through the interface.
- **`voice_engine_agora.mm` (modify):** invoke the new callbacks from
  `connectionChangedToState` (reconnecting/reconnected) and `didOccurError`
  (`on_error`); remove the engine's own `fprintf` logging so `main` is the single output
  authority (engine becomes pure transport).
- **`main.cpp` (modify):** parse `--json` early (next to `--version`/`--help`); construct
  `JsonEventReporter(std::cout)` or `HumanEventReporter(std::cerr)`; wire every
  `VoiceEngineCallbacks` lambda to a reporter method; emit `token_renewed` after a
  successful re-mint and `stopping` before `engine->stop()`.

## Testing

- **Unit (TDD, no SDK):** `JsonEventReporter` against an injected `std::ostringstream` —
  assert exact JSONL for each event type, that `peer_joined`/`peer_left` carry the uid,
  `ready` carries channel+uid, and that channel-name escaping handles quotes,
  backslashes, and control characters. A lighter smoke test for `HumanEventReporter`.
  (`ts` is verified for presence/numeric type, not exact value.)
- **Manual:** `agora-voice-client --json --channel <room> --uid <N>` → observe
  `{"type":"ready",...}` then `{"type":"peer_joined",...}` on stdout; documented in the
  README with a sample `| jq` parse.

## Out of scope (YAGNI)

- No stdin runtime control (mute/leave commands).
- No audio-level / speaking events.
- No embeddable library (the rejected fork).
- No changes to the release-CI work or the engine's audio configuration.

## Notes

- Terminology: events use `peer_joined`/`peer_left` (generic, uid-keyed) rather than
  "ai_agent"; the remote is the **AI Agent** in our glossary, but the machine contract
  stays generic since any remote participant triggers it.
- The `--json` flag should be additive and default-off, so existing interactive use and
  the release artifacts are unaffected.
