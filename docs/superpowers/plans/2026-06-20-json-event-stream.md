# JSONL Event Stream Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `--json` mode so the `agora-voice-client` binary emits a stable JSONL lifecycle-event stream on stdout, making it a well-behaved subprocess any CLI can drive.

**Architecture:** A new pure-C++ `event_reporter` unit (interface + `JsonEventReporter`/`HumanEventReporter`, mutex-guarded, flush-per-event) becomes the single output authority. `main` selects the reporter from `--json`, emits a `meta` handshake first, and wires every engine callback to it. The engine gains semantic `on_reconnecting`/`on_reconnected`/`on_error` callbacks and stops doing its own logging.

**Tech Stack:** C++17, doctest, macOS Objective-C++ (`AgoraRtcEngineKit`), `<mutex>`, `<chrono>`.

**Spec:** `docs/superpowers/specs/2026-06-20-json-event-stream-design.md`. Decisions in ADR-0006. Glossary term: **Consumer**.

**Context for the implementer:** Branch `feat/agora-voice-client`. The client builds with `cmake -S . -B build && cmake --build build` (Agora SDK already fetched at `third_party/agora/`). Tests: `./build/tests`. Existing files of interest: `src/main.cpp` (entry; already handles `--version`/`--help` early and stamps `AVC_VERSION_STR`), `src/voice_engine.h` (`VoiceEngineCallbacks` + `VoiceEngine` interface), `src/voice_engine_agora.mm` (macOS engine + `AvcDelegate`). The engine currently `fprintf`s `[avc] …` lines to stderr from its delegate methods; this plan moves user-facing output into `main`'s reporter.

---

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `src/event_reporter.h` | Create | `EventReporter` interface, `JsonEventReporter`, `HumanEventReporter`, `json_escape`, `kEventProtocolVersion` |
| `src/event_reporter.cpp` | Create | Implementations: JSON line building, escaping, per-event flush, mutex |
| `tests/test_event_reporter.cpp` | Create | Unit tests (escaping exact; each event's JSONL content) |
| `src/voice_engine.h` | Modify | Add `on_reconnecting`, `on_reconnected`, `on_error(int)` callbacks |
| `src/voice_engine_agora.mm` | Modify | Invoke new callbacks; drop delegate `fprintf` logging |
| `src/main.cpp` | Modify | Parse `--json`; build reporter; emit `meta` first; wire callbacks; terminal events |
| `CMakeLists.txt` | Modify | Add `event_reporter.cpp` to both targets; `test_event_reporter.cpp` to tests |

---

## Task 1: `event_reporter` unit (TDD)

**Files:**
- Create: `src/event_reporter.h`, `src/event_reporter.cpp`, `tests/test_event_reporter.cpp`
- Modify: `CMakeLists.txt` (tests target)

- [ ] **Step 1: Create `src/event_reporter.h`**

```cpp
#pragma once
#include <cstdint>
#include <mutex>
#include <ostream>
#include <string>

namespace avc {

// Protocol version of the JSON event stream (bumped only on breaking changes).
constexpr int kEventProtocolVersion = 1;

// JSON-escape a string body (exposed for testing).
std::string json_escape(const std::string& s);

// Reports lifecycle events. One method per event type.
class EventReporter {
 public:
  virtual ~EventReporter() = default;
  virtual void meta(const std::string& client, const std::string& version) = 0;
  virtual void ready(const std::string& channel, std::uint32_t uid) = 0;
  virtual void peer_joined(std::uint32_t uid) = 0;
  virtual void peer_left(std::uint32_t uid) = 0;
  virtual void reconnecting() = 0;
  virtual void reconnected() = 0;
  virtual void token_renewed() = 0;
  virtual void error(int code, const std::string& message) = 0;
  virtual void fatal(int code, const std::string& message) = 0;
  virtual void stopping(const std::string& reason) = 0;
};

// Emits JSONL (one object per line) to the stream, flushing after each event,
// with writes serialized by an internal mutex (events come from multiple threads).
class JsonEventReporter : public EventReporter {
 public:
  explicit JsonEventReporter(std::ostream& os);
  void meta(const std::string& client, const std::string& version) override;
  void ready(const std::string& channel, std::uint32_t uid) override;
  void peer_joined(std::uint32_t uid) override;
  void peer_left(std::uint32_t uid) override;
  void reconnecting() override;
  void reconnected() override;
  void token_renewed() override;
  void error(int code, const std::string& message) override;
  void fatal(int code, const std::string& message) override;
  void stopping(const std::string& reason) override;
 private:
  void write_line(const std::string& body);  // locks, writes, flushes
  std::ostream& os_;
  std::mutex mu_;
};

// Emits human-readable [avc] lines (same mutex + flush treatment).
class HumanEventReporter : public EventReporter {
 public:
  explicit HumanEventReporter(std::ostream& os);
  void meta(const std::string& client, const std::string& version) override;
  void ready(const std::string& channel, std::uint32_t uid) override;
  void peer_joined(std::uint32_t uid) override;
  void peer_left(std::uint32_t uid) override;
  void reconnecting() override;
  void reconnected() override;
  void token_renewed() override;
  void error(int code, const std::string& message) override;
  void fatal(int code, const std::string& message) override;
  void stopping(const std::string& reason) override;
 private:
  void write_line(const std::string& body);
  std::ostream& os_;
  std::mutex mu_;
};

}  // namespace avc
```

- [ ] **Step 2: Write the failing tests** `tests/test_event_reporter.cpp`

```cpp
#include "doctest.h"
#include "event_reporter.h"
#include <sstream>
#include <string>

using avc::JsonEventReporter;

namespace {
bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}
}  // namespace

TEST_CASE("json_escape handles quotes, backslashes, control chars") {
  CHECK(avc::json_escape("a\"b\\c") == "a\\\"b\\\\c");
  CHECK(avc::json_escape("line\nbreak\ttab") == "line\\nbreak\\ttab");
  CHECK(avc::json_escape(std::string("x\x01y")) == "x\\u0001y");
}

TEST_CASE("meta event carries protocol, client, version") {
  std::ostringstream os;
  JsonEventReporter r(os);
  r.meta("agora-voice-client", "v1.2.3");
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"meta\""));
  CHECK(contains(out, "\"protocol\":1"));
  CHECK(contains(out, "\"client\":\"agora-voice-client\""));
  CHECK(contains(out, "\"version\":\"v1.2.3\""));
  CHECK(contains(out, "\"ts\":"));
  CHECK(out.back() == '\n');
}

TEST_CASE("ready event carries channel (escaped) and uid") {
  std::ostringstream os;
  JsonEventReporter r(os);
  r.ready("room\"x", 1001);
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"ready\""));
  CHECK(contains(out, "\"channel\":\"room\\\"x\""));
  CHECK(contains(out, "\"uid\":1001"));
}

TEST_CASE("peer_joined and peer_left carry uid") {
  std::ostringstream os;
  JsonEventReporter r(os);
  r.peer_joined(42);
  r.peer_left(42);
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"peer_joined\""));
  CHECK(contains(out, "\"type\":\"peer_left\""));
  CHECK(contains(out, "\"uid\":42"));
}

TEST_CASE("error and fatal carry code and message") {
  std::ostringstream os;
  JsonEventReporter r(os);
  r.error(7, "warn");
  r.fatal(5, "boom");
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"error\""));
  CHECK(contains(out, "\"code\":7"));
  CHECK(contains(out, "\"message\":\"warn\""));
  CHECK(contains(out, "\"type\":\"fatal\""));
  CHECK(contains(out, "\"code\":5"));
  CHECK(contains(out, "\"message\":\"boom\""));
}

TEST_CASE("stopping carries reason; bare events still well-formed") {
  std::ostringstream os;
  JsonEventReporter r(os);
  r.reconnecting();
  r.reconnected();
  r.token_renewed();
  r.stopping("signal");
  const std::string out = os.str();
  CHECK(contains(out, "\"type\":\"reconnecting\""));
  CHECK(contains(out, "\"type\":\"reconnected\""));
  CHECK(contains(out, "\"type\":\"token_renewed\""));
  CHECK(contains(out, "\"type\":\"stopping\""));
  CHECK(contains(out, "\"reason\":\"signal\""));
  // four lines emitted
  CHECK(std::count(out.begin(), out.end(), '\n') == 4);
}
```

Add `#include <algorithm>` at the top of the test file for `std::count`.

- [ ] **Step 3: Add to the tests target in `CMakeLists.txt`**

The `add_executable(tests ...)` source list must include `tests/test_event_reporter.cpp` and `src/event_reporter.cpp` (keep all existing entries). Result:

```cmake
add_executable(tests
  tests/doctest_main.cpp
  tests/test_smoke.cpp
  tests/test_config.cpp
  tests/test_token.cpp
  tests/test_event_reporter.cpp
  src/config.cpp
  src/token.cpp
  src/event_reporter.cpp
  third_party/agora-token/cpp/src/AccessToken2.cpp
)
```
(If `AccessToken2.cpp` is not currently listed because the token lib is header-only, do not add it — match the existing list and just append `tests/test_event_reporter.cpp` and `src/event_reporter.cpp`.)

- [ ] **Step 4: Run tests to verify they fail**

Run: `cmake -S . -B build && cmake --build build && ./build/tests`
Expected: link error (no `avc::json_escape` / `JsonEventReporter`) or failures.

- [ ] **Step 5: Implement `src/event_reporter.cpp`**

```cpp
#include "event_reporter.h"
#include <chrono>
#include <cstdio>
#include <sstream>

namespace avc {
namespace {

long long now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

// ---- JsonEventReporter ----
JsonEventReporter::JsonEventReporter(std::ostream& os) : os_(os) {}

void JsonEventReporter::write_line(const std::string& body) {
  std::lock_guard<std::mutex> lock(mu_);
  os_ << body << "\n";
  os_.flush();
}

void JsonEventReporter::meta(const std::string& client, const std::string& version) {
  std::ostringstream b;
  b << "{\"type\":\"meta\",\"ts\":" << now_ms()
    << ",\"protocol\":" << kEventProtocolVersion
    << ",\"client\":\"" << json_escape(client) << "\""
    << ",\"version\":\"" << json_escape(version) << "\"}";
  write_line(b.str());
}

void JsonEventReporter::ready(const std::string& channel, std::uint32_t uid) {
  std::ostringstream b;
  b << "{\"type\":\"ready\",\"ts\":" << now_ms()
    << ",\"channel\":\"" << json_escape(channel) << "\",\"uid\":" << uid << "}";
  write_line(b.str());
}

void JsonEventReporter::peer_joined(std::uint32_t uid) {
  std::ostringstream b;
  b << "{\"type\":\"peer_joined\",\"ts\":" << now_ms() << ",\"uid\":" << uid << "}";
  write_line(b.str());
}

void JsonEventReporter::peer_left(std::uint32_t uid) {
  std::ostringstream b;
  b << "{\"type\":\"peer_left\",\"ts\":" << now_ms() << ",\"uid\":" << uid << "}";
  write_line(b.str());
}

void JsonEventReporter::reconnecting() {
  std::ostringstream b;
  b << "{\"type\":\"reconnecting\",\"ts\":" << now_ms() << "}";
  write_line(b.str());
}

void JsonEventReporter::reconnected() {
  std::ostringstream b;
  b << "{\"type\":\"reconnected\",\"ts\":" << now_ms() << "}";
  write_line(b.str());
}

void JsonEventReporter::token_renewed() {
  std::ostringstream b;
  b << "{\"type\":\"token_renewed\",\"ts\":" << now_ms() << "}";
  write_line(b.str());
}

void JsonEventReporter::error(int code, const std::string& message) {
  std::ostringstream b;
  b << "{\"type\":\"error\",\"ts\":" << now_ms()
    << ",\"code\":" << code << ",\"message\":\"" << json_escape(message) << "\"}";
  write_line(b.str());
}

void JsonEventReporter::fatal(int code, const std::string& message) {
  std::ostringstream b;
  b << "{\"type\":\"fatal\",\"ts\":" << now_ms()
    << ",\"code\":" << code << ",\"message\":\"" << json_escape(message) << "\"}";
  write_line(b.str());
}

void JsonEventReporter::stopping(const std::string& reason) {
  std::ostringstream b;
  b << "{\"type\":\"stopping\",\"ts\":" << now_ms()
    << ",\"reason\":\"" << json_escape(reason) << "\"}";
  write_line(b.str());
}

// ---- HumanEventReporter ----
HumanEventReporter::HumanEventReporter(std::ostream& os) : os_(os) {}

void HumanEventReporter::write_line(const std::string& body) {
  std::lock_guard<std::mutex> lock(mu_);
  os_ << "[avc] " << body << "\n";
  os_.flush();
}

void HumanEventReporter::meta(const std::string& client, const std::string& version) {
  write_line(client + " " + version);
}
void HumanEventReporter::ready(const std::string& channel, std::uint32_t uid) {
  std::ostringstream b; b << "ready channel=" << channel << " uid=" << uid; write_line(b.str());
}
void HumanEventReporter::peer_joined(std::uint32_t uid) {
  std::ostringstream b; b << "peer joined uid=" << uid; write_line(b.str());
}
void HumanEventReporter::peer_left(std::uint32_t uid) {
  std::ostringstream b; b << "peer left uid=" << uid; write_line(b.str());
}
void HumanEventReporter::reconnecting() { write_line("reconnecting"); }
void HumanEventReporter::reconnected() { write_line("reconnected"); }
void HumanEventReporter::token_renewed() { write_line("token renewed"); }
void HumanEventReporter::error(int code, const std::string& message) {
  std::ostringstream b; b << "error code=" << code << " " << message; write_line(b.str());
}
void HumanEventReporter::fatal(int code, const std::string& message) {
  std::ostringstream b; b << "fatal code=" << code << " " << message; write_line(b.str());
}
void HumanEventReporter::stopping(const std::string& reason) {
  write_line("stopping reason=" + reason);
}

}  // namespace avc
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build && ./build/tests`
Expected: `[doctest] Status: SUCCESS!` with the new event-reporter cases passing.

- [ ] **Step 7: Commit**

```bash
git add src/event_reporter.h src/event_reporter.cpp tests/test_event_reporter.cpp CMakeLists.txt
git commit -m "feat: event_reporter (JSONL + human), flush + mutex"
```

---

## Task 2: Engine callbacks for reconnect/error

**Files:**
- Modify: `src/voice_engine.h`
- Modify: `src/voice_engine_agora.mm`

- [ ] **Step 1: Add callbacks to `VoiceEngineCallbacks` in `src/voice_engine.h`**

Inside the `struct VoiceEngineCallbacks { … }`, add three members (place after `on_user_left`):

```cpp
  std::function<void()> on_reconnecting;                  // connection dropped, retrying
  std::function<void()> on_reconnected;                   // connection restored
  std::function<void(int code)> on_error;                 // non-fatal SDK error
```

- [ ] **Step 2: Wire them in `src/voice_engine_agora.mm` and drop delegate logging**

In `@implementation AvcDelegate`, add an ivar to track reconnection state. The ivar block (currently `@public VoiceEngineCallbacks _cb;`) becomes:

```objcpp
@implementation AvcDelegate {
@public
  VoiceEngineCallbacks _cb;
  BOOL _reconnecting;
}
```

Replace the `didJoinChannel`, `tokenPrivilegeWillExpire`, `connectionChangedToState`, and `didOccurError` methods so they invoke callbacks instead of `fprintf` (keep `didJoinedOfUid`/`didOfflineOfUid` as they are — they already only call callbacks):

```objcpp
- (void)rtcEngine:(AgoraRtcEngineKit *)engine
    didJoinChannel:(NSString *)channel withUid:(NSUInteger)uid elapsed:(NSInteger)elapsed {
  if (_cb.on_joined) _cb.on_joined();
}

- (void)rtcEngine:(AgoraRtcEngineKit *)engine tokenPrivilegeWillExpire:(NSString *)token {
  if (_cb.on_token_will_expire) _cb.on_token_will_expire();
}

- (void)rtcEngine:(AgoraRtcEngineKit *)engine
    connectionChangedToState:(AgoraConnectionState)state
    reason:(AgoraConnectionChangedReason)reason {
  if (state == AgoraConnectionStateReconnecting) {
    _reconnecting = YES;
    if (_cb.on_reconnecting) _cb.on_reconnecting();
  } else if (state == AgoraConnectionStateConnected) {
    if (_reconnecting) {
      _reconnecting = NO;
      if (_cb.on_reconnected) _cb.on_reconnected();
    }
  } else if (state == AgoraConnectionStateFailed) {
    if (_cb.on_fatal) _cb.on_fatal((int)reason);
  }
}

- (void)rtcEngine:(AgoraRtcEngineKit *)engine didOccurError:(AgoraErrorCode)errorCode {
  if (_cb.on_error) _cb.on_error((int)errorCode);
}
```

IMPORTANT: verify the exact enum spellings against the headers — they were confirmed earlier as `AgoraConnectionStateFailed`; check `AgoraConnectionStateReconnecting` and `AgoraConnectionStateConnected` with:
`grep -n "AgoraConnectionState" third_party/agora/AgoraRtcKit.framework/Headers/AgoraEnumerates.h`
Use the real names if they differ.

Leave the `start()`/`stop()` methods and their internal failure `fprintf`s (already-started guard, bad-utf8 guard, joinChannel-failed) unchanged — those are synchronous start failures `main` reports as a `fatal` event; their stderr detail is harmless and aids debugging.

- [ ] **Step 3: Compile the engine against the framework (compile-only)**

Run:
```bash
clang++ -std=c++17 -fobjc-arc -x objective-c++ -Isrc -Fthird_party/agora \
  -c src/voice_engine_agora.mm -o /tmp/ve.o && echo "COMPILE OK"
rm -f /tmp/ve.o
```
Expected: `COMPILE OK`. Also confirm tests still build/pass: `cmake --build build && ./build/tests`.

- [ ] **Step 4: Commit**

```bash
git add src/voice_engine.h src/voice_engine_agora.mm
git commit -m "feat: engine reconnect/error callbacks; engine no longer logs directly"
```

---

## Task 3: Wire `--json` and the reporter into `main`

**Files:**
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt` (app target)

- [ ] **Step 1: Add `event_reporter.cpp` to the app target in `CMakeLists.txt`**

In `add_executable(agora-voice-client …)`, add `src/event_reporter.cpp` to the source list (keep all existing sources). For example it becomes:

```cmake
add_executable(agora-voice-client
  src/main.cpp
  src/config.cpp
  src/token.cpp
  src/event_reporter.cpp
  src/voice_engine_agora.mm
)
```

- [ ] **Step 2: Replace `src/main.cpp` with the reporter-driven version**

```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "event_reporter.h"
#include "token.h"
#include "voice_engine.h"

#ifndef AVC_VERSION_STR
#define AVC_VERSION_STR "dev"
#endif

namespace {
std::atomic<bool> g_stop{false};
void handle_signal(int) { g_stop.store(true); }
}  // namespace

int main(int argc, char** argv) {
  bool json_mode = false;
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--version") {
      std::cout << "agora-voice-client " << AVC_VERSION_STR << "\n";
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "usage: agora-voice-client --channel <name> [--uid <n>] "
                   "[--token-ttl <seconds>] [--json] [--version]\n";
      return 0;
    }
    if (arg == "--json") { json_mode = true; continue; }
    args.push_back(arg);
  }

  std::unique_ptr<avc::EventReporter> reporter;
  if (json_mode) reporter = std::make_unique<avc::JsonEventReporter>(std::cout);
  else reporter = std::make_unique<avc::HumanEventReporter>(std::cerr);

  reporter->meta("agora-voice-client", AVC_VERSION_STR);

  std::map<std::string, std::string> env;
  for (const char* name : {"AGORA_APP_ID", "AGORA_APP_CERTIFICATE", "AGORA_TOKEN"}) {
    if (const char* v = std::getenv(name)) env[name] = v;
  }

  avc::ClientConfig cfg;
  try {
    cfg = avc::parse_config(env, args);
  } catch (const std::exception& e) {
    reporter->fatal(2, e.what());
    return 2;
  }

  auto mint = [&]() {
    return avc::build_rtc_token(cfg.app_id, cfg.app_certificate, cfg.channel,
                                cfg.uid, cfg.token_ttl);
  };

  std::string token;
  try {
    switch (cfg.token_source) {
      case avc::TokenSource::Explicit: token = cfg.explicit_token; break;
      case avc::TokenSource::Mint:     token = mint(); break;
      case avc::TokenSource::None:     token.clear(); break;
    }
  } catch (const std::exception& e) {
    reporter->fatal(1, e.what());
    return 1;
  }

  auto engine = avc::make_voice_engine();
  std::atomic<int> fatal_code{0};

  avc::VoiceEngineCallbacks cb;
  cb.on_joined        = [&] { reporter->ready(cfg.channel, cfg.uid); };
  cb.on_user_joined   = [&](std::uint32_t uid) { reporter->peer_joined(uid); };
  cb.on_user_left     = [&](std::uint32_t uid) { reporter->peer_left(uid); };
  cb.on_reconnecting  = [&] { reporter->reconnecting(); };
  cb.on_reconnected   = [&] { reporter->reconnected(); };
  cb.on_error         = [&](int code) { reporter->error(code, ""); };
  cb.on_token_will_expire = [&] {
    if (cfg.token_source == avc::TokenSource::Mint) {
      engine->renew_token(mint());
      reporter->token_renewed();
    }
  };
  cb.on_fatal = [&](int code) {
    fatal_code.store(code ? code : 1);
    reporter->fatal(code, "unrecoverable connection failure");
    g_stop.store(true);
  };

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  if (!engine->start(cfg, token, cb)) {
    reporter->fatal(1, "failed to start");
    return 1;
  }

  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  reporter->stopping(fatal_code.load() ? "fatal" : "signal");
  engine->stop();
  return fatal_code.load();
}
```

- [ ] **Step 3: Build**

Run:
```bash
test -d third_party/agora/AgoraRtcKit.framework || ./scripts/fetch-sdk.sh
cmake -S . -B build && cmake --build build && ./build/tests
```
Expected: builds; `./build/tests` → SUCCESS (15+ cases).

- [ ] **Step 4: Verify the `--json` machine contract on the testable paths**

Run:
```bash
echo "--- json config-error path ---"
env -u AGORA_APP_ID ./build/agora-voice-client --json --channel room1 1>/tmp/out.json 2>/dev/null ; echo "exit=$?"
cat /tmp/out.json
```
Expected: `exit=2`, and `/tmp/out.json` contains a `{"type":"meta",...}` line **then** a `{"type":"fatal",...,"code":2,...}` line on stdout (nothing else). Confirm each is valid JSON:
```bash
while IFS= read -r line; do echo "$line" | python3 -c "import sys,json; json.loads(sys.stdin.read()); print('ok')"; done < /tmp/out.json
```
Expected: `ok` for each line.

- [ ] **Step 5: Verify human mode unchanged-ish and `--version` stays human**

Run:
```bash
env -u AGORA_APP_ID ./build/agora-voice-client --channel room1 1>/dev/null 2>/tmp/err.txt ; echo "exit=$?"
grep -q "^\[avc\]" /tmp/err.txt && echo "HUMAN_STDERR_OK"
./build/agora-voice-client --json --version ; echo "version_exit=$?"
```
Expected: `exit=2`; `HUMAN_STDERR_OK` (human `[avc]` lines on stderr, stdout empty); `--json --version` prints `agora-voice-client <ver>` (human, not JSON) with `version_exit=0`.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp CMakeLists.txt
git commit -m "feat: --json event-stream mode wired through main"
```

---

## Task 4: Document the contract

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add a "JSON event stream" section to `README.md`**

Insert this section (after the "Run" section):

````markdown
## Machine mode (`--json`)

Run with `--json` to drive the client from another program. It emits one JSON object
per line on **stdout** (human/debug logs stay on **stderr**):

```bash
./agora-voice-client --json --channel <room> --uid <N> | jq .
```

Event types: `meta` (first; carries `protocol`, `client`, `version`), `ready`
(`channel`, `uid`), `peer_joined`/`peer_left` (`uid`), `reconnecting`, `reconnected`,
`token_renewed`, `error` (`code`, `message`), `fatal` (`code`, `message`), `stopping`
(`reason`). Every line also has a `ts` (Unix ms).

Each run ends with exactly one terminal event matching the exit code: `fatal` for
pre-session failures (exit 1/2), or `stopping` for an established session ended by a
signal (`reason:"signal"`, exit 0) or a runtime failure (`fatal` then
`stopping reason:"fatal"`).

Compatibility: within a `protocol` version, changes are additive — **ignore unknown event
types and fields**. A breaking change bumps `protocol`. See `docs/adr/0006-cli-integration-subprocess-jsonl.md`.
````

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: document the --json event-stream contract"
```

---

## Self-Review

**Spec coverage:**
- `--json` flag toggling machine mode → Task 3. ✓
- stdout purity (only reporter writes stdout; engine stops logging) → Task 2 (drop delegate fprintf) + Task 3 (reporter owns output). ✓
- Flush per event + internal mutex → Task 1 (`write_line` locks + flushes). ✓
- Event schema incl. `meta` first, `ready`, `peer_joined/left`, `reconnecting/reconnected`, `token_renewed`, `error`, `fatal`, `stopping`, `ts` → Tasks 1 & 3. ✓
- Terminal-event guarantee (fatal pre-session; stopping for established; one per run) → Task 3 (config/token/start → fatal; loop end → stopping with reason). ✓
- Forward-compat (`protocol` + additive/ignore-unknowns) → Task 1 (`kEventProtocolVersion`, `meta`) + Task 4 (documented). ✓
- `--version`/`--help` stay human under `--json` → Task 3 (handled before reporter built). ✓
- Engine new callbacks `on_reconnecting`/`on_reconnected`/`on_error` → Task 2. ✓
- Unit tests (escaping exact; per-event content) + manual (`jq`, config-error path) → Task 1 + Task 3 Steps 4–5 + Task 4. ✓

**Placeholder scan:** No TBD/TODO. Every code step shows full content. The only "verify against headers" instruction (Task 2 enum names) is paired with the exact grep to run; `AgoraConnectionStateFailed` was already confirmed in prior work.

**Type/name consistency:** `EventReporter` method names (`meta`, `ready`, `peer_joined`, `peer_left`, `reconnecting`, `reconnected`, `token_renewed`, `error`, `fatal`, `stopping`) are identical across `event_reporter.h`, the `.cpp`, the tests, and `main.cpp`'s callback wiring. `VoiceEngineCallbacks` additions (`on_reconnecting`, `on_reconnected`, `on_error`) match between `voice_engine.h`, the `.mm` invocations, and `main.cpp`. `kEventProtocolVersion` used in `meta`. `AVC_VERSION_STR` reused for the `meta` version field.
