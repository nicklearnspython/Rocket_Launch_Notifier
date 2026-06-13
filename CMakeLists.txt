cmake_minimum_required(VERSION 3.24)
project(rocket_watcher CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ROCKET_WATCHER_BUILD_TESTS "Build unit tests" ON)
option(ROCKET_WATCHER_COVERAGE "Instrument for gcov code coverage" OFF)
option(ROCKET_WATCHER_CLANG_TIDY "Run clang-tidy during compilation" OFF)

if(ROCKET_WATCHER_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_Declare(
  tomlplusplus
  URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
  URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155
)
FetchContent_MakeAvailable(nlohmann_json tomlplusplus)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_library(rocket_watcher_lib
  src/cli.cpp
  src/config.cpp
  src/core.cpp
  src/curl_http.cpp
  src/domain.cpp
  src/formatting.cpp
  src/launch_library.cpp
  src/orchestration.cpp
  src/pushover.cpp
  src/relevance.cpp
  src/storage.cpp
  src/strings.cpp
  src/time_utils.cpp
)
target_include_directories(rocket_watcher_lib PUBLIC include)
target_link_libraries(rocket_watcher_lib
  PUBLIC nlohmann_json::nlohmann_json Threads::Threads
  PRIVATE tomlplusplus::tomlplusplus CURL::libcurl
)
target_compile_options(rocket_watcher_lib PRIVATE -Wall -Wextra)

if(ROCKET_WATCHER_COVERAGE)
  target_compile_options(rocket_watcher_lib PUBLIC --coverage -O0 -g)
  target_link_options(rocket_watcher_lib PUBLIC --coverage)
endif()

add_executable(rocket-watcher src/main.cpp)
target_link_libraries(rocket-watcher PRIVATE rocket_watcher_lib)
# Static stdlib so the binary runs on a slim runtime image whose libstdc++
# may be older than the build toolchain's.
target_link_options(rocket-watcher PRIVATE -static-libstdc++ -static-libgcc)

if(ROCKET_WATCHER_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
  )
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(rocket_watcher_tests
    tests/test_cli.cpp
    tests/test_config.cpp
    tests/test_core.cpp
    tests/test_formatting.cpp
    tests/test_launch_library.cpp
    tests/test_orchestration.cpp
    tests/test_pushover.cpp
    tests/test_relevance.cpp
    tests/test_storage.cpp
  )
  target_include_directories(rocket_watcher_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(rocket_watcher_tests PRIVATE rocket_watcher_lib GTest::gmock_main)
  target_compile_options(rocket_watcher_tests PRIVATE -Wall -Wextra)

  include(GoogleTest)
  gtest_discover_tests(rocket_watcher_tests)
endif()
