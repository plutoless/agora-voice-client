#include "config.h"
#include <limits>
#include <stdexcept>
#include <string>

namespace avc {
namespace {

const char* kUsage =
    "usage: agora-voice-client --channel <name> [--uid <n>] [--token-ttl <seconds>]\n"
    "  env: AGORA_APP_ID (required), AGORA_APP_CERTIFICATE or AGORA_TOKEN (optional)\n";

std::uint32_t parse_u32(const std::string& s, const char* what) {
  try {
    // Reject negative strings explicitly — std::stoul behaviour for "-n" is
    // implementation-defined (MSVC wraps instead of throwing).
    if (!s.empty() && s[0] == '-') throw std::invalid_argument(what);
    size_t pos = 0;
    unsigned long v = std::stoul(s, &pos);
    if (pos != s.size()) throw std::invalid_argument(what);
    if (v > std::numeric_limits<std::uint32_t>::max())
      throw std::invalid_argument(what);
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
