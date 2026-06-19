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
