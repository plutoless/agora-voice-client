#include "IAgoraRtcEngine.h"
#include "voice_engine.h"

#include <cstdint>
#include <memory>
#include <string>

using namespace agora;
using namespace agora::rtc;

namespace {

class AvcHandler : public IRtcEngineEventHandler {
 public:
  avc::VoiceEngineCallbacks cb;
  bool reconnecting = false;

  void onJoinChannelSuccess(const char* channel, uid_t uid, int elapsed) override {
    if (cb.on_joined) cb.on_joined();
  }
  void onUserJoined(uid_t uid, int elapsed) override {
    if (cb.on_user_joined) cb.on_user_joined(static_cast<std::uint32_t>(uid));
  }
  void onUserOffline(uid_t uid, USER_OFFLINE_REASON_TYPE reason) override {
    if (cb.on_user_left) cb.on_user_left(static_cast<std::uint32_t>(uid));
  }
  void onConnectionStateChanged(CONNECTION_STATE_TYPE state,
                                CONNECTION_CHANGED_REASON_TYPE reason) override {
    if (state == CONNECTION_STATE_RECONNECTING) {
      reconnecting = true;
      if (cb.on_reconnecting) cb.on_reconnecting();
    } else if (state == CONNECTION_STATE_CONNECTED) {
      if (reconnecting) {
        reconnecting = false;
        if (cb.on_reconnected) cb.on_reconnected();
      }
    } else if (state == CONNECTION_STATE_FAILED) {
      if (cb.on_fatal) cb.on_fatal(static_cast<int>(reason));
    }
  }
  void onTokenPrivilegeWillExpire(const char* token) override {
    if (cb.on_token_will_expire) cb.on_token_will_expire();
  }
  void onError(int err, const char* msg) override {
    if (cb.on_error) cb.on_error(err);
  }
};

class AgoraWinVoiceEngine : public avc::VoiceEngine {
 public:
  ~AgoraWinVoiceEngine() override { stop(); }

  bool start(const avc::ClientConfig& cfg, const std::string& token,
             const avc::VoiceEngineCallbacks& cb) override {
    if (engine_) return false;
    handler_.cb = cb;
    engine_ = createAgoraRtcEngine();
    if (!engine_) return false;

    RtcEngineContext ctx;
    ctx.appId = cfg.app_id.c_str();
    ctx.eventHandler = &handler_;
    ctx.channelProfile = CHANNEL_PROFILE_LIVE_BROADCASTING;
    ctx.audioScenario = AUDIO_SCENARIO_MEETING;
    if (engine_->initialize(ctx) != 0) {
      IRtcEngine::release(true);
      engine_ = nullptr;
      return false;
    }

    engine_->setChannelProfile(CHANNEL_PROFILE_LIVE_BROADCASTING);
    engine_->setClientRole(CLIENT_ROLE_BROADCASTER);
    engine_->setAudioProfile(AUDIO_PROFILE_SPEECH_STANDARD);
    engine_->enableAudio();

    ChannelMediaOptions options;
    options.publishMicrophoneTrack = true;
    options.autoSubscribeAudio = true;
    options.clientRoleType = CLIENT_ROLE_BROADCASTER;
    options.channelProfile = CHANNEL_PROFILE_LIVE_BROADCASTING;

    int rc = engine_->joinChannel(token.empty() ? nullptr : token.c_str(),
                                  cfg.channel.c_str(), cfg.uid, options);
    if (rc != 0) {
      IRtcEngine::release(true);
      engine_ = nullptr;
      return false;
    }
    return true;
  }

  void renew_token(const std::string& token) override {
    if (engine_) engine_->renewToken(token.c_str());
  }

  void stop() override {
    if (engine_) {
      engine_->leaveChannel();
      IRtcEngine::release(true);
      engine_ = nullptr;
    }
  }

 private:
  IRtcEngine* engine_ = nullptr;
  AvcHandler handler_;
};

}  // namespace

namespace avc {
std::unique_ptr<VoiceEngine> make_voice_engine() {
  return std::make_unique<AgoraWinVoiceEngine>();
}
}  // namespace avc
