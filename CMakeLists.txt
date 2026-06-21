cmake_minimum_required(VERSION 3.20)
project(agora_voice_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
  set(AVC_HMAC_SRC src/hmac_apple.cpp)
elseif(WIN32)
  set(AVC_HMAC_SRC src/hmac_windows.cpp)
endif()

if(WIN32)
  include(FetchContent)
  FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1)
  FetchContent_MakeAvailable(zlib)
  set(AVC_ZLIB zlibstatic)   # zlib's static target; confirm on first CI run
else()
  find_package(ZLIB REQUIRED)
  set(AVC_ZLIB ZLIB::ZLIB)
endif()

# ---- Tests (pure C++, no Agora RTC SDK needed) ----
add_executable(tests
  tests/doctest_main.cpp
  tests/test_smoke.cpp
  tests/test_config.cpp
  tests/test_token.cpp
  tests/test_event_reporter.cpp
  src/config.cpp
  src/token.cpp
  ${AVC_HMAC_SRC}
  src/event_reporter.cpp
)
target_include_directories(tests PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/doctest
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party/agora-token
)
target_link_libraries(tests PRIVATE ${AVC_ZLIB})
if(WIN32)
  target_link_libraries(tests PRIVATE bcrypt)
  target_include_directories(tests PRIVATE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
endif()

enable_testing()
add_test(NAME tests COMMAND tests)

set(AVC_VERSION "dev" CACHE STRING "Version string stamped into the binary")

# ---- Application target (platform-specific) ----
if(APPLE)
  # ===== macOS app target =====
  add_executable(agora-voice-client
    src/main.cpp
    src/config.cpp
    src/token.cpp
    ${AVC_HMAC_SRC}
    src/event_reporter.cpp
    src/voice_engine_agora.mm
  )

  target_include_directories(agora-voice-client PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/agora-token
  )

  target_compile_definitions(agora-voice-client PRIVATE AVC_VERSION_STR="${AVC_VERSION}")

  set(AGORA_FW_DIR ${CMAKE_SOURCE_DIR}/third_party/agora)

  target_compile_options(agora-voice-client PRIVATE -fobjc-arc -F${AGORA_FW_DIR})

  target_link_options(agora-voice-client PRIVATE
    -F${AGORA_FW_DIR}
    "-Wl,-sectcreate,__TEXT,__info_plist,${CMAKE_SOURCE_DIR}/Info.plist"
  )

  target_link_libraries(agora-voice-client PRIVATE
    ${AVC_ZLIB}
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

elseif(WIN32)
  add_executable(agora-voice-client
    src/main.cpp src/config.cpp src/token.cpp src/event_reporter.cpp
    ${AVC_HMAC_SRC} src/voice_engine_agora_win.cpp)
  target_include_directories(agora-voice-client PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/agora-token
    ${CMAKE_SOURCE_DIR}/third_party/agora/include
    ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
  target_compile_definitions(agora-voice-client PRIVATE AVC_VERSION_STR="${AVC_VERSION}")
  file(GLOB AGORA_WIN_LIBS ${CMAKE_SOURCE_DIR}/third_party/agora/lib/*.lib)
  target_link_libraries(agora-voice-client PRIVATE ${AVC_ZLIB} bcrypt ${AGORA_WIN_LIBS})
  add_custom_command(TARGET agora-voice-client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/third_party/agora/bin $<TARGET_FILE_DIR:agora-voice-client>)
endif()
