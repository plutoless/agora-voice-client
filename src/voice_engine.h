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
