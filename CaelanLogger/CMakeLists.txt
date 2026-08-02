add_library(caelogger
    source/Buffer.cpp
    source/ThreadLogger.cpp
    include/ThreadLogger.h
    include/SharedBackend.h
    source/SharedBackend.cpp
    include/UringBackend.h
    source/UringBackend.cpp
    include/SpinGuard.h

    include/FileUtil.h
    include/AsyncLogger.h
    include/TimeUtil.h
    source/TimeUtil.cpp
    include/Level.h
    include/LogStream.h
    include/NormalWriter.h
    source/NormalWriter.cpp
    include/RingBuffer.h
    include/SPMCSpinLockQueue.h
    include/MPSCSpinLockQueue.h
    include/LocalQueue.h
)

target_include_directories(caelogger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# If you use snprintf/strlen etc
target_compile_features(caelogger PUBLIC cxx_std_20)
target_link_libraries(caelogger PUBLIC uring)

if (MSVC)
    target_compile_definitions(caelogger PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_library(caelogger_testing STATIC
    source/Buffer.cpp
    source/ThreadLogger.cpp
    source/SharedBackend.cpp
    source/UringBackend.cpp
    source/TimeUtil.cpp
    source/NormalWriter.cpp
)

target_include_directories(caelogger_testing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(caelogger_testing PUBLIC cxx_std_20)
target_compile_definitions(caelogger_testing PUBLIC CAELOGGER_TESTING)
target_link_libraries(caelogger_testing PUBLIC uring)

if (MSVC)
    target_compile_definitions(caelogger_testing PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
