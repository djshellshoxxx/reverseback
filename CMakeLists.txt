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
    Source/Core/RealtimeTools.cpp
    Source/IO/SettingsStore.cpp
)

target_include_directories(reverseback_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/IO
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

    add_executable(reverseback_realtime_tests
        Tests/Core/test_realtime_tools.cpp
    )
    target_link_libraries(reverseback_realtime_tests PRIVATE reverseback_core)
    add_test(NAME reverseback_realtime_tests COMMAND reverseback_realtime_tests)

    add_executable(reverseback_record_advanced_tests
        Tests/Core/test_record_advanced.cpp
    )
    target_link_libraries(reverseback_record_advanced_tests PRIVATE reverseback_core)
    add_test(NAME reverseback_record_advanced_tests COMMAND reverseback_record_advanced_tests)

    add_executable(reverseback_settings_tests
        Tests/Integration/test_settings.cpp
    )
    target_link_libraries(reverseback_settings_tests PRIVATE reverseback_core)
    add_test(NAME reverseback_settings_tests COMMAND reverseback_settings_tests)

    add_executable(reverseback_callback_allocation_tests
        Tests/Core/test_callback_allocations.cpp
    )
    target_link_libraries(reverseback_callback_allocation_tests PRIVATE reverseback_core)
    add_test(NAME reverseback_callback_allocation_tests COMMAND reverseback_callback_allocation_tests)
endif()


option(REVERSEBACK_BUILD_APP "Build the JUCE standalone application" OFF)

if(REVERSEBACK_BUILD_APP)
    include(FetchContent)

    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_gui_app(ReverseBack
        PRODUCT_NAME "ReverseBack"
        COMPANY_NAME "Circuit Drift Labs"
        VERSION ${PROJECT_VERSION}
    )

    target_sources(ReverseBack PRIVATE
        Source/Main.cpp
        Source/Audio/AudioDeviceAdapter.cpp
        Source/IO/FileService.cpp
        Source/IO/ExportService.cpp
        Source/UI/MainComponent.cpp
        Source/UI/WaveformView.cpp
        Source/UI/AudioSettingsPanel.cpp
    )

    target_include_directories(ReverseBack PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/IO
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/UI
    )

    target_compile_definitions(ReverseBack PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    target_link_libraries(ReverseBack PRIVATE
        reverseback_core
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
endif()
