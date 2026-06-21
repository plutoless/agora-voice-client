#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "config.h"
#include "event_reporter.h"
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
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);  // keep JSONL lines LF-only on Windows
#endif
  bool json_mode = false;
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--version") {
      std::cout << "agora-voice-client " << AVC_VERSION_STR << "\n";
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "usage: agora-voice-client --channel <name> [--uid <n>] "
                   "[--token-ttl <seconds>] [--json] [--version]\n";
      return 0;
    }
    if (arg == "--json") { json_mode = true; continue; }
    args.push_back(arg);
  }

  std::unique_ptr<avc::EventReporter> reporter;
  if (json_mode) reporter = std::make_unique<avc::JsonEventReporter>(std::cout);
  else reporter = std::make_unique<avc::HumanEventReporter>(std::cerr);

  reporter->meta("agora-voice-client", AVC_VERSION_STR);

  std::map<std::string, std::string> env;
  for (const char* name : {"AGORA_APP_ID", "AGORA_APP_CERTIFICATE", "AGORA_TOKEN"}) {
    if (const char* v = std::getenv(name)) env[name] = v;
  }

  avc::ClientConfig cfg;
  try {
    cfg = avc::parse_config(env, args);
  } catch (const std::exception& e) {
    reporter->fatal(2, e.what());
    return 2;
  }

  auto mint = [&]() {
    return avc::build_rtc_token(cfg.app_id, cfg.app_certificate, cfg.channel,
                                cfg.uid, cfg.token_ttl);
  };

  std::string token;
  try {
    switch (cfg.token_source) {
      case avc::TokenSource::Explicit: token = cfg.explicit_token; break;
      case avc::TokenSource::Mint:     token = mint(); break;
      case avc::TokenSource::None:     token.clear(); break;
    }
  } catch (const std::exception& e) {
    reporter->fatal(1, e.what());
    return 1;
  }

  auto engine = avc::make_voice_engine();
  std::atomic<int> fatal_code{0};

  avc::VoiceEngineCallbacks cb;
  cb.on_joined        = [&] { reporter->ready(cfg.channel, cfg.uid); };
  cb.on_user_joined   = [&](std::uint32_t uid) { reporter->peer_joined(uid); };
  cb.on_user_left     = [&](std::uint32_t uid) { reporter->peer_left(uid); };
  cb.on_reconnecting  = [&] { reporter->reconnecting(); };
  cb.on_reconnected   = [&] { reporter->reconnected(); };
  cb.on_error         = [&](int code) { reporter->error(code, ""); };
  cb.on_token_will_expire = [&] {
    if (cfg.token_source != avc::TokenSource::Mint) return;
    try {
      engine->renew_token(mint());
      reporter->token_renewed();
    } catch (const std::exception& e) {
      reporter->fatal(1, e.what());
      fatal_code.store(1);
      g_stop.store(true);
    }
  };
  cb.on_fatal = [&](int code) {
    fatal_code.store(code ? code : 1);
    reporter->fatal(code, "unrecoverable connection failure");
    g_stop.store(true);
  };

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  if (!engine->start(cfg, token, cb)) {
    reporter->fatal(1, "failed to start");
    return 1;
  }

  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  reporter->stopping(fatal_code.load() ? "fatal" : "signal");
  engine->stop();
  return fatal_code.load();
}
