#import <AgoraRtcKit/AgoraRtcEngineKit.h>
#include "voice_engine.h"
#include <cstdio>

using namespace avc;

// NOTE: These delegate methods are invoked on an Agora-internal SDK thread, not the
// main thread. The C++ callbacks in _cb must be safe to call from any thread.
@interface AvcDelegate : NSObject <AgoraRtcEngineDelegate>
@end

@implementation AvcDelegate {
@public
  VoiceEngineCallbacks _cb;
}
- (void)rtcEngine:(AgoraRtcEngineKit * _Nonnull)engine
    didJoinChannel:(NSString * _Nonnull)channel withUid:(NSUInteger)uid elapsed:(NSInteger)elapsed {
  fprintf(stderr, "[avc] joined channel as uid=%lu\n", (unsigned long)uid);
  if (_cb.on_joined) _cb.on_joined();
}
- (void)rtcEngine:(AgoraRtcEngineKit * _Nonnull)engine
    didJoinedOfUid:(NSUInteger)uid elapsed:(NSInteger)elapsed {
  if (_cb.on_user_joined) _cb.on_user_joined((std::uint32_t)uid);
}
- (void)rtcEngine:(AgoraRtcEngineKit * _Nonnull)engine
    didOfflineOfUid:(NSUInteger)uid reason:(AgoraUserOfflineReason)reason {
  if (_cb.on_user_left) _cb.on_user_left((std::uint32_t)uid);
}
- (void)rtcEngine:(AgoraRtcEngineKit * _Nonnull)engine
    tokenPrivilegeWillExpire:(NSString * _Nonnull)token {
  fprintf(stderr, "[avc] token will expire; renewing\n");
  if (_cb.on_token_will_expire) _cb.on_token_will_expire();
}
- (void)rtcEngine:(AgoraRtcEngineKit * _Nonnull)engine
    connectionChangedToState:(AgoraConnectionState)state
                      reason:(AgoraConnectionChangedReason)reason {
  fprintf(stderr, "[avc] connection state=%ld reason=%ld\n", (long)state, (long)reason);
  if (state == AgoraConnectionStateFailed && _cb.on_fatal) _cb.on_fatal((int)reason);
}
- (void)rtcEngine:(AgoraRtcEngineKit * _Nonnull)engine didOccurError:(AgoraErrorCode)errorCode {
  fprintf(stderr, "[avc] error code=%ld\n", (long)errorCode);
}
@end

namespace {
class AgoraVoiceEngine : public VoiceEngine {
 public:
  ~AgoraVoiceEngine() override { stop(); }
  bool start(const ClientConfig& cfg, const std::string& token,
             const VoiceEngineCallbacks& cb) override {
    if (kit_) {
      fprintf(stderr, "[avc] start() called while already started\n");
      return false;
    }
    delegate_ = [[AvcDelegate alloc] init];
    delegate_->_cb = cb;

    AgoraRtcEngineConfig* config = [[AgoraRtcEngineConfig alloc] init];
    NSString* appId = [NSString stringWithUTF8String:cfg.app_id.c_str()];
    if (!appId) {
      fprintf(stderr, "[avc] app_id is not valid UTF-8\n");
      return false;
    }
    config.appId = appId;
    // Set channel profile and audio scenario at init time via config
    config.channelProfile = AgoraChannelProfileLiveBroadcasting;
    // AgoraAudioScenarioMeeting (8): meeting/call scenario — keeps AEC, ANS, AGC enabled
    config.audioScenario = AgoraAudioScenarioMeeting;

    kit_ = [AgoraRtcEngineKit sharedEngineWithConfig:config delegate:delegate_];
    if (!kit_) return false;

    // Broadcaster role: publishes mic, receives remote audio
    [kit_ setClientRole:AgoraClientRoleBroadcaster];
    // Speech-optimised mono profile (8 kHz, 18 kbps, mono); AEC stays on via Meeting scenario
    [kit_ setAudioProfile:AgoraAudioProfileSpeechStandard];
    [kit_ enableAudio];

    AgoraRtcChannelMediaOptions* opts = [[AgoraRtcChannelMediaOptions alloc] init];
    opts.channelProfile = AgoraChannelProfileLiveBroadcasting;
    opts.clientRoleType = AgoraClientRoleBroadcaster;
    opts.publishMicrophoneTrack = YES;
    opts.autoSubscribeAudio = YES;

    NSString* tok = token.empty() ? nil : [NSString stringWithUTF8String:token.c_str()];
    NSString* chan = [NSString stringWithUTF8String:cfg.channel.c_str()];
    // Selector: joinChannelByToken:channelId:uid:mediaOptions:joinSuccess:
    int rc = [kit_ joinChannelByToken:tok
                            channelId:chan
                                  uid:cfg.uid
                         mediaOptions:opts
                          joinSuccess:nil];
    if (rc != 0) {
      fprintf(stderr, "[avc] joinChannel failed rc=%d\n", rc);
      return false;
    }
    return true;
  }

  void renew_token(const std::string& token) override {
    if (kit_) [kit_ renewToken:[NSString stringWithUTF8String:token.c_str()]];
  }

  void stop() override {
    if (kit_) {
      [kit_ leaveChannel:nil];
      [AgoraRtcEngineKit destroy];
      kit_ = nil;
      delegate_ = nil;
    }
  }

 private:
  AgoraRtcEngineKit* kit_ = nil;
  AvcDelegate* delegate_ = nil;
};
}  // namespace

namespace avc {
std::unique_ptr<VoiceEngine> make_voice_engine() {
  return std::make_unique<AgoraVoiceEngine>();
}
}  // namespace avc
