cmake_minimum_required(VERSION 3.20)
project(CaelansLoggerWorkspace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_ASAN "Build with AddressSanitizer + LeakSanitizer" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

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
  caelogger_testing
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(caelogger_tests)


# ---- spdlog ----
FetchContent_Declare(
  spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz
)
FetchContent_MakeAvailable(spdlog)

# ---- Benchmark (standalone) ----
add_executable(caelogger_bench
  CaelanLogger/tests/Benchmark.cpp
)

target_link_libraries(caelogger_bench PRIVATE
  caelogger
  spdlog::spdlog
)

# Optional: keep it out of `ctest`
# (It won't be discovered anyway since it isn't a gtest, but this makes intent clear)
set_property(TARGET caelogger_bench PROPERTY EXCLUDE_FROM_ALL FALSE)