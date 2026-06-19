# Agora Voice Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a self-hosted macOS (arm64) command-line binary that joins an Agora voice channel as a broadcaster, goes live on the real mic/speakers with echo cancellation, and stays connected until Ctrl-C — for talking to a remote AI Agent.

**Architecture:** Pure-C++ core (`config`, `token`, `main`) behind a `VoiceEngine` interface; the macOS engine implementation is Objective-C++ (`voice_engine_agora.mm`) against `AgoraRtcEngineKit`. Tokens are minted locally from the App Certificate (HMAC via CommonCrypto). See `docs/superpowers/specs/2026-06-19-agora-voice-client-design.md` and ADRs 0001–0005.

**Tech Stack:** C++17, Objective-C++, CMake, doctest (vendored, single-header), Agora Voice SDK 4.x for macOS (`AgoraRtcKit.framework`), vendored Agora AccessToken2 builder, CommonCrypto, codesign.

---

## File Structure

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Build config: `tests` exe (no SDK) + `agora-voice-client` exe (links framework, embeds Info.plist, ad-hoc signs) |
| `src/config.h` / `src/config.cpp` | Parse + validate env/flags → `ClientConfig`; decide token source. Pure C++. |
| `src/token.h` / `src/token.cpp` | `hmac_sha256()` (CommonCrypto) + `build_rtc_token()` (wraps vendored builder). Pure C++. |
| `src/voice_engine.h` | Pure-C++ `VoiceEngine` interface + `VoiceEngineCallbacks` + `make_voice_engine()` factory. |
| `src/voice_engine_agora.mm` | macOS Obj-C++ implementation: `AgoraRtcEngineKit` + delegate; logging. |
| `src/main.cpp` | Wire everything; signals; token-renewal loop. Pure C++. |
| `third_party/doctest/doctest.h` | Vendored test framework (committed). |
| `third_party/agora-token/` | Vendored Agora AccessToken2 builder source (committed; HMAC swapped to CommonCrypto). |
| `third_party/agora/` | Fetched `AgoraRtcKit.framework` + headers (gitignored). |
| `scripts/fetch-sdk.sh` | Download pinned Voice SDK, verify checksum, place framework. |
| `scripts/package.sh` | Produce the release tarball (binary + framework). |
| `tests/doctest_main.cpp` | doctest entry point. |
| `tests/test_config.cpp` / `tests/test_token.cpp` | Unit tests. |
| `Info.plist` / `entitlements.plist` | Mic usage string + audio-input entitlement. |
| `README.md` | Get SDK, set env, run, first-run-at-Mac note, manual integration checklist. |
| `.gitignore` | Ignore `third_party/agora/`, `build/`. |

---

## Task 1: Project scaffold + test harness

**Files:**
- Create: `CMakeLists.txt`, `.gitignore`, `third_party/doctest/doctest.h`, `tests/doctest_main.cpp`, `tests/test_smoke.cpp`

- [ ] **Step 1: Vendor doctest**

Download the single header (pinned v2.4.11):

```bash
mkdir -p third_party/doctest
curl -fsSL -o third_party/doctest/doctest.h \
  https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
```

Expected: file exists, ~7000 lines. Verify: `head -n 5 third_party/doctest/doctest.h` shows the doctest license header.

- [ ] **Step 2: Create `.gitignore`**

```gitignore
build/
third_party/agora/
*.o
.DS_Store
```

- [ ] **Step 3: Create the doctest entry point** `tests/doctest_main.cpp`

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

- [ ] **Step 4: Write a smoke test** `tests/test_smoke.cpp`

```cpp
#include "doctest.h"

TEST_CASE("test harness works") {
  CHECK(1 + 1 == 2);
}
```

- [ ] **Step 5: Create `CMakeLists.txt`** (tests target only for now; the app target is added in Task 9)

```cmake
cmake_minimum_required(VERSION 3.20)
project(agora_voice_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Tests (pure C++, no Agora RTC SDK needed) ----
add_executable(tests
  tests/doctest_main.cpp
  tests/test_smoke.cpp
)
target_include_directories(tests PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/doctest
  ${CMAKE_SOURCE_DIR}/src
)

enable_testing()
add_test(NAME tests COMMAND tests)
```

- [ ] **Step 6: Configure, build, run**

Run:
```bash
cmake -S . -B build && cmake --build build && ./build/tests
```
Expected: build succeeds; output ends with `[doctest] Status: SUCCESS!`.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt .gitignore third_party/doctest tests
git commit -m "chore: scaffold CMake build and doctest harness"
```

---

## Task 2: `config` — parse and validate (TDD)

**Files:**
- Create: `src/config.h`, `src/config.cpp`, `tests/test_config.cpp`
- Modify: `CMakeLists.txt` (add `src/config.cpp` + `tests/test_config.cpp` to `tests`)

- [ ] **Step 1: Write `src/config.h`** (defines the types the tests use)

```cpp
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace avc {

enum class TokenSource { Explicit, Mint, None };

struct ClientConfig {
  std::string app_id;
  std::string channel;
  std::uint32_t uid = 0;
  std::uint32_t token_ttl = 3600;
  TokenSource token_source = TokenSource::None;
  std::string explicit_token;   // set when token_source == Explicit
  std::string app_certificate;  // set when token_source == Mint
};

// Parse from environment variables and CLI args (argv WITHOUT argv[0]).
// Throws std::runtime_error (message is usage text) on missing required fields
// or malformed flag values.
ClientConfig parse_config(const std::map<std::string, std::string>& env,
                          const std::vector<std::string>& args);

}  // namespace avc
```

- [ ] **Step 2: Write the failing tests** `tests/test_config.cpp`

```cpp
#include "doctest.h"
#include "config.h"
#include <stdexcept>

using avc::parse_config;
using avc::TokenSource;

TEST_CASE("missing app id throws") {
  CHECK_THROWS_AS(parse_config({}, {"--channel", "room1"}), std::runtime_error);
}

TEST_CASE("missing channel throws") {
  CHECK_THROWS_AS(parse_config({{"AGORA_APP_ID", "app"}}, {}), std::runtime_error);
}

TEST_CASE("explicit token wins") {
  auto c = parse_config({{"AGORA_APP_ID", "app"}, {"AGORA_TOKEN", "tok"},
                         {"AGORA_APP_CERTIFICATE", "cert"}},
                        {"--channel", "room1"});
  CHECK(c.token_source == TokenSource::Explicit);
  CHECK(c.explicit_token == "tok");
}

TEST_CASE("certificate without token means mint") {
  auto c = parse_config({{"AGORA_APP_ID", "app"}, {"AGORA_APP_CERTIFICATE", "cert"}},
                        {"--channel", "room1"});
  CHECK(c.token_source == TokenSource::Mint);
  CHECK(c.app_certificate == "cert");
}

TEST_CASE("no token and no certificate means none") {
  auto c = parse_config({{"AGORA_APP_ID", "app"}}, {"--channel", "room1"});
  CHECK(c.token_source == TokenSource::None);
}

TEST_CASE("uid defaults to 0 and parses when given") {
  auto def = parse_config({{"AGORA_APP_ID", "app"}}, {"--channel", "room1"});
  CHECK(def.uid == 0);
  auto set = parse_config({{"AGORA_APP_ID", "app"}},
                          {"--channel", "room1", "--uid", "1001"});
  CHECK(set.uid == 1001);
}

TEST_CASE("token-ttl defaults to 3600 and parses when given") {
  auto def = parse_config({{"AGORA_APP_ID", "app"}}, {"--channel", "room1"});
  CHECK(def.token_ttl == 3600);
  auto set = parse_config({{"AGORA_APP_ID", "app"}},
                          {"--channel", "room1", "--token-ttl", "600"});
  CHECK(set.token_ttl == 600);
}

TEST_CASE("malformed uid throws") {
  CHECK_THROWS_AS(
      parse_config({{"AGORA_APP_ID", "app"}}, {"--channel", "room1", "--uid", "abc"}),
      std::runtime_error);
}
```

- [ ] **Step 3: Add `config` to the tests target in `CMakeLists.txt`**

```cmake
add_executable(tests
  tests/doctest_main.cpp
  tests/test_smoke.cpp
  tests/test_config.cpp
  src/config.cpp
)
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `cmake --build build && ./build/tests`
Expected: link error (no `parse_config`) or test failures.

- [ ] **Step 5: Implement** `src/config.cpp`

```cpp
#include "config.h"
#include <stdexcept>
#include <string>

namespace avc {
namespace {

const char* kUsage =
    "usage: agora-voice-client --channel <name> [--uid <n>] [--token-ttl <seconds>]\n"
    "  env: AGORA_APP_ID (required), AGORA_APP_CERTIFICATE or AGORA_TOKEN (optional)\n";

std::uint32_t parse_u32(const std::string& s, const char* what) {
  try {
    size_t pos = 0;
    unsigned long v = std::stoul(s, &pos);
    if (pos != s.size()) throw std::invalid_argument(what);
    return static_cast<std::uint32_t>(v);
  } catch (const std::exception&) {
    throw std::runtime_error(std::string("invalid value for ") + what + "\n" + kUsage);
  }
}

std::string get(const std::map<std::string, std::string>& env, const char* key) {
  auto it = env.find(key);
  return it == env.end() ? std::string() : it->second;
}

}  // namespace

ClientConfig parse_config(const std::map<std::string, std::string>& env,
                          const std::vector<std::string>& args) {
  ClientConfig c;
  c.app_id = get(env, "AGORA_APP_ID");

  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= args.size())
        throw std::runtime_error(std::string("missing value for ") + a + "\n" + kUsage);
      return args[++i];
    };
    if (a == "--channel") c.channel = next();
    else if (a == "--uid") c.uid = parse_u32(next(), "--uid");
    else if (a == "--token-ttl") c.token_ttl = parse_u32(next(), "--token-ttl");
    else throw std::runtime_error(std::string("unknown flag: ") + a + "\n" + kUsage);
  }

  if (c.app_id.empty())
    throw std::runtime_error(std::string("AGORA_APP_ID is required\n") + kUsage);
  if (c.channel.empty())
    throw std::runtime_error(std::string("--channel is required\n") + kUsage);

  const std::string explicit_token = get(env, "AGORA_TOKEN");
  const std::string cert = get(env, "AGORA_APP_CERTIFICATE");
  if (!explicit_token.empty()) {
    c.token_source = TokenSource::Explicit;
    c.explicit_token = explicit_token;
  } else if (!cert.empty()) {
    c.token_source = TokenSource::Mint;
    c.app_certificate = cert;
  } else {
    c.token_source = TokenSource::None;
  }
  return c;
}

}  // namespace avc
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build && ./build/tests`
Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 7: Commit**

```bash
git add src/config.h src/config.cpp tests/test_config.cpp CMakeLists.txt
git commit -m "feat: config parsing and validation"
```

---

## Task 3: `hmac_sha256` via CommonCrypto (TDD)

**Files:**
- Create: `src/token.h`, `src/token.cpp`, `tests/test_token.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write `src/token.h`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace avc {

// HMAC-SHA256; returns the 32-byte MAC.
std::vector<unsigned char> hmac_sha256(const std::vector<unsigned char>& key,
                                       const std::vector<unsigned char>& message);

// Builds an Agora AccessToken2 RTC token bound to `uid`, valid for `ttl_seconds`.
std::string build_rtc_token(const std::string& app_id,
                            const std::string& app_certificate,
                            const std::string& channel,
                            std::uint32_t uid,
                            std::uint32_t ttl_seconds);

}  // namespace avc
```

- [ ] **Step 2: Write the failing test** `tests/test_token.cpp` (RFC 4231 Test Case 2 — a known HMAC-SHA256 vector)

```cpp
#include "doctest.h"
#include "token.h"
#include <string>
#include <vector>

namespace {
std::vector<unsigned char> bytes(const std::string& s) {
  return std::vector<unsigned char>(s.begin(), s.end());
}
std::string hex(const std::vector<unsigned char>& v) {
  static const char* d = "0123456789abcdef";
  std::string out;
  for (unsigned char c : v) { out += d[c >> 4]; out += d[c & 0xf]; }
  return out;
}
}  // namespace

TEST_CASE("hmac_sha256 matches RFC 4231 test case 2") {
  auto mac = avc::hmac_sha256(bytes("Jefe"), bytes("what do ya want for nothing?"));
  CHECK(hex(mac) ==
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}
```

- [ ] **Step 3: Add to `CMakeLists.txt`** (`src/token.cpp` + `tests/test_token.cpp`; link CommonCrypto is implicit on macOS)

```cmake
add_executable(tests
  tests/doctest_main.cpp
  tests/test_smoke.cpp
  tests/test_config.cpp
  tests/test_token.cpp
  src/config.cpp
  src/token.cpp
)
```

- [ ] **Step 4: Run to verify failure**

Run: `cmake --build build && ./build/tests`
Expected: link error (no `hmac_sha256`).

- [ ] **Step 5: Implement `hmac_sha256` in `src/token.cpp`** (leave `build_rtc_token` for Task 4)

```cpp
#include "token.h"
#include <CommonCrypto/CommonHMAC.h>

namespace avc {

std::vector<unsigned char> hmac_sha256(const std::vector<unsigned char>& key,
                                       const std::vector<unsigned char>& message) {
  std::vector<unsigned char> out(CC_SHA256_DIGEST_LENGTH);
  CCHmac(kCCHmacAlgSHA256,
         key.empty() ? nullptr : key.data(), key.size(),
         message.empty() ? nullptr : message.data(), message.size(),
         out.data());
  return out;
}

// build_rtc_token is implemented in Task 4.

}  // namespace avc
```

- [ ] **Step 6: Run to verify pass**

Run: `cmake --build build && ./build/tests`
Expected: `SUCCESS!`

- [ ] **Step 7: Commit**

```bash
git add src/token.h src/token.cpp tests/test_token.cpp CMakeLists.txt
git commit -m "feat: HMAC-SHA256 via CommonCrypto"
```

---

## Task 4: Vendor AccessToken2 builder + `build_rtc_token`

**Files:**
- Create: `third_party/agora-token/...` (vendored), append to `src/token.cpp`, append to `tests/test_token.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Vendor the AccessToken2 builder source**

Clone the pinned tools repo and copy the C++ token sources in, preserving the `cpp/src/` path the headers `#include` (`#include "cpp/src/AccessToken2.h"`):

```bash
git clone --depth 1 https://github.com/AgoraIO/Tools /tmp/agora-tools
mkdir -p third_party/agora-token/cpp/src
cp /tmp/agora-tools/DynamicKey/AgoraDynamicKey/cpp/src/AccessToken2.h   third_party/agora-token/cpp/src/
cp /tmp/agora-tools/DynamicKey/AgoraDynamicKey/cpp/src/AccessToken2.cpp third_party/agora-token/cpp/src/
cp /tmp/agora-tools/DynamicKey/AgoraDynamicKey/cpp/src/RtcTokenBuilder2.h third_party/agora-token/cpp/src/
```

Then list the directory (`ls third_party/agora-token/cpp/src`) to confirm the three files are present. If `AccessToken2.cpp` `#include`s additional local utility headers (e.g. a packer/util header in the same `src/` dir), copy those too until it compiles in Step 4.

- [ ] **Step 2: Remove the OpenSSL dependency from the vendored builder**

Open `third_party/agora-token/cpp/src/AccessToken2.cpp`. Find the OpenSSL HMAC usage — search for `#include <openssl` and the `HMAC(` call (it lives in a small signing helper that takes a key string and a message string and returns the raw 32-byte MAC as a `std::string`). Replace the include and the call so it uses our `hmac_sha256`:

Replace the include line:
```cpp
// #include <openssl/hmac.h>   <-- delete this
#include "token.h"             // our hmac_sha256
```

Replace the body of the HMAC helper (it currently calls OpenSSL's `HMAC(EVP_sha256(), ...)`) with:
```cpp
  // key and message are std::string holding raw bytes
  std::vector<unsigned char> k(key.begin(), key.end());
  std::vector<unsigned char> m(message.begin(), message.end());
  std::vector<unsigned char> mac = avc::hmac_sha256(k, m);
  return std::string(mac.begin(), mac.end());
```
Keep the helper's existing signature and the `return`-type (raw-bytes `std::string`) intact — only its body and the include change. If the helper is a static function, add `#include <vector>` at the top of the file.

- [ ] **Step 3: Implement `build_rtc_token`** — append to `src/token.cpp`

```cpp
// ---- appended to src/token.cpp ----
#include "cpp/src/RtcTokenBuilder2.h"   // vendored; resolved via include dir in CMake

namespace avc {

std::string build_rtc_token(const std::string& app_id,
                            const std::string& app_certificate,
                            const std::string& channel,
                            std::uint32_t uid,
                            std::uint32_t ttl_seconds) {
  // Role_Publisher: the Client always publishes its mic (ADR-0004).
  return agora::tools::RtcTokenBuilder2::BuildTokenWithUid(
      app_id, app_certificate, channel, uid,
      agora::tools::Role_Publisher, ttl_seconds, ttl_seconds);
}

}  // namespace avc
```

- [ ] **Step 4: Wire the vendored sources into `CMakeLists.txt`**

Add the vendored `.cpp` to the tests target and expose the include root so `#include "cpp/src/..."` resolves:

```cmake
add_executable(tests
  tests/doctest_main.cpp
  tests/test_smoke.cpp
  tests/test_config.cpp
  tests/test_token.cpp
  src/config.cpp
  src/token.cpp
  third_party/agora-token/cpp/src/AccessToken2.cpp
)
target_include_directories(tests PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/doctest
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party/agora-token
)
```

- [ ] **Step 5: Write the failing smoke test** — append to `tests/test_token.cpp`

```cpp
TEST_CASE("build_rtc_token returns an AccessToken2 string") {
  std::string t = avc::build_rtc_token(
      "0123456789abcdef0123456789abcdef",   // 32-char app id shape
      "0123456789abcdef0123456789abcdef",   // 32-char certificate shape
      "room1", 1001, 3600);
  CHECK(t.rfind("007", 0) == 0);  // AccessToken2 version prefix
  CHECK(t.size() > 20);
}
```

- [ ] **Step 6: Run to verify pass** (build first — compiles vendored sources)

Run: `cmake -S . -B build && cmake --build build && ./build/tests`
Expected: `SUCCESS!`. If compilation fails on a missing vendored header, copy it per Step 1's note and re-run.

- [ ] **Step 7: Commit**

```bash
git add third_party/agora-token src/token.cpp tests/test_token.cpp CMakeLists.txt
git commit -m "feat: local Agora AccessToken2 minting (CommonCrypto HMAC)"
```

---

## Task 5: `fetch-sdk.sh` — acquire the Agora Voice SDK

**Files:**
- Create: `scripts/fetch-sdk.sh`

- [ ] **Step 1: Look up the pinned SDK coordinates**

Open the Agora downloads page <https://docs.agora.io/en/sdks?platform=macos> (or the Voice SDK "More versions" page). Find the latest **4.x Voice SDK for macOS** dynamic package. Note its **direct `download.agora.io` zip URL** and version. Compute the checksum after first download (Step 3 prints it). These three values fill the variables at the top of the script.

- [ ] **Step 2: Create `scripts/fetch-sdk.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail

# Pinned Agora Voice SDK for macOS (4.x). Fill these from the Agora downloads page.
SDK_VERSION="4.x.y"                 # e.g. 4.3.2
SDK_URL="https://download.agora.io/sdk/release/Agora_Native_SDK_for_Mac_v${SDK_VERSION}_VOICE_Dynamic.zip"
SDK_SHA256="PUT_SHA256_HERE"        # see Step 3

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/agora"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading Agora Voice SDK $SDK_VERSION ..."
curl -fSL -o "$TMP/sdk.zip" "$SDK_URL"

if [ "$SDK_SHA256" != "PUT_SHA256_HERE" ]; then
  echo "$SDK_SHA256  $TMP/sdk.zip" | shasum -a 256 -c -
else
  echo "WARNING: checksum not pinned. Computed sha256:"
  shasum -a 256 "$TMP/sdk.zip"
fi

unzip -q "$TMP/sdk.zip" -d "$TMP/unzipped"

# Locate AgoraRtcKit.framework anywhere in the package and copy it in.
FRAMEWORK="$(find "$TMP/unzipped" -name 'AgoraRtcKit.framework' -type d | head -n1)"
if [ -z "$FRAMEWORK" ]; then
  echo "ERROR: AgoraRtcKit.framework not found in the package" >&2
  exit 1
fi
rm -rf "$DEST"
mkdir -p "$DEST"
cp -R "$FRAMEWORK" "$DEST/"
# Copy any sibling frameworks the SDK ships (e.g. Agora*Extension.framework).
for fw in "$(dirname "$FRAMEWORK")"/*.framework; do
  [ "$fw" = "$FRAMEWORK" ] && continue
  cp -R "$fw" "$DEST/"
done
echo "Installed frameworks into $DEST:"
ls "$DEST"
```

- [ ] **Step 3: Make executable, fill the checksum, run**

```bash
chmod +x scripts/fetch-sdk.sh
./scripts/fetch-sdk.sh        # first run prints the computed sha256
```
Copy the printed sha256 into `SDK_SHA256`, then run again.
Expected: `third_party/agora/AgoraRtcKit.framework` exists. Verify: `ls third_party/agora/AgoraRtcKit.framework` shows `Headers`, `Versions`, etc.

- [ ] **Step 4: Commit** (framework itself is gitignored; only the script is committed)

```bash
git add scripts/fetch-sdk.sh
git commit -m "build: pinned Agora Voice SDK fetch script"
```

---

## Task 6: `VoiceEngine` interface

**Files:**
- Create: `src/voice_engine.h`

- [ ] **Step 1: Create `src/voice_engine.h`**

```cpp
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "config.h"

namespace avc {

struct VoiceEngineCallbacks {
  std::function<void()> on_joined;                        // joined channel ok
  std::function<void(std::uint32_t uid)> on_user_joined;  // AI Agent joined
  std::function<void(std::uint32_t uid)> on_user_left;    // AI Agent left
  std::function<void()> on_token_will_expire;             // renew needed
  std::function<void(int code)> on_fatal;                 // unrecoverable; main exits
};

class VoiceEngine {
 public:
  virtual ~VoiceEngine() = default;
  // Initialize engine, apply LIVE_BROADCASTING + BROADCASTER + speech audio + AEC,
  // and join cfg.channel as cfg.uid using `token`. Returns false on sync failure.
  virtual bool start(const ClientConfig& cfg, const std::string& token,
                     const VoiceEngineCallbacks& cb) = 0;
  virtual void renew_token(const std::string& token) = 0;
  virtual void stop() = 0;
};

std::unique_ptr<VoiceEngine> make_voice_engine();

}  // namespace avc
```

- [ ] **Step 2: Commit** (header only; implementation in Task 7)

```bash
git add src/voice_engine.h
git commit -m "feat: VoiceEngine interface"
```

---

## Task 7: macOS engine implementation (`voice_engine_agora.mm`)

**Files:**
- Create: `src/voice_engine_agora.mm`

> Not unit-testable (needs SDK + devices + network). Verified by compile/link in Task 9 and the manual integration test in Task 10.

- [ ] **Step 1: Create `src/voice_engine_agora.mm`**

```objcpp
#import <AgoraRtcKit/AgoraRtcEngineKit.h>
#include "voice_engine.h"
#include <cstdio>

using namespace avc;

// ---- Objective-C delegate bridging Agora events to C++ callbacks ----
@interface AvcDelegate : NSObject <AgoraRtcEngineDelegate>
@end

@implementation AvcDelegate {
@public
  VoiceEngineCallbacks _cb;
}

- (void)rtcEngine:(AgoraRtcEngineKit *)engine
    didJoinChannel:(NSString *)channel withUid:(NSUInteger)uid elapsed:(NSInteger)elapsed {
  fprintf(stderr, "[avc] joined channel as uid=%lu\n", (unsigned long)uid);
  if (_cb.on_joined) _cb.on_joined();
}

- (void)rtcEngine:(AgoraRtcEngineKit *)engine
    didJoinedOfUid:(NSUInteger)uid elapsed:(NSInteger)elapsed {
  if (_cb.on_user_joined) _cb.on_user_joined((std::uint32_t)uid);
}

- (void)rtcEngine:(AgoraRtcEngineKit *)engine
    didOfflineOfUid:(NSUInteger)uid reason:(AgoraUserOfflineReason)reason {
  if (_cb.on_user_left) _cb.on_user_left((std::uint32_t)uid);
}

- (void)rtcEngine:(AgoraRtcEngineKit *)engine tokenPrivilegeWillExpire:(NSString *)token {
  fprintf(stderr, "[avc] token will expire; renewing\n");
  if (_cb.on_token_will_expire) _cb.on_token_will_expire();
}

- (void)rtcEngine:(AgoraRtcEngineKit *)engine
    connectionChangedToState:(AgoraConnectionState)state
    reason:(AgoraConnectionChangedReason)reason {
  fprintf(stderr, "[avc] connection state=%ld reason=%ld\n", (long)state, (long)reason);
  if (state == AgoraConnectionStateFailed && _cb.on_fatal) _cb.on_fatal((int)reason);
}

- (void)rtcEngine:(AgoraRtcEngineKit *)engine didOccurError:(AgoraErrorCode)errorCode {
  fprintf(stderr, "[avc] error code=%ld\n", (long)errorCode);
}
@end

// ---- C++ VoiceEngine implementation ----
namespace {

class AgoraVoiceEngine : public VoiceEngine {
 public:
  ~AgoraVoiceEngine() override { stop(); }

  bool start(const ClientConfig& cfg, const std::string& token,
             const VoiceEngineCallbacks& cb) override {
    delegate_ = [[AvcDelegate alloc] init];
    delegate_->_cb = cb;

    AgoraRtcEngineConfig* config = [[AgoraRtcEngineConfig alloc] init];
    config.appId = [NSString stringWithUTF8String:cfg.app_id.c_str()];
    kit_ = [AgoraRtcEngineKit sharedEngineWithConfig:config delegate:delegate_];
    if (!kit_) return false;

    [kit_ setChannelProfile:AgoraChannelProfileLiveBroadcasting];      // ADR-0004
    [kit_ setClientRole:AgoraClientRoleBroadcaster];                   // publish mic
    // Speech-optimized mono; Default scenario keeps 3A/AEC on (ADR-0002).
    [kit_ setAudioProfile:AgoraAudioProfileSpeechStandard
                 scenario:AgoraAudioScenarioDefault];
    [kit_ enableAudio];

    AgoraRtcChannelMediaOptions* opts = [[AgoraRtcChannelMediaOptions alloc] init];
    opts.channelProfile = AgoraChannelProfileLiveBroadcasting;
    opts.clientRoleType = AgoraClientRoleBroadcaster;
    opts.publishMicrophoneTrack = YES;
    opts.autoSubscribeAudio = YES;   // hear the AI Agent (spec: auto-subscribe)

    NSString* tok = token.empty() ? nil : [NSString stringWithUTF8String:token.c_str()];
    NSString* chan = [NSString stringWithUTF8String:cfg.channel.c_str()];
    int rc = [kit_ joinChannelByToken:tok channelId:chan uid:cfg.uid
                        mediaOptions:opts joinSuccess:nil];
    if (rc != 0) {
      fprintf(stderr, "[avc] joinChannel failed rc=%d\n", rc);
      return false;
    }
    return true;
  }

  void renew_token(const std::string& token) override {
    if (kit_) [kit_ renewToken:[NSString stringWithUTF8String:token.c_str()]];
  }

  void stop() override {
    if (kit_) {
      [kit_ leaveChannel:nil];
      [AgoraRtcEngineKit destroy];
      kit_ = nil;
      delegate_ = nil;
    }
  }

 private:
  AgoraRtcEngineKit* kit_ = nil;
  AvcDelegate* delegate_ = nil;
};

}  // namespace

namespace avc {
std::unique_ptr<VoiceEngine> make_voice_engine() {
  return std::make_unique<AgoraVoiceEngine>();
}
}  // namespace avc
```

> Note on exact enum names: `AgoraAudioProfileSpeechStandard`, `AgoraAudioScenarioDefault`, `AgoraConnectionStateFailed`, and the `joinChannelByToken:...mediaOptions:` selector are from the 4.x macOS Obj-C API. If a name differs in the fetched SDK version, open `third_party/agora/AgoraRtcKit.framework/Headers/` and use the matching symbol. The acceptance criterion is unchanged: LIVE_BROADCASTING + BROADCASTER + speech-mono + **AEC enabled**.

- [ ] **Step 2: Commit** (compiles in Task 9 once linked)

```bash
git add src/voice_engine_agora.mm
git commit -m "feat: macOS Agora voice engine (Obj-C++)"
```

---

## Task 8: `main.cpp` — wiring, signals, token renewal

**Files:**
- Create: `src/main.cpp`

- [ ] **Step 1: Create `src/main.cpp`**

```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "token.h"
#include "voice_engine.h"

namespace {
std::atomic<bool> g_stop{false};
void handle_signal(int) { g_stop.store(true); }
}  // namespace

int main(int argc, char** argv) {
  std::map<std::string, std::string> env;
  for (const char* name : {"AGORA_APP_ID", "AGORA_APP_CERTIFICATE", "AGORA_TOKEN"}) {
    if (const char* v = std::getenv(name)) env[name] = v;
  }
  std::vector<std::string> args(argv + 1, argv + argc);

  avc::ClientConfig cfg;
  try {
    cfg = avc::parse_config(env, args);
  } catch (const std::exception& e) {
    std::cerr << e.what();
    return 2;
  }

  auto mint = [&]() {
    return avc::build_rtc_token(cfg.app_id, cfg.app_certificate, cfg.channel,
                                cfg.uid, cfg.token_ttl);
  };

  std::string token;
  switch (cfg.token_source) {
    case avc::TokenSource::Explicit: token = cfg.explicit_token; break;
    case avc::TokenSource::Mint:     token = mint(); break;
    case avc::TokenSource::None:     token.clear(); break;
  }

  auto engine = avc::make_voice_engine();
  std::atomic<int> fatal{0};

  avc::VoiceEngineCallbacks cb;
  cb.on_joined      = [] { std::cerr << "[avc] ready\n"; };
  cb.on_user_joined = [](std::uint32_t uid) {
    std::cerr << "[avc] AI Agent joined uid=" << uid << "\n"; };
  cb.on_user_left   = [](std::uint32_t uid) {
    std::cerr << "[avc] AI Agent left uid=" << uid << "\n"; };
  cb.on_token_will_expire = [&] {
    if (cfg.token_source == avc::TokenSource::Mint) engine->renew_token(mint());
  };
  cb.on_fatal = [&](int code) {
    fatal.store(code ? code : 1);
    g_stop.store(true);
  };

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  if (!engine->start(cfg, token, cb)) {
    std::cerr << "[avc] failed to start\n";
    return 1;
  }

  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  engine->stop();
  return fatal.load();
}
```

- [ ] **Step 2: Commit**

```bash
git add src/main.cpp
git commit -m "feat: main wiring with signals and token renewal"
```

---

## Task 9: Build the app target — link framework, embed Info.plist, ad-hoc sign

**Files:**
- Create: `Info.plist`, `entitlements.plist`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `Info.plist`** (embedded into the binary; provides the mic prompt text)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key><string>com.local.agora-voice-client</string>
  <key>CFBundleName</key><string>agora-voice-client</string>
  <key>NSMicrophoneUsageDescription</key>
  <string>agora-voice-client needs the microphone to talk to the AI agent.</string>
</dict>
</plist>
```

- [ ] **Step 2: Create `entitlements.plist`** (audio-input entitlement for the hardened runtime)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>com.apple.security.device.audio-input</key><true/>
</dict>
</plist>
```

- [ ] **Step 3: Add the app target to `CMakeLists.txt`** (append below the tests target)

```cmake
# ---- Application (macOS only; links the Agora framework) ----
add_executable(agora-voice-client
  src/main.cpp
  src/config.cpp
  src/token.cpp
  src/voice_engine_agora.mm
  third_party/agora-token/cpp/src/AccessToken2.cpp
)

target_include_directories(agora-voice-client PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party/agora-token
)

set(AGORA_FW_DIR ${CMAKE_SOURCE_DIR}/third_party/agora)

target_compile_options(agora-voice-client PRIVATE -fobjc-arc)

target_link_options(agora-voice-client PRIVATE
  -F${AGORA_FW_DIR}
  "-Wl,-sectcreate,__TEXT,__info_plist,${CMAKE_SOURCE_DIR}/Info.plist"
)

target_link_libraries(agora-voice-client PRIVATE
  "-F${AGORA_FW_DIR}"
  "-framework AgoraRtcKit"
  "-framework Foundation"
  "-framework CoreAudio"
  "-framework AudioToolbox"
  "-framework AVFoundation"
  "-framework CoreMedia"
  "-framework SystemConfiguration"
)

# Find the framework next to the executable at runtime, and in third_party at dev time.
set_target_properties(agora-voice-client PROPERTIES
  BUILD_RPATH   "${AGORA_FW_DIR}"
  INSTALL_RPATH "@loader_path"
)

# Ad-hoc code-sign with hardened runtime + audio-input entitlement (ADR-0002).
add_custom_command(TARGET agora-voice-client POST_BUILD
  COMMAND codesign --force --options runtime
          --entitlements ${CMAKE_SOURCE_DIR}/entitlements.plist
          --sign - $<TARGET_FILE:agora-voice-client>
  COMMENT "Ad-hoc signing agora-voice-client")
```

- [ ] **Step 4: Ensure the SDK is present, then build**

Run:
```bash
./scripts/fetch-sdk.sh            # if not already run
cmake -S . -B build && cmake --build build
```
Expected: `build/agora-voice-client` is produced and signed (no link errors). If a `-framework` line names a framework the SDK doesn't ship, remove it; if the linker reports an undefined Agora symbol, add the missing system framework it names.

- [ ] **Step 5: Verify signing and usage output**

Run:
```bash
codesign -dv --entitlements - build/agora-voice-client 2>&1 | grep -A2 entitlements
./build/agora-voice-client            # no args
```
Expected: entitlements show `com.apple.security.device.audio-input`; running with no args prints the usage text and exits non-zero (`echo $?` → 2).

- [ ] **Step 6: Commit**

```bash
git add Info.plist entitlements.plist CMakeLists.txt
git commit -m "build: link Agora framework, embed Info.plist, ad-hoc sign"
```

---

## Task 10: Packaging + README + manual integration test

**Files:**
- Create: `scripts/package.sh`, `README.md`

- [ ] **Step 1: Create `scripts/package.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/dist/agora-voice-client-macos-arm64"
rm -rf "$OUT" && mkdir -p "$OUT"
cp "$ROOT/build/agora-voice-client" "$OUT/"
cp -R "$ROOT"/third_party/agora/*.framework "$OUT/"
( cd "$ROOT/dist" && tar -czf agora-voice-client-macos-arm64.tar.gz agora-voice-client-macos-arm64 )
echo "Wrote $ROOT/dist/agora-voice-client-macos-arm64.tar.gz"
```

The binary's `@loader_path` rpath (Task 9) makes it find the frameworks placed beside it.

- [ ] **Step 2: Build, package, smoke-test the packaged binary**

Run:
```bash
chmod +x scripts/package.sh && ./scripts/package.sh
./dist/agora-voice-client-macos-arm64/agora-voice-client
```
Expected: usage text prints (proves the packaged binary loads the frameworks via rpath).

- [ ] **Step 3: Create `README.md`**

````markdown
# agora-voice-client

Self-hosted macOS CLI that joins an Agora channel and gives you two-way voice with a
remote AI agent. Single-operator tool: you run it on your own machine with your own
Agora credentials (see `docs/adr/0001-local-token-minting-self-hosted.md`).

## Build

```bash
./scripts/fetch-sdk.sh      # downloads the pinned Agora Voice SDK
cmake -S . -B build && cmake --build build
./build/tests               # unit tests
```

## Run

```bash
export AGORA_APP_ID=<your app id>
export AGORA_APP_CERTIFICATE=<your app certificate>   # tokens are minted locally
./build/agora-voice-client --channel <room> --uid <N>
```

- `--uid <N>` must match the uid the AI agent subscribes to
  (`docs/adr/0003-client-uid-is-a-coordinated-identifier.md`). `--uid 0` is solo testing only.
- Alternatively set `AGORA_TOKEN=<token>` to skip local minting.
- Ctrl-C to leave the channel and exit cleanly.

## First run: microphone permission

The **first** run must be in a Terminal **at the Mac** (a graphical session), so you can
click **Allow** on the microphone prompt. After that, SSH/headless runs work. A rebuild
re-triggers the prompt (ad-hoc signing — see ADR-0002).

## Manual integration test

1. Start a second participant in the same channel: the AI agent, or the
   [Agora web demo](https://webdemo.agora.io/) with the same App ID + channel.
2. Run the client; confirm `[avc] ready` then `[avc] AI Agent joined uid=...`.
3. **Two-way audio:** speak — the other side hears you; the other side speaks — you hear it.
4. **Echo cancellation:** with speakers (not headphones), confirm the far end does NOT
   hear its own voice looped back. (This validates the built-in 3A — the core reason for
   the architecture.)
5. **Clean exit:** Ctrl-C; confirm the participant disappears from the channel (no ghost).
````

- [ ] **Step 4: Commit**

```bash
chmod +x scripts/package.sh
git add scripts/package.sh README.md
git commit -m "build: packaging script and README with integration checklist"
```

---

## Self-Review

**Spec coverage:**
- Audio-only, join one channel, stay until signal → Tasks 7, 8. ✓
- Config: env + flags + local mint, resolution order → Task 2. ✓
- Local token minting from cert, self-hosted (ADR-0001) → Tasks 4, 8. ✓
- Mic permission: Info.plist + ad-hoc sign (ADR-0002) → Task 9. ✓
- HMAC via CommonCrypto, no OpenSSL → Tasks 3, 4. ✓
- Pinned Voice SDK via fetch script + checksum → Task 5. ✓
- Coordinated `--uid`, single source to token+join (ADR-0003) → Tasks 2, 4, 7, 8. ✓
- Automatic token renewal → Tasks 7 (callback), 8 (re-mint). ✓
- Lifecycle: SDK auto-reconnect, exit non-zero on FAILED, AI presence irrelevant → Tasks 7, 8. ✓
- LIVE_BROADCASTING + BROADCASTER + speech mono + AEC (ADR-0004) → Task 7. ✓
- macOS Obj-C++ engine behind C++ interface (ADR-0005) → Tasks 6, 7. ✓
- Auto-subscribe to AI audio → Task 7 (`autoSubscribeAudio = YES`). ✓
- Distribution archive (binary + framework, rpath) → Tasks 9, 10. ✓
- Testing: unit (config, token) + manual integration → Tasks 2, 3, 4, 10. ✓

**Placeholder scan:** The only deliberately-unfilled values are the SDK URL/version/checksum in Task 5 (genuine external lookup, with exact instructions and a page URL) and the "locate the OpenSSL HMAC call" edit in Task 4 (the source can't be quoted verbatim; precise search terms + exact replacement body given). No code placeholders.

**Type consistency:** `ClientConfig`, `TokenSource`, `parse_config`, `hmac_sha256`, `build_rtc_token`, `VoiceEngine`, `VoiceEngineCallbacks`, `make_voice_engine` are defined once and used with identical signatures across tasks. `avc::` namespace throughout. `Role_Publisher`/`agora::tools::RtcTokenBuilder2` match the fetched header signature.
