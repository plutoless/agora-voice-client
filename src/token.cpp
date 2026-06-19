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

}  // namespace avc

// ---- Task 4: build_rtc_token via vendored AccessToken2 ----
#include "cpp/src/RtcTokenBuilder2.h"   // vendored; resolved via include dir in CMake

namespace avc {

std::string build_rtc_token(const std::string& app_id,
                            const std::string& app_certificate,
                            const std::string& channel,
                            std::uint32_t uid,
                            std::uint32_t ttl_seconds) {
  // kRolePublisher: the Client always publishes its mic.
  return agora::tools::RtcTokenBuilder2::BuildTokenWithUid(
      app_id, app_certificate, channel, uid,
      agora::tools::UserRole::kRolePublisher, ttl_seconds, ttl_seconds);
}

}  // namespace avc
