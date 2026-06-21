# CLI integration via subprocess + JSONL events, not an embeddable library

The Client is made consumable by other tools as a **subprocess** that emits JSONL
lifecycle events on **stdout** (`--json` mode), rather than as an embeddable C-ABI library
with in-process callbacks (the model LiveKit uses for its SDKs).

Chosen because the binary must be drivable by **any** CLI regardless of language. A library
would force every consumer into CGO/FFI plus, on macOS, an Objective-C bridge, and would
put us on the hook to maintain per-language bindings — a platform-vendor cost we won't
take on for a one-binary project. The subprocess + JSONL contract is language-agnostic,
process-isolated (a child crash can't take down the parent), low-maintenance, and trivially
testable. It also makes our tool more machine-friendly than `lk room join`, which has no
JSON event stream.

Consequences a future reader needs to know:
- **stdout is a reserved machine channel** in `--json` mode — only the event reporter writes
  there; all engine/SDK/diagnostic output goes to stderr or Agora's log file.
- The stream must **flush per event** (stdout is fully buffered when piped) and serialize
  writes with a **mutex** (events originate on the SDK thread and the main thread).
- The contract evolves **additively** under a `protocol` version announced by the first
  `meta` event; consumers must ignore unknown event types and fields.
- Revisit a library only if this becomes a real multi-language SDK, or a consumer needs
  in-process access to the raw audio (it currently goes straight to the sound card).
