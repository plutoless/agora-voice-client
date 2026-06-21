# Local modifications to vendored Agora AccessToken2 builder

Source: https://github.com/AgoraIO/Tools  (DynamicKey/AgoraDynamicKey/cpp/src/)
Vendored files: AccessToken2.h, RtcTokenBuilder2.h, utils.h, Packer.h (header-only).

## Patch: remove OpenSSL, use CommonCrypto via avc::hmac_sha256

`utils.h` originally computed HMAC with OpenSSL (`#include <openssl/hmac.h>`, `HMAC(EVP_sha256(), ...)`).
We removed OpenSSL entirely:
- `HmacSign2` (SHA-256) now converts its (key, message) to byte vectors and calls
  `avc::hmac_sha256` (declared in `src/token.h`, implemented with macOS CommonCrypto).
- `HmacSign` (legacy SHA-1, not used by RtcTokenBuilder2) was switched to CommonCrypto
  `CCHmac(kCCHmacAlgSHA1, ...)`.
- Added `#include "token.h"`, `#include <vector>`; removed `#include <openssl/hmac.h>`.

**IMPORTANT — when re-vendoring (overwriting these files with a newer SDK), RE-APPLY this
patch**, or the build will pull in OpenSSL again. Verify with:
`grep -rn -i "openssl\|EVP_\|HMAC(" third_party/agora-token` (only comments should match).
