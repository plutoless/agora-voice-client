cmake_minimum_required(VERSION 3.20)
project(agora_voice_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Tests (pure C++, no Agora RTC SDK needed) ----
add_executable(tests
  tests/doctest_main.cpp
  tests/test_smoke.cpp
)
target_include_directories(tests PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/doctest
  ${CMAKE_SOURCE_DIR}/src
)

enable_testing()
add_test(NAME tests COMMAND tests)
