add_library(caelogger
    source/Buffer.cpp
    source/ThreadLogger.cpp
    include/ThreadLogger.h
    include/BackendLogger.h
    source/BackendLogger.cpp
    include/SpinGuard.h
    source/FileUtil.cpp
    include/FileUtil.h
    include/AsyncLogger.h
    include/TimeUtil.h
    source/TimeUtil.cpp
    include/Level.h
    include/LogStream.h
    source/LogStream.cpp
)

target_include_directories(caelogger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# If you use snprintf/strlen etc
target_compile_features(caelogger PUBLIC cxx_std_17)

if (MSVC)
    target_compile_definitions(caelogger PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_library(caelogger_testing STATIC
    source/Buffer.cpp
    source/ThreadLogger.cpp
    source/BackendLogger.cpp
    source/FileUtil.cpp
    source/TimeUtil.cpp
    source/LogStream.cpp
)

target_include_directories(caelogger_testing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(caelogger_testing PUBLIC cxx_std_17)
target_compile_definitions(caelogger_testing PUBLIC CAELOGGER_TESTING)

if (MSVC)
    target_compile_definitions(caelogger_testing PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
