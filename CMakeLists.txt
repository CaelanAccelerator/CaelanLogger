cmake_minimum_required(VERSION 3.20)
project(CaelansLoggerWorkspace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build the library
add_subdirectory(CaelanLogger)   # rename folder, see section B

# ---- GoogleTest ----
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(caelogger_tests
  CaelanLogger/tests/test_Intergration.cpp
  CaelanLogger/tests/test_Time.cpp
)

target_link_libraries(caelogger_tests PRIVATE
  caelogger
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(caelogger_tests)
