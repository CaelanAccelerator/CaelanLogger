add_library(caelogger
    source/Buffer.cpp
    source/ThreadLogger.cpp
 "include/ThreadLogger.h"
 "source/ThreadLogger.cpp"
 "include/BackendLogger.h"
 "source/BackendLogger.cpp"
 "include/SpinGuard.h"
 "source/FileUtil.cpp"
 "include/FileUtil.h"
 "include/AsyncLogger.h"
 "include/TimeUtil.h"
 "source/TimeUtil.cpp"
 
  
 "include/Level.h"
 "include/LogStream.h"
 
 "source/LogStream.cpp" "tests/Benchmark.cpp")

target_include_directories(caelogger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# If you use snprintf/strlen etc
target_compile_features(caelogger PUBLIC cxx_std_17)

if (MSVC)
    target_compile_definitions(caelogger PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
