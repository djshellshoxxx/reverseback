cmake_minimum_required(VERSION 3.20)

project(ReverseBack VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(reverseback_core
    Source/Core/ReverseBuffer.cpp
    Source/Core/FrameScheduler.cpp
    Source/Core/RecordTransport.cpp
    Source/Core/LiveReverseTransport.cpp
    Source/Core/ClipPlayer.cpp
    Source/Core/SignalTools.cpp
)

target_include_directories(reverseback_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core
)

if(MSVC)
    target_compile_options(reverseback_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(reverseback_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(reverseback_core_tests
        Tests/Core/test_core.cpp
    )

    target_link_libraries(reverseback_core_tests PRIVATE reverseback_core)
    add_test(NAME reverseback_core_tests COMMAND reverseback_core_tests)

    add_executable(reverseback_clip_feature_tests
        Tests/Core/test_clip_features.cpp
    )
    target_link_libraries(reverseback_clip_feature_tests PRIVATE reverseback_core)
    add_test(NAME reverseback_clip_feature_tests COMMAND reverseback_clip_feature_tests)
endif()
