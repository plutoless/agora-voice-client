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
