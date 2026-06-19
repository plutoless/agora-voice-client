# macOS microphone permission: embedded Info.plist + ad-hoc signing

macOS gates the microphone behind TCC. A bare CLI binary has no Info.plist and no
stable code identity, so it captures silence with no error. To get a real permission
prompt, the build embeds an `Info.plist` (with `NSMicrophoneUsageDescription`) into the
Mach-O via the linker and **ad-hoc code-signs** the binary. Ad-hoc signing is free and
needs no Apple Developer certificate, which suits the self-hosted, single-Operator model
(ADR-0001).

Consequences a future reader needs to know:
- The **first run must happen in a GUI Terminal session at the Mac** so the Operator can
  approve the mic prompt. After that one approval, headless/SSH runs work. A first run
  over pure SSH cannot grant mic access — a macOS limitation, not a bug.
- Ad-hoc identity is the binary's hash, so **each rebuild re-prompts** for mic
  permission. Acceptable for a personal tool.
- Upgrading to a Developer ID certificate later makes the grant survive rebuilds and is
  required if the binary is ever distributed (which would also force revisiting ADR-0001).
