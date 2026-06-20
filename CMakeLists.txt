cmake_minimum_required(VERSION 3.20)
project(agora_voice_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ZLIB REQUIRED)

# ---- Tests (pure C++, no Agora RTC SDK needed) ----
add_executable(tests
  tests/doctest_main.cpp
  tests/test_smoke.cpp
  tests/test_config.cpp
  tests/test_token.cpp
  src/config.cpp
  src/token.cpp
)
target_include_directories(tests PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/doctest
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party/agora-token
)
target_link_libraries(tests PRIVATE ZLIB::ZLIB)

enable_testing()
add_test(NAME tests COMMAND tests)

# ---- Application (macOS only; links the Agora framework) ----
add_executable(agora-voice-client
  src/main.cpp
  src/config.cpp
  src/token.cpp
  src/voice_engine_agora.mm
)

target_include_directories(agora-voice-client PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party/agora-token
)

set(AGORA_FW_DIR ${CMAKE_SOURCE_DIR}/third_party/agora)

target_compile_options(agora-voice-client PRIVATE -fobjc-arc -F${AGORA_FW_DIR})

target_link_options(agora-voice-client PRIVATE
  -F${AGORA_FW_DIR}
  "-Wl,-sectcreate,__TEXT,__info_plist,${CMAKE_SOURCE_DIR}/Info.plist"
)

target_link_libraries(agora-voice-client PRIVATE
  ZLIB::ZLIB
  "-F${AGORA_FW_DIR}"
  "-framework AgoraRtcKit"
  "-framework Foundation"
  "-framework CoreAudio"
  "-framework AudioToolbox"
  "-framework AVFoundation"
  "-framework CoreMedia"
  "-framework SystemConfiguration"
)

set_target_properties(agora-voice-client PROPERTIES
  BUILD_RPATH   "${AGORA_FW_DIR}"
  INSTALL_RPATH "@loader_path"
)

add_custom_command(TARGET agora-voice-client POST_BUILD
  COMMAND codesign --force --options runtime
          --entitlements ${CMAKE_SOURCE_DIR}/entitlements.plist
          --sign - $<TARGET_FILE:agora-voice-client>
  COMMENT "Ad-hoc signing agora-voice-client")
