# Local modifications to vendored Agora AccessToken2 builder

Source: https://github.com/AgoraIO/Tools  (DynamicKey/AgoraDynamicKey/cpp/src/)
Vendored files: AccessToken2.h, RtcTokenBuilder2.h, utils.h, Packer.h (header-only).

## Patch: remove OpenSSL, use CommonCrypto via avc::hmac_sha256

`utils.h` originally computed HMAC with OpenSSL (`#include <openssl/hmac.h>`, `HMAC(EVP_sha256(), ...)`).
We removed OpenSSL entirely:
- `HmacSign2` (SHA-256) now converts its (key, message) to byte vectors and calls
  `avc::hmac_sha256` (declared in `src/token.h`, implemented per-platform: CommonCrypto on
  macOS, BCrypt on Windows).
- `HmacSign` (legacy SHA-1, not used by RtcTokenBuilder2) is **platform-guarded**:
  `#ifdef __APPLE__` uses CommonCrypto `CCHmac(kCCHmacAlgSHA1, ...)`; `#elif defined(_WIN32)`
  uses CNG **BCrypt** (`BCRYPT_SHA1_ALGORITHM` + `BCRYPT_ALG_HANDLE_HMAC_FLAG`).
- Added `#include "token.h"`, `#include <vector>`; on Windows `#include <windows.h>` +
  `<bcrypt.h>`; removed `#include <openssl/hmac.h>`. (`bcrypt` is linked by CMake on WIN32.)

**IMPORTANT — when re-vendoring (overwriting these files with a newer SDK), RE-APPLY this
patch on BOTH platforms** (the macOS CommonCrypto and the Windows BCrypt branches), or the
build will pull in OpenSSL again / break the Windows leg. Verify with:
`grep -rn -i "openssl\|EVP_\|HMAC(" third_party/agora-token` (only comments should match).
