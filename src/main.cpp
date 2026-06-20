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

#ifndef AVC_VERSION_STR
#define AVC_VERSION_STR "dev"
#endif

namespace {
std::atomic<bool> g_stop{false};
void handle_signal(int) { g_stop.store(true); }
}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--version") {
      std::cout << "agora-voice-client " << AVC_VERSION_STR << "\n";
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "usage: agora-voice-client --channel <name> [--uid <n>] "
                   "[--token-ttl <seconds>] [--version]\n";
      return 0;
    }
  }

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
