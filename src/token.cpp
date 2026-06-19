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
