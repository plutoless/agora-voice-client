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
multi-language SDK or a consumer needs in-process raw-audio access.) Recorded as ADR-0006.

## The contract

- A new `--json` flag switches the binary into machine mode.
- **stdout purity invariant:** in `--json` mode, **only** the `JsonEventReporter` writes to
  stdout — exactly one JSON object per line (JSONL), one per event. All engine, SDK, and
  diagnostic output goes to **stderr** or Agora's own log file. This is what makes stdout a
  reliable machine channel (see ADR-0006). Verified achievable: nothing else in our code or
  the vendored token code writes stdout, and Agora logs to a file by default (we do not
  change its `LogConfig`).
- **Flush per event:** the reporter writes the line and **flushes immediately**. stdout is
  fully buffered when piped to a parent, so without per-event flush the consumer would see
  nothing until the buffer fills or the process exits. Non-negotiable for a live stream.
- **stderr** remains human/debug text (including any Agora SDK noise); a parent ignores
  or captures it separately.
- Default mode (no `--json`) is unchanged: human-readable `[avc]` text, stdout stays clean.
- Lifecycle and exit codes are unchanged: SIGINT/SIGTERM → clean leave; exit 0 normal,
  1 start-failure, 2 usage error, fatal-code on unrecoverable connection failure.
- `--version`/`--help` short-circuit before anything else and stay **human text even under
  `--json`** (they are pre-session, single-shot; not worth a JSON variant).

## Event schema

Every line is `{"type": <string>, "ts": <unix_ms>, ...fields}`. Lifecycle-only set:

| type | fields | when |
|---|---|---|
| `meta` | `protocol` (number), `client` (string), `version` (string) | **first** event in `--json` mode, before anything else |
| `ready` | `channel` (string), `uid` (number) | joined successfully; mic live |
| `peer_joined` | `uid` (number) | a remote participant (the AI agent) joined |
| `peer_left` | `uid` (number) | a remote participant left |
| `reconnecting` | — | connection dropped; SDK auto-retrying |
| `reconnected` | — | connection restored |
| `token_renewed` | — | token re-minted locally and renewed on the engine |
| `error` | `code` (number), `message` (string) | non-fatal SDK error/warning |
| `fatal` | `code` (number), `message` (string) | unrecoverable; process exits non-zero next |
| `stopping` | `reason` (`"signal"` \| `"fatal"`) | clean shutdown of an established session |

`ts` is Unix milliseconds. The only user-supplied string field is `channel`, which is
JSON-escaped.

### Terminal-event guarantee (failure paths are self-describing)

Every `--json` run ends with exactly **one** terminal event on stdout, matching the exit code:
- config/usage error → `fatal` (message = the validation/usage error), then exit 2 (human
  usage still printed to stderr).
- `engine->start()` failure → `fatal` (`message:"failed to start"`), then exit 1.
- unrecoverable runtime failure of an established session → `fatal` then `stopping`
  (`reason:"fatal"`), then exit with the fatal code.
- SIGINT/SIGTERM on an established session → `stopping` (`reason:"signal"`), then exit 0.

`stopping` is emitted **only** when an established (joined) session is being torn down;
pre-session failures emit `fatal` alone (there is nothing to stop).

### Forward compatibility

The `meta` event announces the `protocol` version (currently `1`). Within a protocol
version, changes are **additive only** — new event types and new fields may appear.
**Consumers must ignore unknown event types and unknown fields.** A breaking change bumps
`protocol`. (See ADR-0006.)

## Architecture

Keeps the engine as pure transport and concentrates all user-facing output in `main` via
a testable reporter.

- **`event_reporter` (new, `src/event_reporter.{h,cpp}`, pure C++):** an `EventReporter`
  interface with one method per event (`meta`, `ready`, `peer_joined`, `peer_left`,
  `reconnecting`, `reconnected`, `token_renewed`, `error`, `fatal`, `stopping`). Two
  implementations:
  - `JsonEventReporter` — writes one JSONL object per event to an injected `std::ostream&`,
    **flushing after each line**, with writes serialized by an **internal `std::mutex`** so
    each line is atomic (events arrive from the SDK thread and the main thread). Includes a
    small hand-rolled JSON string-escaper (no JSON library dependency).
  - `HumanEventReporter` — writes `[avc] …` text to an injected `std::ostream&` (same mutex
    + flush treatment for consistency).
  Both take the stream by reference so they are unit-testable.
- **`voice_engine.h` (modify):** add the semantic callbacks the events need but that we
  don't yet surface — `on_reconnecting`, `on_reconnected`, `on_error(int code)`.
  (`on_joined`, `on_user_joined`, `on_user_left`, `on_token_will_expire`, `on_fatal`
  already exist.) No Agora types leak through the interface.
- **`voice_engine_agora.mm` (modify):** invoke the new callbacks from
  `connectionChangedToState` (reconnecting/reconnected) and `didOccurError`
  (`on_error`); remove the engine's own `fprintf` logging so `main` is the single output
  authority (engine becomes pure transport).
- **`main.cpp` (modify):** parse `--json` early (next to `--version`/`--help`); construct
  `JsonEventReporter(std::cout)` or `HumanEventReporter(std::cerr)`; emit `meta` **first**;
  wire every `VoiceEngineCallbacks` lambda to a reporter method; emit `token_renewed` after
  a successful re-mint and `stopping` before `engine->stop()`. On the failure paths emit the
  terminal `fatal` event per the terminal-event guarantee: a config/usage exception →
  `fatal` then exit 2; `engine->start()` returning false → `fatal` then exit 1.

## Testing

- **Unit (TDD, no SDK):** `JsonEventReporter` against an injected `std::ostringstream` —
  assert exact JSONL for each event type, including `meta` (protocol/client/version),
  that `peer_joined`/`peer_left` carry the uid, `ready` carries channel+uid, and that
  channel-name escaping handles quotes, backslashes, and control characters. A lighter
  smoke test for `HumanEventReporter`. (`ts` is verified for presence/numeric type, not
  exact value. The mutex is correctness insurance, not unit-tested; the flush behavior is
  verified manually since `ostringstream` makes content available regardless of flush.)
- **Manual:** `agora-voice-client --json --channel <room> --uid <N> | jq .` → confirm the
  `meta` line appears immediately (proving per-event flush over a pipe), followed by
  `ready` then `peer_joined`; also confirm a config error (`--json` with no `AGORA_APP_ID`)
  emits a `fatal` line then exits 2. Documented in the README.

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
