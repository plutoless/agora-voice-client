# macOS voice_engine is Objective-C++ using AgoraRtcEngineKit

The spec assumed the C++ `IRtcEngine` API could be used uniformly. On macOS that is not
the supported path: the 4.x SDK's documented entry point is the **Objective-C**
`AgoraRtcEngineKit` (inside `AgoraRtcKit.framework`); the C++ `IRtcEngine` is only
reachable by bridging via `getNativeHandle` from the Obj-C object inside an `.mm` file.

Decision: implement the macOS `voice_engine` in **Objective-C++ (`voice_engine_agora.mm`)**
against `AgoraRtcEngineKit` and its `AgoraRtcEngineDelegate`. The `voice_engine` interface
stays pure C++ and exposes semantic callbacks, so `config`, `token`, and `main` remain
pure C++ and never see Objective-C. Future Windows/Linux implementations can use the C++
`IRtcEngine` behind the same interface. This validates the interface-isolation design: the
per-platform API language difference is contained in one file.
